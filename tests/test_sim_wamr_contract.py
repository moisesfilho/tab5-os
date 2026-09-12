#!/usr/bin/env python3
"""Contratos da habilitação de WAMR real no simulador (tests/simulator).

Objetivo aprovado: o simulador passará a executar as 12 apps padrão
(``embedded_apps_pkg/*.tab5pkg``) e apps externas instaladas com o motor
WebAssembly real (``managed_components/espressif__wasm-micro-runtime``),
em vez do braço stub ``HAVE_WAMR=0`` de ``tab5_wasm_runtime.cpp``.

Esta suíte é escrita ANTES da implementação (fase vermelha TDD). Ela fixa os
contratos de quatro áreas pedidas pelo plano, sem alterar código de produção
nem o CMake de implementação:

1. **Simulador usa WAMR real** — o ``tests/simulator/CMakeLists.txt`` precisa
   adicionar/linkar o motor ``espressif__wasm-micro-runtime`` e o guard de
   compilação de ``tab5_wasm_runtime.cpp`` não pode mais amarrar a execução
   real exclusivamente ao ``ESP_PLATFORM`` (hoje o sim cai no stub). Nenhum
   shim de ``tests/simulator/shims`` pode definir símbolos WAMR.
2. **Carregamento/execução de app.wasm** — as 12 apps padrão são tar com
   ``app.wasm`` válido e ``manifest.entry == "app.wasm"``; o launch do
   package manager carrega via runtime (tar→bytes para embutidas, arquivo
   para instaladas), faz preflight do entrypoint (app_main/main, nunca
   _start) e executa com ``tab5_wasm_call_function``.
3. **Determinismo** — o mode cenário congela o relógio (``set_frozen(true)``),
   semeia o RNG (``srand(42)``), captura somente depois do ``settle_ms``
   (janela que drena o despachador assíncrono WASM) e o comparador de
   goldens continua vigente; dispatch de launch permanece síncrono no host.
4. **Caminho de instalação externa** — ``tab5_package_mgr_install`` extrai
   para ``/sdcard/apps/installed/<id>/`` copiando o entry (app.wasm), o scan
   registra instaladas com ``is_embedded=false`` e o launch das instaladas
   percorre exatamente o mesmo caminho de execução WAMR (load_from_file +
   select_entrypoint + call_function).

Além dos contratos estáticos (análise de fonte, sem binário), há uma classe
de contrato executável contra ``build-sim/tab5_sim``: com o binário presente
e WAMR ainda não linkado, o teste falha na fase vermelha com mensagem
explícita; quando o WAMR real entrar no link, o mesmo teste vira a prova de
determinismo (cenário ``app_terminal`` rodado duas vezes deve produzir
capturas com CRC idêntico). Em CI sem o build do simulador (quality-gate),
as classes executáveis fazem skip documentado — os contratos estáticos é que
carregam a fase vermelha.
"""

import os
import re
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIM_CMAKE = (ROOT / "tests/simulator/CMakeLists.txt").read_text(encoding="utf-8")
RUNTIME = (ROOT / "components/os/runtime/tab5_wasm_runtime.cpp").read_text(encoding="utf-8")
PMGR = (ROOT / "components/os/runtime/tab5_package_mgr.cpp").read_text(encoding="utf-8")
MANIFEST = (ROOT / "components/os/runtime/tab5_manifest.cpp").read_text(encoding="utf-8")
DISPATCHER = (ROOT / "components/os/runtime/tab5_wasm_dispatcher.cpp").read_text(encoding="utf-8")
SIM_MAIN = (ROOT / "tests/simulator/main.cpp").read_text(encoding="utf-8")
SIM_SCENARIOS = (ROOT / "tests/simulator/scenarios/sim_scenarios.cpp").read_text(encoding="utf-8")
SIM_SCRIPT = (ROOT / "tools/ci/run_sim_tests.sh").read_text(encoding="utf-8")
EMBEDDED_PKG_DIR = ROOT / "embedded_apps_pkg"
WAMR_COMPONENT = ROOT / "managed_components/espressif__wasm-micro-runtime"
WASM_EXPORT_H = WAMR_COMPONENT / "core/iwasm/include/wasm_export.h"

