#include "ui_bluetooth.h"
#include "app_registry.h"
#include "ui_bar.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#include "ui_keyboard.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "bt_mgr.h"
#include "bt_storage.h"
#include "bsp/esp-bsp.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {

constexpr int MAX_DEVICES = BT_SCAN_MAX_DEVICES;

lv_obj_t *bt_scr = nullptr;
ui_app_bar_t bt_app_bar = {};
lv_obj_t *scan_button = nullptr;
lv_obj_t *scan_label = nullptr;
lv_obj_t *device_list = nullptr;
lv_obj_t *connect_button = nullptr;
lv_obj_t *connect_label = nullptr;
lv_obj_t *disconnect_button = nullptr;
lv_obj_t *disconnect_label = nullptr;
lv_obj_t *forget_button = nullptr;
lv_obj_t *forget_label = nullptr;
lv_obj_t *status_label = nullptr;

bt_device_info_t s_devices[MAX_DEVICES] = {};
int s_device_count = 0;
char s_selected_mac[18] = {};
char s_selected_name[64] = {};
bt_dev_type_t s_selected_type = BT_DEV_TYPE_GENERIC;

/* Estado da tentativa de conexao em curso (reflete o bt_mgr via callback) */
bool s_connecting = false;
char s_connecting_mac[18] = {};

/* Caixa de correio para eventos do NimBLE -> task LVGL */
struct conn_evt_msg_t {
    char mac[18];
    bt_conn_event_t event;
    int reason;
};
#define CONN_EVT_QUEUE_LEN 4
conn_evt_msg_t s_conn_evt_queue[CONN_EVT_QUEUE_LEN] = {};
volatile int s_conn_evt_wr = 0;
volatile int s_conn_evt_rd = 0;

void render_devices(void);
void apply_bt_layout(void);

void set_status(const char *text, uint32_t color)
{
    if (status_label == nullptr) {
        return;
    }
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
}

const char *get_device_icon(bt_dev_type_t type, const char *name)
{
    /* O Lift pode ter sido salvo como teclado por versões anteriores, quando
     * o anúncio não informava Appearance; mantém o ícone correto sem exigir
     * novo pareamento. */
    if (name != nullptr && strcasestr(name, "lift") != nullptr) {
        return LV_SYMBOL_BLUETOOTH;
    }

    switch (type) {
    case BT_DEV_TYPE_KEYBOARD:
        return LV_SYMBOL_KEYBOARD;
    case BT_DEV_TYPE_HEADPHONE:
        return LV_SYMBOL_AUDIO;
    case BT_DEV_TYPE_MOUSE:
    case BT_DEV_TYPE_GENERIC:
    default:
        return LV_SYMBOL_BLUETOOTH;
    }
}

