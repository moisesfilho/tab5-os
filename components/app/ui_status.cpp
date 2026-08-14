#include "ui_status.h"

namespace {

constexpr uint32_t COLOR_SURFACE = 0x1A2130;
constexpr uint32_t COLOR_BORDER = 0x2A3450;
constexpr uint32_t COLOR_ACCENT = 0x3B82F6;
constexpr uint32_t COLOR_TEXT = 0xE7ECF5;

lv_obj_t *status_label = nullptr;

} // namespace

void ui_status_init(lv_obj_t *parent)
{
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "0°");
    lv_obj_set_align(status_label, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(status_label, -28);
    lv_obj_set_y(status_label, 12);
    lv_obj_set_style_bg_color(status_label, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(status_label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(status_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_border_width(status_label, 1, 0);
    lv_obj_set_style_border_color(status_label, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_radius(status_label, 10, 0);
    lv_obj_set_style_pad_left(status_label, 12, 0);
    lv_obj_set_style_pad_right(status_label, 12, 0);
    lv_obj_set_style_pad_top(status_label, 5, 0);
    lv_obj_set_style_pad_bottom(status_label, 5, 0);
    lv_obj_set_style_outline_width(status_label, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(status_label, lv_color_hex(COLOR_ACCENT), LV_STATE_FOCUSED);
}

void ui_status_set_rotation(lv_disp_rotation_t rot)
{
    if (status_label == nullptr) {
        return;
    }

    switch (rot) {
    case LV_DISPLAY_ROTATION_90:
        lv_label_set_text(status_label, "90°");
        break;
    case LV_DISPLAY_ROTATION_180:
        lv_label_set_text(status_label, "180°");
        break;
    case LV_DISPLAY_ROTATION_270:
        lv_label_set_text(status_label, "270°");
        break;
    case LV_DISPLAY_ROTATION_0:
    default:
        lv_label_set_text(status_label, "0°");
        break;
    }
}