# Símbolos que o runtime real consome e que o WAMR linkado precisa fornecer.
WAMR_RUNTIME_SYMBOLS = ("wasm_runtime_load", "wasm_runtime_full_init", "wasm_runtime_instantiate")

# Guards que admitem o build host/sim (alternativas legítimas de implementação).
HOST_ADMIT_RE = re.compile(
    r"defined\s*\(\s*ESP_PLATFORM\s*\)\s*\|\|"  # disjunção com outra plataforma
    r"|defined\s*\(\s*__linux__\s*\)"
    r"|defined\s*\(\s*TAB5_WAMR\s*\)"
    r"|defined\s*\(\s*TAB5_HAVE_WAMR\s*\)"
    r"|defined\s*\(\s*TAB5_SIM\s*\)"
    r"|defined\s*\(\s*WAMR_HOST\s*\)"
    r"|defined\s*\(\s*HAVE_WAMR\s*\)"
    r"|__has_include\s*\(\s*[\"<]wasm_export\.h[\">]\s*\)"
)

# Caminhos de verbos de execução real no runtime.
WAMR_EXEC_APIS = (
    "wasm_runtime_load(",
    "wasm_runtime_instantiate(",
    "wasm_runtime_create_exec_env(",
    "wasm_runtime_call_wasm(",
)

# Apps padrão embutidas (12 pacotes .tab5pkg entregues no simulador).
STANDARD_APP_IDS = (
    "com.tab5.bluetooth",
    "com.tab5.calendar",
    "com.tab5.camera",
    "com.tab5.chat",
    "com.tab5.files",
    "com.tab5.music",
    "com.tab5.gallery",
    "com.tab5.notas",
    "com.tab5.recorder",
    "com.tab5.terminal",
    "com.tab5.wifi",
    "com.tab5.fileserver",
)

APP_SCENARIO_KEY = {
    "com.tab5.bluetooth": "app_bluetooth",
    "com.tab5.calendar": "app_calendar",
    "com.tab5.camera": "app_camera",
    "com.tab5.chat": "app_chat",
    "com.tab5.files": "app_files",
    "com.tab5.music": "app_music",
    "com.tab5.gallery": "app_gallery",
    "com.tab5.notas": "app_notas",
    "com.tab5.recorder": "app_recorder",
    "com.tab5.terminal": "app_terminal",
    "com.tab5.wifi": "app_wifi",
    "com.tab5.fileserver": "app_fileserver",
}

WASM_MAGIC = b"\x00asm\x01\x00\x00\x00"


# ---------------------------------------------------------------------------
# Helpers de inspeção
# ---------------------------------------------------------------------------

def wamr_guard_admits_host() -> bool:
    """True quando o guard de ``tab5_wasm_runtime.cpp`` permite WAMR real no
    build host/sim (não apenas no ESP_PLATFORM)."""
    if "#define HAVE_WAMR 0" not in RUNTIME:
        # Sem braço stub: o runtime é sempre real (ou decide por has_include).
        return True
    if "#ifdef ESP_PLATFORM" in RUNTIME:
        # Exatamente a forma atual: stub preso ao host — contrato violado.
        return False
    return bool(HOST_ADMIT_RE.search(RUNTIME))


def shim_files():
    shims = ROOT / "tests/simulator/shims"
    for path in shims.rglob("*"):
        if path.is_file() and path.suffix in (".c", ".cpp", ".h", ".hpp"):
            yield path


# ---------------------------------------------------------------------------
# 1) Simulador usa WAMR real
# ---------------------------------------------------------------------------

