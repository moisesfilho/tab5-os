#!/usr/bin/env python3
"""Validação device-in-the-loop do comando sys.idle + eco de rid.

Complementa tests/test_serial_bridge_device_validation.py (que não cobre
sys.idle). Executa via /dev/ttyACM0 (console USB-Serial-JTAG compartilhado
com ESP_LOG): a pilha candidata a corrigir no firmware inclui `sys.idle`
(enable/status/disable com restauração dos timeouts de screensaver e
screen-off).

Sequência validada:
  1. sys.idle baseline (query) -> ok, data.enabled bool, rid eco.
  2. sys.idle enable=true -> ok, enabled=true, rid eco.
  3. sys.idle query -> enabled permanece true.
  4. sys.idle enable=false (disable) -> ok, enabled=false, rid eco.
  5. sys.idle query -> enabled permanece false.
  6. Restauração: devolve o estado ao baseline observado em (1).
  7. Estabilidade pós-ciclo: sys.info e app.active seguem respondendo ok.

sys.idle é não-destrutivo por contrato: os setters *volatile* não gravam NVS
(ui_screen_off/ssaver "Ajuste transitório usado por automação"). A suíte pula
inteira se a porta não existe ou o bridge não responde sys.info.

Transcript NDJSON em /tmp/opencode/tab5_device/sys_idle_validation_<ts>/.
"""

import argparse
import json
import os
import sys
import time
import unittest
from pathlib import Path

DEVICE_PORT = os.environ.get("TAB5_DEVICE_PORT", "/dev/ttyACM0")
DEVICE_BAUD = int(os.environ.get("TAB5_DEVICE_BAUD", "115200"))
OUT_ROOT = Path("/tmp/opencode/tab5_device")


class SerialTransport:
    """Mesmo transporte do suite principal: drena o flood ESP_LOG antes de
    cada comando e extrai frames NDJSON (linhas JSON) das respostas."""

    def __init__(self, port, baud, transcript):
        import serial

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

    def exchange(self, command, timeout_s=6.0, label=""):
        self._drain(0.3)
        self.ser.reset_input_buffer()
        payload = command.encode() if isinstance(command, str) else command
        if not payload.endswith(b"\n"):
            payload += b"\n"
        self.ser.write(payload)
        self.ser.flush()
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
        self.transcript.write(
            json.dumps({"ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
                        "cmd": command, "label": label,
                        "elapsed_s": round(elapsed, 3), "frames": frames},
                       ensure_ascii=False) + "\n")
        self.transcript.flush()
        return frames


