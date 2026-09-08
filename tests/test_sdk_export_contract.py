"""Contract test for the SDK's WASM entrypoint export."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "sdk/tab5-app-sdk/include/tab5_sdk.h"


def read_uleb(data: bytes, offset: int):
    value = 0
    shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7


def wasm_exports(path: Path):
    data = path.read_bytes()
    if data[:4] != b"\0asm":
        raise AssertionError("compiler did not produce a WASM module")
    offset = 8
    exports = []
    while offset < len(data):
        section_id = data[offset]
        size, payload = read_uleb(data, offset + 1)
        end = payload + size
        if section_id == 7:
            count, pos = read_uleb(data, payload)
            for _ in range(count):
                length, pos = read_uleb(data, pos)
                exports.append(data[pos : pos + length].decode())
                pos += length
                pos += 1  # skip export kind
                _, pos = read_uleb(data, pos)
        offset = end
    return exports


class SdkExportContract(unittest.TestCase):
    def test_header_separates_callback_and_entrypoint_exports(self):
        source = HEADER.read_text(encoding="utf-8")
        self.assertIn("defined(__wasm32__)", source)
        self.assertIn('export_name("app_main")', source)
        self.assertIn("#define TAB5_APP_EXPORT __attribute__((visibility(\"default\")))", source)
        self.assertIn("#define TAB5_APP_ENTRYPOINT_EXPORT", source)
        self.assertRegex(
            source,
            r"#else\s*\n#define TAB5_APP_EXPORT\s*\n#define TAB5_APP_ENTRYPOINT_EXPORT\s*\n#endif",
        )

    def test_clang_wasi_emits_single_app_main_and_callback_export(self):
        clang = shutil.which("clang") or "/home/moises/.wasi-sdk/bin/clang"
        if not Path(clang).is_file():
            self.skipTest("WASI clang unavailable")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            source = directory / "probe.c"
            output = directory / "probe.wasm"
            source.write_text(
                '#include "tab5_sdk.h"\n'
                "TAB5_APP_EXPORT void callback(void) {}\n"
                "TAB5_APP_ENTRYPOINT_EXPORT int tab5_wasm_app_main(void) { return 0; }\n",
                encoding="utf-8",
            )
            subprocess.run(
                [
                    clang,
                    "--target=wasm32-wasi",
                    f"-I{HEADER.parent}",
                    "-Wl,--export=main",
                    "-Wl,--export=callback",
                    "-Wl,--allow-undefined",
                    "-o",
                    str(output),
                    str(source),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            exports = wasm_exports(output)
            self.assertIn("app_main", exports)
            self.assertIn("callback", exports)
            self.assertEqual(exports.count("app_main"), 1)

    def test_native_compiler_accepts_export_macro(self):
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("native C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "probe.c"
            source.write_text(
                '#include "tab5_sdk.h"\n'
                "TAB5_APP_EXPORT void callback(void) {}\n"
                "TAB5_APP_ENTRYPOINT_EXPORT int tab5_wasm_app_main(void) { return 0; }\n",
                encoding="utf-8",
            )
            subprocess.run(
                [compiler, "-std=c11", "-fsyntax-only", f"-I{HEADER.parent}", str(source)],
                check=True,
                capture_output=True,
                text=True,
            )


if __name__ == "__main__":
    unittest.main()