class SimUsesRealWamrContract(unittest.TestCase):
    """O simulador deve compilar/linkar o motor WAMR real e sair do stub."""

    def test_sim_cmake_references_wamr_engine(self):
        self.assertIn(
            "espressif__wasm-micro-runtime",
            SIM_CMAKE,
            "tests/simulator/CMakeLists.txt não referencia o componente "
            "managed_components/espressif__wasm-micro-runtime; sem ele o sim "
            "compila o stub do runtime (HAVE_WAMR=0) e nenhuma app.wasm é "
            "executada de verdade",
        )

    def test_sim_cmake_links_wamr_engine_to_tab5_sim(self):
        link_block = re.search(r"target_link_libraries\(\s*tab5_sim\b.*?\)", SIM_CMAKE, re.S)
        self.assertIsNotNone(
            link_block,
            "tests/simulator/CMakeLists.txt precisa ter target_link_libraries"
            "(tab5_sim ...) linkando o motor WAMR",
        )
        self.assertRegex(
            link_block.group(0),
            r"\b(?:iwasm|vmlib|wasm-micro-runtime|wamr)\b",
            "o link do tab5_sim precisa incluir o alvo/biblioteca do motor WAMR "
            "(iwasm/vmlib/wamr); hoje o sim linka apenas lvgl + wraps",
        )

    def test_wasm_runtime_guard_admits_host_build(self):
        self.assertTrue(
            wamr_guard_admits_host(),
            "tab5_wasm_runtime.cpp ainda amarra a execução WAMR real ao "
            "#ifdef ESP_PLATFORM (braço #else -> HAVE_WAMR=0 para o host). "
            "Para o simulador executar app.wasm com WAMR real, o guard precisa "
            "admitir o build host (ex.: defined(__linux__), TAB5_WAMR, "
            "__has_include(wasm_export.h) ou a remoção do braço stub)",
        )

    def test_wasm_runtime_keeps_real_exec_api_calls(self):
        for api in WAMR_EXEC_APIS:
            self.assertIn(
                api,
                RUNTIME,
                f"{api} precisa continuar no caminho real de execução de "
                "tab5_wasm_runtime.cpp (é a API que carrega/instancia/chama "
                "o bytecode das apps)",
            )

    def test_no_sim_shim_stubs_wamr_symbols(self):
        for path in shim_files():
            text = path.read_text(encoding="utf-8")
            hit = [s for s in WAMR_RUNTIME_SYMBOLS if s in text]
            self.assertEqual(
                hit,
                [],
                f"{path.relative_to(ROOT)} define/{hit} para WAMR; o simulador "
                "deve linkar o motor real de managed_components, não stubs",
            )
            self.assertNotIn(
                "wasm_export.h",
                text,
                f"{path.relative_to(ROOT)} provê um wasm_export.h de mentira; "
                "o header precisa vir do managed_components do WAMR",
            )

    def test_wamr_engine_header_available(self):
        self.assertTrue(
            WASM_EXPORT_H.is_file(),
            "managed_components/espressif__wasm-micro-runtime não expõe "
            "core/iwasm/include/wasm_export.h — o componente WAMR está "
            "incompleto/ausente no checkout; o sim não tem o que linkar",
        )


# ---------------------------------------------------------------------------
# 2) Carregamento/execução de app.wasm (12 apps padrão)
# ---------------------------------------------------------------------------

class EmbeddedAppsWasmFixtureContract(unittest.TestCase):
    """As 12 apps padrão são pacotes com app.wasm válido para o WAMR executar."""

    def embedded_packages(self):
        return sorted(EMBEDDED_PKG_DIR.glob("*.tab5pkg"))

    def test_twelve_standard_apps_shipped(self):
        import tarfile

        pkgs = self.embedded_packages()
        self.assertEqual(
            len(pkgs),
            12,
            "embedded_apps_pkg precisa entregar exatamente os 12 pacotes das "
            "apps padrão para o simulador",
        )
        ids = set()
        for p in pkgs:
            with tarfile.open(p) as t:
                manifest = json_load(t, "manifest.json")
            ids.add(manifest.get("id"))
        self.assertEqual(ids, set(STANDARD_APP_IDS), "ids dos pacotes divergem das 12 apps padrão")

    def test_every_package_is_tar_with_manifest_and_app_wasm(self):
        import tarfile

        for p in self.embedded_packages():
            with tarfile.open(p) as t:
                names = {m.name for m in t.getmembers() if m.isfile()}
            self.assertIn("manifest.json", names, f"{p.name} sem manifest.json")
            self.assertIn("app.wasm", names, f"{p.name} sem app.wasm (entry do manifest)")

    def test_every_app_wasm_is_valid_wasm_binary(self):
        import tarfile

        for p in self.embedded_packages():
            with tarfile.open(p) as t:
                data = t.extractfile("app.wasm").read()
            self.assertGreater(len(data), 8, f"app.wasm de {p.name} vazio demais")
            self.assertEqual(
                data[:8],
                WASM_MAGIC,
                f"app.wasm de {p.name} não começa com o magic+versão WASM "
                "(\x00asm\\x01\\x00\\x00\\x00) — o WAMR não aceitaria",
            )

    def test_every_manifest_entry_resolves_to_app_wasm(self):
        import tarfile

        for p in self.embedded_packages():
            with tarfile.open(p) as t:
                manifest = json_load(t, "manifest.json")
            self.assertEqual(
                manifest.get("entry"),
                "app.wasm",
                f"manifest de {p.name} precisa apontar entry para app.wasm "
                "(o arquivo que o WAMR vai carregar)",
            )

    def test_every_standard_app_has_visual_golden(self):
        for app_id, scenario in APP_SCENARIO_KEY.items():
            golden_dir = ROOT / "tests/simulator/goldens" / scenario
            shots = sorted(golden_dir.glob("01_*.bmp")) if golden_dir.is_dir() else []
            self.assertTrue(
                shots,
                f"{scenario} ({app_id}) não tem golden 01_*.bmp — o comparador "
                "de regressão visual não tem referência para validar o "
                "determinismo da execução WAMR",
            )


