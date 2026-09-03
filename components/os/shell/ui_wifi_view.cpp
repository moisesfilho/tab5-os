#include "ui_wifi_view.h"
#include "app_registry.h"
#include "ui_bar.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#include "ui_keyboard.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "wifi_mgr.h"
#include "wifi_storage.h"
#include "bsp/esp-bsp.h"

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
ui_app_bar_t wifi_app_bar = {};
lv_obj_t *scan_button = nullptr;
lv_obj_t *scan_label = nullptr;
lv_obj_t *network_list = nullptr;
lv_obj_t *password_ta = nullptr;
lv_obj_t *show_pwd_btn = nullptr;
lv_obj_t *show_pwd_label = nullptr;
lv_obj_t *connect_button = nullptr;
lv_obj_t *connect_label = nullptr;
lv_obj_t *disconnect_button = nullptr;
lv_obj_t *disconnect_label = nullptr;
lv_obj_t *forget_button = nullptr;
lv_obj_t *forget_label = nullptr;
lv_obj_t *status_label = nullptr;
lv_group_t *wifi_group = nullptr;
lv_timer_t *connect_timer = nullptr;
lv_timer_t *password_cursor_timer = nullptr;

network_item_t networks[MAX_NETWORKS] = {};
int network_count = 0;
char selected_ssid[33] = {};
uint32_t connect_ticks = 0;
bool show_password = false;

void render_networks(void);
void apply_wifi_layout(void);

void cursor_blink_cb(lv_timer_t *timer)
{
    lv_obj_t *ta = (lv_obj_t *)lv_timer_get_user_data(timer);
    if (ta == nullptr) {
        return;
    }
    lv_opa_t opa = lv_obj_get_style_bg_opa(ta, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, opa == LV_OPA_TRANSP ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_CURSOR);
}

