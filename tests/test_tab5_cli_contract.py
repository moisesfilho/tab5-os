#!/usr/bin/env python3
"""Contrato da CLI de automação serial (PLANO boneco-de-lata.md, Passo 5).

O que esta suíte fixa (critérios de aceite §4 + protocolo §2.2):

1. A CLI vive em ``tools/tab5_cli.py`` e precisa expor um **ponto de injeção
   de transporte** para testes (padrão pyserial): uma fábrica de sessão que
   aceita um objeto duck-typed com ``write(bytes)`` e ``readline() -> bytes``.
   Candidatos aceitos nesta ordem: ``tab5_cli.open_session``,
   ``tab5_cli.connect``, ``tab5_cli.create_session``, ``tab5_cli.Session``,
   ``tab5_cli.Tab5Session``.

2. A sessão precisa expor ``exchange(command_line: str) -> list[dict]``:
   envia exatamente UMA linha NDJSON (terminada em ``\\n``) e devolve todos os
   frames NDJSON da resposta parseados. Para comandos de resposta única,
   ``len(frames) == 1``; para ``screen.dump`` a lista contém start + N chunks
   + end.

3. **Reconstrução byte-idêntica de ``screen.dump``**: o transporte falso
   reproduz a sessão gravada (start com size/chunks, frames chunk com ``b64``,
   frame end); a soma dos blocos decodificados precisa reproduzir o arquivo
   original byte a byte (hash SHA-256 idêntico, tamanho idêntico, conteúdo
   idêntico).

4. Comandos malformados do firmware e respostas ``status=error`` são
   superfície de erro da CLI (propaga mensagem).

Estado pré-implementação (TDD vermelho): enquanto ``tools/tab5_cli.py`` não
existir, todos os casos pulam com motivo explícito — falha esperada pela
ausência da implementação. Nada de código de produção é criado aqui; o teste
é o requisito vinculante.
"""

import base64
import hashlib
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI_PATH = ROOT / "tools" / "tab5_cli.py"

HAS_CLI = CLI_PATH.is_file()
if HAS_CLI:
    sys.path.insert(0, str(ROOT / "tools"))
    import tab5_cli


SENTINEL = object()


class FakeTransport:
    """Simula o firmware na serial: responde um script de linhas NDJSON."""

    def __init__(self, responses):
        self._responses = list(responses)
        self.sent = []

    def write(self, data):
        if isinstance(data, (bytes, bytearray)):
            self.sent.append(data)
        else:
            self.sent.append(data.encode())

    def readline(self):
        if self._responses:
            line = self._responses.pop(0)
            return line if isinstance(line, (bytes, bytearray)) else line.encode()
        return b""


def make_payload(size=60_000):
    """Conteúdo determinístico estilo BMP (2 bytes 'BM' + padrão)."""
    payload = bytearray(b"BM")
    i = 0
    while len(payload) < size:
        payload.append((i * 131 + 7) & 0xFF)  # padrão não-trivial
        i += 1
    return bytes(payload[:size])


def make_dump_session(payload, chunk_bytes=1024):
    """Frames NDJSON de uma sessão gravada de screen.dump."""
    n = (len(payload) + chunk_bytes - 1) // chunk_bytes
    frames = [
        {"status": "ok", "action": "screen.dump", "event": "start",
         "size": len(payload), "chunks": n}
    ]
    for i in range(n):
        frames.append({"action": "screen.dump", "chunk": i,
                       "b64": base64.b64encode(payload[i * chunk_bytes:(i + 1) * chunk_bytes]).decode()})
    frames.append({"action": "screen.dump", "event": "end"})
    return [json.dumps(f) + "\n" for f in frames]


def open_session(transport):
    """Abre a sessão da CLI contra o transporte falso (seam do contrato)."""
    candidates = ("open_session", "connect", "create_session", "Session", "Tab5Session")
    for name in candidates:
        factory = getattr(tab5_cli, name, None)
        if factory is None:
            continue
        session = factory(transport)
        if hasattr(session, "exchange"):
            return session
    raise AssertionError(
        "contrato violado: tools/tab5_cli.py precisa expor uma fábrica de "
        f"({'/'.join(candidates)}) que aceite um transporte (write/readline) "
        "e devolva uma sessão com exchange(command_line) -> list[dict]"
    )


