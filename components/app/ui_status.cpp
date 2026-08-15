#include "ui_status.h"
#include "ui_theme.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "wifi_mgr.h"

namespace {

lv_obj_t *wifi_icon_btn = nullptr;
lv_obj_t *wifi_icon_label = nullptr;
lv_timer_t *wifi_status_timer = nullptr;
bool s_last_wifi_connected = false;

/* Atualiza o tema do icone de status Wi-Fi */
void apply_status_theme(void)
{
    const ui_palette_t *pal = ui_theme_get();

    if (wifi_icon_label != nullptr) {
        lv_obj_set_style_text_color(wifi_icon_label,
                                    lv_color_hex(s_last_wifi_connected ? pal->accent : pal->text_muted), 0);
    }
}

void wifi_status_update(void)
{
    wifi_status_t status = {};
    bool connected = false;
    if (wifi_mgr_get_status(&status) == ESP_OK) {
        connected = status.connected;
    }

    s_last_wifi_connected = connected;

    if (wifi_icon_label != nullptr) {
        const ui_palette_t *pal = ui_theme_get();
        lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(connected ? pal->accent : pal->text_muted), 0);
    }
}

void wifi_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    wifi_status_update();
}

void wifi_icon_click_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_wifi();
}

} // namespace

void ui_status_init(lv_obj_t *parent)
{
    /* Icone Wi-Fi interativo alinhado a esquerda do relogio */
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

    wifi_status_timer = lv_timer_create(wifi_status_timer_cb, 1000, nullptr);
    wifi_status_update();
}

void ui_status_refresh_theme(void)
{
    apply_status_theme();
}

void ui_status_set_rotation(lv_disp_rotation_t rot)
{
    (void)rot;
}
