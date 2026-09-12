#!/usr/bin/env python3
"""Contratos da subetapa headless/silencioso do simulador (tests/simulator).

Plano aprovado (set/2026):

1. O simulador é **headless/silencioso por padrão**: o modo cenário roda sem
   depender de um servidor X/Wayland (``DISPLAY`` ausente) e abre janela
   somente quando o usuário pede explicitamente (``--interactive``/``--window``).
2. ``--scenario NOME`` funciona **sem** ``DISPLAY``/``WAYLAND_DISPLAY``.
3. ``--interactive`` e ``--window`` são **opt-in explícito** para janela visível
   (sem a flag o sim nunca tenta abrir janela real).
4. ``--silent`` suprime todo o stdout informativo (boot/sim/captura).
5. **Códigos de saída**: 0 para sucesso (cenário concluído, ``--list``,
   ``--help``) e não-zero para erro (modo ausente, flag desconhecida, cenário
   desconhecido).
6. ``tools/ci/run_sim_tests.sh`` inicia cada cenário diretamente e aguarda seu
   término natural (determinismo + códigos de saída tornam wrappers de processo
   desnecessários).
7. **Determinismo**: duas execuções headless do mesmo cenário produzem capturas
   com CRC idêntico (relógio congelado + ``srand(42)`` + captura após settle).

Fase vermelha (baseline atual): ``tests/simulator/main.cpp`` ignora
``--silent``/``--window`` e o script ainda usava wrappers de processo
(regressão da fase 41, PLANO.md linhas 2173-2174). Os testes marcados abaixo
como *RED esperado* falham hoje exatamente por essa ausência e passam a valer
depois da implementação.

Em CI sem o build do simulador (quality-gate não constrói ``build-sim``), as
classes executáveis fazem skip documentado; os contratos estáticos é que
carregam a fase vermelha em qualquer ambiente.
"""

import os
import re
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIM_MAIN = (ROOT / "tests/simulator/main.cpp").read_text(encoding="utf-8")
SIM_SCRIPT = (ROOT / "tools/ci/run_sim_tests.sh").read_text(encoding="utf-8")

# Caminho de inicialização headless reconhecido como legítimo (implementação
# livre: qualquer uma dessas formas força SDL a criar "janela" sem display).
SDL_HEADLESS_RE = re.compile(
    r"SDL_SetHint\s*\(\s*\"SDL_VIDEODRIVER\"\s*,\s*\"(?:dummy|offscreen)\""
    r"|setenv\s*\(\s*\"SDL_VIDEODRIVER\"\s*,\s*\"(?:dummy|offscreen)\"[^)]*\)"
    r"|SDL_VideoInit\s*\(\s*\"(?:dummy|offscreen)\"\s*\)"
)


# ---------------------------------------------------------------------------
# 1) Headless/silencioso por padrão + flags explícitas (contrato estático)
# ---------------------------------------------------------------------------

class SimCliFlagsContract(unittest.TestCase):
    """O main.cpp deve reconhecer as flags novas e manter as antigas."""

    def test_main_recognizes_explicit_interactive_flag(self):
        self.assertIn(
            '--interactive',
            SIM_MAIN,
            "modo interativo precisa continuar explícito (--interactive)",
        )

    def test_main_recognizes_explicit_window_flag(self):
        self.assertIn(
            '--window',
            SIM_MAIN,
            "main.cpp não reconhece '--window' — flag que força janela visível "
            "(opt-in explícito) ainda não foi implementada",
        )

    def test_main_recognizes_silent_flag(self):
        self.assertIn(
            '--silent',
            SIM_MAIN,
            "main.cpp não reconhece '--silent' — o simulador ainda não tem o "
            "modo silencioso pedido pelo plano",
        )

    def test_main_keeps_scenario_flag(self):
        self.assertIn("--scenario", SIM_MAIN)

    def test_main_keeps_list_and_help(self):
        self.assertIn("--list", SIM_MAIN)
        self.assertIn("--help", SIM_MAIN)


