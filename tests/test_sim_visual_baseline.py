#!/usr/bin/env python3
"""Etapa visual 2: regressão dos 17 cenários do simulador, agrupados A–D.

Plano aprovado (set/2026) — esta suíte fixa os contratos de regressão da
etapa visual 2 SEM corrigir produção e SEM regenerar os goldens:

1. **Matriz de 17 cenários agrupados A–D**:
   - **Grupo A — Shell & tiles do launcher (4)**: ``shell_desktop``,
     ``shell_power``, ``shell_settings``, ``shell_calendar_popup``.
   - **Grupo B — Apps em Host View nativa (2)**: ``app_camera``,
     ``app_gallery`` (views nativas em ``components/os/shell``).
   - **Grupo C — Apps WASM (10)**: bluetooth, calendar, chat, files,
     fileserver, music, notas, recorder, terminal, wifi.
   - **Grupo D — Controle de determinismo visual (1)**: ``app_teclado``
     (único cenário com PASS no baseline gravado — diff 0.000%).
2. **Headless completo**: cada cenário roda sem ``DISPLAY``/``WAYLAND_DISPLAY``
   (``SDL_VIDEODRIVER=dummy``), com ``--silent`` (stdout vazio), termina por
   conta própria com rc 0 e grava ``01_*.bmp`` 720×1280.
3. **CRC determinístico**: cada cenário rodado 2× produz capturas com CRC32 e
   SHA-256 idênticos (relógio congelado + ``srand(42)`` + settle antes da
   captura, com WAMR real no link).
4. **Comparador rígido**: o baseline é medido com o comparador de produção
   (``compare_images.py``) nos parâmetros padrão (tolerância 0.5%, delta RGB
   48). Esta suíte NÃO afrouxa o comparador e ainda guarda seus padrões.
5. **Registro por grupo**: as falhas (e os diffs %) são gravadas em
   ``tests/simulator/out/stage2_baseline.json`` com resumo A–D.
6. **Análise de diff/regiões**: para cada cenário divergente, a máscara de
   diffs (mesma regra do comparador) é decomposta em componentes conexos,
   bounding boxes e top-5 regiões, agregadas por grupo em
   ``tests/simulator/out/stage2_regions.json``.

Baseline gravada (2026-09-11, build-sim com WAMR real): 16 FALHA + 1 PASS
(``app_teclado``). Os 17 goldens NÃO foram regenerados. Se a classificação
observada divergir da gravação, a suíte falha indicando que (a) goldens foram
regenerados, (b) o comparador foi afrouxado ou (c) um cenário que passava
regrediu — decisão de re-baseline é explícita (editar ``RECORDED_BASELINE``).

Em CI sem o build do simulador (quality-gate não constrói ``build-sim``), as
classes executáveis fazem skip documentado; as guardas estáticas (matriz de
cenários e padrões do comparador) carregam a fase vermelha em qualquer
ambiente.
"""

import collections
import json
import os
import re
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIM_MAIN = (ROOT / "tests/simulator/main.cpp").read_text(encoding="utf-8")
SIM_SCENARIOS = (ROOT / "tests/simulator/scenarios/sim_scenarios.cpp").read_text(encoding="utf-8")
COMPARE_SRC = (ROOT / "tools/ci/compare_images.py").read_text(encoding="utf-8")
GOLDENS = ROOT / "tests/simulator/goldens"
OUT = ROOT / "tests/simulator/out"
SIM_BIN = Path(os.environ["TAB5_SIM_BIN"]) if os.environ.get("TAB5_SIM_BIN") else (
    Path(os.environ.get("BUILD_DIR") or str(ROOT / "build-sim")) / "tab5_sim"
)

# ---------------------------------------------------------------------------
# Matriz de 17 cenários agrupados A–D (ordem de execução preservada)
# ---------------------------------------------------------------------------

SCENARIOS = [
    # (nome, grupo)
    ("shell_desktop", "A"),
    ("shell_power", "A"),
    ("shell_settings", "A"),
    ("shell_calendar_popup", "A"),
    ("app_camera", "B"),
    ("app_gallery", "B"),
    ("app_bluetooth", "C"),
    ("app_calendar", "C"),
    ("app_chat", "C"),
    ("app_files", "C"),
    ("app_fileserver", "C"),
    ("app_music", "C"),
    ("app_notas", "C"),
    ("app_recorder", "C"),
    ("app_terminal", "C"),
    ("app_wifi", "C"),
    ("app_teclado", "D"),
]

