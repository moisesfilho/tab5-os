#include "ui_desktop.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_font.h"

namespace {

lv_obj_t *desktop_scr = nullptr;
lv_obj_t *app_tile = nullptr;
lv_obj_t *app_icon = nullptr;
lv_obj_t *app_icon_label = nullptr;
lv_obj_t *app_label = nullptr;
lv_obj_t *wifi_tile = nullptr;
lv_obj_t *wifi_icon = nullptr;
lv_obj_t *wifi_icon_label = nullptr;
lv_obj_t *wifi_label = nullptr;
lv_obj_t *files_tile = nullptr;
lv_obj_t *files_icon = nullptr;
lv_obj_t *files_icon_label = nullptr;
lv_obj_t *files_label = nullptr;
lv_obj_t *bt_tile = nullptr;
lv_obj_t *bt_icon = nullptr;
lv_obj_t *bt_icon_label = nullptr;
lv_obj_t *bt_label = nullptr;

void app_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_notas();
}

void wifi_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_wifi();
}

void files_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_files();
}

void bt_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_bluetooth();
}

/* Reaplica a paleta ativa na area de trabalho. */
void apply_desktop_theme(void)
{
    if (desktop_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(desktop_scr, lv_color_hex(pal->background), 0);

    if (app_icon != nullptr) {
        lv_obj_set_style_bg_color(app_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (app_icon_label != nullptr) {
        lv_obj_set_style_text_color(app_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (app_label != nullptr) {
        lv_obj_set_style_text_color(app_label, lv_color_hex(pal->text), 0);
    }
    if (wifi_icon != nullptr) {
        lv_obj_set_style_bg_color(wifi_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (wifi_icon_label != nullptr) {
        lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (wifi_label != nullptr) {
        lv_obj_set_style_text_color(wifi_label, lv_color_hex(pal->text), 0);
    }
    if (files_icon != nullptr) {
        lv_obj_set_style_bg_color(files_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (files_icon_label != nullptr) {
        lv_obj_set_style_text_color(files_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (files_label != nullptr) {
        lv_obj_set_style_text_color(files_label, lv_color_hex(pal->text), 0);
    }
    if (bt_icon != nullptr) {
        lv_obj_set_style_bg_color(bt_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (bt_icon_label != nullptr) {
        lv_obj_set_style_text_color(bt_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (bt_label != nullptr) {
        lv_obj_set_style_text_color(bt_label, lv_color_hex(pal->text), 0);
    }
}

} // namespace

void ui_desktop_create(lv_obj_t *scr)
{
    desktop_scr = scr;

    /* Tile do app Notas */
    app_tile = lv_obj_create(scr);
    lv_obj_set_size(app_tile, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(app_tile, LV_ALIGN_TOP_LEFT, 16, UI_BAR_HEIGHT + 24);
    lv_obj_set_style_bg_opa(app_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(app_tile, 0, 0);
    lv_obj_set_style_shadow_width(app_tile, 0, 0);
    lv_obj_set_style_pad_all(app_tile, 8, 0);
    lv_obj_clear_flag(app_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(app_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(app_tile, app_tile_cb, LV_EVENT_CLICKED, nullptr);

    app_icon = lv_obj_create(app_tile);
    lv_obj_set_size(app_icon, 76, 76);
    lv_obj_set_style_radius(app_icon, 18, 0);
    lv_obj_set_style_border_width(app_icon, 0, 0);
    lv_obj_set_style_shadow_width(app_icon, 0, 0);
    lv_obj_clear_flag(app_icon, LV_OBJ_FLAG_CLICKABLE);

    app_icon_label = lv_label_create(app_icon);
    lv_label_set_text(app_icon_label, "N");
    lv_obj_set_style_text_font(app_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(app_icon_label);

    app_label = lv_label_create(app_tile);
    lv_label_set_text(app_label, "Notas");
    lv_obj_set_style_text_font(app_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(app_label, 6, 0);

    /* Tile do app WiFi */
    wifi_tile = lv_obj_create(scr);
    lv_obj_set_size(wifi_tile, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(wifi_tile, LV_ALIGN_TOP_LEFT, 124, UI_BAR_HEIGHT + 24);
    lv_obj_set_style_bg_opa(wifi_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_tile, 0, 0);
    lv_obj_set_style_shadow_width(wifi_tile, 0, 0);
    lv_obj_set_style_pad_all(wifi_tile, 8, 0);
    lv_obj_clear_flag(wifi_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(wifi_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(wifi_tile, wifi_tile_cb, LV_EVENT_CLICKED, nullptr);

    wifi_icon = lv_obj_create(wifi_tile);
    lv_obj_set_size(wifi_icon, 76, 76);
    lv_obj_set_style_radius(wifi_icon, 18, 0);
    lv_obj_set_style_border_width(wifi_icon, 0, 0);
    lv_obj_set_style_shadow_width(wifi_icon, 0, 0);
    lv_obj_clear_flag(wifi_icon, LV_OBJ_FLAG_CLICKABLE);
    wifi_icon_label = lv_label_create(wifi_icon);
    lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(wifi_icon_label);

    wifi_label = lv_label_create(wifi_tile);
    lv_label_set_text(wifi_label, "WiFi");
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(wifi_label, 6, 0);

    /* Tile do app Arquivos */
    files_tile = lv_obj_create(scr);
    lv_obj_set_size(files_tile, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(files_tile, LV_ALIGN_TOP_LEFT, 232, UI_BAR_HEIGHT + 24);
    lv_obj_set_style_bg_opa(files_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(files_tile, 0, 0);
    lv_obj_set_style_shadow_width(files_tile, 0, 0);
    lv_obj_set_style_pad_all(files_tile, 8, 0);
    lv_obj_clear_flag(files_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(files_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(files_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(files_tile, files_tile_cb, LV_EVENT_CLICKED, nullptr);

    files_icon = lv_obj_create(files_tile);
    lv_obj_set_size(files_icon, 76, 76);
    lv_obj_set_style_radius(files_icon, 18, 0);
    lv_obj_set_style_border_width(files_icon, 0, 0);
    lv_obj_set_style_shadow_width(files_icon, 0, 0);
    lv_obj_clear_flag(files_icon, LV_OBJ_FLAG_CLICKABLE);
    files_icon_label = lv_label_create(files_icon);
    lv_label_set_text(files_icon_label, LV_SYMBOL_DIRECTORY);
    lv_obj_set_style_text_font(files_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(files_icon_label);

    files_label = lv_label_create(files_tile);
    lv_label_set_text(files_label, "Arquivos");
    lv_obj_set_style_text_font(files_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(files_label, 6, 0);

    /* Tile do app Bluetooth */
    bt_tile = lv_obj_create(scr);
    lv_obj_set_size(bt_tile, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(bt_tile, LV_ALIGN_TOP_LEFT, 340, UI_BAR_HEIGHT + 24);
    lv_obj_set_style_bg_opa(bt_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bt_tile, 0, 0);
    lv_obj_set_style_shadow_width(bt_tile, 0, 0);
    lv_obj_set_style_pad_all(bt_tile, 8, 0);
    lv_obj_clear_flag(bt_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bt_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bt_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(bt_tile, bt_tile_cb, LV_EVENT_CLICKED, nullptr);

    bt_icon = lv_obj_create(bt_tile);
    lv_obj_set_size(bt_icon, 76, 76);
    lv_obj_set_style_radius(bt_icon, 18, 0);
    lv_obj_set_style_border_width(bt_icon, 0, 0);
    lv_obj_set_style_shadow_width(bt_icon, 0, 0);
    lv_obj_clear_flag(bt_icon, LV_OBJ_FLAG_CLICKABLE);
    bt_icon_label = lv_label_create(bt_icon);
    lv_label_set_text(bt_icon_label, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(bt_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(bt_icon_label);

    bt_label = lv_label_create(bt_tile);
    lv_label_set_text(bt_label, "Bluetooth");
    lv_obj_set_style_text_font(bt_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(bt_label, 6, 0);

    apply_desktop_theme();
}

void ui_desktop_refresh_theme(void)
{
    apply_desktop_theme();
}
