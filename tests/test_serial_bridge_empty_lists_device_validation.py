#!/usr/bin/env python3
"""Validação device-in-the-loop: listas BLE/Wi-Fi não vazias + ui.dump.

Complementa tests/test_serial_bridge_device_validation.py com o critério
final do plano aprovado (sem simulador, validação exclusivamente via serial):

  >= 1 rede Wi-Fi e >= 1 dispositivo BLE visíveis no dispositivo físico Tab5.

O bridge NDJSON (console USB-Serial-JTAG compartilhado com ESP_LOG) precisa
expor os comandos sincronos `wifi.scan` e `ble.scan` (timeout_ms + resultados),
que percorrem exatamente o mesmo caminho das Host ABIs usadas pelos apps
(`tab5_wifi_scan`/`tab5_bt_scan`, que delegam aos wrappers com timeout
`tab5_wifi_scan_with_timeout`/`tab5_bt_scan_with_timeout` (ESP_ERR_INVALID_STATE) — ver os contracts estáticos em
`../../tab5-app-wifi/tests/test_wifi_scan_serialize_retry_contracts.py` e
`../../tab5-app-bluetooth/tests/test_ble_bridge_sync_contracts.py`.

Sequência validada:
  1. Descoberta de porta (TAB5_DEVICE_PORT -> /dev/ttyACM0 -> list_ports).
     Auto-skip SOMENTE se nenhuma porta existir ou o bridge não responder
     sys.info — mas sempre registra/informa o resultado da descoberta.
  2. sys.info: bridge acessível, Wi-Fi conectado no ambiente de validação.
  3. wifi.scan -> data.count >= 1, aps[] bem formados (ssid, rssi).
   4. ble.scan  -> data.count >= 1, devices[] bem formados (mac, rssi; name opcional
      — MAC e o identificador do dispositivo BLE).
  5. ui.dump dentro do app Wi-Fi: lista renderiza >= 1 SSID do wifi.scan e
     NÃO mostra o estado vazio "Nenhuma rede encontrada no alcance".
  6. ui.dump dentro do app Bluetooth: lista renderiza >= 1 MAC do ble.scan e
     NÃO mostra "Nenhum dispositivo BLE encontrado".
  7. Estabilidade pós-ciclo (sys.info responde).

Registro: transcript NDJSON em /tmp/opencode/tab5_device/empty_lists_<ts>/.
"""

import argparse
import json
import os
import re
import sys
import time
import unittest
from pathlib import Path

DEVICE_BAUD = int(os.environ.get("TAB5_DEVICE_BAUD", "115200"))
OUT_ROOT = Path("/tmp/opencode/tab5_device")
DEFAULT_PORTS = ("/dev/ttyACM0",)

APP_WIFI_ID = "com.tab5.wifi"
APP_BT_ID = "com.tab5.bluetooth"

WIFI_EMPTY_HINT = "Nenhuma rede encontrada"
BT_EMPTY_HINT = "Nenhum dispositivo BLE"
# LV_SYMBOL_REFRESH (lv_symbol_def.h) = U+F021 = "\xEF\x80\xA1"; a label do
# botão de scan do app Wi-Fi começa com esse glifo: find_ui casa por strstr.
LV_SYMBOL_REFRESH_ESCAPE = "\uf021"

MAC_RE = re.compile(r"^([0-9A-F]{2}:){5}[0-9A-F]{2}$")