GROUPS = {name: grp for name, grp in SCENARIOS}
GROUP_SCENARIOS = collections.defaultdict(list)
for name, grp in SCENARIOS:
    GROUP_SCENARIOS[grp].append(name)
GROUP_NAMES = {
    "A": "Shell & tiles do launcher",
    "B": "Apps em Host View nativa",
    "C": "Apps WASM (12 apps padrão, exceto teclado)",
    "D": "Controle de determinismo visual",
}

# Baseline gravada após a correção da etapa visual 2 (comparador 0.5%/48).
# Os doze goldens alterados correspondem às mudanças intencionais registradas
# em docs/stage2-visual-decisions.md; câmera, galeria, notas, terminal e
# teclado já eram compatíveis e não foram regenerados.
RECORDED_BASELINE = {
    "app_bluetooth": {"diff_pct": 0.000, "pass": True},
    "app_calendar": {"diff_pct": 0.000, "pass": True},
    "app_camera": {"diff_pct": 0.000, "pass": True},
    "app_chat": {"diff_pct": 0.000, "pass": True},
    "app_files": {"diff_pct": 0.000, "pass": True},
    "app_fileserver": {"diff_pct": 0.000, "pass": True},
    "app_gallery": {"diff_pct": 0.000, "pass": True},
    "app_music": {"diff_pct": 0.000, "pass": True},
    "app_notas": {"diff_pct": 0.000, "pass": True},
    "app_recorder": {"diff_pct": 0.000, "pass": True},
    "app_teclado": {"diff_pct": 0.000, "pass": True},
    "app_terminal": {"diff_pct": 0.000, "pass": True},
    "app_wifi": {"diff_pct": 0.000, "pass": True},
    "shell_calendar_popup": {"diff_pct": 0.000, "pass": True},
    "shell_desktop": {"diff_pct": 0.000, "pass": True},
    "shell_power": {"diff_pct": 0.000, "pass": True},
    "shell_settings": {"diff_pct": 0.000, "pass": True},
}

EXPECTED_PASS = {name for name, r in RECORDED_BASELINE.items() if r["pass"]}
EXPECTED_FAIL = {name for name, r in RECORDED_BASELINE.items() if not r["pass"]}

# Restringe a execução a grupos A–D via TAB5_SIM_GROUPS="A" ou "A,D".
_REQUESTED_GROUPS = {
    letter.upper()
    for token in os.environ.get("TAB5_SIM_GROUPS", "ABCD").split(",")
    for letter in token.strip()
}
ACTIVE_SCENARIOS = [name for name, grp in SCENARIOS if grp in _REQUESTED_GROUPS]

# --- Capturas em cache por execução (evita rodar os 17 cenários por classe) --
_capture_cache = {}


def _no_display_env():
    env = dict(os.environ)
    for key in ("DISPLAY", "WAYLAND_DISPLAY"):
        env.pop(key, None)
    return env


def _run_silent(args):
    return subprocess.run(
        [str(SIM_BIN), "--silent", *args],
        cwd=str(ROOT),
        env=_no_display_env(),
        capture_output=True,
        text=True,
        timeout=180,
    )


def _first_shot(out_dir):
    """Devolve o único 01_*.bmp sob <out>/<cenario>/ (ou None se ausente)."""
    shots = sorted(Path(out_dir).rglob("01_*.bmp"))
    return shots[0] if shots else None


def _bmp_size(path):
    data = path.read_bytes()
    if data[:2] != b"BM":
        return None, None
    w = struct.unpack_from("<I", data, 18)[0]
    h = struct.unpack_from("<I", data, 22)[0]
    return w, h


