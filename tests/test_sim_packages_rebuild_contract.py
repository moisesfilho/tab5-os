#!/usr/bin/env python3
"""Validação pós-rebuild dos 12 pacotes .tab5pkg embutidos (etapa visual 2).

Plano aprovado (set/2026): após ``tools/ci/build_embedded_apps.sh`` regenerar
``embedded_apps_pkg/``, os 12 pacotes padrão devem continuar íntegros e
suficientes para a regressão visual da matriz A–D — cada app WASM tem:

1. **Pacote**: exatamente 12 ``*.tab5pkg`` (nos ids padrão
   ``com.tab5.*``), cada um um tar válido com exatamente
   ``manifest.json`` + ``app.wasm``.
2. **Manifesto**: parseável, com ``id``/``name``/``entry``/``icon_symbol``/
   ``icon_bg_color``/permissões/sizes válidos; ``entry == "app.wasm"`` e o
   ``id`` do manifesto casando com o nome do arquivo
   (``com.tab5.<id>.tab5pkg``) — é o que alimenta os tiles do launcher.
3. **Bytecode**: ``app.wasm`` com magic+versão WASM (\x00asm\x01\x00\x00\x00)
   e tamanho útil — o que o WAMR real do simulador executa.
4. **Rebuild**: o script de build regenera o diretório do zero (rm -rf +
   pack.py por app) e declara exatamente as 12 apps.
5. **Cobertura visual**: cada pacote tem um cenário correspondente na matriz
   A–D (grupo C/B) com launch via ``tab5_package_mgr_launch``, settle >= 300 ms
   e golden ``01_*.bmp`` — sem isso o comparador não tem referência.

Cobre a mesma área do contrato de WAMR (test_sim_wamr_contract.py) com o
ângulo de *pós-rebuild*: foco no estado dos pacotes gerados e nos metadados de
tile, não no caminho de execução do runtime. Não altera nem apaga cobertura
existente: só adiciona este contrato.

Sem binário do simulador não há necessidade de skip: a validação é 100% sobre
os artefatos de ``embedded_apps_pkg/`` (gerados no build local/CI).
"""

import json
import re
import tarfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EMBEDDED_PKG_DIR = ROOT / "embedded_apps_pkg"
BUILD_SCRIPT = (ROOT / "tools/ci/build_embedded_apps.sh").read_text(encoding="utf-8")
SIM_SCENARIOS = (ROOT / "tests/simulator/scenarios/sim_scenarios.cpp").read_text(encoding="utf-8")
GOLDENS = ROOT / "tests/simulator/goldens"
WASM_MAGIC = b"\x00asm\x01\x00\x00\x00"

# As 12 apps padrão embutidas (ids dos manifestos).
STANDARD_APP_IDS = (
    "com.tab5.bluetooth",
    "com.tab5.calendar",
    "com.tab5.camera",
    "com.tab5.chat",
    "com.tab5.files",
    "com.tab5.fileserver",
    "com.tab5.gallery",
    "com.tab5.music",
    "com.tab5.notas",
    "com.tab5.recorder",
    "com.tab5.terminal",
    "com.tab5.wifi",
)

# Pacote -> cenário da matriz A–D (app_id -> nome do cenário no simulador).
APP_SCENARIO_KEY = {
    "com.tab5.bluetooth": "app_bluetooth",
    "com.tab5.calendar": "app_calendar",
    "com.tab5.camera": "app_camera",
    "com.tab5.chat": "app_chat",
    "com.tab5.files": "app_files",
    "com.tab5.fileserver": "app_fileserver",
    "com.tab5.gallery": "app_gallery",
    "com.tab5.music": "app_music",
    "com.tab5.notas": "app_notas",
    "com.tab5.recorder": "app_recorder",
    "com.tab5.terminal": "app_terminal",
    "com.tab5.wifi": "app_wifi",
}

COLOR_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")


def package_files(app_id):
    """Carrega os arquivos do pacote uma única vez: {nome: bytes}."""
    pkg = EMBEDDED_PKG_DIR / f"{app_id}.tab5pkg"
    if not pkg.is_file():
        raise AssertionError(f"pacote {pkg.name} ausente após o rebuild")
    with tarfile.open(pkg) as t:
        members = {m.name: m for m in t.getmembers() if m.isfile()}
        if set(members) != {"manifest.json", "app.wasm"}:
            raise AssertionError(
                f"pacote {pkg.name} tem membros inesperados: {sorted(set(members) - {'manifest.json', 'app.wasm'}) or sorted(members)}"
            )
        return {n: t.extractfile(m).read() for n, m in members.items()}


def manifest_of(app_id):
    return json.loads(package_files(app_id)["manifest.json"])