def discover_port(candidates=DEFAULT_PORTS, env_port=None, use_list_ports=True) -> tuple:
    """Descobre a porta serial do Tab5. Retorna (porta, fonte) ou (None, motivo).

    Ordem: TAB5_DEVICE_PORT -> candidatos padrão -> serial.tools.list_ports
    (USB Serial / Espressif 303a / CP210x 10c4 / CH340 1a86).
    use_list_ports=False desabilita o fallback (para testes self-check determinísticos).
    """
    if env_port:
        if Path(env_port).exists():
            return env_port, "TAB5_DEVICE_PORT"
        return None, f"TAB5_DEVICE_PORT={env_port!r} definida mas o caminho não existe"
    for candidate in candidates:
        if Path(candidate).exists():
            return candidate, f"candidato padrão ({candidate})"
    if not use_list_ports:
        return None, "nenhum candidato padrão existe (use_list_ports=False desabilita fallback)"
    try:
        from serial.tools import list_ports
    except Exception:
        return None, "nenhum candidato padrão existe e pyserial ausente (list_ports indisponível)"
    try:
        for port in list_ports.comports():
            device = getattr(port, "device", "") or ""
            description = getattr(port, "description", "") or ""
            vid, pid = getattr(port, "vid", None), getattr(port, "pid", None)
            vidpid = f"{vid:04x}:{pid:04x}" if vid and pid else ""
            haystack = description.lower()
            plausible = any(key in haystack for key in
                            ("serial", "esp", "cp210", "ch340", "uart", "jtag", "usb"))
            plausible = plausible or any(key in vidpid for key in ("303a", "10c4", "1a86", "1a86"))
            if plausible and Path(device).exists():
                return device, f"list_ports ({device}: {description})"
    except Exception:
        pass
    return None, "nenhuma porta serial plausível encontrada (list_ports vazio ou sem match)"


class SerialBridgeTransport:
    """Transporte NDJSON sobre o console USB-Serial-JTAG compartilhado."""

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

    def exchange(self, command, timeout_s=15.0, label=""):
        self._drain(0.3)
        self._write(command)
        t0 = time.monotonic()
        buf = bytearray()
        frames = []
        request = json.loads(command)
        expected_action = request.get("cmd") if isinstance(request, dict) else None
        expected_rid = request.get("rid") if isinstance(request, dict) else None
        got_response = False
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
                        if (obj.get("action") in (None, expected_action)
                                and ("rid" not in request or obj.get("rid") == expected_rid)):
                            got_response = True
                            break
            if got_response:
                break
        elapsed = time.monotonic() - t0
        self.transcript.write(json.dumps(
            {"ts": time.strftime("%Y-%m-%dT%H:%M:%S"), "cmd": command, "label": label,
             "elapsed_s": round(elapsed, 3), "frames": frames},
            ensure_ascii=False) + "\n")
        self.transcript.flush()
        return frames

    def settle(self, seconds):
        time.sleep(seconds)
        self._drain(1.0)