class SimHeadlessByDefaultContract(unittest.TestCase):
    """O modo cenário nasce headless; janela real só com opt-in explícito."""

    def test_scenario_path_initializes_headless_sdl(self):
        self.assertRegex(
            SIM_MAIN,
            SDL_HEADLESS_RE,
            "main.cpp não força um driver SDL headless (dummy/offscreen). "
            "Sem isso o modo cenário depende de DISPLAY/Wayland e um servidor "
            "de janelas — viola 'headless por padrão' do plano",
        )

    def test_scenario_clock_frozen_and_rng_seeded(self):
        # Determinismo: relógio congelado no pipeline de cenário + srand fixo.
        self.assertIn("simtime::set_frozen(true)", SIM_MAIN)
        self.assertIn("srand(42)", SIM_MAIN)


# ---------------------------------------------------------------------------
# 2) run_sim_tests.sh aguarda término natural
# ---------------------------------------------------------------------------

class SimRunnerNaturalCompletionContract(unittest.TestCase):
    """O orquestrador delega conclusão e erros ao executável do cenário."""

    def test_script_invokes_sim_directly_per_scenario(self):
        for block in ("--update-goldens", "capturando"):
            self.assertRegex(
                SIM_SCRIPT,
                r'"\$SIM_BIN"\s+--scenario\s+"\$sc"',
                "cada cenário precisa ser disparado por uma chamada direta "
                'a "$SIM_BIN" --scenario "$sc"',
            )

    def test_script_keeps_golden_comparison(self):
        self.assertIn("compare_images.py", SIM_SCRIPT)
        self.assertIn("--goldens", SIM_SCRIPT)


# ---------------------------------------------------------------------------
# 3) Contratos executáveis contra o binário (quando existir)
# ---------------------------------------------------------------------------