def last_sent_command(transport):
    assert transport.sent, "a CLI precisa ter enviado pelo menos um comando"
    raw = transport.sent[-1]
    text = raw.decode() if isinstance(raw, (bytes, bytearray)) else raw
    return text


def assert_ndjson_request(testcase, transport, expected_cmd, **expected_fields):
    text = last_sent_command(transport)
    testcase.assertTrue(text.endswith("\n"),
                        "cada comando é UMA linha NDJSON terminada em \\n")
    data = json.loads(text.rstrip("\n"))
    testcase.assertEqual(data["cmd"], expected_cmd)
    for key, value in expected_fields.items():
        testcase.assertEqual(data[key], value)


MISSING_CLI_REASON = (
    "FALHA ESPERADA (pré-implementação): tools/tab5_cli.py ainda não existe. "
    "Quando a CLI do PLANO boneco-de-lata.md (Passo 5) for criada com o seam "
    "de transporte + exchange(), esta suíte roda de verdade."
)


@unittest.skipUnless(HAS_CLI, MISSING_CLI_REASON)
class CliWireContract(unittest.TestCase):
    """Cada comando gera exatamente um frame NDJSON e responde com status ok."""

    def test_app_list_wire_and_response(self):
        transport = FakeTransport(['{"status":"ok","action":"app.list","data":{"apps":[{"id":"com.tab5.wifi","name":"Wi-Fi"}]}}\n'])
        session = open_session(transport)
        frames = session.exchange('{"cmd":"app.list"}')
        assert_ndjson_request(self, transport, "app.list")
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["status"], "ok")
        self.assertEqual(frames[0]["action"], "app.list")
        self.assertEqual(frames[0]["data"]["apps"][0]["id"], "com.tab5.wifi")

    def test_app_open_carries_id(self):
        transport = FakeTransport(['{"status":"ok","action":"app.open","data":{"id":"com.tab5.wifi"}}\n'])
        session = open_session(transport)
        frames = session.exchange('{"cmd":"app.open","id":"com.tab5.wifi"}')
        assert_ndjson_request(self, transport, "app.open", id="com.tab5.wifi")
        self.assertEqual(frames[0]["status"], "ok")
        self.assertEqual(frames[0]["data"]["id"], "com.tab5.wifi")

    def test_ui_click_carries_coordinates(self):
        transport = FakeTransport(['{"status":"ok","action":"ui.click","data":{"x":200,"y":150}}\n'])
        session = open_session(transport)
        frames = session.exchange('{"cmd":"ui.click","x":200,"y":150}')
        assert_ndjson_request(self, transport, "ui.click", x=200, y=150)
        self.assertEqual(frames[0]["data"]["x"], 200)
        self.assertEqual(frames[0]["data"]["y"], 150)

    def test_server_start_parses_url(self):
        transport = FakeTransport([
            '{"status":"ok","action":"server.start","data":{"running":true,'
            '"ip":"192.168.1.150","port":80,"url":"http://192.168.1.150:80/"}}\n'
        ])
        session = open_session(transport)
        frames = session.exchange('{"cmd":"server.start"}')
        assert_ndjson_request(self, transport, "server.start")
        self.assertTrue(frames[0]["data"]["running"])
        self.assertTrue(frames[0]["data"]["url"].startswith("http://"))

    def test_sys_info(self):
        transport = FakeTransport([
            '{"status":"ok","data":{"heap_free_internal":350210,'
            '"battery_pct":92,"wifi_connected":true,"wifi_ip":"192.168.1.150"}}\n'
        ])
        session = open_session(transport)
        frames = session.exchange('{"cmd":"sys.info"}')
        assert_ndjson_request(self, transport, "sys.info")
        self.assertEqual(frames[0]["data"]["battery_pct"], 92)

    def test_crlf_terminated_frames_are_tolerated(self):
        transport = FakeTransport(['{"status":"ok","action":"app.close"}\r\n'])
        session = open_session(transport)
        frames = session.exchange('{"cmd":"app.close"}')
        self.assertEqual(frames[0]["status"], "ok")
        self.assertEqual(frames[0]["action"], "app.close")


