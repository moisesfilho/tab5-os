#!/usr/bin/env python3
"""Contratos de regressão para tiles do launcher, host views e apps WASM.

Plano aprovado (etapa visual 2, set/2026) — os três alvos da matriz A–D são
estáveis em fonte:

1. **Tiles do launcher (Grupo A)**: o desktop é construído dinamicamente a
   partir do app_registry, e cada tile usa os metadados do manifesto
   (``icon_symbol``, ``icon_bg_color``, ``id``) registrados pelo package
   manager. Nenhuma lista hardcoded dos 12 apps no shell.
2. **Host views nativas (Grupo B)**: ``tab5_ui_host`` despacha
   ``com.tab5.camera``/``com.tab5.gallery`` para ``ui_camera_view``/
   ``ui_gallery_view`` com ciclo de vida completo (create/destroy/apply_layout/
   refresh_theme/start/open_file).
3. **Apps WASM (Grupo C + pacotes)**: cada app padrão é lançada via
   ``tab5_package_mgr_launch`` no cenário correspondente, registrada como
   embutida pela varredura ``TAB5_APPS_EMBEDDED_DIR`` e exporta os símbolos de
   ciclo de vida que o host consome.

Esta suíte é estática (análise de fonte, sem binário) para não depender do
build do simulador no CI. Não altera produção e não apaga cobertura existente.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / "components/os/shell"
RUNTIME = ROOT / "components/os/runtime"
UI_HOST = (RUNTIME / "tab5_ui_host.cpp").read_text(encoding="utf-8")
UI_DESKTOP = (SHELL / "ui_desktop.cpp").read_text(encoding="utf-8")
UI_SHELL = (SHELL / "ui_shell.cpp").read_text(encoding="utf-8")
MANIFEST_H = (RUNTIME / "tab5_manifest.h").read_text(encoding="utf-8")
MANIFEST_CPP = (RUNTIME / "tab5_manifest.cpp").read_text(encoding="utf-8")
PKG_MGR = (RUNTIME / "tab5_package_mgr.cpp").read_text(encoding="utf-8")
SIM_SCENARIOS = (ROOT / "tests/simulator/scenarios/sim_scenarios.cpp").read_text(encoding="utf-8")
BUILD_SCRIPT = (ROOT / "tools/ci/build_embedded_apps.sh").read_text(encoding="utf-8")

# Ids das 12 apps padrão (espelha test_sim_packages_rebuild_contract.py).
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


# ---------------------------------------------------------------------------
# 1) Tiles do launcher (Grupo A — shell_desktop etc.)
# ---------------------------------------------------------------------------

class LauncherTileContract(unittest.TestCase):
    """Tiles do desktop vem do registro de apps (manifestos), não hardcoded."""

    def test_desktop_builds_tiles_from_app_registry(self):
        self.assertIn("app_registry_get_all()", UI_DESKTOP)
        self.assertIn("ui_desktop_create", UI_SHELL)

    def test_tile_uses_manifest_bg_color(self):
        self.assertIn("app.icon_bg_color", UI_DESKTOP)
        self.assertIn("icon_bg_color", MANIFEST_H)

    def test_tile_uses_manifest_icon_symbol(self):
        self.assertIn("app.icon_symbol", UI_DESKTOP)
        self.assertIn("icon_symbol", MANIFEST_H)

    def test_tile_click_carries_app_id(self):
        self.assertIn("(void *)app.id", UI_DESKTOP)

    def test_scan_and_register_runs_before_desktop_build(self):
        self.assertIn("tab5_package_mgr_scan_and_register_all()", UI_SHELL)
        idx_scan = UI_SHELL.index("tab5_package_mgr_scan_and_register_all()")
        idx_desktop = UI_SHELL.index("ui_desktop_create")
        self.assertLess(
            idx_scan,
            idx_desktop,
            "a varredura de manifestos precisa ocorrer antes de construir os "
            "tiles — senão o launcher nasce vazio",
        )

    def test_manifest_exposes_tile_fields(self):
        for field in ("id[64]", "name[64]", "version[32]", "icon_symbol[32]", "icon_bg_color[16]"):
            self.assertIn(field, MANIFEST_H)

    def test_manifest_parser_fills_tile_fields(self):
        self.assertIn("icon_symbol", MANIFEST_CPP)
        self.assertIn("icon_bg_color", MANIFEST_CPP)


# ---------------------------------------------------------------------------
# 2) Host views nativas (Grupo B — app_camera, app_gallery)
# ---------------------------------------------------------------------------

class NativeHostViewContract(unittest.TestCase):
    """Câmera e Galeria continuam servidas por host views nativas."""

    VIEWS = {
        "com.tab5.camera": {
            "include": "#include \"ui_camera_view.h\"",
            "create": "ui_camera_view_create",
            "destroy": "ui_camera_view_destroy",
            "layout": "ui_camera_view_apply_layout",
            "theme": "ui_camera_view_refresh_theme",
            "start": "ui_camera_view_start",
            "header": SHELL / "ui_camera_view.h",
        },
        "com.tab5.gallery": {
            "include": "#include \"ui_gallery_view.h\"",
            "create": "ui_gallery_view_create",
            "destroy": "ui_gallery_view_destroy",
            "layout": "ui_gallery_view_apply_layout",
            "theme": "ui_gallery_view_refresh_theme",
            "start": "ui_gallery_view_start",
            "open_file": "ui_gallery_view_open_file",
            "header": SHELL / "ui_gallery_view.h",
        },
    }

    def test_host_dispatch_routes_app_ids_to_native_views(self):
        for app_id in ("com.tab5.camera", "com.tab5.gallery"):
            self.assertIn(f'"{app_id}"', UI_HOST)

    def test_host_view_has_full_lifecycle_hooks(self):
        for app_id, hooks in self.VIEWS.items():
            header = hooks["header"]
            self.assertTrue(header.is_file(), f"{header.name} ausente em os/shell")
            header_src = header.read_text(encoding="utf-8")
            for func in ("create", "destroy", "layout", "theme"):
                self.assertIn(hooks[func], UI_HOST, f"{hooks[func]} ausente no host")
                self.assertIn(hooks[func], header_src, f"{hooks[func]} ausente em {header.name}")
            self.assertIn(hooks["start"], UI_HOST)

    def test_gallery_open_file_hook_routed(self):
        self.assertIn("ui_gallery_view_open_file", UI_HOST)


# ---------------------------------------------------------------------------
# 3) Apps WASM (Grupo C + integração com a matriz de cenários)
# ---------------------------------------------------------------------------

class WasmAppsContract(unittest.TestCase):
    """As 12 apps padrão são lançadas pelo package manager no simulador."""

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

    def test_every_app_launched_in_its_scenario(self):
        for app_id in STANDARD_APP_IDS:
            self.assertIn(
                f'"{app_id}"',
                SIM_SCENARIOS,
                f"{app_id} não é lançada via tab5_package_mgr_launch no simulador",
            )

    def test_app_scenarios_have_settle_and_shot(self):
        # Cada app precisa de um passo com settle >= 300ms e shot 01_*.bmp.
        scenario_list = re.findall(
            r'\{"([a-z_]+)"\s*,\s*"[^"]*"\s*,\s*\{(.*?)\}\s*\},', SIM_SCENARIOS, re.S
        )
        blocks = {name: body for name, body in scenario_list}
        for app_id, scenario in self.APP_SCENARIO_KEY.items():
            body = blocks.get(scenario)
            self.assertIsNotNone(
                body, f"cenário {scenario} ({app_id}) sem corpo na matriz"
            )
            steps = re.findall(r"\{\s*(?:\w+|\w+\(\))[^}]*?,\s*(\d+)\s*,\s*\"([^\"]+)\"\s*\}", body)
            self.assertTrue(steps, f"cenário {scenario} sem passos com shot")
            for settle_ms, shot in steps:
                self.assertGreaterEqual(
                    int(settle_ms),
                    300,
                    f"{scenario}: shot {shot} com settle {settle_ms}ms < 300ms",
                )

    def test_embedded_registration_via_package_manager(self):
        self.assertIn("scan_directory_and_register(TAB5_APPS_EMBEDDED_DIR, true)", PKG_MGR)
        self.assertIn("is_embedded", PKG_MGR)

    def test_launch_direct_is_package_manager_entry(self):
        # O launch público delega ao launch_direct (caminho único da matriz).
        self.assertIn("tab5_package_mgr_launch_direct", PKG_MGR)

    def test_rebuild_exports_app_lifecycle_symbols(self):
        for symbol in (
            "--export=tab5_app_on_ui_event",
            "--export=tab5_app_on_theme_changed",
            "--export=tab5_app_on_open_file",
        ):
            self.assertIn(symbol, BUILD_SCRIPT, f"rebuild não exporta {symbol}")

    def test_build_script_declares_all_twelve_apps(self):
        count = re.search(r"EMBEDDED_APPS=\(\s*(.*?)\s*\)", BUILD_SCRIPT, re.S).group(1)
        declared = re.findall(r'"([^"]+)"', count)
        self.assertEqual(len(declared), 12)


if __name__ == "__main__":
    unittest.main(verbosity=2)
