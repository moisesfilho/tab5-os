#!/usr/bin/env python3
"""Contracts for the clang-tidy gate's failure and toolchain semantics."""

import pathlib
import os
import stat
import subprocess
import tempfile
import unittest
import json


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = (ROOT / "tools/ci/run_clang_tidy.sh").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/quality-gate.yml").read_text(encoding="utf-8")


class TestClangTidyGateContract(unittest.TestCase):
    def test_script_requires_compile_database_and_tool(self):
        self.assertIn("exit 127", SCRIPT)
        self.assertIn("exit 2", SCRIPT)
        self.assertIn('COMPILE_DB="$BUILD_DIR/compile_commands.json"', SCRIPT)
        self.assertIn("compile database ausente ou vazio", SCRIPT)

    def test_script_does_not_filter_diagnostics_or_discard_exit_code(self):
        self.assertIn("TIDY_STATUS=${PIPESTATUS[0]}", SCRIPT)
        self.assertIn('exit "$TIDY_STATUS"', SCRIPT)
        self.assertNotIn("grep -vE", SCRIPT)
        self.assertIn("incompatibilidade entre compile database/toolchain", SCRIPT)
        self.assertIn("não foram suprimidos", SCRIPT)

    def test_script_resolves_files_from_database_and_preserves_p(self):
        self.assertIn('python3 - "$ROOT_DIR" "$COMPILE_DB"', SCRIPT)
        self.assertIn('source = pathlib.Path(entry["file"])', SCRIPT)
        self.assertIn('-p "$BUILD_DIR"', SCRIPT)
        self.assertIn('source.suffix in {".c", ".cpp"}', SCRIPT)

    def test_workflow_uses_repository_gate(self):
        self.assertIn("./tools/ci/run_clang_tidy.sh", WORKFLOW)
        self.assertRegex(WORKFLOW, r"silkeh/clang:18@sha256:3914c93a02e866795aafc80737488e515b96390eff3d2787cf8c5095997baea9")
        self.assertIn("apt-get install -y --no-install-recommends cppcheck cmake git python3", WORKFLOW)
        self.assertIn("-DCMAKE_C_COMPILER=clang", WORKFLOW)
        self.assertIn("-DFETCHCONTENT_UPDATES_DISCONNECTED=ON", WORKFLOW)
        self.assertIn("clang-tidy --version | grep -Fq 'LLVM version 18.'", WORKFLOW)
        self.assertNotIn("grep -vE", WORKFLOW)
        self.assertNotIn("set +e", WORKFLOW)

    def test_gate_behavior_with_fixture_tools(self):
        """The wrapper must expose environmental and source failures distinctly."""
        with tempfile.TemporaryDirectory() as directory:
            fixture = pathlib.Path(directory)
            database = fixture / "compile_commands.json"
            database.write_text(json.dumps([
                {"directory": str(ROOT), "file": "main/app_main.cpp",
                 "arguments": ["clang++", "-DKEEP_THIS_ARGUMENT", "-c",
                                "main/app_main.cpp"]},
                {"directory": str(ROOT), "file": "tests/not-a-target.cpp",
                 "arguments": ["clang++", "-c", "tests/not-a-target.cpp"]},
            ]) + "\n", encoding="utf-8")
            database_contents = database.read_text(encoding="utf-8")
            invocation = fixture / "invocation.txt"

            def run(output="", status=0, have_database=True, files=None):
                tool = fixture / "fake-clang-tidy"
                tool.write_text(
                    "#!/bin/sh\n"
                    f"printf '%s\\n' \"$@\" > {invocation!s}\n"
                    f"printf '%s\\n' {output!r}\n"
                    f"exit {status}\n",
                    encoding="utf-8",
                )
                tool.chmod(tool.stat().st_mode | stat.S_IXUSR)
                if not have_database:
                    database.unlink(missing_ok=True)
                elif not database.exists():
                    database.write_text(database_contents, encoding="utf-8")
                return subprocess.run(
                    [str(ROOT / "tools/ci/run_clang_tidy.sh")]
                    + (files or ["main/app_main.cpp", "tests/not-a-target.cpp"]),
                    cwd=ROOT,
                    env={**os.environ, "CLANG_TIDY_BIN": str(tool),
                         "CLANG_TIDY_BUILD_DIR": str(fixture)},
                    capture_output=True, text=True,
                )

            self.assertEqual(run(have_database=False).returncode, 2)
            successful = run()
            self.assertEqual(successful.returncode, 0)
            invocation_args = invocation.read_text(encoding="utf-8").splitlines()
            self.assertEqual(invocation_args[:2], ["-p", str(fixture)])
            self.assertIn("main/app_main.cpp", invocation_args)
            self.assertNotIn("tests/not-a-target.cpp", invocation_args)
            self.assertEqual(run("warning: real source warning").returncode, 1)
            self.assertEqual(run("fatal error: 'missing.h' file not found").returncode, 2)
            self.assertEqual(run("warning: ignored by non-zero tool", status=7).returncode, 7)
            success = run(files=["main/app_main.cpp"])
            self.assertEqual(success.returncode, 0)
            self.assertIn("análise válida", success.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