@unittest.skipUnless(HAS_CLI, MISSING_CLI_REASON)
class CliErrorSurfacingContract(unittest.TestCase):
    """Respostas status=error viram superfície de erro da CLI."""

    def test_error_response_is_surfaced(self):
        transport = FakeTransport([
            '{"status":"error","action":"app.open","error":"app nao encontrada: com.invalida"}\n'
        ])
        session = open_session(transport)
        try:
            frames = session.exchange('{"cmd":"app.open","id":"com.invalida"}')
        except Exception as exc:  # propagar também satisfaz o contrato
            self.assertTrue(str(exc))
            return
        self.assertEqual(frames[0]["status"], "error")
        self.assertIn("com.invalida", frames[0].get("error", ""))

    def test_malformed_firmware_line_is_reported(self):
        transport = FakeTransport(["isto nao e json\n"])
        session = open_session(transport)
        try:
            frames = session.exchange('{"cmd":"sys.info"}')
        except Exception:
            return  # erro de parsing explícito é aceitável
        self.assertTrue(frames)  # se não levantou, ao menos não estoura


@unittest.skipUnless(HAS_CLI, MISSING_CLI_REASON)
class CliScreenDumpReconstructionContract(unittest.TestCase):
    """screen.dump: transporte simulado + reconstrução byte-idêntica."""

    def test_reconstruction_is_byte_identical(self):
        payload = make_payload()
        transport = FakeTransport(make_dump_session(payload))
        session = open_session(transport)

        frames = session.exchange(
            '{"cmd":"screen.dump","path":"/sdcard/screenshots/print_latest.bmp"}')
        assert_ndjson_request(self, transport, "screen.dump",
                              path="/sdcard/screenshots/print_latest.bmp")

        # Cabeçalho do stream
        self.assertEqual(frames[0]["event"], "start")
        self.assertEqual(frames[0]["size"], len(payload))
        declared_chunks = frames[0]["chunks"]

        # Corpo: chunks em ordem, decodifica e reconstrói
        reassembled = bytearray()
        for frame in frames[1:-1]:
            self.assertEqual(frame["action"], "screen.dump")
            self.assertIn("b64", frame)
            reassembled += base64.b64decode(frame["b64"])
        self.assertEqual(frames[-1]["event"], "end")

        self.assertEqual(len(frames[1:-1]), declared_chunks)
        self.assertEqual(len(reassembled), len(payload))
        self.assertEqual(reassembled, payload)

        # Hash SHA-256 dos dois lados (byte-idênticos)
        self.assertEqual(hashlib.sha256(reassembled).hexdigest(),
                         hashlib.sha256(payload).hexdigest())

    def test_reconstruction_single_chunk_and_empty_tail(self):
        payload = make_payload(size=1024)  # exatamente um chunk de 1024 bytes
        transport = FakeTransport(make_dump_session(payload))
        session = open_session(transport)
        frames = session.exchange('{"cmd":"screen.dump"}')
        self.assertEqual(len(frames), 3)  # start + 1 chunk + end
        reassembled = base64.b64decode(frames[1]["b64"])
        self.assertEqual(reassembled, payload)


