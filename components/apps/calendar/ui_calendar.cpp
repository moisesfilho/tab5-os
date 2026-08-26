#include "ui_calendar.h"
#include "ui_calendar_view.h"
#include "app_registry.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#include "calendar_logic.h"

namespace {

lv_obj_t *calendar_scr = nullptr;
ui_app_bar_t calendar_app_bar = {};
lv_obj_t *content_cont = nullptr;
ui_calendar_view_t calendar_view = {};

void close_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_shell_close_calendar();
}

static void ui_calendar_build_icon(lv_obj_t *icon_box)
{
    const ui_palette_t *pal = ui_theme_get();

    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(icon_box, LV_SCROLLBAR_MODE_OFF);

    /* Cartão base do calendário */
    lv_obj_t *sheet = lv_obj_create(icon_box);
    lv_obj_set_size(sheet, 48, 48);
    lv_obj_align(sheet, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(sheet, 8, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_width(sheet, 1, 0);
    lv_obj_set_style_border_color(sheet, lv_color_hex(pal->border), 0);
    lv_obj_set_style_pad_all(sheet, 0, 0);
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sheet, LV_SCROLLBAR_MODE_OFF);

    /* Faixa superior colorida */
    lv_obj_t *top_strip = lv_obj_create(sheet);
    lv_obj_set_size(top_strip, 48, 14);
    lv_obj_align(top_strip, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(top_strip, 6, 0);
    lv_obj_set_style_bg_color(top_strip, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_border_width(top_strip, 0, 0);
    lv_obj_set_style_pad_all(top_strip, 0, 0);
    lv_obj_clear_flag(top_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(top_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(top_strip, LV_SCROLLBAR_MODE_OFF);

    /* Número do dia */
    lv_obj_t *day_lbl = lv_label_create(sheet);
    lv_label_set_text(day_lbl, "31");
    lv_obj_set_style_text_font(day_lbl, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(day_lbl, lv_color_hex(pal->text), 0);
    lv_obj_align(day_lbl, LV_ALIGN_CENTER, 0, 6);
}

static void ui_calendar_theme_icon(lv_obj_t *icon_box)
{
    const ui_palette_t *pal = ui_theme_get();
    if (icon_box == nullptr) {
        return;
    }

    uint32_t count = lv_obj_get_child_count(icon_box);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *sheet = lv_obj_get_child(icon_box, i);
        if (sheet == nullptr) {
            continue;
        }
        lv_obj_set_style_bg_color(sheet, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(sheet, lv_color_hex(pal->border), 0);

        uint32_t sheet_children = lv_obj_get_child_count(sheet);
        for (uint32_t j = 0; j < sheet_children; j++) {
            lv_obj_t *child = lv_obj_get_child(sheet, j);
            if (child == nullptr) {
                continue;
            }
            if (lv_obj_check_type(child, &lv_label_class)) {
                lv_obj_set_style_text_color(child, lv_color_hex(pal->text), 0);
            } else {
                lv_obj_set_style_bg_color(child, lv_color_hex(pal->accent), 0);
            }
        }
    }
}

} // namespace

void ui_calendar_register(void)
{
    static const app_desc_t s_calendar_desc = {
        .id = "calendar",
        .name = "Calendário",
        .icon_symbol = nullptr,
        .icon_builder = ui_calendar_build_icon,
        .icon_theme_refresh = ui_calendar_theme_icon,
        .on_launch = ui_shell_open_calendar,
        .file_extensions = nullptr,
        .on_open_file = nullptr,
    };
    app_registry_register(&s_calendar_desc);
}

lv_obj_t *ui_calendar_create(void)
{
    const ui_palette_t *pal = ui_theme_get();

    calendar_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(calendar_scr, lv_color_hex(pal->background), 0);
    lv_obj_clear_flag(calendar_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Barra de título do app */
    calendar_app_bar = ui_app_bar_create(calendar_scr, "Calendário", close_btn_cb, nullptr);

    /* Área útil de conteúdo posicionada logo abaixo da barra do app */
    content_cont = lv_obj_create(calendar_scr);
    int32_t top = 2 * UI_BAR_HEIGHT;
    lv_obj_set_size(content_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(content_cont, 0, top);
    lv_obj_set_style_bg_opa(content_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_cont, 0, 0);
    lv_obj_set_style_pad_hor(content_cont, 16, 0);
    lv_obj_set_style_pad_top(content_cont, 8, 0);
    lv_obj_set_style_pad_bottom(content_cont, 16, 0);
    lv_obj_clear_flag(content_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Criação da grade de calendário dedicada */
    calendar_view = ui_calendar_view_create(content_cont, false);

    return calendar_scr;
}

void ui_calendar_refresh_theme(void)
{
    if (calendar_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(calendar_scr, lv_color_hex(pal->background), 0);
    ui_app_bar_refresh_theme(&calendar_app_bar);
    ui_calendar_view_refresh_theme(&calendar_view);
}

void ui_calendar_apply_layout(void)
{
    if (calendar_scr == nullptr) {
        return;
    }
    int32_t width = lv_display_get_horizontal_resolution(nullptr);
    if (calendar_app_bar.bar != nullptr && width > 0) {
        lv_obj_set_width(calendar_app_bar.bar, width);
    }
    ui_calendar_view_apply_layout(&calendar_view);
}

void ui_calendar_on_open(void)
{
    ui_calendar_view_update_today(&calendar_view);
    calendar_view.view_year = calendar_view.today_year;
    calendar_view.view_month = calendar_view.today_month;
    calendar_view.selected_day = calendar_view.today_day;
    ui_calendar_view_refresh(&calendar_view);
}