class SysIdleDeviceValidation(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls):
        cls.out_dir = OUT_ROOT / ("sys_idle_validation_" + time.strftime("%Y%m%d_%H%M%S"))
        cls.out_dir.mkdir(parents=True, exist_ok=True)
        cls.transcript = (cls.out_dir / "transcript.ndjson").open("a", encoding="utf-8")
        cls.findings = []

        if not Path(DEVICE_PORT).exists():
            raise unittest.SkipTest(
                f"dispositivo ausente: {DEVICE_PORT} não existe; validação de dispositivo")
        try:
            cls.transport = SerialTransport(DEVICE_PORT, DEVICE_BAUD, cls.transcript)
        except Exception as exc:
            raise unittest.SkipTest(f"não foi possível abrir {DEVICE_PORT}: {exc!r}")

        probe = cls.transport.exchange('{"cmd":"sys.info"}', timeout_s=12, label="probe sys.info")
        if not probe or probe[0].get("status") != "ok":
            raise unittest.SkipTest(f"bridge não respondeu sys.info em {DEVICE_PORT}: {probe!r}")
        cls.sysinfo = probe[0]
        cls.baseline_enabled = None

    @classmethod
    def tearDownClass(cls):
        # Restauração de segurança: devolve o estado ao baseline observado.
        if cls.baseline_enabled is not None:
            try:
                cls.transport.exchange(
                    json.dumps({"cmd": "sys.idle", "enable": cls.baseline_enabled}),
                    timeout_s=6, label="teardown restore baseline")
            except Exception:
                pass
        cls.transport.settle_time = None
        try:
            cls.transport.exchange('{"cmd":"app.close"}', timeout_s=6,
                                   label="final app.close (limpeza)")
        except Exception:
            pass
        cls.transcript.write(json.dumps(
            {"ts": time.strftime("%Y-%m-%dT%H:%M:%S"), "findings": cls.findings},
            ensure_ascii=False) + "\n")
        cls.transcript.close()

    @classmethod
    def record_finding(cls, level, scope, message):
        cls.findings.append({"level": level, "scope": scope, "message": message})
        print(f"[FINDING {level}] {scope}: {message}", flush=True)

    def idle_query(self, rid=None, timeout_s=6.0, label="sys.idle query"):
        cmd = {"cmd": "sys.idle"}
        if rid:
            cmd["rid"] = rid
        return self.transport.exchange(json.dumps(cmd), timeout_s=timeout_s, label=label)

    def test_00_sysidle_cycle_and_restore(self):
        # baseline
        base = self.idle_query(rid="sidle-base-1", label="sys.idle baseline")
        with self.subTest("sys.idle baseline ok"):
            self.assertTrue(base, "nenhum frame para sys.idle baseline")
            first = base[0]
            self.assertEqual(first.get("status"), "ok", first)
            self.assertEqual(first.get("action"), "sys.idle", first)
            self.assertEqual(first.get("rid"), "sidle-base-1", "rid não ecoado no baseline")
            self.assertIn("enabled", first.get("data") or {}, first)
        type(self).baseline_enabled = base[0]["data"]["enabled"]
        self.record_finding("info", "sys.idle baseline",
                            f"enabled={type(self).baseline_enabled}")

        # enable
        en = self.transport.exchange(
            json.dumps({"cmd": "sys.idle", "enable": True, "rid": "sidle-en-1"}),
            timeout_s=6, label="sys.idle enable=true")
        with self.subTest("sys.idle enable"):
            self.assertTrue(en, "nenhum frame para sys.idle enable")
            first = en[0]
            self.assertEqual(first.get("status"), "ok", first)
            self.assertEqual(first.get("rid"), "sidle-en-1", "rid não ecoado no enable")
            self.assertTrue((first.get("data") or {}).get("enabled"), first)
        time.sleep(0.5)

        # status permanece true
        st1 = self.idle_query(rid="sidle-st-1", label="sys.idle query após enable")
        with self.subTest("sys.idle enabled permanece true"):
            self.assertTrue(st1, "nenhum frame de status")
            self.assertEqual(st1[0].get("status"), "ok", st1)
            self.assertEqual(st1[0].get("rid"), "sidle-st-1", "rid não ecoado no status")
            self.assertTrue((st1[0].get("data") or {}).get("enabled"), st1)

        # disable
        dis = self.transport.exchange(
            json.dumps({"cmd": "sys.idle", "enable": False, "rid": "sidle-dis-1"}),
            timeout_s=6, label="sys.idle enable=false")
        with self.subTest("sys.idle disable"):
            self.assertTrue(dis, "nenhum frame para sys.idle disable")
            first = dis[0]
            self.assertEqual(first.get("status"), "ok", first)
            self.assertEqual(first.get("rid"), "sidle-dis-1", "rid não ecoado no disable")
            self.assertFalse((first.get("data") or {}).get("enabled"), first)
        time.sleep(0.5)

        # status permanece false
        st2 = self.idle_query(rid="sidle-st-2", label="sys.idle query após disable")
        with self.subTest("sys.idle disabled permanece false"):
            self.assertTrue(st2, "nenhum frame de status")
            self.assertEqual(st2[0].get("status"), "ok", st2)
            self.assertEqual(st2[0].get("rid"), "sidle-st-2", "rid não ecoado no status")
            self.assertFalse((st2[0].get("data") or {}).get("enabled"), st2)

        # restauração para o baseline
        if not type(self).baseline_enabled:
            self.record_finding("ok", "sys.idle", "baseline encerrado em false (idempotente)")
        ret = self.transport.exchange(
            json.dumps({"cmd": "sys.idle", "enable": type(self).baseline_enabled,
                        "rid": "sidle-res-1"}),
            timeout_s=6, label="sys.idle restore baseline")
        with self.subTest("sys.idle restaurado para baseline"):
            self.assertTrue(ret, "nenhum frame no restore")
            self.assertEqual(ret[0].get("status"), "ok", ret[0])
            self.assertEqual(ret[0].get("rid"), "sidle-res-1", "rid não ecoado no restore")
            self.assertEqual((ret[0].get("data") or {}).get("enabled"),
                             type(self).baseline_enabled, ret[0])
        final = self.idle_query(rid="sidle-final-1", label="sys.idle query final")
        with self.subTest("estado final == baseline"):
            self.assertTrue(final, "nenhum frame no query final")
            self.assertEqual(final[0].get("status"), "ok", final[0])
            self.assertEqual((final[0].get("data") or {}).get("enabled"),
                             type(self).baseline_enabled, final[0])
        self.record_finding("ok", "sys.idle ciclo",
                            f"enable->status->disable->restore OK (baseline={type(self).baseline_enabled})")

    def test_01_stability_after_idle_cycle(self):
        after1 = self.transport.exchange('{"cmd":"sys.info","rid":"sidle-stab-1"}',
                                         timeout_s=10, label="sys.info pós ciclo idle")
        with self.subTest("sys.info pós ciclo"):
            self.assertTrue(after1, "nenhum frame sys.info")
            self.assertEqual(after1[0].get("status"), "ok", after1[0])
            self.assertEqual(after1[0].get("action"), "sys.info", after1[0])
            self.assertEqual(after1[0].get("rid"), "sidle-stab-1", "rid não ecoado")
        after2 = self.transport.exchange('{"cmd":"app.active","rid":"sidle-stab-2"}',
                                         timeout_s=10, label="app.active pós ciclo idle")
        with self.subTest("app.active pós ciclo"):
            self.assertTrue(after2, "nenhum frame app.active")
            self.assertEqual(after2[0].get("status"), "ok", after2[0])
            self.assertEqual(after2[0].get("rid"), "sidle-stab-2", "rid não ecoado")
        self.record_finding("ok", "estabilidade",
                            "bridge respondeu sys.info/app.active após ciclo sys.idle")


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
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SysIdleDeviceValidation)
    runner = unittest.TextTestRunner(verbosity=2 if args.verbose else 1, stream=sys.stdout)
    return 0 if runner.run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
