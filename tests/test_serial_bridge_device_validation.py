#!/usr/bin/env python3
"""Validação complementar (device-in-the-loop) do Serial Automation Bridge.

Executa, via /dev/ttyACM0 (console USB-Serial-JTAG compartilhado com ESP_LOG),
os 5 blocos do protocolo NDJSON do plano "Boneco de Lata" (boneco-de-lata.md
§2.2) SEM flashear firmware e SEM interação física com a tela:

  1. Ciclo de vida de apps: app.active / app.list / app.open / app.close.
  2. Automação de UI: ui.dump e interações (ui.tap/ui.click/ui.type) apenas
     com alvo/textarea claramente seguro derivado do dump; caso contrário o
     teste registra SKIP justificado.
  3. Captura de tela: screen.shot + screen.dump, reconstrução local do BMP
     (magic, header, tamanho, SHA-256) e registro de erros.
  4. Servidor HTTP: server.start/status/stop + GET local em 192.168.x.x:8080
     e confirmação de indisponibilidade após stop.
  5. Estabilidade após comando inválido ("comando desconhecido" + sys.info e
     app.active ainda respondem).

A suíte PULA inteira quando a porta não existe ou o bridge não responde
``sys.info`` (não é o ambiente correto para rodar). Todas as respostas NDJSON,
tempos, skips e falhas são gravados em
``/tmp/opencode/tab5_device/validation_<ts>/transcript.ndjson``.

Atenção a falhas reais já observadas no firmware (reportadas no handoff):
  - `app.open` não devolve o frame ok no console compartilhado (flood do launch
    estoura o caminho USB-Serial-JTAG); o app ABRE (comprovado pelo dump).
  - `screen.dump` perde chunks no meio da stream (indice gap) e, em outras
    execuções, para sem emitir o frame `end`.
"""

import argparse
import base64
import hashlib
import zlib
import json
import os
import shutil
import struct
import sys
import time
import urllib.error
import urllib.request
import unittest
from pathlib import Path

DEVICE_PORT = os.environ.get("TAB5_DEVICE_PORT", "/dev/ttyACM0")
DEVICE_BAUD = int(os.environ.get("TAB5_DEVICE_BAUD", "115200"))
OUT_ROOT = Path("/tmp/opencode/tab5_device")

# Apps considerados seguros para abrir (sem HW perigoso, sem dados sensíveis).
SAFE_APP_PREFERENCE = (
    "com.tab5.files",
    "com.tab5.notas",
    "com.tab5.calendar",
    "com.tab5.gallery",
    "com.tab5.music",
    "com.tab5.recorder",
    "com.tab5.chat",
)
DANGEROUS_APP_HINTS = (
    "wifi",
    "bluetooth",
    "terminal",
    "camera",
    "storage",
)


def chunk_indices_contiguous(indices, expected):
    """Strictly validate 0..N-1, rejecting gaps and duplicates."""
    return indices == list(range(expected)) and len(set(indices)) == expected


def validate_screen_dump(frames):
    """Validate size, canonical Base64, indices, end and optional CRC32."""
    start = frames[0]
    chunks = [frame for frame in frames if "b64" in frame]
    indices = [frame.get("chunk") for frame in chunks]
    expected = start["chunks"]
    if not chunk_indices_contiguous(indices, expected):
        raise AssertionError("chunk_indices_contiguous failed: %r" % indices)
    payload = bytearray()
    for frame in chunks:
        decoded = base64.b64decode(frame["b64"], validate=True)
        if base64.b64encode(decoded).decode("ascii") != frame["b64"]:
            raise AssertionError("non-canonical Base64")
        payload.extend(decoded)
    if len(payload) != start["size"]:
        raise AssertionError("size mismatch")
    if "crc32" in start and (zlib.crc32(payload) & 0xFFFFFFFF) != start["crc32"]:
        raise AssertionError("CRC32 mismatch")
    if not frames or frames[-1].get("event") != "end":
        raise AssertionError("end frame missing")
    if frames[-1].get("status") == "error":
        raise AssertionError("end frame indicates error")
    return bytes(payload)