# ---------------------------------------------------------------------------
# 3) Carregamento/execução via runtime (launch path compartilhado)
# ---------------------------------------------------------------------------

class WasmLaunchExecutionContract(unittest.TestCase):
    """O launch carrega o entry do manifest pelo runtime real e executa."""

    def test_manifest_entry_defaults_to_app_wasm(self):
        self.assertIn('strncpy(out_manifest->entry, "app.wasm"', MANIFEST)

    def test_embedded_packages_load_wasm_from_tar_bytes(self):
        launch = launch_direct_body()
        self.assertIn("tab5_package_read_file_from_tar", launch)
        self.assertIn("tab5_wasm_load_from_bytes", launch)
        self.assertRegex(
            launch,
            r'\.tab5pkg',
            "o ramo embutido precisa reconhecer o instal_dir .tab5pkg para "
            "ler o wasm de dentro do tar",
        )

    def test_installed_apps_load_wasm_from_file(self):
        launch = launch_direct_body()
        self.assertIn("tab5_wasm_load_from_file", launch)
        self.assertRegex(
            launch,
            r'install_dir\s*\+\s*"/"\s*\+\s*entry->manifest\.entry',
            "apps instaladas (não-tar) precisam abrir <install_dir>/<entry> "
            "via tab5_wasm_load_from_file",
        )

    def test_preflight_selects_entrypoint_before_execution(self):
        launch = launch_direct_body()
        self.assertIn("tab5_wasm_select_entrypoint", launch)
        self.assertIn("tab5_wasm_call_function", launch)
        self.assertLess(
            launch.index("tab5_wasm_select_entrypoint"),
            launch.index("tab5_wasm_call_function"),
            "o preflight de entrypoint precisa ocorrer antes de chamar função "
            "(rollback precoce garante determinismo)",
        )

    def test_entrypoint_order_prefers_app_main_and_never_start(self):
        # Seleção textual da função select_entrypoint no runtime (braço real).
        m = re.search(
            r'extern\s+"C"\s+tab5_err_t\s+tab5_wasm_select_entrypoint.*?\n\}',
            RUNTIME,
            re.S,
        )
        self.assertIsNotNone(m, "select_entrypoint não encontrado no runtime")
        body = m.group(0)
        self.assertLess(body.index('"app_main"'), body.index('"main"'))
        self.assertNotIn("_start", body, "entrypoint _start não pode ser aceito")

    def test_message_entry_executed_after_load_and_init(self):
        launch = launch_direct_body()
        self.assertRegex(
            launch,
            r'entry_err\s*=\s*tab5_wasm_call_function\s*\(\s*&entry->wasm_inst\s*,\s*entrypoint\s*,\s*0\s*,\s*nullptr\s*\)',
            "o entrypoint selecionado é o que alimenta a execução WAMR real",
        )


# ---------------------------------------------------------------------------
# 4) Determinismo do simulador com WAMR real
# ---------------------------------------------------------------------------