class EmptyListsDeviceValidation(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls):
        cls.out_dir = OUT_ROOT / ("empty_lists_" + time.strftime("%Y%m%d_%H%M%S"))
        cls.out_dir.mkdir(parents=True, exist_ok=True)
        cls.transcript = (cls.out_dir / "transcript.ndjson").open("a", encoding="utf-8")
        cls.findings = []

        # --- descoberta de porta -------------------------------------------------
        port, source = discover_port(env_port=os.environ.get("TAB5_DEVICE_PORT"))
        print(f"[DESCOBERTA PORTA] fonte={source}", flush=True)
        if port is None:
            cls.record_skip(
                "porta serial",
                f"nenhuma porta encontrada ({source}); validação device-in-the-loop "
                "não executada — conecte o Tab5 e rode na máquina host",
            )
            raise unittest.SkipTest(
                f"nenhuma porta serial encontrada ({source}); validação de dispositivo")

        cls.device_port = port
        try:
            cls.transport = SerialBridgeTransport(port, DEVICE_BAUD, cls.transcript)
        except Exception as exc:
            cls.record_skip("porta serial",
                            f"não foi possível abrir {port} ({exc!r}); validação não executada")
            raise unittest.SkipTest(f"não foi possível abrir {port}: {exc!r}")

        probe = cls.transport.exchange('{"cmd":"sys.info","rid":"probe-1"}',
                                       timeout_s=12, label="probe sys.info")
        if not probe or probe[0].get("status") != "ok":
            cls.record_skip(
                "bridge",
                f"bridge não respondeu sys.info em {port}: {probe!r}; validação não executada",
            )
            raise unittest.SkipTest(f"bridge não respondeu sys.info em {port}: {probe!r}")
        cls.sysinfo = probe[0]
        cls.wifi_aps = []     # [{ssid, rssi, ...}] do wifi.scan
        cls.ble_devs = []     # [{mac, name, rssi, ...}] do ble.scan
        cls.app_ids = []

        apps_frames = cls.transport.exchange('{"cmd":"app.list","rid":"apps-1"}',
                                             timeout_s=10, label="app.list")
        if apps_frames and apps_frames[0].get("status") == "ok":
            cls.app_ids = [(a.get("id") or "") for a in ((apps_frames[0].get("data") or {}).get("apps") or [])]
        cls.record_finding("info", "porta", f"{port} ({source}) | apps instalados: {len(cls.app_ids)}")

    @classmethod
    def tearDownClass(cls):
        try:
            cls.transport.exchange('{"cmd":"app.close"}', timeout_s=8,
                                   label="final app.close (limpeza)")
        except Exception:
            pass
        cls.transcript.write(json.dumps(
            {"ts": time.strftime("%Y-%m-%dT%H:%M:%S"), "findings": cls.findings},
            ensure_ascii=False) + "\n")
        cls.transcript.close()

    # ---------------------------------------------------------------- helpers
    @classmethod
    def record_finding(cls, level, scope, message):
        cls.findings.append({"level": level, "scope": scope, "message": message})
        print(f"[FINDING {level}] {scope}: {message}", flush=True)

    @classmethod
    def record_skip(cls, scope, message):
        cls.findings.append({"level": "skip", "scope": scope, "message": message})
        print(f"[FINDING skip] {scope}: {message}", flush=True)

    def assert_frame_ok(self, frames, action, label):
        self.assertTrue(frames, f"{label}: nenhum frame NDJSON recebido")
        first = frames[0]
        self.assertEqual(first.get("action"), action, f"{label}: action inesperado: {first}")
        self.assertEqual(first.get("status"), "ok", f"{label}: status!=ok: {first}")

    def app_present(self, app_id):
        return app_id in self.app_ids

    def dump_texts(self, label="ui.dump"):
        frames = self.transport.exchange('{"cmd":"ui.dump","rid":"dump-1"}',
                                         timeout_s=12, label=label)
        if not frames:
            return [], frames
        items = ((frames[0].get("data") or {}).get("items")) or []
        return [str(it.get("text", "")) for it in items], frames

    def open_app(self, app_id, label):
        frames = self.transport.exchange(json.dumps({"cmd": "app.open", "id": app_id}),
                                         timeout_s=15, label=label)
        if frames and frames[0].get("status") == "ok":
            self.record_finding("ok", "app.open", f"{app_id}: frame ok recebido")
        else:
            # Falha real conhecida no console compartilhado: o frame ok de
            # app.open pode se perder no flood do launch (o app ABRE mesmo
            # assim — comprovado pelos dumps a seguir).
            self.record_finding("info", "app.open",
                                f"{app_id}: frame ok não entregue (flood do launch?); "
                                f"frames={frames!r}")
        self.transport.settle(2.0)

    # ------------------------------------------------------------- cenários
    def test_00_sysinfo_bridge_and_wifi(self):
        data = self.sysinfo.get("data") or {}
        with self.subTest("sys.info ok"):
            self.assertEqual(self.sysinfo.get("status"), "ok", self.sysinfo)
            self.assertEqual(self.sysinfo.get("rid"), "probe-1", "rid não ecoado no probe")
        self.assertIn("heap_free_internal", data)
        self.record_finding(
            "info", "sys.info",
            f"heap_internal={data.get('heap_free_internal')} "
            f"wifi_connected={data.get('wifi_connected')} ip={data.get('wifi_ip')}")
        with self.subTest("ambiente espera Wi-Fi conectado"):
            self.assertTrue(data.get("wifi_connected"),
                            "esperado Wi-Fi conectado no ambiente de validação")
            self.assertTrue(data.get("wifi_ip"), f"IP Wi-Fi vazio: {data}")

    def test_01_wifi_scan_gte1(self):
        """wifi.scan deve expor >= 1 rede com aps[] bem formados."""
        frames = self.transport.exchange(
            json.dumps({"cmd": "wifi.scan", "timeout_ms": 25000, "rid": "wscan-1"}),
            timeout_s=30, label="wifi.scan")
        # Gate decisivo: se o bridge não respondeu o frame ok de wifi.scan
        # (ex.: erro "comando desconhecido"), este assertion falha com o frame
        # completo na mensagem e o teste encerra — sem saltar para asserts
        # irrelevantes (count/isint) que só poluiriam o diagnóstico.
        self.assert_frame_ok(frames, "wifi.scan", "wifi.scan")
        data = frames[0].get("data") or {}
        aps = data.get("aps") or []
        count = data.get("count")
        self.assertIsInstance(count, int, f"count não é int: {frames[0]}")
        self.assertEqual(count, len(aps), f"count={count} != len(aps)={len(aps)}")
        for ap in aps:
            self.assertTrue(str(ap.get("ssid", "")).strip(),
                            f"SSID vazio em AP: {ap}")
            self.assertIsInstance(ap.get("rssi"), int, f"rssi inválido: {ap}")
        self.assertGreaterEqual(
            count, 1,
            "wifi.scan retornou 0 redes no ambiente de validação — o plano "
            "exige >= 1 rede (lista vazia é exatamente a falha a corrigir)")
        type(self).wifi_aps = [
            {"ssid": str(ap.get("ssid", "")), "rssi": ap.get("rssi")} for ap in aps
        ]
        self.record_finding("ok", "wifi.scan",
                            f"{count} rede(s): {[ap['ssid'] for ap in self.wifi_aps]}")

    def test_02_ble_scan_gte1(self):
        """ble.scan deve expor >= 1 dispositivo BLE com devices[] bem formados."""
        frames = self.transport.exchange(
            json.dumps({"cmd": "ble.scan", "timeout_ms": 30000, "rid": "bscan-1"}),
            timeout_s=35, label="ble.scan")
        # Gate decisivo (mesmo critério do test_01): frame ok do ble.scan.
        self.assert_frame_ok(frames, "ble.scan", "ble.scan")
        data = frames[0].get("data") or {}
        devices = data.get("devices") or []
        count = data.get("count")
        self.assertIsInstance(count, int, f"count não é int: {frames[0]}")
        self.assertEqual(count, len(devices),
                         f"count={count} != len(devices)={len(devices)}")
        for dev in devices:
            mac = str(dev.get("mac", ""))
            self.assertRegex(mac, MAC_RE, f"MAC mal formado: {mac!r}")
            self.assertTrue(mac, f"dispositivo BLE sem MAC: {dev}")
            self.assertIsInstance(dev.get("rssi"), int, f"rssi inválido: {dev}")
            # Muitos periféricos BLE transmitem apenas advertisement (MAC +
            # RSSI), sem nome no advertising data. O MAC é o identificador do
            # dispositivo; o campo name é opcional e deve ser string quando presente.
            name = dev.get("name")
            self.assertTrue(name is None or isinstance(name, str),
                            f"name deve ser string ou ausente: {dev}")
        self.assertGreaterEqual(
            count, 1,
            "ble.scan retornou 0 dispositivos no ambiente de validação — o "
            "plano exige >= 1 (lista vazia é exatamente a falha a corrigir)")
        type(self).ble_devs = [
            {"mac": str(d.get("mac", "")), "name": str(d.get("name", "")),
             "rssi": d.get("rssi")} for d in devices
        ]
        self.record_finding("ok", "ble.scan",
                            f"{count} dispositivo(s): {[d['mac'] for d in self.ble_devs]}")

    def test_03_ui_dump_wifi_app_lists_networks(self):
        """ui.dump dentro do app Wi-Fi: lista com >= 1 rede (não vazia)."""
        if not self.app_present(APP_WIFI_ID):
            self.record_finding("skip", "app.wifi",
                                f"{APP_WIFI_ID} não instalado; dump do app Wi-Fi não validado")
            self.skipTest(f"{APP_WIFI_ID} não instalado")
        if not self.wifi_aps:
            self.skipTest("wifi.scan não executou antes (ordem de testes)")

        self.open_app(APP_WIFI_ID, "wifi: app.open")
        texts, frames = self.dump_texts(label="wifi: ui.dump (antes do tap)")
        with self.subTest("ui.dump dentro do app Wi-Fi responde ok"):
            self.assertTrue(frames, "ui.dump sem frame")
            self.assertEqual(frames[0].get("status"), "ok", frames[0])

        matched = self._texts_contain_any(texts, [ap["ssid"] for ap in self.wifi_aps])
        if not matched:
            # O app não escaneia sozinho ao abrir: toca o botão "Escanear"
            # (glifo LV_SYMBOL_REFRESH, U+F021) via ui.tap por símbolo.
            self.record_finding("info", "wifi app",
                                "dump inicial sem SSID do wifi.scan; tentando ui.tap "
                                "no botão Escanear (símbolo REFRESH)")
            tap = self.transport.exchange(
                json.dumps({"cmd": "ui.tap", "symbol": LV_SYMBOL_REFRESH_ESCAPE,
                            "rid": "wifi-tap-1"}),
                timeout_s=12, label="wifi: ui.tap escanear")
            found = bool(tap) and ((tap[0].get("data") or {}).get("found"))
            self.record_finding("info", "wifi app",
                                f"ui.tap Escanear found={found}, frames={tap!r}")
            self.transport.settle(6.0)  # scan síncrono pode levar alguns segundos
            texts, frames = self.dump_texts(label="wifi: ui.dump (após tap)")
            matched = self._texts_contain_any(texts, [ap["ssid"] for ap in self.wifi_aps])

        self._validate_dump_items(frames, "app Wi-Fi")
        self.record_finding("info", "wifi app",
                            f"dump pós-scan: {len([t for t in texts if t.strip()])} "
                            f"labels, ssids casados={matched}")
        with self.subTest("estado vazio NÃO renderizado no app Wi-Fi"):
            self.assertFalse(
                any(WIFI_EMPTY_HINT in t for t in texts),
                "app Wi-Fi renderizou o estado vazio 'Nenhuma rede encontrada' "
                "— lista vazia é exatamente a falha a corrigir")
        with self.subTest("lista Wi-Fi do app contém >= 1 rede do wifi.scan"):
            self.assertTrue(
                matched,
                f"nenhum label do dump contém um SSID do wifi.scan "
                f"({[ap['ssid'] for ap in self.wifi_aps]}); labels={texts}")

    def test_04_ui_dump_bt_app_lists_devices(self):
        """ui.dump dentro do app Bluetooth: lista com >= 1 dispositivo (não vazia)."""
        if not self.app_present(APP_BT_ID):
            self.record_finding("skip", "app.bt",
                                f"{APP_BT_ID} não instalado; dump do app BT não validado")
            self.skipTest(f"{APP_BT_ID} não instalado")
        if not self.ble_devs:
            self.skipTest("ble.scan não executou antes (ordem de testes)")

        self.open_app(APP_BT_ID, "bt: app.open")
        self.transport.settle(6.0)  # o app escaneia ao construir a tela
        texts, frames = self.dump_texts(label="bt: ui.dump (1)")
        matched = self._texts_contain_any(texts, [d["mac"] for d in self.ble_devs])
        if not matched:
            self.transport.settle(5.0)
            texts, frames = self.dump_texts(label="bt: ui.dump (2) após settle")
            matched = self._texts_contain_any(texts, [d["mac"] for d in self.ble_devs])

        self._validate_dump_items(frames, "app Bluetooth")
        self.record_finding("info", "bt app",
                            f"dump pós-scan: {len([t for t in texts if t.strip()])} "
                            f"labels, macs casados={matched}")
        with self.subTest("estado vazio NÃO renderizado no app Bluetooth"):
            self.assertFalse(
                any(BT_EMPTY_HINT in t for t in texts),
                "app Bluetooth renderizou o estado vazio 'Nenhum dispositivo BLE' "
                "— lista vazia é exatamente a falha a corrigir")
        with self.subTest("lista BT do app contém >= 1 dispositivo do ble.scan"):
            self.assertTrue(
                matched,
                f"nenhum label do dump contém um MAC do ble.scan "
                f"({[d['mac'] for d in self.ble_devs]}); labels={texts}")

    def test_05_stability_after_scan_cycle(self):
        after = self.transport.exchange('{"cmd":"sys.info","rid":"stab-1"}',
                                        timeout_s=10, label="sys.info pós ciclo")
        with self.subTest("bridge estável após wifi.scan/ble.scan"):
            self.assert_frame_ok(after, "sys.info", "sys.info pós ciclo")
        self.record_finding("ok", "estabilidade",
                            "bridge respondeu sys.info após wifi.scan + ble.scan + dumps")

    # ------------------------------------------------------------- internals
    def _validate_dump_items(self, frames, scope):
        if not frames:
            return
        data = frames[0].get("data") or {}
        items = data.get("items") or []
        with self.subTest(f"ui.dump itens bem formados ({scope})"):
            for it in items:
                self.assertGreaterEqual(it.get("x", -1), 0, f"x<0: {it}")
                self.assertGreaterEqual(it.get("y", -1), 0, f"y<0: {it}")
                self.assertGreater(it.get("w", 0), 0, f"w<=0: {it}")
                self.assertGreater(it.get("h", 0), 0, f"h<=0: {it}")
                self.assertIn("text", it)

    @staticmethod
    def _texts_contain_any(texts, needles):
        for needle in needles:
            if not needle:
                continue
            if any(needle in text for text in texts):
                return True
        return False


