"""Contratos de manifest, build e pacote do app Terminal (com.tab5.terminal).

O app WASM que hospeda o Micro-Shell é empacotado em `.tab5pkg` (tar POSIX
`manifest.json` + `app.wasm`) e embutido no firmware em `embedded_apps_pkg/`.
Um manifest inválido, exports ausentes ou pacote corrompido impedem o Terminal
no dispositivo — os mesmos problemas que a pipeline valida em CI.

Contratos:

1. `manifest.json` do fonte (`tab5-app-terminal/manifest.json`): id
   `com.tab5.terminal`, entry `app.wasm`, version semântica, permissions com
   `ui.keyboard` (requerida pelo fluxo help+Enter), stack/heap positivos,
   icon_symbol presente.
2. `app.wasm` é módulo WebAssembly válido (magic + version) e — no mínimo —
   exporta `app_main` e `tab5_app_on_ui_event` (entrypoint do ciclo de vida e
   dispatch de eventos de UI; o dispatch de tecla é o caminho do bug).
3. Os pacotes `dist/com.tab5.terminal.tab5pkg` (build do app) e
   `embedded_apps_pkg/com.tab5.terminal.tab5pkg` (firmware) são tar POSIX
   contendo `manifest.json` e `app.wasm`; o wasm embutido tem magic válida e
   o manifest embutido bate com o manifest fonte (id e entry).
4. O empacotamento é reproduzível pela toolchain do SDK (`pack_app`) a partir
   do diretório do app (mesmo tool que a pipeline das features).
"""

import json
import re
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP_DIR = ROOT.parent / "tab5-app-terminal"
MANIFEST_SRC = APP_DIR / "manifest.json"
WASM_SRC = APP_DIR / "app.wasm"
PKG_DIST = APP_DIR / "dist" / "com.tab5.terminal.tab5pkg"
PKG_EMBEDDED = ROOT / "embedded_apps_pkg" / "com.tab5.terminal.tab5pkg"

# Mesmo padrão do test_pack_tool.py: o SDK não é um pacote importável
# (`sdk/` não tem __init__.py); o tool é um módulo de topo em
# <root>/sdk/tab5-app-sdk/tools/pack.py que só carrega com o diretório no path.
SDK_TOOLS_DIR = ROOT / "sdk" / "tab5-app-sdk" / "tools"
sys.path.insert(0, str(ROOT / "tools" / "ci"))
sys.path.insert(0, str(SDK_TOOLS_DIR))
from validate_wasm_entrypoint import exported_names  # noqa: E402
from pack import pack_app  # noqa: E402


class TerminalManifestContract(unittest.TestCase):
    """Campos obrigatórios do manifest fonte."""

    @classmethod
    def setUpClass(cls):
        if not MANIFEST_SRC.exists():
            raise unittest.SkipTest(f"manifest do app ausente: {MANIFEST_SRC}")
        cls.m = json.loads(MANIFEST_SRC.read_text(encoding="utf-8"))

    def test_id_e_entry(self):
        self.assertEqual(self.m.get("id"), "com.tab5.terminal")
        self.assertEqual(self.m.get("entry"), "app.wasm")

    def test_version_semantica(self):
        self.assertRegex(self.m.get("version", ""), r"^\d+\.\d+\.\d+$")

    def test_permissions_incluem_ui_keyboard(self):
        perms = self.m.get("permissions", [])
        self.assertIsInstance(perms, list)
        self.assertIn("ui.keyboard", perms)

    def test_stack_heap_positivos_e_icon(self):
        self.assertGreater(self.m.get("stack_size", 0), 0)
        self.assertGreater(self.m.get("heap_size", 0), 0)
        self.assertTrue(self.m.get("icon_symbol"))