void update_action_buttons_state(void)
{
    if (bt_scr == nullptr) {
        return;
    }

    bool is_selected = s_selected_mac[0] != '\0';
    bool is_connected_curr = false;
    bool is_saved = false;

    if (is_selected) {
        for (int i = 0; i < s_device_count; i++) {
            if (strcmp(s_devices[i].mac, s_selected_mac) == 0) {
                is_connected_curr = s_devices[i].connected;
                is_saved = s_devices[i].paired;
                break;
            }
        }
        if (!is_saved) {
            bt_saved_device_t saved = {};
            is_saved = bt_storage_find(s_selected_mac, &saved);
        }
    }

    if (disconnect_button != nullptr) {
        if (is_selected && is_connected_curr) {
            lv_obj_remove_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (forget_button != nullptr) {
        if (is_selected && is_saved) {
            lv_obj_remove_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (connect_button != nullptr) {
        if (is_selected && !is_connected_curr) {
            lv_obj_remove_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

    apply_bt_layout();
}

void apply_bt_layout(void)
{
    if (bt_scr == nullptr) {
        return;
    }

    int32_t width = lv_display_get_horizontal_resolution(nullptr);
    int32_t height = lv_display_get_vertical_resolution(nullptr);
    int32_t side = width < 560 ? 12 : 24;
    int32_t usable_w = width - 2 * side;

    /* Linha 1: Botão Buscar */
    if (scan_button != nullptr) {
        lv_obj_set_width(scan_button, usable_w);
        lv_obj_set_x(scan_button, side);
        lv_obj_set_y(scan_button, 2 * UI_BAR_HEIGHT + 8);
    }

    /* Linha 2: Ações de Conexão */
    int32_t act_y = 2 * UI_BAR_HEIGHT + 60;
    int32_t btn_h = 44;
    int32_t btn_gap = 8;
    int32_t num_visible_btns = 0;

    if (connect_button != nullptr && !lv_obj_has_flag(connect_button, LV_OBJ_FLAG_HIDDEN)) {
        num_visible_btns++;
    }
    if (disconnect_button != nullptr && !lv_obj_has_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN)) {
        num_visible_btns++;
    }
    if (forget_button != nullptr && !lv_obj_has_flag(forget_button, LV_OBJ_FLAG_HIDDEN)) {
        num_visible_btns++;
    }

    int32_t btn_w = num_visible_btns > 0 ? (usable_w - (num_visible_btns - 1) * btn_gap) / num_visible_btns : 0;
    int32_t cur_x = side;

    if (connect_button != nullptr && !lv_obj_has_flag(connect_button, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_size(connect_button, btn_w, btn_h);
        lv_obj_set_pos(connect_button, cur_x, act_y);
        cur_x += btn_w + btn_gap;
    }
    if (disconnect_button != nullptr && !lv_obj_has_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_size(disconnect_button, btn_w, btn_h);
        lv_obj_set_pos(disconnect_button, cur_x, act_y);
        cur_x += btn_w + btn_gap;
    }
    if (forget_button != nullptr && !lv_obj_has_flag(forget_button, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_size(forget_button, btn_w, btn_h);
        lv_obj_set_pos(forget_button, cur_x, act_y);
    }

    int32_t list_top = num_visible_btns > 0 ? act_y + btn_h + 10 : act_y;

    if (status_label != nullptr) {
        lv_obj_set_x(status_label, side);
        lv_obj_set_y(status_label, list_top);
    }

    int32_t list_y = list_top + 28;
    int32_t list_h = height - list_y - 12;
    if (list_h < 80) {
        list_h = 80;
    }

    if (device_list != nullptr) {
        lv_obj_set_size(device_list, usable_w, list_h);
        lv_obj_set_pos(device_list, side, list_y);
    }
}

void apply_bt_theme(void)
{
    if (bt_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(bt_scr, lv_color_hex(pal->background), 0);

    ui_app_bar_refresh_theme(&bt_app_bar);

    if (scan_button != nullptr) {
        lv_obj_set_style_bg_color(scan_button, lv_color_hex(pal->accent_soft), 0);
        lv_obj_set_style_border_color(scan_button, lv_color_hex(pal->border), 0);
    }

    if (scan_label != nullptr) {
        lv_obj_set_style_text_color(scan_label, lv_color_hex(pal->accent), 0);
    }

    if (connect_button != nullptr) {
        lv_obj_set_style_bg_color(connect_button, lv_color_hex(pal->accent), 0);
    }

    if (connect_label != nullptr) {
        lv_obj_set_style_text_color(connect_label, lv_color_hex(pal->surface), 0);
    }

    if (disconnect_button != nullptr) {
        lv_obj_set_style_bg_color(disconnect_button, lv_color_hex(pal->accent_soft), 0);
        lv_obj_set_style_border_color(disconnect_button, lv_color_hex(pal->border), 0);
    }

    if (disconnect_label != nullptr) {
        lv_obj_set_style_text_color(disconnect_label, lv_color_hex(pal->accent), 0);
    }

    if (forget_button != nullptr) {
        lv_obj_set_style_bg_color(forget_button, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(forget_button, lv_color_hex(pal->border), 0);
    }

    if (forget_label != nullptr) {
        lv_obj_set_style_text_color(forget_label, lv_color_hex(pal->text_muted), 0);
    }

    if (device_list != nullptr) {
        lv_obj_set_style_bg_color(device_list, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(device_list, lv_color_hex(pal->border), 0);
    }

    render_devices();
}

void async_render_cb(void *arg)
{
    (void)arg;
    render_devices();
    update_action_buttons_state();
}

void poll_bt_status_cb(lv_timer_t *timer)
{
    (void)timer;
    bt_status_t st = {};
    if (bt_mgr_get_status(&st) == ESP_OK) {
        bool changed = false;
        for (int i = 0; i < s_device_count; i++) {
            bool is_conn = (st.any_connected && strcasecmp(st.last_connected_mac, s_devices[i].mac) == 0);
            if (s_devices[i].connected != is_conn) {
                s_devices[i].connected = is_conn;
                changed = true;
            }
        }
        if (changed) {
            render_devices();
            update_action_buttons_state();
            ui_keyboard_notify_hardware_change();
        }
    }
}

void handle_conn_event_ui(const conn_evt_msg_t *evt)
{
    const ui_palette_t *pal = ui_theme_get();
    const char *name = evt->mac;
    for (int i = 0; i < s_device_count; i++) {
        if (strcasecmp(s_devices[i].mac, evt->mac) == 0 && s_devices[i].name[0] != '\0') {
            name = s_devices[i].name;
            break;
        }
    }

    bool touches_selection =
        (strcasecmp(evt->mac, s_selected_mac) == 0) || (strcasecmp(evt->mac, s_connecting_mac) == 0);

    char msg[96];
    switch (evt->event) {
    case BT_CONN_STARTED:
        if (strcasecmp(evt->mac, s_selected_mac) == 0) {
            s_connecting = true;
            snprintf(s_connecting_mac, sizeof(s_connecting_mac), "%s", evt->mac);
            snprintf(msg, sizeof(msg), "Conectando a %s...", name[0] != '\0' ? name : evt->mac);
            set_status(msg, pal->accent);
            if (connect_label != nullptr) {
                lv_label_set_text(connect_label, "Conectando...");
            }
        } else {
            ESP_LOGI("ui_bt", "Conexao automatica iniciada para %s", evt->mac);
        }
        break;
    case BT_CONN_CONNECTED:
        snprintf(msg, sizeof(msg), "%s conectado", name[0] != '\0' ? name : evt->mac);
        set_status(msg, pal->accent);
        break;
    case BT_CONN_READY:
        if (evt->mac[0] != '\0') {
            snprintf(msg, sizeof(msg), "%s pronto (entrada HID ativa)", name[0] != '\0' ? name : evt->mac);
            set_status(msg, pal->accent);
        }
        break;
    case BT_CONN_FAILED:
        if (evt->reason == 13 || evt->reason == 62) {
            snprintf(msg, sizeof(msg), "Tempo esgotado ao conectar em %s", name[0] != '\0' ? name : evt->mac);
        } else {
            snprintf(msg, sizeof(msg), "Falha na conexao (%d)", evt->reason);
        }
        set_status(msg, pal->text_muted);
        break;
    case BT_CONN_DISCONNECTED:
        if (evt->mac[0] != '\0') {
            snprintf(msg, sizeof(msg), "%s desconectado", name[0] != '\0' ? name : evt->mac);
            set_status(msg, pal->text_muted);
        }
        break;
    default:
        break;
    }

    if (touches_selection || evt->event == BT_CONN_FAILED || evt->event == BT_CONN_DISCONNECTED) {
        if (evt->event == BT_CONN_CONNECTED || evt->event == BT_CONN_READY || evt->event == BT_CONN_FAILED ||
            evt->event == BT_CONN_DISCONNECTED) {
            s_connecting = false;
            s_connecting_mac[0] = '\0';
            if (connect_label != nullptr) {
                lv_label_set_text(connect_label, LV_SYMBOL_OK " Conectar");
            }
        }
    }

    render_devices();
    update_action_buttons_state();
}

void process_conn_events(void *arg)
{
    (void)arg;
    while (s_conn_evt_rd != s_conn_evt_wr) {
        conn_evt_msg_t evt = s_conn_evt_queue[s_conn_evt_rd];
        s_conn_evt_rd = (s_conn_evt_rd + 1) % CONN_EVT_QUEUE_LEN;
        handle_conn_event_ui(&evt);
    }
}

/* Roda na task do NimBLE: enfileira e marshalla para a task de UI. */
void bt_mgr_conn_event_cb(const char *mac, bt_conn_event_t event, int reason, void *ctx)
{
    (void)ctx;
    int next = (s_conn_evt_wr + 1) % CONN_EVT_QUEUE_LEN;
    if (next == s_conn_evt_rd) {
        return; /* fila cheia: o poll periodico ressincroniza a UI */
    }

    conn_evt_msg_t *msg = &s_conn_evt_queue[s_conn_evt_wr];
    snprintf(msg->mac, sizeof(msg->mac), "%s", mac ? mac : "");
    msg->event = event;
    msg->reason = reason;
    s_conn_evt_wr = next;

    if (bsp_display_lock(pdMS_TO_TICKS(200))) {
        lv_async_call(process_conn_events, nullptr);
        bsp_display_unlock();
    }
}

void device_item_click_cb(lv_event_t *event)
{
    const char *mac = (const char *)lv_event_get_user_data(event);
    if (mac == nullptr) {
        return;
    }

    snprintf(s_selected_mac, sizeof(s_selected_mac), "%s", mac);

    for (int i = 0; i < s_device_count; i++) {
        if (strcmp(s_devices[i].mac, mac) == 0) {
            snprintf(s_selected_name, sizeof(s_selected_name), "%s", s_devices[i].name);
            s_selected_type = s_devices[i].type;
            break;
        }
    }

    ESP_LOGI("ui_bt", "Linha do dispositivo clicada! mac=%s name=\"%s\"", s_selected_mac, s_selected_name);

    const ui_palette_t *pal = ui_theme_get();

    if (s_connecting) {
        set_status("Aguarde: já existe uma conexão em andamento", pal->text_muted);
        return;
    }

    char status_msg[96];
    snprintf(status_msg, sizeof(status_msg), "Conectando a %s...", s_selected_name[0] ? s_selected_name : mac);
    set_status(status_msg, pal->accent);

    esp_err_t err = bt_mgr_connect(s_selected_mac, s_selected_name, s_selected_type);
    if (err != ESP_OK) {
        ESP_LOGE("ui_bt", "bt_mgr_connect retornou erro: %d", err);
        set_status("Não foi possível iniciar a conexão", pal->text_muted);
    } else {
        s_connecting = true;
        snprintf(s_connecting_mac, sizeof(s_connecting_mac), "%s", s_selected_mac);
        if (connect_label != nullptr) {
            lv_label_set_text(connect_label, "Conectando...");
        }
    }

    lv_async_call(async_render_cb, nullptr);
}

void render_devices(void)
{
    if (device_list == nullptr) {
        return;
    }

    lv_obj_clean(device_list);

    const ui_palette_t *pal = ui_theme_get();

    if (!bt_mgr_is_enabled()) {
        lv_obj_t *empty_lbl = lv_label_create(device_list);
        lv_label_set_text(empty_lbl, "Bluetooth desativado nas configurações");
        lv_obj_set_style_text_font(empty_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(empty_lbl, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(empty_lbl);
        return;
    }

    if (s_device_count == 0) {
        lv_obj_t *empty_lbl = lv_label_create(device_list);
        lv_label_set_text(empty_lbl, "Nenhum dispositivo Bluetooth encontrado.\nToque em Buscar.");
        lv_obj_set_style_text_font(empty_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(empty_lbl, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(empty_lbl);
        return;
    }

    for (int i = 0; i < s_device_count; i++) {
        const bt_device_info_t *dev = &s_devices[i];
        bool is_sel = (s_selected_mac[0] != '\0' && strcmp(s_selected_mac, dev->mac) == 0);

        lv_obj_t *row = lv_obj_create(device_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 62);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_left(row, 14, 0);
        lv_obj_set_style_pad_right(row, 14, 0);
        lv_obj_set_style_pad_top(row, 4, 0);
        lv_obj_set_style_pad_bottom(row, 4, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        if (is_sel) {
            lv_obj_set_style_bg_color(row, lv_color_hex(pal->accent_soft), 0);
            lv_obj_set_style_border_color(row, lv_color_hex(pal->accent), 0);
        } else {
            lv_obj_set_style_bg_color(row, lv_color_hex(pal->surface), 0);
            lv_obj_set_style_border_color(row, lv_color_hex(pal->border), 0);
        }

        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, device_item_click_cb, LV_EVENT_CLICKED, (void *)dev->mac);

        /* Ícone do dispositivo */
        lv_obj_t *icon_lbl = lv_label_create(row);
        lv_label_set_text(icon_lbl, get_device_icon(dev->type, dev->name));
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(icon_lbl, lv_color_hex(dev->connected ? pal->accent : pal->text), 0);
        lv_obj_clear_flag(icon_lbl, LV_OBJ_FLAG_CLICKABLE);

        /* Coluna com Nome e MAC */
        lv_obj_t *col = lv_obj_create(row);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *name_lbl = lv_label_create(col);
        lv_label_set_text(name_lbl, dev->name[0] != '\0' ? dev->name : "Dispositivo sem nome");
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(pal->text), 0);
        lv_obj_clear_flag(name_lbl, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *mac_lbl = lv_label_create(col);
        lv_label_set_text(mac_lbl, dev->mac);
        lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(mac_lbl, lv_color_hex(pal->text_muted), 0);
        lv_obj_clear_flag(mac_lbl, LV_OBJ_FLAG_CLICKABLE);

        /* Status Badge à direita */
        if (dev->connected) {
            lv_obj_t *badge = lv_label_create(row);
            lv_label_set_text(badge, "(Conectado)");
            lv_obj_set_style_text_font(badge, &lv_font_montserrat_18_latin1, 0);
            lv_obj_set_style_text_color(badge, lv_color_hex(pal->accent), 0);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        } else if (dev->paired) {
            lv_obj_t *badge = lv_label_create(row);
            lv_label_set_text(badge, "(Pareado)");
            lv_obj_set_style_text_font(badge, &lv_font_montserrat_18_latin1, 0);
            lv_obj_set_style_text_color(badge, lv_color_hex(pal->text_muted), 0);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

void scan_ui_cb(void *user_data)
{
    (void)user_data;
    const ui_palette_t *pal = ui_theme_get();

    if (scan_label != nullptr) {
        lv_label_set_text(scan_label, LV_SYMBOL_REFRESH "  Buscar Dispositivos");
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%d dispositivos encontrados", s_device_count);
    set_status(buf, pal->text_muted);

    render_devices();
    update_action_buttons_state();
}

void scan_complete_cb(const bt_device_info_t *devices, int count, void *ctx)
{
    (void)ctx;
    s_device_count = count > MAX_DEVICES ? MAX_DEVICES : count;
    for (int i = 0; i < s_device_count; i++) {
        s_devices[i] = devices[i];
    }

    if (bsp_display_lock(pdMS_TO_TICKS(500))) {
        lv_async_call(scan_ui_cb, nullptr);
        bsp_display_unlock();
    }
}

void scan_button_cb(lv_event_t *event)
{
    (void)event;
    const ui_palette_t *pal = ui_theme_get();
    if (!bt_mgr_is_enabled()) {
        set_status("Bluetooth desativado nas configurações", pal->accent);
        return;
    }
    if (scan_label != nullptr) {
        lv_label_set_text(scan_label, "Buscando...");
    }
    set_status("Buscando dispositivos Bluetooth...", pal->accent);
    esp_err_t err = bt_mgr_scan(scan_complete_cb, nullptr);
    if (err != ESP_OK) {
        if (scan_label != nullptr) {
            lv_label_set_text(scan_label, LV_SYMBOL_REFRESH "  Buscar Dispositivos");
        }
        set_status("Busca em andamento...", pal->text_muted);
    }
}

void connect_button_cb(lv_event_t *event)
{
    (void)event;
    const ui_palette_t *pal = ui_theme_get();
    if (!bt_mgr_is_enabled()) {
        set_status("Bluetooth desativado nas configurações", pal->accent);
        return;
    }
    if (s_selected_mac[0] == '\0') {
        return;
    }
    if (s_connecting) {
        set_status("Aguarde: já existe uma conexão em andamento", pal->text_muted);
        return;
    }

    set_status("Conectando...", pal->accent);

    esp_err_t err = bt_mgr_connect(s_selected_mac, s_selected_name, s_selected_type);
    if (err != ESP_OK) {
        set_status("Não foi possível iniciar a conexão", pal->text_muted);
    } else {
        s_connecting = true;
        snprintf(s_connecting_mac, sizeof(s_connecting_mac), "%s", s_selected_mac);
        if (connect_label != nullptr) {
            lv_label_set_text(connect_label, "Conectando...");
        }
    }

    render_devices();
    update_action_buttons_state();
}

void disconnect_button_cb(lv_event_t *event)
{
    (void)event;
    if (s_selected_mac[0] == '\0') {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    bt_mgr_disconnect(s_selected_mac);
    ui_keyboard_notify_hardware_change();

    s_connecting = false;
    s_connecting_mac[0] = '\0';
    if (connect_label != nullptr) {
        lv_label_set_text(connect_label, LV_SYMBOL_OK " Conectar");
    }

    for (int i = 0; i < s_device_count; i++) {
        if (strcmp(s_devices[i].mac, s_selected_mac) == 0) {
            s_devices[i].connected = false;
            break;
        }
    }

    set_status("Desconectado", pal->text_muted);
    render_devices();
    update_action_buttons_state();
}

void forget_button_cb(lv_event_t *event)
{
    (void)event;
    if (s_selected_mac[0] == '\0') {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    bt_mgr_forget(s_selected_mac);
    ui_keyboard_notify_hardware_change();

    s_connecting = false;
    s_connecting_mac[0] = '\0';
    if (connect_label != nullptr) {
        lv_label_set_text(connect_label, LV_SYMBOL_OK " Conectar");
    }

    for (int i = 0; i < s_device_count; i++) {
        if (strcmp(s_devices[i].mac, s_selected_mac) == 0) {
            s_devices[i].connected = false;
            s_devices[i].paired = false;
            break;
        }
    }

    s_selected_mac[0] = '\0';
    s_selected_name[0] = '\0';
    set_status("Dispositivo esquecido", pal->text_muted);
    render_devices();
    update_action_buttons_state();
}

void close_button_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_close_bluetooth();
}

void load_saved_devices(void)
{
    bt_saved_list_t saved_list = {};
    if (bt_storage_load_all(&saved_list) == ESP_OK) {
        s_device_count = 0;
        for (int i = 0; i < saved_list.count && s_device_count < MAX_DEVICES; i++) {
            bt_device_info_t *dev = &s_devices[s_device_count++];
            snprintf(dev->mac, sizeof(dev->mac), "%s", saved_list.items[i].mac);
            snprintf(dev->name, sizeof(dev->name), "%s", saved_list.items[i].name);
            dev->type = saved_list.items[i].type;
            dev->rssi = -55;
            dev->paired = saved_list.items[i].paired;
            dev->connected = false;
        }
    }
}

} // namespace

lv_obj_t *ui_bluetooth_create(void)
{
    bt_scr = lv_obj_create(nullptr);
    lv_obj_set_size(bt_scr, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(bt_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Barra padronizada do app */
    bt_app_bar = ui_app_bar_create(bt_scr, "Bluetooth", close_button_cb, nullptr);

    /* Botão Buscar */
    scan_button = lv_obj_create(bt_scr);
    lv_obj_set_height(scan_button, 44);
    lv_obj_set_style_radius(scan_button, 8, 0);
    lv_obj_set_style_border_width(scan_button, 1, 0);
    lv_obj_set_style_pad_all(scan_button, 0, 0);
    lv_obj_clear_flag(scan_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scan_button, scan_button_cb, LV_EVENT_CLICKED, nullptr);

    scan_label = lv_label_create(scan_button);
    lv_label_set_text(scan_label, LV_SYMBOL_REFRESH "  Buscar Dispositivos");
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_clear_flag(scan_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(scan_label);

    /* Botões de Ação */
    connect_button = lv_obj_create(bt_scr);
    lv_obj_set_style_radius(connect_button, 8, 0);
    lv_obj_set_style_border_width(connect_button, 0, 0);
    lv_obj_set_style_pad_all(connect_button, 0, 0);
    lv_obj_clear_flag(connect_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(connect_button, connect_button_cb, LV_EVENT_CLICKED, nullptr);

    connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, LV_SYMBOL_OK " Conectar");
    lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(connect_label);

    disconnect_button = lv_obj_create(bt_scr);
    lv_obj_set_style_radius(disconnect_button, 8, 0);
    lv_obj_set_style_border_width(disconnect_button, 1, 0);
    lv_obj_set_style_pad_all(disconnect_button, 0, 0);
    lv_obj_clear_flag(disconnect_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(disconnect_button, disconnect_button_cb, LV_EVENT_CLICKED, nullptr);

    disconnect_label = lv_label_create(disconnect_button);
    lv_label_set_text(disconnect_label, LV_SYMBOL_CLOSE " Desconectar");
    lv_obj_set_style_text_font(disconnect_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(disconnect_label);

    forget_button = lv_obj_create(bt_scr);
    lv_obj_set_style_radius(forget_button, 8, 0);
    lv_obj_set_style_border_width(forget_button, 1, 0);
    lv_obj_set_style_pad_all(forget_button, 0, 0);
    lv_obj_clear_flag(forget_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(forget_button, forget_button_cb, LV_EVENT_CLICKED, nullptr);

    forget_label = lv_label_create(forget_button);
    lv_label_set_text(forget_label, LV_SYMBOL_TRASH " Esquecer");
    lv_obj_set_style_text_font(forget_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(forget_label);

    /* Label de status */
    status_label = lv_label_create(bt_scr);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18_latin1, 0);

    /* Lista de dispositivos */
    device_list = lv_obj_create(bt_scr);
    lv_obj_set_style_radius(device_list, 10, 0);
    lv_obj_set_style_border_width(device_list, 1, 0);
    lv_obj_set_style_pad_all(device_list, 8, 0);
    lv_obj_set_flex_flow(device_list, LV_FLEX_FLOW_COLUMN);

    load_saved_devices();
    apply_bt_theme();
    apply_bt_layout();

    bt_mgr_set_conn_callback(bt_mgr_conn_event_cb, nullptr);
    lv_timer_create(poll_bt_status_cb, 500, nullptr);

    return bt_scr;
}

void ui_bluetooth_refresh_theme(void)
{
    apply_bt_theme();
}

void ui_bluetooth_apply_layout(void)
{
    apply_bt_layout();
}

void ui_bluetooth_register(void)
{
    static const app_desc_t s_bluetooth_desc = {
        .id = "bluetooth",
        .name = "Bluetooth",
        .icon_symbol = LV_SYMBOL_BLUETOOTH,
        .icon_bg_color = nullptr,
        .icon_builder = nullptr,
        .icon_theme_refresh = nullptr,
        .on_launch = ui_shell_open_bluetooth,
        .file_extensions = nullptr,
        .on_open_file = nullptr,
    };
    app_registry_register(&s_bluetooth_desc);
}