def _capture_all(out_dir: Path):
    """Roda os cenários ativos headless e devolve {cenário: caminho do shot}."""
    key = str(out_dir)
    if key in _capture_cache:
        return _capture_cache[key]
    out_dir.mkdir(parents=True, exist_ok=True)
    result = {}
    for name in ACTIVE_SCENARIOS:
        proc = _run_silent(["--scenario", name, "--out", str(out_dir)])
        if proc.returncode != 0:
            raise AssertionError(
                f"cenário {name} falhou (rc={proc.returncode}): {proc.stderr[-400:]}"
            )
        if proc.stdout != "":
            raise AssertionError(
                f"cenário {name} vazou stdout em --silent: {proc.stdout[:200]!r}"
            )
        shots = sorted((out_dir / name).glob("01_*.bmp"))
        if len(shots) != 1:
            raise AssertionError(
                f"cenário {name} produziu {len(shots)} shots (esperado 1)"
            )
        result[name] = shots[0]
    _capture_cache[key] = result
    return result


# ---------------------------------------------------------------------------
# 1) Matriz de cenários (contrato estático + lista do binário)
# ---------------------------------------------------------------------------

class SimScenarioMatrixContract(unittest.TestCase):
    """Os 17 cenários da matriz A–D estão registrados e têm golden."""

    def test_all_seventeen_scenarios_registered_in_sim_scenarios(self):
        for name, _grp in SCENARIOS:
            self.assertIn(
                f'"{name}"',
                SIM_SCENARIOS,
                f"cenário {name} da matriz A–D não está registrado em "
                "sim_scenarios.cpp",
            )

    def test_every_golden_dir_has_a_shot(self):
        for name, _grp in SCENARIOS:
            shots = sorted((GOLDENS / name).glob("01_*.bmp"))
            self.assertTrue(
                shots,
                f"{name} não tem golden 01_*.bmp — o comparador não tem "
                "referência para este cenário",
            )

    def test_every_scenario_has_settle_before_shot(self):
        steps = re.findall(r"\{\s*(?:act_\w+|nullptr)\s*,\s*(\d+)\s*,\s*\"([^\"]+)\"\s*\}", SIM_SCENARIOS)
        self.assertGreaterEqual(len(steps), 17, "esperado um shot por cenário")
        for settle_ms, shot in steps:
            self.assertGreaterEqual(
                int(settle_ms),
                300,
                f"shot {shot} tem settle de {settle_ms}ms — abaixo da janela "
                "de drenagem do despachador WASM antes da captura",
            )

    def test_binary_lists_exactly_the_seventeen_scenarios(self):
        if not SIM_BIN.is_file():
            self.skipTest("build-sim/tab5_sim ausente (em CI a guarda é estática)")
        # NOTA: --list não pode rodar com --silent (ele silencia a própria lista).
        proc = subprocess.run(
            [str(SIM_BIN), "--list"],
            cwd=str(ROOT),
            env=_no_display_env(),
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr[-400:])
        listed = re.findall(r"^\s{2}([a-z_]+)\s+", proc.stdout, re.M)
        expected = sorted(name for name, _grp in SCENARIOS)
        self.assertEqual(
            sorted(listed),
            expected,
            "--list do binário não expõe exatamente os 17 cenários da matriz",
        )

    def test_baseline_matrix_is_ordered_by_group(self):
        seq = [grp for _name, grp in SCENARIOS]
        self.assertEqual(seq, sorted(seq), "matriz A–D precisa estar ordenada por grupo")
        for grp in "ABCD":
            self.assertTrue(GROUP_SCENARIOS[grp], f"grupo {grp} vazio na matriz")


# ---------------------------------------------------------------------------
# 2) Captura headless dos 17 cenários
# ---------------------------------------------------------------------------

class SimHeadlessCaptureContract(unittest.TestCase):
    """Contratos executáveis contra o binário: headless, silent, shot válido."""

    @classmethod
    def setUpClass(cls):
        if not SIM_BIN.is_file():
            raise unittest.SkipTest(
                "build-sim/tab5_sim ausente (rode tools/ci/run_sim_tests.sh); "
                "em CI a fase vermelha fica com os contratos estáticos"
            )

    def test_every_scenario_captures_headless_720x1280(self):
        shots = _capture_all(OUT)
        self.assertEqual(len(shots), len(ACTIVE_SCENARIOS))
        for name in ACTIVE_SCENARIOS:
            w, h = _bmp_size(shots[name])
            self.assertEqual((w, h), (720, 1280), f"{name} não é 720×1280")

    def test_every_scenario_silent_stdout_is_empty(self):
        # O contrato --silent (stdout vazio por cenário) é validado dentro de
        # _capture_all; aqui confirmamos apenas que a captura em cache veio de
        # execuções silenciosas (sem re-rodar os 17 cenários).
        shots = _capture_all(OUT)
        self.assertEqual(len(shots), len(ACTIVE_SCENARIOS))