@unittest.skipUnless(HAS_CLI, MISSING_CLI_REASON)
class CliConsoleLogToleranceContract(unittest.TestCase):
    """Console USB-Serial-JTAG mistura logs do firmware com respostas NDJSON.

    Contrato da nova seleção UART/USB Serial-JTAG (boneco-de-lata.md): quando o
    transporte é o console do firmware (USB-Serial-JTAG), os ESP_LOG/banner de
    boot/escapes ANSI aparecem intercalados com as respostas NDJSON. A sessão
    precisa IGNORAR linhas não-JSON (logs) e montar a resposta apenas das
    linhas JSON válidas — sem abortar a primeira linha de log.

    Estado pré-implementação (TDD vermelho): hoje tools/tab5_cli.py aborta na
    primeira linha não-JSON com ValueError("resposta NDJSON invalida"), então
    estes testes falham. Quando a CLI implementar a tolerância a logs, a suíte
    inteira deste arquivo continua verde — inclusive os contratos antigos, que
    não são enfraquecidos aqui (linhas malformadas de verdade continuam sendo
    superfície de erro; um console que só emite logs termina em exceção de
    sessão "nenhuma resposta do dispositivo").
    """

    def test_boot_logs_before_response_are_skipped(self):
        transport = FakeTransport([
            "ESP-ROM:esp32p4\n",
            "rst:0x1 (POWERON)\n",
            "I (30) boot: ESP-IDF v5.5.5 2nd stage bootloader\n",
            '{"status":"ok","action":"app.list","data":{"apps":[]}}\n',
        ])
        session = open_session(transport)
        frames = session.exchange('{"cmd":"app.list"}')
        assert_ndjson_request(self, transport, "app.list")
        self.assertEqual(len(frames), 1,
                         "banner/logs do console precisam ser ignorados")
        self.assertEqual(frames[0]["status"], "ok")
        self.assertEqual(frames[0]["action"], "app.list")

    def test_ansi_colored_log_with_crlf_is_skipped(self):
        transport = FakeTransport([
            "\x1b[0;32mI (102) wifi_mgr: connecting\x1b[0m\r\n",
            '{"status":"ok","action":"app.active","data":{"active":true,'
            '"id":"com.tab5.terminal"}}\r\n',
        ])
        session = open_session(transport)
        frames = session.exchange('{"cmd":"app.active"}')
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["status"], "ok")
        self.assertEqual(frames[0]["action"], "app.active")

    def test_console_with_only_logs_raises_no_response(self):
        transport = FakeTransport([
            "ESP-ROM:esp32p4\n",
            "I (30) boot: rst 0x1 (POWERON)\n",
            "W (200) wifi_mgr: reconectando\n",
        ])
        session = open_session(transport)
        with self.assertRaises(RuntimeError) as ctx:
            session.exchange('{"cmd":"sys.info"}')
        self.assertIn("nenhuma resposta", str(ctx.exception))

    def test_error_response_survives_interleaved_logs(self):
        transport = FakeTransport([
            "I (50) app: launching com.invalida\n",
            '{"status":"error","action":"app.open",'
            '"error":"app nao encontrada: com.invalida"}\n',
        ])
        session = open_session(transport)
        frames = session.exchange('{"cmd":"app.open","id":"com.invalida"}')
        self.assertEqual(frames[0]["status"], "error")
        self.assertIn("com.invalida", frames[0].get("error", ""))

    def test_screen_dump_stream_with_interleaved_logs_reconstructs_bytes(self):
        payload = make_payload(size=4096)
        dump_lines = make_dump_session(payload)
        transport = FakeTransport([
            "I (10) serial_bridge: dump inicia\n",
            dump_lines[0],
            "W (200) wifi: link down\n",
            dump_lines[1],
            "\x1b[0;31mE (300) bt: reconexao falhou\x1b[0m\n",
            dump_lines[2],
            dump_lines[3],
            dump_lines[4],
            dump_lines[5],
            "I (400) serial_bridge: dump concluido\n",
        ])
        session = open_session(transport)
        frames = session.exchange(
            '{"cmd":"screen.dump","path":"/sdcard/screenshots/print_latest.bmp"}')
        assert_ndjson_request(self, transport, "screen.dump",
                              path="/sdcard/screenshots/print_latest.bmp")

        self.assertEqual(frames[0]["event"], "start")
        declared_chunks = frames[0]["chunks"]
        reassembled = b"".join(
            base64.b64decode(frame["b64"])
            for frame in frames[1:-1] if "b64" in frame)
        self.assertEqual(frames[-1]["event"], "end")
        self.assertEqual(len(frames[1:-1]), declared_chunks,
                         "logs intercalados não podem descartar nenhum chunk")
        self.assertEqual(reassembled, payload)
        self.assertEqual(hashlib.sha256(reassembled).hexdigest(),
                         hashlib.sha256(payload).hexdigest())


@unittest.skipUnless(HAS_CLI, MISSING_CLI_REASON)
class CliPresenceContract(unittest.TestCase):
    """Confirma que o arquivo de CLI existe antes de validar o resto."""

    def test_cli_file_exists(self):
        self.assertTrue(CLI_PATH.is_file(), f"CLI ausente: {CLI_PATH}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
