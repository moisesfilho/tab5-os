#!/usr/bin/env python3
"""Static safety contract for the fixed-size serial bridge task."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/os/core/serial_bridge.cpp"


class SerialBridgeStackContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_task_stack_remains_8kib(self):
        self.assertIn(
            'xTaskCreatePinnedToCore(serial_bridge_task, "serial_bridge", 8192',
            self.source,
        )

    def test_large_response_and_path_buffers_are_not_stack_objects(self):
        self.assertNotIn("char response[16384]", self.source)
        self.assertNotIn("char safe_path[PATH_MAX]", self.source)
        self.assertNotIn("char root_real[PATH_MAX]", self.source)
        self.assertNotIn("char path_real[PATH_MAX]", self.source)
        self.assertIn("new (std::nothrow) char[kMaxResponseBytes]", self.source)
        self.assertIn("new (std::nothrow) char[kMaxInputPathBytes]", self.source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