class TerminalWasmContract(unittest.TestCase):
    """app.wasm é módulo válido e exporta o que o runtime despacha."""

    @classmethod
    def setUpClass(cls):
        if not WASM_SRC.exists():
            raise unittest.SkipTest(f"wasm do app ausente: {WASM_SRC}")
        cls.data = WASM_SRC.read_bytes()

    def test_wasm_magic(self):
        self.assertEqual(
            self.data[:8], b"\x00asm\x01\x00\x00\x00",
            "app.wasm não é um módulo WebAssembly (magic/version)",
        )

    def test_exports_entrypoint_e_event_dispatch(self):
        names = exported_names(self.data)
        self.assertIn("app_main", names, "runtime precisa do entrypoint app_main")
        self.assertIn(
            "tab5_app_on_ui_event", names,
            "dispatch de tecla (UI_EVENT) precisa ser exportado — caminho do bug",
        )


class TerminalPackageContract(unittest.TestCase):
    """Pacotes tar válidos, com wasm e manifest coerentes com o fonte."""

    def _assert_pkg(self, pkg: Path, label: str):
        self.assertTrue(pkg.is_file(), f"{label}: pacote ausente {pkg}")
        with tarfile.open(pkg, "r:") as tar:
            names = tar.getnames()
            self.assertIn("manifest.json", names, f"{label}: sem manifest.json")
            self.assertIn("app.wasm", names, f"{label}: sem app.wasm")
            m = json.loads(tar.extractfile("manifest.json").read())  # type: ignore
            self.assertEqual(m["id"], "com.tab5.terminal", f"{label}: id divergente")
            self.assertEqual(m["entry"], "app.wasm", f"{label}: entry divergente")
            wasm = tar.extractfile("app.wasm").read()  # type: ignore
            self.assertEqual(
                wasm[:8], b"\x00asm\x01\x00\x00\x00", f"{label}: wasm com magic inválida")
            self.assertIn(
                "app_main", exported_names(wasm), f"{label}: wasm sem app_main")

    def test_pkg_dist_do_build(self):
        if not PKG_DIST.parent.exists():
            self.skipTest("dist/ ausente (app não buildado)")
        self._assert_pkg(PKG_DIST, "dist")

    def test_pkg_embutido_no_firmware(self):
        self._assert_pkg(PKG_EMBEDDED, "embedded_apps_pkg")

    def test_manifest_embutido_bate_com_o_fonte(self):
        if not (PKG_EMBEDDED.is_file() and MANIFEST_SRC.exists()):
            self.skipTest("pacote embutido ou manifest fonte ausentes")
        with tarfile.open(PKG_EMBEDDED, "r:") as tar:
            emb = json.loads(tar.extractfile("manifest.json").read())  # type: ignore
        src = json.loads(MANIFEST_SRC.read_text(encoding="utf-8"))
        for key in ("id", "name", "version", "entry", "permissions", "stack_size", "heap_size"):
            with self.subTest(key=key):
                self.assertEqual(emb.get(key), src.get(key), f"{key} divergiu no pacote")


class TerminalPackToolingContract(unittest.TestCase):
    """pack_app reproduz o pacote a partir do diretório do app."""

    def test_pack_app_gera_pacote_valido(self):
        if not APP_DIR.is_dir() or not WASM_SRC.exists():
            self.skipTest("diretório do app incompleto")
        with tempfile.TemporaryDirectory(prefix="tab5_pkg_") as tmp:
            out = Path(tmp)
            pkg = pack_app(str(APP_DIR), output_dir=tmp, package_name=None)
            pkg_path = Path(pkg)
            self.assertEqual(pkg_path.suffix, ".tab5pkg")
            with tarfile.open(pkg_path, "r:") as tar:
                self.assertIn("manifest.json", tar.getnames())
                self.assertIn("app.wasm", tar.getnames())
                m = json.loads(tar.extractfile("manifest.json").read())  # type: ignore
                self.assertEqual(m["id"], "com.tab5.terminal")
            self.assertLessEqual(pkg_path.stat().st_size, 4 * 1024 * 1024,
                                 "pacote acima de 4MB — manifest incorreto?")


if __name__ == "__main__":
    unittest.main()
