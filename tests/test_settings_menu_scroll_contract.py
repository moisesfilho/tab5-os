#!/usr/bin/env python3
"""Contratos de rolagem do painel de configurações do shell."""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI_BAR = (ROOT / "components/os/shell/ui_bar.cpp").read_text(encoding="utf-8")


class SettingsMenuScrollContract(unittest.TestCase):
    def test_settings_panel_is_height_limited_to_screen(self):
        self.assertIn("lv_obj_update_layout(menu_panel);", UI_BAR)
        self.assertIn("screen_height - panel_top - bottom_margin", UI_BAR)
        self.assertIn("lv_obj_set_height(menu_panel, max_panel_height);", UI_BAR)

    def test_vertical_scroll_is_confined_to_menu_panel(self):
        self.assertIn("lv_obj_set_scroll_dir(menu_panel, LV_DIR_VER);", UI_BAR)
        self.assertIn("lv_obj_add_flag(menu_panel, LV_OBJ_FLAG_SCROLLABLE);", UI_BAR)
        self.assertIn("lv_obj_clear_flag(menu_panel, LV_OBJ_FLAG_SCROLL_CHAIN_VER);", UI_BAR)

    def test_full_screen_overlay_is_not_scrollable(self):
        overlay_setup = UI_BAR.split("menu_overlay = lv_obj_create(layer);", 1)[1].split(
            "menu_panel = lv_obj_create(layer);", 1
        )[0]
        self.assertIn("lv_obj_clear_flag(menu_overlay, LV_OBJ_FLAG_SCROLLABLE);", overlay_setup)


if __name__ == "__main__":
    unittest.main()
