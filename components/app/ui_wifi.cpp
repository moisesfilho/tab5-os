#include "ui_wifi.h"
#include "ui_bar.h"
#include "ui_font.h"
#include "ui_keyboard.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "wifi_mgr.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr int MAX_NETWORKS = WIFI_SCAN_MAX_APS;

struct network_item_t {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
};

lv_obj_t *wifi_scr = nullptr;
lv_obj_t *wifi_bar = nullptr;
lv_obj_t *wifi_title = nullptr;
lv_obj_t *wifi_close = nullptr;
lv_obj_t *wifi_close_label = nullptr;
lv_obj_t *scan_button = nullptr;
lv_obj_t *scan_label = nullptr;
lv_obj_t *network_list = nullptr;
lv_obj_t *password_ta = nullptr;
lv_obj_t *connect_button = nullptr;
lv_obj_t *connect_label = nullptr;
lv_obj_t *status_label = nullptr;
lv_group_t *wifi_group = nullptr;
lv_timer_t *connect_timer = nullptr;

network_item_t networks[MAX_NETWORKS] = {};
int network_count = 0;
char selected_ssid[33] = {};
uint32_t connect_ticks = 0;

void set_status(const char *text, uint32_t color)
{
    if (status_label == nullptr) {
        return;
    }
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
}

void apply_wifi_layout(void)
{
    if (wifi_scr == nullptr) {
        return;
    }

    int32_t width = lv_display_get_horizontal_resolution(nullptr);
    int32_t height = lv_display_get_vertical_resolution(nullptr);
    int32_t content_height = height - 2 * UI_BAR_HEIGHT;
    int32_t side = width < 560 ? 12 : 24;

    lv_obj_set_width(scan_button, width - 2 * side);
    lv_obj_set_width(network_list, width - 2 * side);
    lv_obj_set_width(password_ta, width - 2 * side);
    lv_obj_set_width(connect_button, width - 2 * side);
    lv_obj_set_height(network_list, content_height - 180);
    lv_obj_set_x(scan_button, side);
    lv_obj_set_x(network_list, side);
    lv_obj_set_x(password_ta, side);
    lv_obj_set_x(connect_button, side);
}

void resolution_cb(lv_event_t *event)
{
    (void)event;
    apply_wifi_layout();
}

