#include "ui_status.h"
#include "ui_theme.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "wifi_mgr.h"
#include "bt_mgr.h"

namespace {

lv_obj_t *bt_icon_btn = nullptr;
lv_obj_t *bt_icon_label = nullptr;
lv_obj_t *wifi_icon_btn = nullptr;
lv_obj_t *wifi_icon_label = nullptr;
lv_timer_t *status_timer = nullptr;
bool s_last_wifi_connected = false;
bool s_last_bt_connected = false;

/* Atualiza o tema dos icones de status */
void apply_status_theme(void)
{
    const ui_palette_t *pal = ui_theme_get();

    if (wifi_icon_label != nullptr) {
        lv_obj_set_style_text_color(wifi_icon_label,
                                    lv_color_hex(s_last_wifi_connected ? pal->accent : pal->text_muted), 0);
    }
    if (bt_icon_label != nullptr) {
        lv_obj_set_style_text_color(bt_icon_label, lv_color_hex(s_last_bt_connected ? pal->accent : pal->text_muted),
                                    0);
    }
}

void status_update(void)
{
    wifi_status_t w_status = {};
    bool w_connected = false;
    if (wifi_mgr_get_status(&w_status) == ESP_OK) {
        w_connected = w_status.connected;
    }
    s_last_wifi_connected = w_connected;

    bt_status_t b_status = {};
    bool b_connected = false;
    if (bt_mgr_get_status(&b_status) == ESP_OK) {
        b_connected = b_status.any_connected;
    }
    s_last_bt_connected = b_connected;

    const ui_palette_t *pal = ui_theme_get();
    if (wifi_icon_label != nullptr) {
        lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(w_connected ? pal->accent : pal->text_muted), 0);
    }
    if (bt_icon_label != nullptr) {
        lv_obj_set_style_text_color(bt_icon_label, lv_color_hex(b_connected ? pal->accent : pal->text_muted), 0);
    }
}

void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    status_update();
}

void wifi_icon_click_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_wifi();
}

void bt_icon_click_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_bluetooth();
}

} // namespace

void ui_status_init(lv_obj_t *parent)
{
    /* Icone Bluetooth interativo */
    bt_icon_btn = lv_obj_create(parent);
    lv_obj_set_size(bt_icon_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bt_icon_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bt_icon_btn, 0, 0);
    lv_obj_set_style_shadow_width(bt_icon_btn, 0, 0);
    lv_obj_set_style_radius(bt_icon_btn, 8, 0);
    lv_obj_set_style_pad_left(bt_icon_btn, 6, 0);
    lv_obj_set_style_pad_right(bt_icon_btn, 6, 0);
    lv_obj_set_style_pad_top(bt_icon_btn, 4, 0);
    lv_obj_set_style_pad_bottom(bt_icon_btn, 4, 0);
    lv_obj_set_style_margin_right(bt_icon_btn, 2, 0);
    lv_obj_clear_flag(bt_icon_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bt_icon_btn, bt_icon_click_cb, LV_EVENT_CLICKED, nullptr);

    bt_icon_label = lv_label_create(bt_icon_btn);
    lv_label_set_text(bt_icon_label, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(bt_icon_label, &lv_font_montserrat_14_latin1, 0);

    /* Icone Wi-Fi interativo alinhado ao lado do Bluetooth */
    wifi_icon_btn = lv_obj_create(parent);
    lv_obj_set_size(wifi_icon_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wifi_icon_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_icon_btn, 0, 0);
    lv_obj_set_style_shadow_width(wifi_icon_btn, 0, 0);
    lv_obj_set_style_radius(wifi_icon_btn, 8, 0);
    lv_obj_set_style_pad_left(wifi_icon_btn, 6, 0);
    lv_obj_set_style_pad_right(wifi_icon_btn, 6, 0);
    lv_obj_set_style_pad_top(wifi_icon_btn, 4, 0);
    lv_obj_set_style_pad_bottom(wifi_icon_btn, 4, 0);
    lv_obj_set_style_margin_right(wifi_icon_btn, 6, 0);
    lv_obj_clear_flag(wifi_icon_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(wifi_icon_btn, wifi_icon_click_cb, LV_EVENT_CLICKED, nullptr);

    wifi_icon_label = lv_label_create(wifi_icon_btn);
    lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon_label, &lv_font_montserrat_14_latin1, 0);

    apply_status_theme();

    status_timer = lv_timer_create(status_timer_cb, 1000, nullptr);
    status_update();
}

void ui_status_refresh_theme(void)
{
    apply_status_theme();
}

void ui_status_set_rotation(lv_disp_rotation_t rot)
{
    (void)rot;
}
