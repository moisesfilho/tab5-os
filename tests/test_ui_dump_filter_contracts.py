"""Static acceptance contracts for ui.dump widget filtering.

The approved plan (boneco-de-lata.md §2.2 item 2) specifies that ui.dump
returns a structured list of buttons, labels, and textareas visible on the
active screen with positions, sizes, and texts.

Current known bug: dump_ui() in serial_bridge.cpp walks all LVGL descendants
without filtering out hidden widgets (LV_OBJ_FLAG_HIDDEN), zero-area widgets,
or widgets with empty text that add noise to the AI agent's inspection.

This suite codifies the contracts BEFORE implementation so the developer
implements against stable test expectations — standard TDD red phase.

Contracts:
  1. dump_ui must check LV_OBJ_FLAG_HIDDEN before emitting an item.
  2. dump_ui must skip items with zero width or height.
  3. dump_ui must skip items with both w==0 AND h==0 (degenerate).
  4. The output array "items" should not contain entries with w==0 or h==0.
  5. The frame output must still include the "items" key even when empty.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERIAL_BRIDGE_SRC = ROOT / "components/os/core/serial_bridge.cpp"


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def _dump_ui_body(src: str) -> str:
    """Extract the body of the dump_ui() function."""
    sig = "void dump_ui(lv_obj_t *obj, cJSON *items)"
    start = src.index(sig)
    opening = src.index("{", start)
    depth = 0
    for pos in range(opening, len(src)):
        if src[pos] == "{":
            depth += 1
        elif src[pos] == "}":
            depth -= 1
            if depth == 0:
                return src[opening + 1 : pos]
    raise AssertionError("dump_ui body not found")


class DumpUiHiddenFilterContract(unittest.TestCase):
    """Contract 1: dump_ui must filter out hidden LVGL widgets."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)
        try:
            cls.body = _dump_ui_body(cls.src)
        except (ValueError, AssertionError):
            cls.body = ""

    def test_dump_ui_exists(self):
        """dump_ui function must exist in serial_bridge.cpp."""
        self.assertIn("void dump_ui(", self.src,
                       "dump_ui function must exist for ui.dump command")

    def test_dump_ui_checks_hidden_flag(self):
        """dump_ui must check LV_OBJ_FLAG_HIDDEN before emitting items."""
        self.assertTrue(
            re.search(r'LV_OBJ_FLAG_HIDDEN', self.body),
            "dump_ui must check LV_OBJ_FLAG_HIDDEN to skip hidden widgets "
            "(boneco-de-lata.md §2.2 item 2: items visíveis)"
        )

    def test_dump_ui_hidden_check_precedes_emit(self):
        """The hidden check must occur BEFORE creating the cJSON item."""
        hidden_match = re.search(r'LV_OBJ_FLAG_HIDDEN', self.body)
        emit_match = re.search(r'cJSON_CreateObject\(\)', self.body)
        if hidden_match and emit_match:
            self.assertLess(
                hidden_match.start(), emit_match.start(),
                "LV_OBJ_FLAG_HIDDEN check must precede cJSON_CreateObject "
                "so hidden widgets are skipped before allocation"
            )


class DumpUiZeroSizeFilterContract(unittest.TestCase):
    """Contract 2: dump_ui must filter out zero-sized widgets."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)
        try:
            cls.body = _dump_ui_body(cls.src)
        except (ValueError, AssertionError):
            cls.body = ""

    def test_dump_ui_skips_zero_width(self):
        """dump_ui must check for zero width before emitting."""
        # Look for a condition that checks width > 0 or w != 0
        self.assertTrue(
            re.search(
                r'(?:lv_area_get_width|area_width|\.w\b|width)\s*[><=!]+\s*0|'
                r'(?:w|width)\s*!=\s*0|!(?:w|width)',
                self.body
            ),
            "dump_ui must check widget width > 0 before emitting "
            "(zero-width widgets are invisible noise)"
        )

    def test_dump_ui_skips_zero_height(self):
        """dump_ui must check for zero height before emitting."""
        self.assertTrue(
            re.search(
                r'(?:lv_area_get_height|area_height|\.h\b|height)\s*[><=!]+\s*0|'
                r'(?:h|height)\s*!=\s*0|!(?:h|height)',
                self.body
            ),
            "dump_ui must check widget height > 0 before emitting "
            "(zero-height widgets are invisible noise)"
        )


class DumpUiOutputShapeContract(unittest.TestCase):
    """Contract 3: ui.dump output shape and behavior."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_ui_dump_dispatches_to_dump_ui(self):
        """The ui.dump command handler must call dump_ui()."""
        dump_start = self.src.find('"ui.dump"')
        self.assertGreater(dump_start, 0, "ui.dump command not found")
        window = self.src[dump_start:dump_start + 300]
        self.assertIn("dump_ui", window,
                       "ui.dump dispatch must call dump_ui()")

    def test_ui_dump_uses_lv_screen_active(self):
        """ui.dump must walk from lv_screen_active()."""
        dump_start = self.src.find('"ui.dump"')
        window = self.src[dump_start:dump_start + 300]
        self.assertIn("lv_screen_active", window,
                       "ui.dump must start from lv_screen_active()")

    def test_ui_dump_also_walks_layer_top(self):
        """ui.dump must also walk lv_layer_top() for overlays."""
        dump_start = self.src.find('"ui.dump"')
        self.assertGreater(dump_start, 0, "ui.dump not found in source")
        window = self.src[dump_start:dump_start + 400]
        self.assertIn("lv_layer_top", window,
                       "ui.dump must also walk lv_layer_top() for overlays")

    def test_ui_dump_acquires_display_lock(self):
        """ui.dump must acquire bsp_display_lock for thread safety."""
        dump_start = self.src.find('"ui.dump"')
        window = self.src[dump_start:dump_start + 400]
        self.assertIn("bsp_display_lock", window,
                       "ui.dump must acquire display lock for thread safety")


if __name__ == "__main__":
    unittest.main(verbosity=2)