void apply_wifi_theme(void)
{
    if (wifi_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(wifi_scr, lv_color_hex(pal->background), 0);
    lv_obj_set_style_bg_color(wifi_bar, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_border_color(wifi_bar, lv_color_hex(pal->border), 0);
    lv_obj_set_style_text_color(wifi_title, lv_color_hex(pal->text), 0);
    lv_obj_set_style_bg_opa(wifi_close, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_close, 1, 0);
    lv_obj_set_style_border_color(wifi_close, lv_color_hex(pal->text_muted), 0);
    lv_obj_set_style_bg_opa(wifi_close, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(wifi_close, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(wifi_close_label, lv_color_hex(pal->text), 0);

    lv_obj_set_style_bg_color(scan_button, lv_color_hex(pal->accent_soft), 0);
    lv_obj_set_style_bg_color(scan_button, lv_color_hex(pal->accent), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(scan_label, lv_color_hex(pal->text), 0);
    lv_obj_set_style_bg_color(network_list, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(network_list, lv_color_hex(pal->border), 0);
    lv_obj_set_style_bg_color(password_ta, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_text_color(password_ta, lv_color_hex(pal->text), 0);
    lv_obj_set_style_text_color(password_ta, lv_color_hex(pal->text_muted), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(password_ta, lv_color_hex(pal->accent), LV_PART_CURSOR);
    lv_obj_set_style_bg_color(connect_button, lv_color_hex(pal->accent_soft), 0);
    lv_obj_set_style_bg_color(connect_button, lv_color_hex(pal->accent), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(connect_label, lv_color_hex(pal->text), 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(pal->text_muted), 0);
}

void select_network_cb(lv_event_t *event)
{
    network_item_t *item = static_cast<network_item_t *>(lv_event_get_user_data(event));
    if (item == nullptr) {
        return;
    }
    std::strncpy(selected_ssid, item->ssid, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';
    lv_textarea_set_text(password_ta, "");
    lv_textarea_set_placeholder_text(password_ta, selected_ssid);
    set_status("Rede selecionada", ui_theme_get()->text_muted);
    lv_group_focus_obj(password_ta);
    ui_keyboard_attach(password_ta);
}

void render_networks(void)
{
    lv_obj_clean(network_list);
    if (network_count == 0) {
        lv_obj_t *empty = lv_label_create(network_list);
        lv_label_set_text(empty, "Nenhuma rede encontrada");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14_latin1, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 12, 10);
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    for (int i = 0; i < network_count; ++i) {
        lv_obj_t *item = lv_obj_create(network_list);
        lv_obj_set_width(item, lv_pct(100));
        lv_obj_set_height(item, 40);
        lv_obj_set_style_bg_color(item, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_pad_left(item, 12, 0);
        lv_obj_set_style_pad_right(item, 12, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(item, select_network_cb, LV_EVENT_CLICKED, &networks[i]);

        lv_obj_t *label = lv_label_create(item);
        char text[64];
        std::snprintf(text, sizeof(text), "%s   %d dBm", networks[i].ssid, networks[i].rssi);
        lv_label_set_text(label, text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14_latin1, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(pal->text), 0);
        lv_obj_center(label);
    }
}

void scan_ui_cb(void *data)
{
    (void)data;
    render_networks();
    lv_label_set_text(scan_label, "Escanear");
    set_status(network_count > 0 ? "Redes encontradas" : "Nenhuma rede encontrada", ui_theme_get()->text_muted);
}

void scan_cb(const wifi_ap_record_t *aps, int count, void *ctx)
{
    (void)ctx;
    network_count = count > MAX_NETWORKS ? MAX_NETWORKS : count;
    for (int i = 0; i < network_count; ++i) {
        std::memset(&networks[i], 0, sizeof(networks[i]));
        std::strncpy(networks[i].ssid, reinterpret_cast<const char *>(aps[i].ssid), sizeof(networks[i].ssid) - 1);
        networks[i].rssi = aps[i].rssi;
        networks[i].channel = aps[i].primary;
        networks[i].authmode = aps[i].authmode;
    }
    lv_async_call(scan_ui_cb, nullptr);
}

void scan_cb_event(lv_event_t *event)
{
    (void)event;
    lv_label_set_text(scan_label, "Escaneando...");
    set_status("Procurando redes...", ui_theme_get()->text_muted);
    if (wifi_mgr_scan(scan_cb, nullptr) != ESP_OK) {
        lv_label_set_text(scan_label, "Escanear");
        set_status("Falha ao escanear", ui_theme_get()->accent);
    }
}

void connect_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    wifi_status_t status = {};
    if (wifi_mgr_get_status(&status) == ESP_OK && status.connected &&
        std::strncmp(status.ssid, selected_ssid, sizeof(selected_ssid)) == 0) {
        set_status("Conectado", ui_theme_get()->accent);
        lv_timer_pause(connect_timer);
        return;
    }
    if (++connect_ticks >= 30) {
        set_status("Falhou ao conectar", ui_theme_get()->accent);
        lv_timer_pause(connect_timer);
    }
}

void connect_cb(lv_event_t *event)
{
    (void)event;
    const char *password = lv_textarea_get_text(password_ta);
    if (selected_ssid[0] == '\0') {
        set_status("Selecione uma rede", ui_theme_get()->accent);
        return;
    }
    if (wifi_mgr_connect(selected_ssid, password) != ESP_OK) {
        set_status("Falhou ao conectar", ui_theme_get()->accent);
        return;
    }
    connect_ticks = 0;
    set_status("Conectando...", ui_theme_get()->text_muted);
    lv_timer_resume(connect_timer);
}

void close_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_close_wifi();
}

} // namespace

lv_obj_t *ui_wifi_create(void)
{
    wifi_scr = lv_obj_create(nullptr);
    wifi_bar = lv_obj_create(wifi_scr);
    lv_obj_set_size(wifi_bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_align(wifi_bar, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT);
    lv_obj_set_style_border_width(wifi_bar, 1, 0);
    lv_obj_set_style_border_side(wifi_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(wifi_bar, 0, 0);
    lv_obj_clear_flag(wifi_bar, LV_OBJ_FLAG_SCROLLABLE);

    wifi_title = lv_label_create(wifi_bar);
    lv_label_set_text(wifi_title, "WiFi");
    lv_obj_set_style_text_font(wifi_title, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(wifi_title, LV_ALIGN_LEFT_MID, 0, 0);

    wifi_close = lv_obj_create(wifi_bar);
    lv_obj_set_size(wifi_close, 36, 36);
    lv_obj_align(wifi_close, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(wifi_close, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(wifi_close, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(wifi_close, close_cb, LV_EVENT_CLICKED, nullptr);
    wifi_close_label = lv_label_create(wifi_close);
    lv_label_set_text(wifi_close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(wifi_close_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(wifi_close_label);

    scan_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(scan_button, 38);
    lv_obj_align(scan_button, LV_ALIGN_TOP_MID, 0, 2 * UI_BAR_HEIGHT + 12);
    lv_obj_set_style_radius(scan_button, 8, 0);
    lv_obj_clear_flag(scan_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scan_button, scan_cb_event, LV_EVENT_CLICKED, nullptr);
    scan_label = lv_label_create(scan_button);
    lv_label_set_text(scan_label, "Escanear");
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(scan_label);

    network_list = lv_obj_create(wifi_scr);
    lv_obj_align(network_list, LV_ALIGN_TOP_MID, 0, 2 * UI_BAR_HEIGHT + 58);
    lv_obj_set_style_radius(network_list, 8, 0);
    lv_obj_set_style_pad_all(network_list, 0, 0);
    lv_obj_set_scroll_dir(network_list, LV_DIR_VER);

    password_ta = lv_textarea_create(wifi_scr);
    lv_obj_set_height(password_ta, 38);
    lv_obj_align(password_ta, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_textarea_set_placeholder_text(password_ta, "Senha da rede selecionada");
    lv_textarea_set_password_mode(password_ta, true);
    lv_obj_set_style_text_font(password_ta, &lv_font_montserrat_14_latin1, LV_PART_MAIN);
    lv_obj_add_event_cb(
        password_ta,
        [](lv_event_t *event) {
            (void)event;
            lv_group_focus_obj(password_ta);
            ui_keyboard_attach(password_ta);
        },
        LV_EVENT_CLICKED, nullptr);

    connect_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(connect_button, 38);
    lv_obj_align(connect_button, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_radius(connect_button, 8, 0);
    lv_obj_clear_flag(connect_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(connect_button, connect_cb, LV_EVENT_CLICKED, nullptr);
    connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, "Conectar");
    lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(connect_label);

    status_label = lv_label_create(wifi_scr);
    lv_label_set_text(status_label, "Selecione uma rede para conectar");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 12, -2);

    wifi_group = lv_group_create();
    lv_group_add_obj(wifi_group, password_ta);
    apply_wifi_layout();
    lv_display_add_event_cb(lv_display_get_default(), resolution_cb, LV_EVENT_RESOLUTION_CHANGED, nullptr);
    apply_wifi_theme();
    connect_timer = lv_timer_create(connect_timer_cb, 500, nullptr);
    lv_timer_pause(connect_timer);
    render_networks();
    return wifi_scr;
}

void ui_wifi_refresh_theme(void)
{
    apply_wifi_theme();
    render_networks();
}