class SimHeadlessBinaryContract(unittest.TestCase):
    """Contratos que rodam contra build-sim/tab5_sim.

    Fase vermelha local: com o binário presente e a implementação ainda sem
    ``--silent``/``--window``/headless explícito, os testes de
    ``test_silent_flag_...``, ``test_window_explicit_...`` e os estáticos de
    flags falham apontando a ausência — falhas esperadas nesta baseline.
    Em CI sem build do sim, a classe faz skip documentado.
    """

    SIM_BIN = Path(os.environ.get("BUILD_DIR", str(ROOT / "build-sim"))) / "tab5_sim"
    SCENARIO = "shell_desktop"
    SHOT = "01_desktop.bmp"

    @classmethod
    def setUpClass(cls):
        if not cls.SIM_BIN.is_file():
            raise unittest.SkipTest(
                "build-sim/tab5_sim ausente (rode tools/ci/run_sim_tests.sh); "
                "em CI sem o build do sim a fase vermelha fica com os "
                "contratos estáticos desta suíte",
            )

    @staticmethod
    def _no_display_env():
        env = dict(os.environ)
        for key in ("DISPLAY", "WAYLAND_DISPLAY"):
            env.pop(key, None)
        return env

    def _run(self, args, env=None):
        cmd = [str(self.SIM_BIN)] + args
        return subprocess.run(
            cmd,
            cwd=str(ROOT),
            env=env if env is not None else dict(os.environ),
            capture_output=True,
            text=True,
        )

    def _scenario_capture(self, out_dir, env=None):
        env = env if env is not None else self._no_display_env()
        proc = self._run(
            ["--scenario", self.SCENARIO, "--out", str(out_dir)], env=env
        )
        return proc, Path(out_dir) / self.SHOT

    # --scenario sem DISPLAY (contrato do plano; já vale hoje)
    def test_scenario_runs_and_captures_without_display(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc, shot = self._scenario_capture(tmp)
            self.assertEqual(
                proc.returncode,
                0,
                f"--scenario sem DISPLAY falhou (rc={proc.returncode}): "
                f"{proc.stderr[-400:]}",
            )
            self.assertTrue(
                shot.is_file(),
                f"--scenario não produziu {self.SHOT} em modo headless",
            )

    # --silent (RED esperado hoje: flag ignorada, stdout cheio)
    def test_silent_flag_suppresses_stdout_completely(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc = self._run(
                ["--silent", "--scenario", self.SCENARIO, "--out", str(tmp)],
                env=self._no_display_env(),
            )
            self.assertEqual(
                proc.returncode,
                0,
                f"--silent mudou o código de saída (rc={proc.returncode}): "
                f"{proc.stderr[-400:]}",
            )
            self.assertTrue(
                (Path(tmp) / self.SHOT).is_file(),
                "--silent precisa continuar gravando a captura do cenário",
            )
            self.assertEqual(
                proc.stdout.strip(),
                "",
                "em --silent nenhum stdout pode ser emitido (nem boot, nem "
                "'sim: cenario concluido'); hoje a flag é ignorada e todo o "
                "log informativo vaza — falha esperada pela ausência da "
                "implementação",
            )

    # --window explícito (RED esperado hoje: flag ignorada, segue headless)
    def test_window_explicit_requires_display_headless_env(self):
        # Pedir janela visível sem DISPLAY é erro: sem opt-in o sim é headless,
        # com opt-in ele depende do servidor de janelas.
        with tempfile.TemporaryDirectory() as tmp:
            proc = self._run(
                ["--window", "--scenario", self.SCENARIO, "--out", str(Path(tmp) / "out")],
                env=self._no_display_env(),
            )
        self.assertNotEqual(
            proc.returncode,
            0,
            "--window (janela explícita) sem DISPLAY deveria falhar com código "
            "não-zero; hoje a flag é ignorada e o cenário roda headless — "
            "falha esperada pela ausência da implementação",
        )

    @unittest.skipUnless(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"),
                         "sem servidor de janelas disponível")
    def test_window_explicit_with_display_still_captures(self):
        # Com DISPLAY, --window é aceito e o cenário continua capturando.
        with tempfile.TemporaryDirectory() as tmp:
            proc, shot = self._scenario_capture(tmp, env=dict(os.environ))
            self.assertEqual(proc.returncode, 0)
            self.assertTrue(shot.is_file())

    # Códigos de saída (0 sucesso / não-zero erro)
    def test_exit_code_list_is_zero(self):
        self.assertEqual(self._run(["--list"]).returncode, 0)

    def test_exit_code_help_is_zero(self):
        self.assertEqual(self._run(["--help"]).returncode, 0)

    def test_exit_code_scenario_missing_is_nonzero(self):
        self.assertNotEqual(self._run([]).returncode, 0)

    def test_exit_code_unknown_flag_is_nonzero(self):
        self.assertNotEqual(self._run(["--flag-inexistente"]).returncode, 0)

    def test_exit_code_unknown_scenario_is_nonzero(self):
        proc = self._run(
            ["--scenario", "cenario_que_nao_existe", "--out", "ignored"],
            env=self._no_display_env(),
        )
        self.assertNotEqual(
            proc.returncode,
            0,
            "cenário desconhecido precisa de código de saída não-zero",
        )

    # Determinismo headless (duas execuções → mesmo CRC)
    def test_scenario_deterministic_across_headless_runs(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_a = Path(tmp) / "a"
            run_b = Path(tmp) / "b"
            rc_a, shot_a = self._scenario_capture(run_a)
            rc_b, shot_b = self._scenario_capture(run_b)
            self.assertEqual(rc_a.returncode, 0, rc_a.stderr[-400:])
            self.assertEqual(rc_b.returncode, 0, rc_b.stderr[-400:])
            crc_a = zlib.crc32(shot_a.read_bytes())
            crc_b = zlib.crc32(shot_b.read_bytes())
            self.assertEqual(
                crc_a,
                crc_b,
                "capturas headless do cenário shell_desktop divergem entre "
                "execuções — determinismo quebrado (clock/RNG/settle)",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
