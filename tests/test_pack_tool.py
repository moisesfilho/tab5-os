#!/usr/bin/env python3
"""
Testes unitários para a ferramenta de empacotamento sdk/tab5-app-sdk/tools/pack.py
"""

import unittest
import tempfile
import os
import shutil
import json
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../sdk/tab5-app-sdk/tools')))
import pack

class TestPackTool(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.test_dir)

    def test_valid_manifest_and_packaging(self):
        app_dir = os.path.join(self.test_dir, "my_app")
        os.makedirs(app_dir)

        manifest = {
            "id": "com.example.testapp",
            "name": "Test App",
            "version": "1.0.0",
            "entry": "app.wasm",
            "permissions": ["ui_keyboard"]
        }
        with open(os.path.join(app_dir, "manifest.json"), "w") as f:
            json.dump(manifest, f)

        with open(os.path.join(app_dir, "app.wasm"), "wb") as f:
            f.write(b"\x00asm\x01\x00\x00\x00")

        out_dir = os.path.join(self.test_dir, "dist")
        pkg_path = pack.pack_app(app_dir, out_dir)

        self.assertTrue(os.path.isdir(pkg_path))
        self.assertTrue(pkg_path.endswith("com.example.testapp.tab5pkg"))
        self.assertTrue(os.path.exists(os.path.join(pkg_path, "manifest.json")))
        self.assertTrue(os.path.exists(os.path.join(pkg_path, "app.wasm")))

    def test_missing_manifest_raises_error(self):
        app_dir = os.path.join(self.test_dir, "empty_app")
        os.makedirs(app_dir)
        with self.assertRaises(FileNotFoundError):
            pack.pack_app(app_dir)

    def test_invalid_app_id_raises_error(self):
        app_dir = os.path.join(self.test_dir, "bad_app")
        os.makedirs(app_dir)
        manifest = {
            "id": "Invalid ID With Spaces!",
            "name": "Test App",
            "version": "1.0.0"
        }
        with open(os.path.join(app_dir, "manifest.json"), "w") as f:
            json.dump(manifest, f)
        with open(os.path.join(app_dir, "app.wasm"), "wb") as f:
            f.write(b"\x00asm")

        with self.assertRaises(ValueError):
            pack.pack_app(app_dir)

    def test_missing_wasm_entry_raises_error(self):
        app_dir = os.path.join(self.test_dir, "no_wasm_app")
        os.makedirs(app_dir)
        manifest = {
            "id": "com.example.nowasm",
            "name": "No Wasm App",
            "version": "1.0.0",
            "entry": "missing.wasm"
        }
        with open(os.path.join(app_dir, "manifest.json"), "w") as f:
            json.dump(manifest, f)

        with self.assertRaises(FileNotFoundError):
            pack.pack_app(app_dir)

if __name__ == "__main__":
    unittest.main()
