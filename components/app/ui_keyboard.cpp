#include "ui_keyboard.h"
#include <string.h>

namespace {

/* Paleta escura compartilhada visualmente com o indicador de orientacao. */
constexpr uint32_t COLOR_BACKGROUND = 0x10141C;
constexpr uint32_t COLOR_SURFACE = 0x1A2130;
constexpr uint32_t COLOR_SURFACE_ALT = 0x202A3D;
constexpr uint32_t COLOR_BORDER = 0x2A3450;
constexpr uint32_t COLOR_ACCENT = 0x3B82F6;
constexpr uint32_t COLOR_ACCENT_SOFT = 0x263E68;
constexpr uint32_t COLOR_TEXT = 0xE7ECF5;
constexpr uint32_t COLOR_TEXT_MUTED = 0x8491A8;

bool is_action_key(const char *text)
{
    if (text == nullptr) {
        return false;
    }

    return strstr(text, LV_SYMBOL_BACKSPACE) != nullptr ||
           strstr(text, LV_SYMBOL_OK) != nullptr ||
           strstr(text, LV_SYMBOL_LEFT) != nullptr ||
           strstr(text, LV_SYMBOL_RIGHT) != nullptr ||
           strstr(text, LV_SYMBOL_CLOSE) != nullptr ||
           strstr(text, LV_SYMBOL_UP) != nullptr ||
           strcmp(text, "abc") == 0 || strcmp(text, "ABC") == 0 ||
           strcmp(text, "1#") == 0;
}

/* Marca as teclas de acao com LV_STATE_CHECKED para receberem o destaque
 * via estilo LV_PART_ITEMS | LV_STATE_CHECKED (mecanismo do LVGL 9). */
void accent_action_keys(lv_obj_t *kb)
{
    const char *const *map = lv_buttonmatrix_get_map(kb);
    uint32_t id = 0;
    for (uint32_t i = 0; map[i] != nullptr && map[i][0] != '\0'; i++) {
        if (map[i][0] == '\n') {
            continue;
        }
        if (is_action_key(map[i])) {
            lv_buttonmatrix_set_button_ctrl(kb, id, LV_BUTTONMATRIX_CTRL_CHECKED);
        } else {
            lv_buttonmatrix_clear_button_ctrl(kb, id, LV_BUTTONMATRIX_CTRL_CHECKED);
        }
        id++;
    }
}

} // namespace

void ui_keyboard_create(lv_obj_t *parent)
{
    /* Area de digitacao */
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, lv_pct(90));
    lv_obj_set_height(ta, lv_pct(22));
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 40);
    lv_textarea_set_placeholder_text(ta, "Toque para digitar...");
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_cursor_click_pos(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(COLOR_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_pad_left(ta, 18, 0);
    lv_obj_set_style_pad_right(ta, 18, 0);
    lv_obj_set_style_pad_top(ta, 12, 0);
    lv_obj_set_style_pad_bottom(ta, 12, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COLOR_TEXT_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, lv_color_hex(COLOR_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_width(ta, 2, LV_PART_CURSOR);

    /* Teclado virtual */
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, lv_pct(100), lv_pct(58));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_bg_color(kb, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_pad_all(kb, 8, 0);
    lv_obj_set_style_pad_row(kb, 7, 0);
    lv_obj_set_style_pad_column(kb, 7, 0);
    lv_obj_set_style_radius(kb, 14, 0);

    /* Estado normal das teclas, pressionado e foco tem contraste claro. */
    lv_obj_set_style_bg_color(kb, lv_color_hex(COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(COLOR_SURFACE_ALT), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(COLOR_ACCENT_SOFT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(kb, lv_color_hex(COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(COLOR_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    accent_action_keys(kb);
}