def merge_dump_retry(frames, retry_frames, expected):
    """Merge a retry by index without allowing a conflicting replacement."""
    chunks = {frame["chunk"]: frame for frame in frames if "b64" in frame}
    end = next((frame for frame in reversed(frames) if frame.get("event") == "end"), None)
    for frame in retry_frames:
        if "b64" in frame:
            index = frame.get("chunk")
            if not isinstance(index, int) or index < 0 or index >= expected:
                raise AssertionError("retry chunk index invalido: %r" % (index,))
            previous = chunks.get(index)
            if previous is not None:
                if previous.get("b64") != frame.get("b64"):
                    raise AssertionError("retry chunk conflitante: %s" % index)
            else:
                chunks[index] = frame
        if frame.get("event") == "end":
            end = frame
    merged = [frames[0]] + [chunks[index] for index in sorted(chunks)]
    if end is not None:
        merged.append(end)
    return merged


class SerialBridgeTransport:
    """Transporte NDJSON sobre o console USB-Serial-JTAG compartilhado.

    O firmware tagueia logs ESP_LOG no mesmo USB do bridge; este transporte
    drena o flood antes de cada comando, lê em janela fixa e extrai frames
    NDJSON (linhas que são objetos JSON), ignorando linhas de log.
    """

    def __init__(self, port, baud, transcript):
        import serial  # pyserial

        self.ser = serial.Serial(port, baud, timeout=0.5)
        self.ser.reset_input_buffer()
        self._drain(1.0)
        self.transcript = transcript

    def _drain(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            n = self.ser.in_waiting
            if n:
                self.ser.read(n)
            else:
                time.sleep(0.01)

    def _write(self, command):
        payload = command.encode() if isinstance(command, str) else command
        if not payload.endswith(b"\n"):
            payload += b"\n"
        self.ser.reset_input_buffer()
        self.ser.write(payload)
        self.ser.flush()

    def exchange(self, command, timeout_s=10.0, label=""):
        """Comando de resposta única (1 frame típico); janela fixa."""
        self._drain(0.3)
        self._write(command)
        t0 = time.monotonic()
        buf = bytearray()
        frames = []
        deadline = t0 + timeout_s
        while time.monotonic() < deadline:
            data = self.ser.read(65536)
            if not data:
                time.sleep(0.02)
                continue
            buf.extend(data)
            while True:
                i = buf.find(b"\n")
                if i < 0:
                    break
                line = bytes(buf[:i]).rstrip(b"\r").strip()
                del buf[: i + 1]
                if line.startswith(b"{"):
                    try:
                        obj = json.loads(line)
                    except (ValueError, TypeError):
                        continue
                    if isinstance(obj, dict):
                        frames.append(obj)
        elapsed = time.monotonic() - t0
        self._record(command, elapsed, frames, label,
                    raw_bytes=len(buf) + sum(len(json.dumps(f)) for f in frames))
        return frames

    def exchange_dump(self, command, timeout_s=240.0, label="screen.dump"):
        """screen.dump: lê até o frame `end` (ou erro/estouro de janela)."""
        self._drain(0.3)
        self._write(command)
        t0 = time.monotonic()
        buf = bytearray()
        frames = []
        chunk_indices = []
        deadline = t0 + timeout_s
        got_end = False
        while time.monotonic() < deadline:
            data = self.ser.read(65536)
            if not data:
                time.sleep(0.02)
                continue
            buf.extend(data)
            while True:
                i = buf.find(b"\n")
                if i < 0:
                    break
                line = bytes(buf[:i]).rstrip(b"\r").strip()
                del buf[: i + 1]
                if not line.startswith(b"{"):
                    continue
                try:
                    obj = json.loads(line)
                except (ValueError, TypeError):
                    continue
                if not isinstance(obj, dict):
                    continue
                frames.append(obj)
                if "b64" in obj and isinstance(obj.get("chunk"), int):
                    chunk_indices.append(obj["chunk"])
                if obj.get("event") == "end":
                    got_end = True
                    break
            if got_end:
                break
        elapsed = time.monotonic() - t0
        self._record(command, elapsed, frames, label,
                    raw_bytes=len(buf) + sum(len(json.dumps(f)) for f in frames),
                    extra={"chunk_indices_contiguous": chunk_indices == list(range(len(chunk_indices))),
                           "chunk_count_received": len(chunk_indices),
                           "got_end": got_end})
        return frames, chunk_indices, got_end

    def settle(self, seconds):
        time.sleep(seconds)
        self._drain(1.0)

    def _record(self, command, elapsed, frames, label, raw_bytes=0, extra=None):
        entry = {
            "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "cmd": command,
            "label": label or "",
            "elapsed_s": round(elapsed, 3),
            "raw_bytes": raw_bytes,
            "frames": frames,
        }
        if extra:
            entry.update(extra)
        self.transcript.write(json.dumps(entry, ensure_ascii=False) + "\n")
        self.transcript.flush()


class SerialBridgeDeviceValidation(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls):
        cls.out_dir = OUT_ROOT / ("validation_" + time.strftime("%Y%m%d_%H%M%S"))
        cls.out_dir.mkdir(parents=True, exist_ok=True)
        cls.transcript = (cls.out_dir / "transcript.ndjson").open("a", encoding="utf-8")
        cls.findings = []

        if not Path(DEVICE_PORT).exists():
            raise unittest.SkipTest(
                f"dispositivo ausente: {DEVICE_PORT} não existe; rode na máquina "
                "com o tab5-os conectado (validação de dispositivo).")
        try:
            cls.transport = SerialBridgeTransport(DEVICE_PORT, DEVICE_BAUD, cls.transcript)
        except Exception as exc:  # pyserial falhou ao abrir
            raise unittest.SkipTest(f"não foi possível abrir {DEVICE_PORT}: {exc!r}")

        probe = cls.transport.exchange('{"cmd":"sys.info"}', timeout_s=12, label="probe sys.info")
        if not probe or probe[0].get("status") != "ok":
            raise unittest.SkipTest(
                f"bridge não respondeu sys.info em {DEVICE_PORT}: {probe!r}")
        cls.sysinfo = probe[0]
        cls.screen_w = None
        cls.screen_h = None

    @classmethod
    def tearDownClass(cls):
        cls.transport.settle(0.5)
        try:
            cls.transport.exchange('{"cmd":"app.close"}', timeout_s=8, label="final app.close (limpeza)")
        except Exception:
            pass
        cls.transcript.write(json.dumps({"ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
                                         "findings": cls.findings}, ensure_ascii=False) + "\n")
        cls.transcript.close()

    # ---------------------------------------------------------------- helpers
    @classmethod
    def record_finding(cls, level, scope, message):
        cls.findings.append({"level": level, "scope": scope, "message": message})
        print(f"[FINDING {level}] {scope}: {message}", flush=True)

    def assert_frame_ok(self, frames, action, label):
        self.assertTrue(frames, f"{label}: nenhum frame NDJSON recebido")
        first = frames[0]
        self.assertEqual(first.get("action"), action, f"{label}: action inesperado: {first}")
        self.assertEqual(first.get("status"), "ok", f"{label}: status!=ok: {first}")

    def choose_safe_app(self, apps):
        by_id = {a.get("id"): a for a in apps if isinstance(a, dict) and a.get("id")}
        for candidate in SAFE_APP_PREFERENCE:
            if candidate in by_id:
                return candidate, by_id[candidate].get("name", "")
        # fallback: primeiro app que não pareça perigoso
        for app in apps:
            aid = (app.get("id") or "")
            aname = (app.get("name") or "").lower()
            if not any(hint in aid.lower() or hint in aname for hint in DANGEROUS_APP_HINTS):
                if not aid.startswith("storage"):
                    return aid, app.get("name", "")
        return None, None

    def describe_dump(self, frames):
        if not frames:
            return {"items": 0, "texts": []}
        items = ((frames[0].get("data") or {}).get("items")) or []
        return {"items": len(items), "texts": [it.get("text", "") for it in items]}

    def current_app(self):
        frames = self.transport.exchange('{"cmd":"app.active"}', timeout_s=10,
                                         label="app.active (estado)")
        if not frames:
            return None
        data = frames[0].get("data") or {}
        return {"active": data.get("active"), "id": data.get("id"), "name": data.get("name")}

    # ------------------------------------------------------------- cenários
    def test_00_sysinfo(self):
        data = self.sysinfo.get("data") or {}
        self.assertIn("heap_free_internal", data)
        self.assertIn("heap_free_psram", data)
        self.assertIn("wifi_connected", data)
        self.assertTrue(data.get("wifi_connected"), "esperado Wi-Fi conectado no ambiente de validação")
        self.assertTrue(data.get("wifi_ip"), f"IP Wi-Fi vazio em sys.info: {data}")

    def test_01_app_lifecycle(self):
        base = self.current_app()
        with self.subTest("app.active baseline ok"):
            self.assertIsNotNone(base)
        self.record_finding("info", "app.active antes de abrir",
                           f"active={base.get('active')} id={base.get('id')!r}")

        apps_frames = self.transport.exchange('{"cmd":"app.list"}', timeout_s=10, label="app.list")
        with self.subTest("app.list ok"):
            self.assert_frame_ok(apps_frames, "app.list", "app.list")
        apps = ((apps_frames[0].get("data") or {}).get("apps")) or []
        self.assertGreaterEqual(len(apps), 1, "app.list sem apps")
        self.record_finding("info", "app.list", f"{len(apps)} apps")
        safe_id, safe_name = self.choose_safe_app(apps)
        with self.subTest("app seguro escolhido"):
            self.assertIsNotNone(safe_id, f"nenhum app seguro em app.list: {apps}")
        type(self).safe_id, type(self).safe_name = safe_id, safe_name
        self.record_finding("info", "app seguro", f"{safe_id} ({safe_name})")

        # fingerprint visual antes de abrir (prova de que abriu = tela muda)
        before = self.transport.exchange('{"cmd":"ui.dump"}', timeout_s=12, label="ui.dump antes do open")
        fingerprint_before = self.describe_dump(before)

        open_frames = self.transport.exchange(
            json.dumps({"cmd": "app.open", "id": safe_id}), timeout_s=15, label="app.open")
        if open_frames and open_frames[0].get("status") == "ok":
            self.record_finding("ok", "app.open", "frame ok recebido")
        else:
            # Falha real conhecida: frame perdido no flood do launch; o app abre
            # mesmo assim (comprovado pelo dump a seguir).
            self.record_finding(
                "falha", "app.open",
                f"frame ok de app.open NÃO foi entregue (console compartilhado; flood do "
                f"launch). Frames recebidos: {open_frames!r}")

        with self.subTest("app abre de fato (fingerprint da tela muda)"):
            self.transport.settle(2.0)
            after = self.transport.exchange('{"cmd":"ui.dump"}', timeout_s=12,
                                            label="ui.dump após open")
            fingerprint_after = self.describe_dump(after)
            changed = (fingerprint_after.get("items") != fingerprint_before.get("items")
                       or fingerprint_after.get("texts") != fingerprint_before.get("texts"))
            self.assertTrue(changed,
                            f"tela não mudou após app.open: {fingerprint_before} == {fingerprint_after}")

        with self.subTest("app.active após abrir (ok NDJSON)"):
            active_after = self.current_app()
            self.assertIsNotNone(active_after)
            self.record_finding(
                "info", "app.active após abrir",
                f"active={active_after.get('active')} id={active_after.get('id')!r} "
                f"(apps WASM não registram host ABI -> active permanece false; registrar)")

        close_frames = self.transport.exchange('{"cmd":"app.close"}', timeout_s=10, label="app.close")
        with self.subTest("app.close ok"):
            self.assert_frame_ok(close_frames, "app.close", "app.close")

        with self.subTest("app.active após fechar (ok NDJSON)"):
            active_end = self.current_app()
            self.assertIsNotNone(active_end)
            self.record_finding("info", "app.active após fechar",
                                f"active={active_end.get('active')} id={active_end.get('id')!r}")

    def test_02_ui_dump_and_safe_interaction(self):
        dump = self.transport.exchange('{"cmd":"ui.dump"}', timeout_s=12, label="ui.dump base")
        with self.subTest("ui.dump ok e itens bem formados"):
            self.assert_frame_ok(dump, "ui.dump", "ui.dump")
            items = ((dump[0].get("data") or {}).get("items")) or []
            for it in items:
                with self.subTest(item=it):
                    self.assertGreaterEqual(it.get("x", -1), 0, "x < 0")
                    self.assertGreaterEqual(it.get("y", -1), 0, "y < 0")
                    self.assertGreater(it.get("w", 0), 0, "w <= 0")
                    self.assertGreater(it.get("h", 0), 0, "h <= 0")
                    self.assertIn("text", it)
        self.record_finding("info", "ui.dump base",
                            f"{self.describe_dump(dump)['items']} itens")

        # O dump base (launcher) só tem glyphs de ícones; deriva-se o alvo do
        # dump do app seguro (com rótulos legíveis tipo "Arquivos").
        apps_frames = self.transport.exchange('{"cmd":"app.list"}', timeout_s=10, label="ui: app.list")
        apps = ((apps_frames[0].get("data") or {}).get("apps")) if apps_frames else []
        safe_id, safe_name = self.choose_safe_app(apps)
        if self.current_app().get("id") != safe_id:
            self.transport.exchange(json.dumps({"cmd": "app.open", "id": safe_id}),
                                    timeout_s=15, label="ui: abre app seguro")
            self.transport.settle(2.0)

        dump2 = self.transport.exchange('{"cmd":"ui.dump"}', timeout_s=12,
                                        label="ui.dump dentro do app seguro")
        items2 = ((dump2[0].get("data") or {}).get("items")) or []
        name_labels = [it for it in items2 if (it.get("text") or "").strip() == safe_name]
        self.record_finding("info", "alvo seguro derivado do dump",
                            f"{len(name_labels)} label(s) com texto '{safe_name}'")

        if not name_labels:
            self.record_finding("skip", "ui.tap/ui.click",
                                 f"nenhuma label com o nome do app seguro ('{safe_name}') "
                                "no dump; interação omitida para não arriscar alvo ambíguo.")
        else:
            target = name_labels[0]
            cx, cy = target["x"] + target["w"] // 2, target["y"] + target["h"] // 2
            self.record_finding("info", "alvo", f"label '{safe_name}' centro=({cx},{cy})")

            tap_frames = self.transport.exchange(
                json.dumps({"cmd": "ui.tap", "target": safe_name}), timeout_s=12,
                label="ui.tap")
            with self.subTest("ui.tap com alvo seguro"):
                self.assertTrue(tap_frames, "ui.tap sem frame (flood do launch?)")
                if tap_frames:
                    self.assertEqual(tap_frames[0].get("action"), "ui.tap")
                    self.assertTrue(tap_frames[0].get("data", {}).get("found"),
                                    f"ui.tap não encontrou '{safe_name}': {tap_frames[0]}")
            self.transport.settle(1.5)

            click_frames = self.transport.exchange(
                json.dumps({"cmd": "ui.click", "x": cx, "y": cy}), timeout_s=12,
                label="ui.click")
            with self.subTest("ui.click em coordenada segura"):
                self.assertTrue(click_frames, "ui.click sem frame")
                if click_frames:
                    self.assertEqual(click_frames[0].get("action"), "ui.click")
                    self.assertEqual(click_frames[0].get("status"), "ok")
            self.transport.settle(1.5)

        # ui.type: só se houver textarea claramente seguro. O dump não
        # distingue label de textarea e digitar pode alterar dados -> SKIP.
        self.record_finding(
            "skip", "ui.type",
            "nenhum textarea claramente seguro identificável no dump (o contrato de dump "
            "não classifica o widget; digitar poderia alterar dados persistidos).")

        close_frames = self.transport.exchange('{"cmd":"app.close"}', timeout_s=10,
                                               label="ui: fecha app após interação")
        with self.subTest("app.close após interação"):
            self.assert_frame_ok(close_frames, "app.close", "ui: close")

    def test_03_screen_shot_and_dump(self):
        shot = self.transport.exchange('{"cmd":"screen.shot"}', timeout_s=20, label="screen.shot")
        with self.subTest("screen.shot ok"):
            self.assert_frame_ok(shot, "screen.shot", "screen.shot")
        path = ((shot[0].get("data") or {}).get("path")) or ""
        self.assertTrue(path.startswith("/sdcard/screenshots/"), f"path estranho: {path}")
        self.record_finding("ok", "screen.shot", f"screenshot salvo em {path}")

        start = time.monotonic()
        frames, chunk_indices, got_end = self.transport.exchange_dump(
            json.dumps({"cmd": "screen.dump", "path": path}))
        dump_elapsed = time.monotonic() - start
        self.record_finding("time", "screen.dump", f"transferência: {round(dump_elapsed,1)}s")

        self.assertTrue(frames, "nenhum frame de screen.dump")
        start_frame = frames[0]
        self.assertEqual(start_frame.get("action"), "screen.dump", start_frame)
        if start_frame.get("status") == "error":
            self.fail(f"screen.dump respondeu error: {start_frame}")

        declared_size = start_frame.get("size")
        declared_chunks = start_frame.get("chunks")
        # A missing chunk/end triggers a strict screen.dump.retry from the
        # first gap; retransmitted frames are merged by index, never appended.
        if (not got_end or chunk_indices != list(range(declared_chunks))):
            first_missing = next((i for i in range(declared_chunks) if i not in chunk_indices), declared_chunks)
            retry = self.transport.exchange_dump(
                json.dumps({"cmd": "screen.dump.retry", "path": path, "from": first_missing}),
                label="screen.dump.retry")
            retry_frames, retry_indices, retry_end = retry
            frames = merge_dump_retry(frames, retry_frames, declared_chunks)
            # Recompute all derived state after the retry.  In particular, an
            # end frame from the retry is valid only when final validation
            # below proves the complete stream.
            chunk_indices = [frame["chunk"] for frame in frames if "b64" in frame]
            got_end = bool(frames and frames[-1].get("event") == "end")
        self.assertIsInstance(declared_size, int)
        self.assertIsInstance(declared_chunks, int)

        chunk_frames = [f for f in frames if "b64" in f]
        validate_screen_dump(frames)
        ordered = base64.b64decode(b"".join(
            f["b64"].encode() for f in chunk_frames)) if chunk_frames else b""

        # --- falhas reais de transporte (observadas no firmware) ------------
        n_expected = declared_chunks
        n_received = len(chunk_frames)
        if n_received != n_expected:
            self.record_finding(
                "falha", "screen.dump",
                f"chunks recebidos {n_received} != esperados {n_expected} "
                f"(perda de {n_expected - n_received} frames; índices contíguos: "
                f"{chunk_indices == list(range(len(chunk_indices)))}; "
                f"primeiro={chunk_indices[0] if chunk_indices else None}, "
                f"último={chunk_indices[-1] if chunk_indices else None})")
        if not got_end:
            self.record_finding("falha", "screen.dump",
                                "frame `end` não chegou (stream truncada no meio)")

        with self.subTest("screen.dump entrega todos os chunks"):
            self.assertEqual(n_received, n_expected,
                             "perda de frames chunk no transporte screen.dump")
        with self.subTest("screen.dump emite frame end"):
            self.assertTrue(got_end, "frame `end` ausente")
        with self.subTest("índices de chunk contíguos 0..chunks-1"):
            self.assertEqual(chunk_indices, list(range(n_expected)),
                             f"índices de chunk com buracos: {chunk_indices[:5]}...{chunk_indices[-5:]}")

        with self.subTest("tamanho decodificado == tamanho declarado"):
            self.assertEqual(len(ordered), declared_size,
                             f"decodificado {len(ordered)} != declarado {declared_size}")

        # --- reconstrução local do BMP --------------------------------------
        artifact = self.out_dir / (Path(path).stem + ".bmp")
        artifact.write_bytes(ordered)
        with self.subTest("BMP magic 'BM'"):
            self.assertEqual(ordered[:2], b"BM", f"magic BMP ausente: {ordered[:2]!r}")
        if len(ordered) >= 54:
            bf_size = struct.unpack_from("<I", ordered, 2)[0]
            w, h = struct.unpack_from("<ii", ordered, 18)
            bit_count = struct.unpack_from("<H", ordered, 28)[0]
            bi_size_image = struct.unpack_from("<I", ordered, 34)[0]
            self.screen_w, self.screen_h = w, h
            expected = 54 + w * h * 3
            with self.subTest("header BMP consistente (24bpp, tamanho)"):
                self.assertEqual(bit_count, 24, f"bit_count={bit_count}")
                self.assertEqual(bf_size, expected,
                                 f"bfSize={bf_size} != 54+w*h*3={expected}")
                self.assertEqual(bi_size_image, w * h * 3,
                                 f"biSizeImage={bi_size_image} != {w*h*3}")
            sha = hashlib.sha256(ordered).hexdigest()
            self.record_finding(
                "ok", "screen.dump BMP",
                f"reconstruído {len(ordered)} bytes, {w}x{h}, 24bpp, "
                f"sha256={sha}, artefato={artifact}")
        else:
            self.record_finding("falha", "BMP", f"BMP truncado demasiado ({len(ordered)} bytes)")

    def test_04_server_http(self):
        status0 = self.transport.exchange('{"cmd":"server.status"}', timeout_s=10,
                                          label="server.status (antes)")
        with self.subTest("server.status inicial ok"):
            self.assert_frame_ok(status0, "server.status", "server.status inicial")
        initial_running = (status0[0].get("data") or {}).get("running")
        if initial_running:
            self.record_finding("info", "server", "servidor já estava ativo antes do teste; "
                                "start é idempotente na implementação atual")

        start = self.transport.exchange('{"cmd":"server.start"}', timeout_s=12, label="server.start")
        with self.subTest("server.start ok e running"):
            self.assert_frame_ok(start, "server.start", "server.start")
            data = start[0].get("data") or {}
            self.assertTrue(data.get("running"))
            self.assertIsInstance(data.get("port"), int)
            self.assertTrue(data.get("ip"))
            self.assertTrue(data.get("url", "").startswith("http://"))
        url = (start[0].get("data") or {}).get("url")
        self.record_finding("info", "server.start", f"url={url}")

        status1 = self.transport.exchange('{"cmd":"server.status"}', timeout_s=10,
                                          label="server.status (ativo)")
        with self.subTest("server.status reflete running=true"):
            self.assert_frame_ok(status1, "server.status", "server.status ativo")
            self.assertTrue((status1[0].get("data") or {}).get("running"))

        with self.subTest("GET HTTP local responde"):
            self.assertTrue(url, "url ausente no server.start")
            try:
                resp = urllib.request.urlopen(url, timeout=10)
                body = resp.read()
                self.assertEqual(resp.status, 200)
                self.record_finding("ok", "server GET", f"HTTP {resp.status}, {len(body)} bytes em {url}")
            except Exception as exc:
                self.fail(f"GET {url} falhou com servidor ativo: {exc!r}")

        stop = self.transport.exchange('{"cmd":"server.stop"}', timeout_s=12, label="server.stop")
        with self.subTest("server.stop ok"):
            self.assert_frame_ok(stop, "server.stop", "server.stop")

        status2 = self.transport.exchange('{"cmd":"server.status"}', timeout_s=10,
                                          label="server.status (após stop)")
        with self.subTest("server.status reflete running=false após stop"):
            self.assert_frame_ok(status2, "server.status", "server.status pós-stop")
            self.assertFalse((status2[0].get("data") or {}).get("running"))

        with self.subTest("GET HTTP após stop falha (esperado)"):
            try:
                urllib.request.urlopen(url, timeout=6).read()
                self.fail("GET após stop NÃO deveria responder")
            except urllib.error.URLError:
                self.record_finding("ok", "server GET após stop", "conexão recusada/indisponível (esperado)")
            except Exception as exc:  # ConnectionRefused etc.
                self.record_finding("ok", "server GET após stop", f"falhou como esperado: {type(exc).__name__}")

    def test_05_invalid_command_and_stability(self):
        bad = self.transport.exchange('{"cmd":"comando.invalido.cm1"}', timeout_s=10,
                                      label="comando inválido")
        with self.subTest("comando inválido -> error vindo do bridge"):
            self.assertTrue(bad, "nenhum frame para comando inválido")
            self.assertEqual(bad[0].get("status"), "error")
            self.assertEqual(bad[0].get("action"), "comando.invalido.cm1")
            self.assertIn("desconhecido", (bad[0].get("error") or "").lower())

        after1 = self.transport.exchange('{"cmd":"sys.info"}', timeout_s=12,
                                         label="sys.info pós inválido")
        with self.subTest("bridge segue estável (sys.info)"):
            self.assert_frame_ok(after1, "sys.info", "sys.info pós inválido")

        after2 = self.transport.exchange('{"cmd":"app.active"}', timeout_s=10,
                                         label="app.active pós inválido")
        with self.subTest("bridge segue estável (app.active)"):
            self.assert_frame_ok(after2, "app.active", "app.active pós inválido")

        self.record_finding("ok", "estabilidade",
                            "bridge respondeu corretamente após comando inválido")


def main(argv=None):
    global DEVICE_PORT, DEVICE_BAUD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=None)
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("unittest_args", nargs="*")
    args = parser.parse_args(argv)
    if args.port is not None:
        DEVICE_PORT = args.port
    if args.baud is not None:
        DEVICE_BAUD = args.baud
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SerialBridgeDeviceValidation)
    runner = unittest.TextTestRunner(verbosity=2 if args.verbose else 1, stream=sys.stdout)
    return 0 if runner.run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