class SimDeterminismContract(unittest.TestCase):
    """O pipeline de cenário permanece determinístico com WAMR real."""

    def test_scenario_mode_freezes_clock_and_seeds_rng(self):
        self.assertIn("simtime::set_frozen(true)", SIM_MAIN)
        self.assertIn("srand(42)", SIM_MAIN)

    def test_capture_happens_after_settle_pump(self):
        # Escopo no runner de cenários: o primeiro sim_capture_to_bmp do arquivo
        # pertence ao modo interativo (atalho S) e não ao pipeline de capturas.
        runner = function_body(SIM_MAIN, "int run_scenario(const std::string &name")
        idx_pump = runner.index("pump(step.settle_ms)")
        idx_shot = runner.index("sim_capture_to_bmp")
        self.assertLess(
            idx_pump,
            idx_shot,
            "a captura precisa acontecer só depois do pump de settle_ms — a "
            "janela que drena jobs assíncronos do despachador WASM",
        )

    def test_every_scenario_shot_has_settle_window(self):
        steps = re.findall(r"\{\s*(?:act_\w+|nullptr)\s*,\s*(\d+)\s*,\s*\"([^\"]+)\"\s*\}", SIM_SCENARIOS)
        self.assertGreaterEqual(
            len(steps),
            15,
            "é esperado ao menos um passo disparado por cenário com settle+shot",
        )
        for settle_ms, shot in steps:
            self.assertGreaterEqual(
                int(settle_ms),
                300,
                f"shot {shot} tem settle de {settle_ms}ms — abaixo da janela "
                "mínima para drenar o despacho WASM antes da captura",
            )

    def test_golden_comparison_still_enforced(self):
        self.assertIn("compare_images.py", SIM_SCRIPT)
        self.assertIn("--goldens", SIM_SCRIPT)
        self.assertIn("--out", SIM_SCRIPT)

    def test_host_launch_dispatch_is_synchronous(self):
        # No host/desktop o JOB_LAUNCH executa inline (não vira job de fila),
        # o que mantém o launch determinístico dentro do passo do cenário.
        m = re.search(
            r"#ifndef ESP_PLATFORM.*?JOB_LAUNCH.*?execute_job\(job\);.*?#endif",
            DISPATCHER,
            re.S,
        )
        self.assertIsNotNone(
            m,
            "tab5_wasm_dispatcher.cpp precisa manter o launch síncrono no "
            "build host (sem isso o cenário pode capturar antes do app abrir)",
        )


# ---------------------------------------------------------------------------
# 5) Caminho de instalação externa
# ---------------------------------------------------------------------------