class DevicePortDiscoverySelfCheck(unittest.TestCase):
    """Sanidade do discovery (verde, não depende de dispositivo/bridge)."""

    def test_env_port_absent_reports_reason(self):
        port, source = discover_port(candidates=("/tmp/opencode/nao-existe-xyz",),
                                     env_port="/tmp/opencode/nao-existe-abc")
        self.assertIsNone(port)
        self.assertIn("não existe", source)

    def test_env_port_used_first(self):
        port, source = discover_port(candidates=("/tmp/opencode/nao-existe-xyz",),
                                     env_port=__file__)
        self.assertEqual(port, __file__)
        self.assertEqual(source, "TAB5_DEVICE_PORT")

    def test_default_candidate_used(self):
        port, source = discover_port(candidates=(__file__,), env_port=None)
        self.assertEqual(port, __file__)
        self.assertIn("candidato padrão", source)

    def test_no_port_reports_reason(self):
        """Determinístico: lista candidates vazia + list_ports desabilitado → None."""
        port, source = discover_port(
            candidates=("/tmp/opencode/nao-existe-xyz",),
            env_port=None,
            use_list_ports=False,
        )
        self.assertIsNone(port)
        self.assertTrue(source)

    def test_mac_regex(self):
        self.assertRegex("AA:BB:CC:DD:EE:01", MAC_RE)
        self.assertNotRegex("aa:bb:cc:dd:ee:01", MAC_RE)
        self.assertNotRegex("AA:BB:CC:DD:EE", MAC_RE)


def main(argv=None):
    global DEVICE_BAUD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=None)
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("unittest_args", nargs="*")
    args = parser.parse_args(argv)
    if args.port is not None:
        os.environ["TAB5_DEVICE_PORT"] = args.port
    if args.baud is not None:
        DEVICE_BAUD = args.baud
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(EmptyListsDeviceValidation)
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(DevicePortDiscoverySelfCheck))
    runner = unittest.TextTestRunner(verbosity=2 if args.verbose else 1, stream=sys.stdout)
    return 0 if runner.run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