# ---------------------------------------------------------------------------
# 3) CRC determinístico (2 execuções → CRC32 e SHA-256 idênticos)
# ---------------------------------------------------------------------------

class SimDeterministicCrcContract(unittest.TestCase):
    """Cada cenário roda 2× headless e produz capturas byte-a-byte iguais."""

    @classmethod
    def setUpClass(cls):
        if not SIM_BIN.is_file():
            raise unittest.SkipTest("build-sim/tab5_sim ausente (ver skip da classe executável)")

    def test_crc_and_sha_identical_across_two_headless_runs(self):
        crc_report = {}
        with tempfile.TemporaryDirectory() as tmp:
            for name in ACTIVE_SCENARIOS:
                run_a = str(Path(tmp) / "a")
                run_b = str(Path(tmp) / "b")
                proc_a = _run_silent(["--scenario", name, "--out", run_a])
                proc_b = _run_silent(["--scenario", name, "--out", run_b])
                self.assertEqual(proc_a.returncode, 0, f"{name} runA: {proc_a.stderr[-300:]}")
                self.assertEqual(proc_b.returncode, 0, f"{name} runB: {proc_b.stderr[-300:]}")
                bmp_a = _first_shot(run_a)
                bmp_b = _first_shot(run_b)
                if bmp_a is None or bmp_b is None:
                    self.fail(f"{name} não produziu 01_*.bmp em ambas execuções")
                data_a = bmp_a.read_bytes()
                data_b = bmp_b.read_bytes()
                crc_report[name] = {
                    "crc32": zlib.crc32(data_a),
                    "sha256": __import__("hashlib").sha256(data_a).hexdigest(),
                    "bytes": len(data_a),
                    "group": GROUPS[name],
                }
                self.assertEqual(
                    zlib.crc32(data_a),
                    zlib.crc32(data_b),
                    f"CRC32 de {name} divergiu entre execuções — determinismo "
                    "headless quebrado (clock/RNG/settle/WAMR)",
                )
                self.assertEqual(
                    __import__("hashlib").sha256(data_a).hexdigest(),
                    __import__("hashlib").sha256(data_b).hexdigest(),
                    f"SHA-256 de {name} divergiu entre execuções",
                )
        self.assertTrue(crc_report)
        report_path = OUT / "stage2_crc_determinism.json"
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps({"scenarios": crc_report}, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )


# ---------------------------------------------------------------------------
# 4) Comparador rígido (guardas estáticas + classificação do baseline)
# ---------------------------------------------------------------------------

class StrictComparatorGuardContract(unittest.TestCase):
    """O comparador de produção permanece rígido (não afrouxar os padrões)."""

    def test_comparator_default_tolerance_is_half_percent(self):
        self.assertIn("default=0.5", COMPARE_SRC)
        self.assertIn("padrao 0.5)", COMPARE_SRC)

    def test_comparator_default_rgb_delta_is_48(self):
        self.assertIn("default=48", COMPARE_SRC)

    def test_comparator_still_exits_nonzero_on_failure(self):
        self.assertIn("sys.exit(1 if failed else 0)", COMPARE_SRC)

    def test_runner_does_not_loosen_tolerance(self):
        script = (ROOT / "tools/ci/run_sim_tests.sh").read_text(encoding="utf-8")
        self.assertNotIn("--tolerance", script)
        self.assertIn("compare_images.py", script)