class ExternalInstallPathContract(unittest.TestCase):
    """App externa instalada segue o mesmo caminho WAMR das embutidas."""

    def test_install_targets_installed_dir_by_app_id(self):
        # O código real envolve a macro em std::string(...) — a regex tolera.
        self.assertRegex(
            PMGR,
            r"TAB5_APPS_INSTALLED_DIR\s*\)?\s*\+\s*\"/\"\s*\+\s*manifest\.id",
        )

    def test_install_copies_manifest_entry_to_target(self):
        self.assertIn('target_dir + "/" + manifest.entry', PMGR)

    def test_scan_registers_installed_as_not_embedded(self):
        self.assertIn("scan_directory_and_register(TAB5_APPS_INSTALLED_DIR, false)", PMGR)
        self.assertIn("scan_directory_and_register(TAB5_APPS_EMBEDDED_DIR, true)", PMGR)

    def test_embedded_and_external_share_wasm_execution_path(self):
        launch = launch_direct_body()
        # Depois do load, embutida e instalada seguem o mesmo código linear de
        # execução: select_entrypoint -> init -> call_function -> resume.
        for token in ("tab5_wasm_select_entrypoint", "tab5_lifecycle_host_init_app",
                      "tab5_wasm_call_function", "tab5_lifecycle_host_resume_app"):
            self.assertIn(token, launch)
        # Não pode haver bifurcação is_embedded que pule a execução WAMR.
        self.assertNotIn("entry->is_embedded", launch)

    def test_external_package_install_supported_by_host_tests(self):
        host_cmake = (ROOT / "tests/host/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("src/test_package_mgr.cpp", host_cmake)


# ---------------------------------------------------------------------------
# 6) Contrato executável: binário tab5_sim com WAMR real + determinismo
# ---------------------------------------------------------------------------

class SimWamrBinaryContract(unittest.TestCase):
    """Contratos que rodam contra o binário do simulador (quando existir).

    Fase vermelha local: com ``build-sim/tab5_sim`` presente e WAMR ainda não
    linkado, ``test_sim_binary_must_link_real_wamr_symbols`` falha apontando a
    ausência da implementação. O teste de determinismo fica em skip documentado
    até o link real existir (não dá para provar determinismo do bytecode que o
    binário não executa). Em CI (quality-gate não constrói o sim), ambos fazem
    skip documentado — a fase vermelha fica com os contratos estáticos.
    """

    SIM_BIN = Path(os.environ.get("BUILD_DIR", str(ROOT / "build-sim"))) / "tab5_sim"

    def _binary_present(self):
        return self.SIM_BIN.is_file()

    def _wamr_linked(self):
        try:
            out = subprocess.run(
                ["nm", "-C", str(self.SIM_BIN)],
                capture_output=True,
                text=True,
            )
        except OSError:
            return False
        symbols = set()
        for line in out.stdout.splitlines():
            for sym in WAMR_RUNTIME_SYMBOLS:
                if sym in line:
                    symbols.add(sym)
        missing = set(WAMR_RUNTIME_SYMBOLS) - symbols
        self._missing = missing
        return not missing

    def test_sim_binary_must_link_real_wamr_symbols(self):
        if not self._binary_present():
            self.skipTest(
                "build-sim/tab5_sim ausente (rode tools/ci/run_sim_tests.sh); "
                "em CI sem o build do sim a fase vermelha é carregada pelos "
                "contratos estáticos acima",
            )
        self.assertTrue(
            self._wamr_linked(),
            "tab5_sim não linka símbolos WAMR; faltam definidos: "
            f"{getattr(self, '_missing', '?')}. O simulador ainda compila o "
            "stub (HAVE_WAMR=0) — falha esperada pela ausência da "
            "implementação de WAMR real no CMake do simulador",
        )

    def _run_scenario(self, out_dir):
        env = dict(os.environ)
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        return subprocess.run(
            [str(self.SIM_BIN), "--scenario", "app_terminal", "--out", out_dir],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            env=env,
        )

    def test_app_terminal_captures_deterministic_with_real_wamr(self):
        if not self._binary_present():
            self.skipTest("sem build-sim/tab5_sim — ver skip documentado do teste acima")
        if not self._wamr_linked():
            self.skipTest(
                "sem WAMR real no binário o bytecode não é executado; a prova "
                "de determinismo só é válida depois do link (deficiência já "
                "registrada em test_sim_binary_must_link_real_wamr_symbols)",
            )
        with tempfile.TemporaryDirectory() as tmp:
            run_a = str(Path(tmp) / "a")
            run_b = str(Path(tmp) / "b")
            rc_a = self._run_scenario(run_a)
            rc_b = self._run_scenario(run_b)
            self.assertEqual(rc_a.returncode, 0, f"execução A falhou: {rc_a.stderr[-400:]}")
            self.assertEqual(rc_b.returncode, 0, f"execução B falhou: {rc_b.stderr[-400:]}")
            bmp_a = Path(run_a) / "01_terminal.bmp"
            bmp_b = Path(run_b) / "01_terminal.bmp"
            self.assertTrue(bmp_a.is_file(), "execução A não produziu 01_terminal.bmp")
            self.assertTrue(bmp_b.is_file(), "execução B não produziu 01_terminal.bmp")
            crc_a = zlib.crc32(bmp_a.read_bytes())
            crc_b = zlib.crc32(bmp_b.read_bytes())
            self.assertEqual(
                crc_a,
                crc_b,
                "capturas do cenário app_terminal divergem entre execuções: a "
                "execução WAMR real quebrou o determinismo do simulador "
                "(clock congelado/settle não bastaram)",
            )


# ---------------------------------------------------------------------------
# Helpers de extração de corpo de função
# ---------------------------------------------------------------------------

def function_body(source: str, signature: str) -> str:
    """Retorna o corpo de uma função respeitando chaves aninhadas."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : pos]
    raise AssertionError(f"função sem fechamento: {signature}")


def launch_direct_body() -> str:
    return function_body(PMGR, "tab5_err_t tab5_package_mgr_launch_direct")


def json_load(tar, member):
    import json

    return json.load(tar.extractfile(tar.getmember(member)))


if __name__ == "__main__":
    unittest.main(verbosity=2)