void show_pwd_event_cb(lv_event_t *event)
{
    (void)event;
    show_password = !show_password;
    if (password_ta != nullptr) {
        lv_textarea_set_password_mode(password_ta, !show_password);
    }
    if (show_pwd_label != nullptr) {
        lv_label_set_text(show_pwd_label, show_password ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    }
}

void set_status(const char *text, uint32_t color)
{
    if (status_label == nullptr) {
        return;
    }
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
}

void update_action_buttons_state(void)
{
    if (wifi_scr == nullptr) {
        return;
    }

    wifi_status_t status = {};
    wifi_mgr_get_status(&status);

    bool is_selected = selected_ssid[0] != '\0';
    bool is_connected_curr = status.connected && is_selected && (strcmp(status.ssid, selected_ssid) == 0);
    bool is_saved = is_selected && wifi_storage_find(selected_ssid, nullptr, 0);

    /* Botao Desconectar so aparece se a rede selecionada estiver conectada */
    if (disconnect_button != nullptr) {
        if (is_connected_curr) {
            lv_obj_remove_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Botao Esquecer aparece se a rede estiver salva */
    if (forget_button != nullptr) {
        if (is_saved) {
            lv_obj_remove_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Botao Conectar fica visivel se nao estiver ja conectada */
    if (connect_button != nullptr) {
        if (is_connected_curr) {
            lv_obj_add_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

    apply_wifi_layout();
}

void apply_wifi_layout(void)
{
    if (wifi_scr == nullptr) {
        return;
    }

    int32_t width = lv_display_get_horizontal_resolution(nullptr);
    int32_t height = lv_display_get_vertical_resolution(nullptr);
    int32_t side = width < 560 ? 12 : 24;
    int32_t usable_w = width - 2 * side;

    /* Posicionamento em relacao ao topo da tela (2 * UI_BAR_HEIGHT = 80) */
    /* Linha 1: Botao Escanear (altura 38) */
    if (scan_button != nullptr) {
        lv_obj_set_width(scan_button, usable_w);
        lv_obj_set_x(scan_button, side);
        lv_obj_set_y(scan_button, 2 * UI_BAR_HEIGHT + 8);
    }

    /* Linha 2: Campo de Senha + Eye + Botoes de Acao (Conectar, Desconectar, Esquecer) */
    wifi_status_t status = {};
    wifi_mgr_get_status(&status);
    bool is_selected = selected_ssid[0] != '\0';
    bool is_connected_curr = status.connected && is_selected && (strcmp(status.ssid, selected_ssid) == 0);
    bool is_saved = is_selected && wifi_storage_find(selected_ssid, nullptr, 0);

    int32_t btn_w = 125;
    int32_t eye_w = 44;
    int32_t gap = 6;

    int32_t extra_btns_w = btn_w + gap; /* Conectar ou Desconectar */
    if (is_saved) {
        extra_btns_w += btn_w + gap; /* Esquecer */
    }

    int32_t ta_w = usable_w - eye_w - gap - extra_btns_w;
    if (ta_w < 120) {
        ta_w = 120;
    }

    int32_t cur_x = side;
    if (password_ta != nullptr) {
        lv_obj_set_width(password_ta, ta_w);
        lv_obj_set_x(password_ta, cur_x);
        lv_obj_set_y(password_ta, (2 * UI_BAR_HEIGHT) + 60);
        cur_x += ta_w + gap;
    }
    if (show_pwd_btn != nullptr) {
        lv_obj_set_width(show_pwd_btn, eye_w);
        lv_obj_set_x(show_pwd_btn, cur_x);
        lv_obj_set_y(show_pwd_btn, (2 * UI_BAR_HEIGHT) + 60);
        cur_x += eye_w + gap;
    }

    if (connect_button != nullptr && !is_connected_curr) {
        lv_obj_set_width(connect_button, btn_w);
        lv_obj_set_x(connect_button, cur_x);
        lv_obj_set_y(connect_button, (2 * UI_BAR_HEIGHT) + 60);
        cur_x += btn_w + gap;
    }

    if (disconnect_button != nullptr && is_connected_curr) {
        lv_obj_set_width(disconnect_button, btn_w);
        lv_obj_set_x(disconnect_button, cur_x);
        lv_obj_set_y(disconnect_button, (2 * UI_BAR_HEIGHT) + 60);
        cur_x += btn_w + gap;
    }

    if (forget_button != nullptr && is_saved) {
        lv_obj_set_width(forget_button, btn_w);
        lv_obj_set_x(forget_button, cur_x);
        lv_obj_set_y(forget_button, (2 * UI_BAR_HEIGHT) + 60);
    }

    /* Linha 3: Status da conexao / selecao */
    if (status_label != nullptr) {
        lv_obj_set_x(status_label, side);
        lv_obj_set_y(status_label, (2 * UI_BAR_HEIGHT) + 112);
    }

    /* Linha 4 em diante: Lista de redes ocupando o espaco restante */
    if (network_list != nullptr) {
        int32_t list_top = (2 * UI_BAR_HEIGHT) + 140;
        int32_t kb_h = ui_keyboard_is_visible() ? ui_keyboard_get_height() : 0;
        int32_t list_h = height - list_top - kb_h - 12;
        if (list_h < 70) {
            list_h = 70;
        }
        lv_obj_set_width(network_list, usable_w);
        lv_obj_set_height(network_list, list_h);
        lv_obj_set_x(network_list, side);
        lv_obj_set_y(network_list, list_top);
    }
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

    ui_app_bar_refresh_theme(&wifi_app_bar);

    if (scan_button != nullptr) {
        lv_obj_set_style_bg_color(scan_button, lv_color_hex(pal->accent_soft), 0);
        lv_obj_set_style_bg_color(scan_button, lv_color_hex(pal->accent), LV_STATE_PRESSED);
    }
    if (scan_label != nullptr) {
        lv_obj_set_style_text_color(scan_label, lv_color_hex(pal->text), 0);
    }
    if (network_list != nullptr) {
        lv_obj_set_style_bg_color(network_list, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(network_list, lv_color_hex(pal->border), 0);
    }
    if (password_ta != nullptr) {
        lv_obj_set_style_bg_color(password_ta, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_text_color(password_ta, lv_color_hex(pal->text), 0);
        lv_obj_set_style_text_color(password_ta, lv_color_hex(pal->text_muted), LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_bg_color(password_ta, lv_color_hex(pal->accent), LV_PART_CURSOR);
    }
    if (show_pwd_btn != nullptr) {
        lv_obj_set_style_bg_color(show_pwd_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(show_pwd_btn, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(show_pwd_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (show_pwd_label != nullptr) {
        lv_obj_set_style_text_color(show_pwd_label, lv_color_hex(pal->text), 0);
    }
    if (connect_button != nullptr) {
        lv_obj_set_style_bg_color(connect_button, lv_color_hex(pal->accent_soft), 0);
        lv_obj_set_style_bg_color(connect_button, lv_color_hex(pal->accent), LV_STATE_PRESSED);
    }
    if (connect_label != nullptr) {
        lv_obj_set_style_text_color(connect_label, lv_color_hex(pal->text), 0);
    }
    if (disconnect_button != nullptr) {
        lv_obj_set_style_bg_color(disconnect_button, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(disconnect_button, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(disconnect_button, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (disconnect_label != nullptr) {
        lv_obj_set_style_text_color(disconnect_label, lv_color_hex(pal->text), 0);
    }
    if (forget_button != nullptr) {
        lv_obj_set_style_bg_color(forget_button, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(forget_button, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(forget_button, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (forget_label != nullptr) {
        lv_obj_set_style_text_color(forget_label, lv_color_hex(pal->text), 0);
    }
    if (status_label != nullptr) {
        lv_obj_set_style_text_color(status_label, lv_color_hex(pal->text_muted), 0);
    }
}

void select_network_cb(lv_event_t *event)
{
    network_item_t *item = static_cast<network_item_t *>(lv_event_get_user_data(event));
    if (item == nullptr) {
        return;
    }
    std::strncpy(selected_ssid, item->ssid, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';

    char saved_pwd[65] = "";
    bool is_saved = wifi_storage_find(selected_ssid, saved_pwd, sizeof(saved_pwd));
    if (is_saved) {
        lv_textarea_set_text(password_ta, saved_pwd);
    } else {
        lv_textarea_set_text(password_ta, "");
    }
    lv_textarea_set_placeholder_text(password_ta, selected_ssid);

    wifi_status_t status = {};
    wifi_mgr_get_status(&status);
    if (status.connected && strcmp(status.ssid, selected_ssid) == 0) {
        set_status("Rede conectada", ui_theme_get()->accent);
    } else if (is_saved) {
        set_status("Rede salva (senha memorizada)", ui_theme_get()->text_muted);
    } else {
        set_status("Rede selecionada", ui_theme_get()->text_muted);
    }

    update_action_buttons_state();
    render_networks();

    lv_obj_set_style_bg_opa(password_ta, LV_OPA_COVER, LV_PART_CURSOR);
    if (password_cursor_timer != nullptr) {
        lv_timer_resume(password_cursor_timer);
    }
    lv_group_focus_obj(password_ta);
    ui_keyboard_attach(password_ta);
}

void render_networks(void)
{
    if (network_list == nullptr) {
        return;
    }

    lv_obj_clean(network_list);
    if (!wifi_mgr_is_enabled()) {
        lv_obj_t *empty = lv_label_create(network_list);
        lv_label_set_text(empty, "Wi-Fi desativado nas configurações");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_18_latin1, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 12, 10);
        return;
    }

    if (network_count == 0) {
        lv_obj_t *empty = lv_label_create(network_list);
        lv_label_set_text(empty, "Nenhuma rede encontrada");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_18_latin1, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 12, 10);
        return;
    }

    wifi_status_t status = {};
    wifi_mgr_get_status(&status);

    const ui_palette_t *pal = ui_theme_get();
    for (int i = 0; i < network_count; ++i) {
        bool is_conn = status.connected && (strcmp(status.ssid, networks[i].ssid) == 0);
        bool is_saved = wifi_storage_find(networks[i].ssid, nullptr, 0);
        bool is_selected = (selected_ssid[0] != '\0') && (strcmp(selected_ssid, networks[i].ssid) == 0);

        lv_obj_t *item = lv_obj_create(network_list);
        lv_obj_set_width(item, lv_pct(100));
        lv_obj_set_height(item, 54);

        if (is_selected) {
            lv_obj_set_style_bg_color(item, lv_color_hex(pal->accent_soft), 0);
        } else {
            lv_obj_set_style_bg_color(item, lv_color_hex(pal->surface), 0);
        }
        lv_obj_set_style_bg_color(item, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_pad_left(item, 12, 0);
        lv_obj_set_style_pad_right(item, 12, 0);
        lv_obj_set_style_pad_top(item, 4, 0);
        lv_obj_set_style_pad_bottom(item, 4, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(item, select_network_cb, LV_EVENT_CLICKED, &networks[i]);

        /* Icone de status: Conectado (OK) ou Salva (SAVE) ou espaco */
        lv_obj_t *icon_lbl = lv_label_create(item);
        if (is_conn) {
            lv_label_set_text(icon_lbl, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(icon_lbl, lv_color_hex(pal->accent), 0);
        } else if (is_saved) {
            lv_label_set_text(icon_lbl, LV_SYMBOL_SAVE);
            lv_obj_set_style_text_color(icon_lbl, lv_color_hex(pal->text_muted), 0);
        } else {
            lv_label_set_text(icon_lbl, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(icon_lbl, lv_color_hex(pal->text_muted), 0);
        }
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_margin_right(icon_lbl, 8, 0);

        /* Nome da rede (SSID) */
        lv_obj_t *name_lbl = lv_label_create(item);
        char name_text[64];
        if (is_conn) {
            std::snprintf(name_text, sizeof(name_text), "%s (Conectado)", networks[i].ssid);
        } else if (is_saved) {
            std::snprintf(name_text, sizeof(name_text), "%s (Salva)", networks[i].ssid);
        } else {
            std::snprintf(name_text, sizeof(name_text), "%s", networks[i].ssid);
        }
        lv_label_set_text(name_lbl, name_text);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name_lbl, 1);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(pal->text), 0);

        /* Sinal em dBm */
        lv_obj_t *rssi_lbl = lv_label_create(item);
        char rssi_text[20];
        std::snprintf(rssi_text, sizeof(rssi_text), "%d dBm", networks[i].rssi);
        lv_label_set_text(rssi_lbl, rssi_text);
        lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(rssi_lbl, lv_color_hex(pal->text_muted), 0);
    }
}

void scan_ui_cb(void *data)
{
    (void)data;
    render_networks();
    if (scan_label != nullptr) {
        lv_label_set_text(scan_label, "Escanear");
    }
    set_status(network_count > 0 ? "Redes encontradas" : "Nenhuma rede encontrada", ui_theme_get()->text_muted);
    update_action_buttons_state();
}

void scan_cb(const wifi_ap_record_t *aps, int count, void *ctx)
{
    (void)ctx;
    network_count = 0;

    for (int i = 0; i < count; ++i) {
        const char *ssid = reinterpret_cast<const char *>(aps[i].ssid);
        if (ssid[0] == '\0') {
            continue; /* Desconsidera redes ocultas sem SSID */
        }

        /* Procura se ja existe uma entrada com este SSID */
        int existing_idx = -1;
        for (int j = 0; j < network_count; ++j) {
            if (strcmp(networks[j].ssid, ssid) == 0) {
                existing_idx = j;
                break;
            }
        }

        if (existing_idx >= 0) {
            /* Se ja existe, mantem a de maior intensidade (RSSI mais alto / menos negativo) */
            if (aps[i].rssi > networks[existing_idx].rssi) {
                networks[existing_idx].rssi = aps[i].rssi;
                networks[existing_idx].channel = aps[i].primary;
                networks[existing_idx].authmode = aps[i].authmode;
            }
        } else if (network_count < MAX_NETWORKS) {
            std::memset(&networks[network_count], 0, sizeof(networks[network_count]));
            std::strncpy(networks[network_count].ssid, ssid, sizeof(networks[network_count].ssid) - 1);
            networks[network_count].rssi = aps[i].rssi;
            networks[network_count].channel = aps[i].primary;
            networks[network_count].authmode = aps[i].authmode;
            network_count++;
        }
    }

    if (bsp_display_lock(0)) {
        lv_async_call(scan_ui_cb, nullptr);
        bsp_display_unlock();
    }
}

void scan_cb_event(lv_event_t *event)
{
    (void)event;
    if (scan_label != nullptr) {
        lv_label_set_text(scan_label, "Escaneando...");
    }
    set_status("Procurando redes...", ui_theme_get()->text_muted);
    if (wifi_mgr_scan(scan_cb, nullptr) != ESP_OK) {
        if (scan_label != nullptr) {
            lv_label_set_text(scan_label, "Escanear");
        }
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
        render_networks();
        update_action_buttons_state();
        return;
    }
    if (++connect_ticks >= 30) {
        set_status("Falhou ao conectar", ui_theme_get()->accent);
        lv_timer_pause(connect_timer);
        render_networks();
        update_action_buttons_state();
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

void disconnect_cb(lv_event_t *event)
{
    (void)event;
    wifi_mgr_disconnect();
    set_status("Desconectado", ui_theme_get()->text_muted);
    render_networks();
    update_action_buttons_state();
}

void forget_cb(lv_event_t *event)
{
    (void)event;
    if (selected_ssid[0] == '\0') {
        return;
    }
    wifi_mgr_forget(selected_ssid);
    lv_textarea_set_text(password_ta, "");
    set_status("Rede esquecida", ui_theme_get()->text_muted);
    render_networks();
    update_action_buttons_state();
}

void password_ta_click_cb(lv_event_t *event)
{
    (void)event;
    if (password_ta != nullptr) {
        lv_obj_set_style_bg_opa(password_ta, LV_OPA_COVER, LV_PART_CURSOR);
        if (password_cursor_timer != nullptr) {
            lv_timer_resume(password_cursor_timer);
        }
        lv_group_focus_obj(password_ta);
        ui_keyboard_attach(password_ta);
    }
}

void close_cb(lv_event_t *event)
{
    (void)event;
    if (password_cursor_timer != nullptr) {
        lv_timer_pause(password_cursor_timer);
    }
    if (password_ta != nullptr) {
        lv_obj_set_style_bg_opa(password_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    }
    ui_shell_close_wifi();
}

} // namespace

struct ui_wifi_view_s {
    lv_obj_t *parent = nullptr;
    ui_app_bar_t app_bar = {};
};

ui_wifi_view_t *ui_wifi_view_create(lv_obj_t *parent, ui_app_bar_t app_bar)
{
    (void)app_bar;
    wifi_scr = parent;

    scan_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(scan_button, 44);
    lv_obj_set_style_radius(scan_button, 8, 0);
    lv_obj_clear_flag(scan_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scan_button, scan_cb_event, LV_EVENT_CLICKED, nullptr);
    scan_label = lv_label_create(scan_button);
    lv_label_set_text(scan_label, "Escanear");
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(scan_label);

    network_list = lv_obj_create(wifi_scr);
    lv_obj_set_style_radius(network_list, 8, 0);
    lv_obj_set_style_border_width(network_list, 1, 0);
    lv_obj_set_style_pad_all(network_list, 4, 0);
    lv_obj_set_style_pad_row(network_list, 4, 0);
    lv_obj_set_flex_flow(network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(network_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(network_list, LV_DIR_VER);

    password_ta = lv_textarea_create(wifi_scr);
    lv_obj_set_height(password_ta, 44);
    lv_textarea_set_one_line(password_ta, true);
    lv_textarea_set_placeholder_text(password_ta, "Senha da rede selecionada");
    lv_textarea_set_password_mode(password_ta, true);
    lv_textarea_set_cursor_click_pos(password_ta, true);
    lv_obj_clear_flag(password_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_anim_duration(password_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(password_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_text_font(password_ta, &lv_font_montserrat_18_latin1, LV_PART_MAIN);
    lv_obj_add_event_cb(password_ta, password_ta_click_cb, LV_EVENT_CLICKED, nullptr);

    show_pwd_btn = lv_obj_create(wifi_scr);
    lv_obj_set_height(show_pwd_btn, 44);
    lv_obj_set_style_radius(show_pwd_btn, 8, 0);
    lv_obj_set_style_border_width(show_pwd_btn, 1, 0);
    lv_obj_clear_flag(show_pwd_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(show_pwd_btn, show_pwd_event_cb, LV_EVENT_CLICKED, nullptr);
    show_pwd_label = lv_label_create(show_pwd_btn);
    lv_label_set_text(show_pwd_label, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_font(show_pwd_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(show_pwd_label);

    /* Botao Conectar */
    connect_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(connect_button, 44);
    lv_obj_set_style_radius(connect_button, 8, 0);
    lv_obj_clear_flag(connect_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(connect_button, connect_cb, LV_EVENT_CLICKED, nullptr);
    connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, "Conectar");
    lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(connect_label);

    /* Botao Desconectar */
    disconnect_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(disconnect_button, 44);
    lv_obj_set_style_radius(disconnect_button, 8, 0);
    lv_obj_set_style_border_width(disconnect_button, 1, 0);
    lv_obj_clear_flag(disconnect_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(disconnect_button, disconnect_cb, LV_EVENT_CLICKED, nullptr);
    disconnect_label = lv_label_create(disconnect_button);
    lv_label_set_text(disconnect_label, "Desconectar");
    lv_obj_set_style_text_font(disconnect_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(disconnect_label);

    /* Botao Esquecer */
    forget_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(forget_button, 44);
    lv_obj_set_style_radius(forget_button, 8, 0);
    lv_obj_set_style_border_width(forget_button, 1, 0);
    lv_obj_clear_flag(forget_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(forget_button, forget_cb, LV_EVENT_CLICKED, nullptr);
    forget_label = lv_label_create(forget_button);
    lv_label_set_text(forget_label, "Esquecer");
    lv_obj_set_style_text_font(forget_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(forget_label);

    status_label = lv_label_create(wifi_scr);
    lv_label_set_text(status_label, "Selecione uma rede para conectar");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18_latin1, 0);

    wifi_group = lv_group_create();
    lv_group_add_obj(wifi_group, password_ta);
    apply_wifi_layout();
    apply_wifi_theme();

    password_cursor_timer = lv_timer_create(cursor_blink_cb, 500, password_ta);
    lv_timer_pause(password_cursor_timer);

    connect_timer = lv_timer_create(connect_timer_cb, 500, nullptr);
    lv_timer_pause(connect_timer);
    render_networks();
    update_action_buttons_state();

    ui_wifi_view_t *view = new ui_wifi_view_t();
    view->parent = parent;
    view->app_bar = app_bar;
    return view;
}

void ui_wifi_view_refresh_theme(ui_wifi_view_t *view)
{
    (void)view;
    ui_wifi_refresh_theme();
}

void ui_wifi_view_apply_layout(ui_wifi_view_t *view)
{
    (void)view;
    ui_wifi_apply_layout();
}

void ui_wifi_view_destroy(ui_wifi_view_t *view)
{
    if (password_cursor_timer != nullptr) {
        lv_timer_delete(password_cursor_timer);
        password_cursor_timer = nullptr;
    }
    if (connect_timer != nullptr) {
        lv_timer_delete(connect_timer);
        connect_timer = nullptr;
    }
    wifi_scr = nullptr;
    delete view;
}

lv_obj_t *ui_wifi_create(void)
{
    wifi_scr = lv_obj_create(nullptr);
    wifi_app_bar = ui_app_bar_create(wifi_scr, "WiFi", close_cb, nullptr);

    scan_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(scan_button, 44);
    lv_obj_set_style_radius(scan_button, 8, 0);
    lv_obj_clear_flag(scan_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scan_button, scan_cb_event, LV_EVENT_CLICKED, nullptr);
    scan_label = lv_label_create(scan_button);
    lv_label_set_text(scan_label, "Escanear");
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(scan_label);

    network_list = lv_obj_create(wifi_scr);
    lv_obj_set_style_radius(network_list, 8, 0);
    lv_obj_set_style_border_width(network_list, 1, 0);
    lv_obj_set_style_pad_all(network_list, 4, 0);
    lv_obj_set_style_pad_row(network_list, 4, 0);
    lv_obj_set_flex_flow(network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(network_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(network_list, LV_DIR_VER);

    password_ta = lv_textarea_create(wifi_scr);
    lv_obj_set_height(password_ta, 44);
    lv_textarea_set_one_line(password_ta, true);
    lv_textarea_set_placeholder_text(password_ta, "Senha da rede selecionada");
    lv_textarea_set_password_mode(password_ta, true);
    lv_textarea_set_cursor_click_pos(password_ta, true);
    lv_obj_clear_flag(password_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_anim_duration(password_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(password_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_text_font(password_ta, &lv_font_montserrat_18_latin1, LV_PART_MAIN);
    lv_obj_add_event_cb(password_ta, password_ta_click_cb, LV_EVENT_CLICKED, nullptr);

    show_pwd_btn = lv_obj_create(wifi_scr);
    lv_obj_set_height(show_pwd_btn, 44);
    lv_obj_set_style_radius(show_pwd_btn, 8, 0);
    lv_obj_set_style_border_width(show_pwd_btn, 1, 0);
    lv_obj_clear_flag(show_pwd_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(show_pwd_btn, show_pwd_event_cb, LV_EVENT_CLICKED, nullptr);
    show_pwd_label = lv_label_create(show_pwd_btn);
    lv_label_set_text(show_pwd_label, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_font(show_pwd_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(show_pwd_label);

    /* Botao Conectar */
    connect_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(connect_button, 44);
    lv_obj_set_style_radius(connect_button, 8, 0);
    lv_obj_clear_flag(connect_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(connect_button, connect_cb, LV_EVENT_CLICKED, nullptr);
    connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, "Conectar");
    lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(connect_label);

    /* Botao Desconectar */
    disconnect_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(disconnect_button, 44);
    lv_obj_set_style_radius(disconnect_button, 8, 0);
    lv_obj_set_style_border_width(disconnect_button, 1, 0);
    lv_obj_clear_flag(disconnect_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(disconnect_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(disconnect_button, disconnect_cb, LV_EVENT_CLICKED, nullptr);
    disconnect_label = lv_label_create(disconnect_button);
    lv_label_set_text(disconnect_label, "Desconectar");
    lv_obj_set_style_text_font(disconnect_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(disconnect_label);

    /* Botao Esquecer */
    forget_button = lv_obj_create(wifi_scr);
    lv_obj_set_height(forget_button, 44);
    lv_obj_set_style_radius(forget_button, 8, 0);
    lv_obj_set_style_border_width(forget_button, 1, 0);
    lv_obj_clear_flag(forget_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(forget_button, forget_cb, LV_EVENT_CLICKED, nullptr);
    forget_label = lv_label_create(forget_button);
    lv_label_set_text(forget_label, "Esquecer");
    lv_obj_set_style_text_font(forget_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(forget_label);

    status_label = lv_label_create(wifi_scr);
    lv_label_set_text(status_label, "Selecione uma rede para conectar");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18_latin1, 0);

    wifi_group = lv_group_create();
    lv_group_add_obj(wifi_group, password_ta);
    apply_wifi_layout();
    lv_display_add_event_cb(lv_display_get_default(), resolution_cb, LV_EVENT_RESOLUTION_CHANGED, nullptr);
    apply_wifi_theme();

    password_cursor_timer = lv_timer_create(cursor_blink_cb, 500, password_ta);
    lv_timer_pause(password_cursor_timer);

    connect_timer = lv_timer_create(connect_timer_cb, 500, nullptr);
    lv_timer_pause(connect_timer);
    render_networks();
    update_action_buttons_state();
    return wifi_scr;
}

void ui_wifi_refresh_theme(void)
{
    apply_wifi_theme();
    render_networks();
}

void ui_wifi_apply_layout(void)
{
    apply_wifi_layout();
}

void ui_wifi_register(void)
{
    static const app_desc_t s_wifi_desc = {
        .id = "wifi",
        .name = "WiFi",
        .icon_symbol = LV_SYMBOL_WIFI,
        .icon_bg_color = nullptr,
        .icon_builder = nullptr,
        .icon_theme_refresh = nullptr,
        .on_launch = ui_shell_open_wifi,
        .file_extensions = nullptr,
        .on_open_file = nullptr,
    };
    app_registry_register(&s_wifi_desc);
}