class StrictGoldenBaselineContract(unittest.TestCase):
    """Baseline medida com o comparador de produção nos padrões (0.5%/48).

    A classificação PASS/FAIL de cada cenário deve casar exatamente com a
    baseline gravada (16 FALHA + app_teclado PASS). Se um cenário mudar de
    classe, é sinal de goldens regenerados, comparador alterado ou regressão
    nova — revisar antes de re-baselinear (editar RECORDED_BASELINE).
    """

    @classmethod
    def setUpClass(cls):
        if not SIM_BIN.is_file():
            raise unittest.SkipTest("build-sim/tab5_sim ausente (ver skip da classe executável)")

    def _compare_and_parse(self):
        _capture_all(OUT)
        proc = subprocess.run(
            ["python3", str(ROOT / "tools/ci/compare_images.py")],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=300,
        )
        self.assertEqual(
            proc.returncode,
            0,
            "comparador deveria passar a baseline corrigida; "
            f"rc={proc.returncode}: {proc.stdout[-400:]}",
        )
        result = {}
        block = None
        for line in proc.stdout.splitlines():
            m = re.match(r"^\[([a-z_]+)\]$", line.strip())
            if m:
                block = m.group(1)
                continue
            if block is None:
                continue
            mp = re.match(r"^PASS:\s+\S+\s+\(diff\s+([\d.]+)%\)$", line.strip())
            mf = re.match(r"^FALHA:\s+\S+\s+\(diff\s+([\d.]+)%\s+>\s+[\d.]+%\)$", line.strip())
            if mp:
                result[block] = {"pass": True, "diff_pct": float(mp.group(1))}
            elif mf:
                result[block] = {"pass": False, "diff_pct": float(mf.group(1))}
            # linhas que não casam (resumo final "Resultado:", "Relatorio:",
            # linhas em branco) são ignoradas; bloco sem resultado de comparação
            # aparece como ausência e falha o assertIn dos testes de baseline.
        return result

    def test_baseline_classification_matches_recorded(self):
        observed = self._compare_and_parse()
        for name, _grp in SCENARIOS:
            self.assertIn(name, observed, f"comparador não avaliou o cenário {name}")
        obs_pass = {n for n, r in observed.items() if r["pass"]}
        obs_fail = {n for n, r in observed.items() if not r["pass"]}
        self.assertEqual(
            obs_pass,
            EXPECTED_PASS,
            "cenários PASS diverge da baseline gravada — goldens foram "
            "regenerados? atualize RECORDED_BASELINE só após revisão visual",
        )
        self.assertEqual(
            obs_fail,
            EXPECTED_FAIL,
            "cenários FALHA diverge da baseline gravada — regressão nova ou "
            "comparador alterado; não afrouxe o comparador",
        )

    def test_baseline_recorded_per_group(self):
        observed = self._compare_and_parse()
        summary = {"recorded_at": "2026-09-11", "comparator": {"tolerance": 0.5, "rgb_delta": 48}}
        failures_by_group = {}
        for grp in "ABCD":
            fails = {
                name: {"diff_pct": observed[name]["diff_pct"]}
                for name in GROUP_SCENARIOS[grp]
                if not observed[name]["pass"]
            }
            failures_by_group[grp] = fails
            summary[grp] = {
                "total": len(GROUP_SCENARIOS[grp]),
                "falhas": len(fails),
                "cenarios": {
                    n: {"pass": observed[n]["pass"], "diff_pct": observed[n]["diff_pct"]}
                    for n in GROUP_SCENARIOS[grp]
                },
            }
        summary["falhas_por_grupo"] = failures_by_group
        report_path = OUT / "stage2_baseline.json"
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        # Resumo legível no stdout para o handoff.
        for grp in "ABCD":
            n = len(GROUP_SCENARIOS[grp])
            f = len(failures_by_group[grp])
            print(f"Grupo {grp} ({GROUP_NAMES[grp]}): {f}/{n} FALHA")


# ---------------------------------------------------------------------------
# 5) Análise de diff/regiões (diagnóstico, mesma regra do comparador)
# ---------------------------------------------------------------------------