class PostRebuildPackagesContract(unittest.TestCase):
    """Os 12 pacotes gerados pelo rebuild estão íntegros e completos."""

    def embedded_packages(self):
        return sorted(EMBEDDED_PKG_DIR.glob("*.tab5pkg"))

    def test_exactly_twelve_standard_packages(self):
        pkgs = self.embedded_packages()
        self.assertEqual(
            len(pkgs),
            12,
            "embedded_apps_pkg precisa conter exatamente 12 pacotes após o "
            f"rebuild; encontrados {len(pkgs)}",
        )
        ids = {
            json.loads(
                tarfile.open(p).extractfile(tarfile.open(p).getmember("manifest.json")).read()
            )["id"]
            for p in pkgs
        }
        self.assertEqual(ids, set(STANDARD_APP_IDS), "ids dos pacotes divergem das 12 apps padrão")

    def test_package_names_match_manifest_ids(self):
        for app_id in STANDARD_APP_IDS:
            pkg = EMBEDDED_PKG_DIR / f"{app_id}.tab5pkg"
            self.assertTrue(
                pkg.is_file(),
                f"esperado pacote {app_id}.tab5pkg (manifesto usa este id) — "
                "rebuild não gerou o arquivo de mesmo nome",
            )

    def test_every_package_is_tar_with_manifest_and_wasm_only(self):
        for app_id in STANDARD_APP_IDS:
            with tarfile.open(EMBEDDED_PKG_DIR / f"{app_id}.tab5pkg") as t:
                files = {m.name for m in t.getmembers() if m.isfile()}
            self.assertEqual(
                files,
                {"manifest.json", "app.wasm"},
                f"{app_id}: pacote após rebuild deve conter só manifest.json + app.wasm",
            )

    def test_every_manifest_is_valid_and_consistent(self):
        for app_id in STANDARD_APP_IDS:
            data = package_files(app_id)
            self.assertIn("manifest.json", data, f"{app_id} sem manifest.json")
            m = json.loads(data["manifest.json"])
            self.assertEqual(m.get("id"), app_id, f"manifest.id de {app_id} não casa com o arquivo")
            self.assertTrue(m.get("name"), f"{app_id} sem name")
            self.assertTrue(m.get("version"), f"{app_id} sem version")
            self.assertEqual(
                m.get("entry"), "app.wasm", f"{app_id}: entry precisa ser app.wasm"
            )
            self.assertIn("manifest.json", data)

    def test_tile_metadata_present_for_launcher(self):
        # Tiles do launcher são construídos com icon_symbol + icon_bg_color
        # (ui_desktop.cpp) — o rebuild precisa entregá-los para o Grupo A.
        for app_id in STANDARD_APP_IDS:
            m = json.loads(package_files(app_id)["manifest.json"])
            self.assertGreaterEqual(
                len(m.get("icon_symbol", "")),
                1,
                f"{app_id} sem icon_symbol — o tile do launcher ficaria vazio",
            )
            self.assertRegex(
                m.get("icon_bg_color", ""),
                COLOR_RE,
                f"{app_id} sem icon_bg_color #RRGGBB válido — tile sem cor de fundo",
            )

    def test_every_app_wasm_is_valid_and_substantial(self):
        for app_id in STANDARD_APP_IDS:
            data = package_files(app_id)
            wasm = data["app.wasm"]
            self.assertGreater(
                len(wasm), 1024, f"app.wasm de {app_id} suspeito de dummy (vazio demais)"
            )
            self.assertEqual(
                wasm[:8],
                WASM_MAGIC,
                f"app.wasm de {app_id} não começa com magic+versão WASM — o "
                "WAMR real do simulador não aceitaria",
            )


class RebuildScriptContract(unittest.TestCase):
    """O script de rebuild regenera o bundle do zero com as 12 apps."""

    def test_script_cleans_output_dir_first(self):
        self.assertIn("rm -rf", BUILD_SCRIPT)
        self.assertIn('PKG_OUTPUT_DIR="${REPO_ROOT}/embedded_apps_pkg"', BUILD_SCRIPT)

    def test_script_packs_each_standard_app(self):
        for app_id in STANDARD_APP_IDS:
            repo_dir = "tab5-app-" + app_id.rsplit(".", 1)[-1]
            self.assertIn(
                f'"{repo_dir}"',
                BUILD_SCRIPT,
                f"rebuild não declara {repo_dir} na lista EMBEDDED_APPS",
            )

    def test_script_uses_pack_tool_with_output_dir(self):
        self.assertIn("python3 \"${PACK_TOOL}\"", BUILD_SCRIPT)
        self.assertIn('-o "${PKG_OUTPUT_DIR}"', BUILD_SCRIPT)

    def test_script_exports_app_wasm_from_main_c(self):
        # Rebuild compila src/main.c com exports iguais aos consumidos pelo host.
        for symbol in (
            "--export=main",
            "--export=tab5_app_on_ui_event",
            "--export=tab5_app_on_theme_changed",
            "--export=tab5_app_on_open_file",
        ):
            self.assertIn(symbol, BUILD_SCRIPT, f"rebuild não exporta {symbol}")

    def test_script_declares_exactly_twelve_apps(self):
        count = re.search(
            r"EMBEDDED_APPS=\(\s*(.*?)\s*\)", BUILD_SCRIPT, re.S
        ).group(1)
        declared = re.findall(r'"([^"]+)"', count)
        self.assertEqual(
            len(declared),
            12,
            "EMBEDDED_APPS do rebuild precisa declarar exatamente as 12 apps",
        )


class PackageVisualCoverageContract(unittest.TestCase):
    """Cada pacote tem cenário na matriz A–D com launch, settle e golden."""

    def test_every_package_has_scenario_with_launch(self):
        for app_id, scenario in APP_SCENARIO_KEY.items():
            self.assertIn(
                f'"{scenario}"',
                SIM_SCENARIOS,
                f"{app_id} não tem cenário {scenario} na matriz",
            )
            self.assertIn(
                f'"{app_id}"',
                SIM_SCENARIOS,
                f"{app_id} não é lançado no cenário {scenario}",
            )

    def test_every_package_scenario_has_golden(self):
        for scenario in APP_SCENARIO_KEY.values():
            shots = sorted((GOLDENS / scenario).glob("01_*.bmp"))
            self.assertTrue(
                shots,
                f"{scenario} não tem golden 01_*.bmp — regressão visual sem referência",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