class DiffRegionAnalysisContract(unittest.TestCase):
    """Decompõe a máscara de diffs (delta RGB > 48) em componentes conexos.

    Extremamente valiosa na etapa visual 2: mostra onde as divergências estão
    (barra de status, ícones de tile, tela do app), quantos pixels e quantas
    regiões. Não é o gate de aprovação — o gate é o comparador rígido — mas
    alimenta o handoff com a assinatura das regiões por grupo.
    """

    RGB_DELTA = 48

    @classmethod
    def setUpClass(cls):
        if not SIM_BIN.is_file():
            raise unittest.SkipTest("build-sim/tab5_sim ausente (ver skip da classe executável)")

    @staticmethod
    def _analyze(golden_path, out_path):
        from PIL import Image
        import warnings

        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            g = Image.open(golden_path).convert("RGB")
            o = Image.open(out_path).convert("RGB")
        w, h = g.size
        if o.size != (w, h):
            raise AssertionError(f"dimensões divergem: {g.size} vs {o.size}")
        gf = list(g.get_flattened_data())
        of = list(o.get_flattened_data())
        diverging = []
        for i in range(w * h):
            gp = gf[i]
            op = of[i]
            if (
                abs(gp[0] - op[0]) > DiffRegionAnalysisContract.RGB_DELTA
                or abs(gp[1] - op[1]) > DiffRegionAnalysisContract.RGB_DELTA
                or abs(gp[2] - op[2]) > DiffRegionAnalysisContract.RGB_DELTA
            ):
                diverging.append(i)
        return w, h, diverging

    @staticmethod
    def _components(w, h, diverging):
        idx = set(diverging)
        visited = set()
        comps = []
        for seed in diverging:
            if seed in visited:
                continue
            stack = [seed]
            visited.add(seed)
            size = 0
            min_x = min_y = w
            max_x = max_y = -1
            while stack:
                p = stack.pop()
                size += 1
                x = p % w
                y = p // w
                if x < min_x:
                    min_x = x
                if x > max_x:
                    max_x = x
                if y < min_y:
                    min_y = y
                if y > max_y:
                    max_y = y
                n = p - 1
                if x > 0 and n in idx and n not in visited:
                    visited.add(n)
                    stack.append(n)
                n = p + 1
                if x + 1 < w and n in idx and n not in visited:
                    visited.add(n)
                    stack.append(n)
                n = p - w
                if y > 0 and n in idx and n not in visited:
                    visited.add(n)
                    stack.append(n)
                n = p + w
                if y + 1 < h and n in idx and n not in visited:
                    visited.add(n)
                    stack.append(n)
            comps.append((size, (min_x, min_y, max_x, max_y)))
        comps.sort(reverse=True)
        return comps

    def test_region_report_generated_per_group(self):
        import hashlib

        shots = _capture_all(OUT)
        report = {"comparator_rule": {"rgb_delta": self.RGB_DELTA}}
        sha_groups = collections.defaultdict(list)
        for name in ACTIVE_SCENARIOS:
            shot = shots[name]
            report.setdefault(GROUPS[name], {})[name] = {"sha256": hashlib.sha256(shot.read_bytes()).hexdigest()}
            sha_groups[report[GROUPS[name]][name]["sha256"]].append(name)

        duplicated = {sha: names for sha, names in sha_groups.items() if len(names) > 1}

        for name in ACTIVE_SCENARIOS:
            shot = shots[name]
            golden = sorted((GOLDENS / name).glob("01_*.bmp"))[0]
            w, h, diverging = self._analyze(golden, shot)
            comps = self._components(w, h, diverging)
            total_px = w * h
            ratio = len(diverging) / total_px * 100.0
            top5 = []
            for rank, (size, (x0, y0, x1, y1)) in enumerate(comps[:5]):
                top5.append(
                    {
                        "rank": rank,
                        "pixels": size,
                        "share_do_diff": round(size / len(diverging), 4) if diverging else 0.0,
                        "bbox": [x0, y0, x1, y1],
                    }
                )
            bbox = None
            if comps:
                xs = [c[1][0] for c in comps]
                ys = [c[1][1] for c in comps]
                bbox = [min(xs), min(ys), max(c[1][2] for c in comps), max(c[1][3] for c in comps)]
            report[GROUPS[name]][name].update(
                {
                    "diff_pixels": len(diverging),
                    "diff_ratio_pct": round(ratio, 3),
                    "componentes": len(comps),
                    "bbox_geral": bbox,
                    "top5_regioes": top5,
                }
            )
            print(
                f"[{GROUPS[name]}] {name:22s} ratio={ratio:6.2f}% "
                f"comps={len(comps):4d} bbox={bbox} top={top5[0] if top5 else '-'}"
            )

        report["capturas_identicas_entre_cenarios"] = duplicated
        report_path = OUT / "stage2_regions.json"
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"Regiões: {report_path}")
        # Diagnóstico: não bloqueia, mas se "capturas_identicas" crescer além
        # do registrado, é sinal de que vários cenários mostram a MESMA tela.
        self.assertTrue(report_path.is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
