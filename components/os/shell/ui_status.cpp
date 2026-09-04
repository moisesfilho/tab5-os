#include "ui_status.h"
#include "ui_theme.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_bar.h"
#include "wifi_mgr.h"
#include "bt_mgr.h"
#include "music_player.h"
#include "battery_reader.h"
#include "tab5_package_mgr.h"

namespace {

lv_obj_t *bt_icon_btn = nullptr;
lv_obj_t *bt_icon_label = nullptr;
lv_obj_t *wifi_icon_btn = nullptr;
lv_obj_t *wifi_icon_label = nullptr;
lv_obj_t *music_icon_btn = nullptr;
lv_obj_t *music_icon_label = nullptr;
lv_obj_t *bat_icon_btn = nullptr;
lv_obj_t *bat_icon_label = nullptr;
lv_obj_t *bat_pct_label = nullptr;
lv_obj_t *bat_popup_backdrop = nullptr;
lv_obj_t *bat_popup_state_label = nullptr;
lv_obj_t *bat_popup_volt_label = nullptr;
lv_obj_t *bat_popup_curr_label = nullptr;
lv_obj_t *bat_popup_lvl_label = nullptr;
lv_timer_t *status_timer = nullptr;
bool s_last_wifi_connected = false;
bool s_last_bt_connected = false;
music_player_state_t s_last_music_state = MUSIC_PLAYER_STATE_IDLE;
battery_source_t s_last_bat_source = BATTERY_SOURCE_BATTERY;
int s_last_bat_percent = 0;
int32_t s_last_bat_voltage_mv = 0;
int32_t s_last_bat_current_ma = 0;
bool s_last_bat_protect = false;
bool s_last_bat_available = false;

#define BAT_CRITICAL_COLOR 0xE5484D

void update_battery_popup_text(void);

/* Simbolo da bateria conforme o nivel */
const char *battery_level_symbol(int percent)
{
    if (percent > 87) {
        return LV_SYMBOL_BATTERY_FULL;
    }
    if (percent > 62) {
        return LV_SYMBOL_BATTERY_3;
    }
    if (percent > 37) {
        return LV_SYMBOL_BATTERY_2;
    }
    if (percent > 12) {
        return LV_SYMBOL_BATTERY_1;
    }
    return LV_SYMBOL_BATTERY_EMPTY;
}

/* Aplica cor/simbolo do icone de bateria conforme o estado em cache */
void update_battery_icon_visuals(void)
{
    const ui_palette_t *pal = ui_theme_get();

    if (bat_icon_btn != nullptr) {
        if (!s_last_bat_available) {
            lv_obj_add_flag(bat_icon_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(bat_icon_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (bat_icon_label != nullptr) {
        const char *sym;
        if (s_last_bat_source == BATTERY_SOURCE_CHARGING) {
            sym = LV_SYMBOL_CHARGE;
        } else if (s_last_bat_source == BATTERY_SOURCE_NO_BATTERY) {
            sym = LV_SYMBOL_PLUS;
        } else {
            sym = battery_level_symbol(s_last_bat_percent);
        }
        uint32_t color = pal->text;
        if (s_last_bat_source != BATTERY_SOURCE_BATTERY) {
            color = pal->accent;
        } else if (s_last_bat_percent <= 15) {
            color = BAT_CRITICAL_COLOR;
        }
        lv_label_set_text(bat_icon_label, sym);
        lv_obj_set_style_text_color(bat_icon_label, lv_color_hex(color), 0);
    }
    if (bat_pct_label != nullptr) {
        if (s_last_bat_source == BATTERY_SOURCE_NO_BATTERY || !s_last_bat_available) {
            lv_label_set_text(bat_pct_label, "");
        } else {
            uint32_t color = (s_last_bat_source != BATTERY_SOURCE_BATTERY) ? pal->accent : pal->text;
            lv_label_set_text_fmt(bat_pct_label, "%d%%", s_last_bat_percent);
            lv_obj_set_style_text_color(bat_pct_label, lv_color_hex(color), 0);
        }
    }
}

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
    if (music_icon_label != nullptr) {
        lv_obj_set_style_text_color(
            music_icon_label,
            lv_color_hex((s_last_music_state == MUSIC_PLAYER_STATE_PLAYING) ? pal->accent : pal->text_muted), 0);
    }
    update_battery_icon_visuals();
}

void status_update(void)
{
    bool w_enabled = wifi_mgr_is_enabled();
    wifi_status_t w_status = {};
    bool w_connected = false;
    if (w_enabled && wifi_mgr_get_status(&w_status) == ESP_OK) {
        w_connected = w_status.connected;
    }
    s_last_wifi_connected = w_connected;

    bool b_enabled = bt_mgr_is_enabled();
    bt_status_t b_status = {};
    bool b_connected = false;
    if (b_enabled && bt_mgr_get_status(&b_status) == ESP_OK) {
        b_connected = b_status.any_connected;
    }
    s_last_bt_connected = b_connected;

    music_player_status_t m_status = {};
    music_player_get_status(&m_status);
    s_last_music_state = m_status.state;

    const ui_palette_t *pal = ui_theme_get();
    if (wifi_icon_btn != nullptr) {
        if (!w_enabled) {
            lv_obj_add_flag(wifi_icon_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(wifi_icon_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (wifi_icon_label != nullptr) {
        lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(w_connected ? pal->accent : pal->text_muted), 0);
    }

    if (bt_icon_btn != nullptr) {
        if (!b_enabled) {
            lv_obj_add_flag(bt_icon_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(bt_icon_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (bt_icon_label != nullptr) {
        lv_obj_set_style_text_color(bt_icon_label, lv_color_hex(b_connected ? pal->accent : pal->text_muted), 0);
    }

    if (music_icon_btn != nullptr) {
        if (m_status.state == MUSIC_PLAYER_STATE_IDLE) {
            lv_obj_add_flag(music_icon_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(music_icon_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (music_icon_label != nullptr) {
        lv_obj_set_style_text_color(
            music_icon_label,
            lv_color_hex((m_status.state == MUSIC_PLAYER_STATE_PLAYING) ? pal->accent : pal->text_muted), 0);
    }

    battery_status_t bstat = {};
    s_last_bat_available = battery_reader_get_status(&bstat);
    if (s_last_bat_available) {
        s_last_bat_source = bstat.source;
        s_last_bat_percent = bstat.percent;
        s_last_bat_voltage_mv = bstat.voltage_mv;
        s_last_bat_current_ma = bstat.current_ma;
        s_last_bat_protect = bstat.protect_active;
    }
    update_battery_icon_visuals();
    if (bat_popup_backdrop != nullptr && !lv_obj_has_flag(bat_popup_backdrop, LV_OBJ_FLAG_HIDDEN)) {
        update_battery_popup_text();
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
    tab5_package_mgr_launch("com.tab5.wifi", nullptr);
}

void bt_icon_click_cb(lv_event_t *event)
{
    (void)event;
    tab5_package_mgr_launch("com.tab5.bluetooth", nullptr);
}

void music_icon_click_cb(lv_event_t *event)
{
    (void)event;
    music_player_status_t m_status = {};
    music_player_get_status(&m_status);
    if (m_status.state == MUSIC_PLAYER_STATE_PLAYING) {
        music_player_pause();
    } else if (m_status.state == MUSIC_PLAYER_STATE_PAUSED) {
        music_player_resume();
    }
    status_update();
}

/* Tema proprio do popup de bateria (nao usa apply_menu_theme, que retorna cedo) */
void apply_battery_popup_theme(void)
{
    const ui_palette_t *pal = ui_theme_get();
    lv_obj_t *panel = (bat_popup_backdrop != nullptr) ? lv_obj_get_child(bat_popup_backdrop, 0) : nullptr;
    if (panel == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(panel, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(pal->border), 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(panel); i++) {
        lv_obj_t *child = lv_obj_get_child(panel, i);
        if (i == 0 && lv_obj_get_child_count(child) == 2) {
            lv_obj_t *close_btn = lv_obj_get_child(child, 1);
            lv_obj_set_style_text_color(close_btn, lv_color_hex(pal->text), 0);
            lv_obj_set_style_bg_color(close_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
            lv_obj_t *title = lv_obj_get_child(child, 0);
            lv_obj_t *close_label = lv_obj_get_child(close_btn, 0);
            lv_obj_set_style_text_color(title, lv_color_hex(pal->text), 0);
            lv_obj_set_style_text_color(close_label, lv_color_hex(pal->text), 0);
        } else {
            lv_obj_set_style_text_color(child, lv_color_hex(pal->text), 0);
        }
    }
}

void update_battery_popup_text(void)
{
    if (bat_popup_state_label == nullptr || bat_popup_volt_label == nullptr || bat_popup_curr_label == nullptr ||
        bat_popup_lvl_label == nullptr) {
        return;
    }

    if (!s_last_bat_available) {
        lv_label_set_text(bat_popup_state_label, "Estado: Indisponivel");
        lv_label_set_text(bat_popup_volt_label, "Tensão: --");
        lv_label_set_text(bat_popup_curr_label, "Corrente: --");
        lv_label_set_text(bat_popup_lvl_label, "Nível: --");
        return;
    }

    const char *state;
    if (s_last_bat_source == BATTERY_SOURCE_CHARGING) {
        state = "Carregando";
    } else if (s_last_bat_source == BATTERY_SOURCE_EXTERNAL) {
        state = s_last_bat_protect ? "Na tomada (proteção 90%)" : "Na tomada";
    } else if (s_last_bat_source == BATTERY_SOURCE_NO_BATTERY) {
        state = "Somente cabo (sem bateria)";
    } else {
        state = "Na bateria";
    }
    lv_label_set_text_fmt(bat_popup_state_label, "Estado: %s", state);
    lv_label_set_text_fmt(bat_popup_volt_label, "Tensão: %ld mV", (long)s_last_bat_voltage_mv);
    lv_label_set_text_fmt(bat_popup_curr_label, "Corrente: %ld mA", (long)s_last_bat_current_ma);
    lv_label_set_text_fmt(bat_popup_lvl_label, "Nível: %d%%", s_last_bat_percent);
}

void close_battery_popup(void)
{
    if (bat_popup_backdrop != nullptr) {
        lv_obj_add_flag(bat_popup_backdrop, LV_OBJ_FLAG_HIDDEN);
    }
}

void backdrop_click_cb(lv_event_t *event)
{
    (void)event;
    close_battery_popup();
}

void popup_close_click_cb(lv_event_t *event)
{
    (void)event;
    close_battery_popup();
}

void bat_icon_click_cb(lv_event_t *event)
{
    (void)event;
    if (bat_popup_backdrop != nullptr && !lv_obj_has_flag(bat_popup_backdrop, LV_OBJ_FLAG_HIDDEN)) {
        close_battery_popup();
        return;
    }

    if (bat_popup_backdrop == nullptr) {
        /* Fundo que engole toques fora do painel e fecha o popup */
        bat_popup_backdrop = lv_obj_create(lv_layer_top());
        lv_obj_set_size(bat_popup_backdrop, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(bat_popup_backdrop, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bat_popup_backdrop, 0, 0);
        lv_obj_set_style_pad_all(bat_popup_backdrop, 0, 0);
        lv_obj_clear_flag(bat_popup_backdrop, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(bat_popup_backdrop, backdrop_click_cb, LV_EVENT_CLICKED, nullptr);

        /* Painel de detalhes */
        lv_obj_t *panel = lv_obj_create(bat_popup_backdrop);
        lv_obj_set_size(panel, 380, LV_SIZE_CONTENT);
        lv_obj_align(panel, LV_ALIGN_TOP_RIGHT, -12, UI_BAR_HEIGHT + 8);
        lv_obj_set_style_radius(panel, 12, 0);
        lv_obj_set_style_pad_all(panel, 20, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 12, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        /* Cabecalho com titulo + fechar */
        lv_obj_t *header = lv_obj_create(panel);
        lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_pad_all(header, 0, 0);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *title = lv_label_create(header);
        lv_label_set_text(title, "Bateria");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_flex_grow(title, 1);

        lv_obj_t *close_btn = lv_obj_create(header);
        lv_obj_set_size(close_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(close_btn, 0, 0);
        lv_obj_set_style_shadow_width(close_btn, 0, 0);
        lv_obj_set_style_radius(close_btn, 8, 0);
        lv_obj_set_style_pad_left(close_btn, 8, 0);
        lv_obj_set_style_pad_right(close_btn, 8, 0);
        lv_obj_set_style_pad_top(close_btn, 6, 0);
        lv_obj_set_style_pad_bottom(close_btn, 6, 0);
        lv_obj_add_event_cb(close_btn, popup_close_click_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *close_label = lv_label_create(close_btn);
        lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(close_label, &lv_font_montserrat_18_latin1, 0);

        bat_popup_state_label = lv_label_create(panel);
        bat_popup_volt_label = lv_label_create(panel);
        bat_popup_curr_label = lv_label_create(panel);
        bat_popup_lvl_label = lv_label_create(panel);
        lv_obj_t *rows[4] = {bat_popup_state_label, bat_popup_volt_label, bat_popup_curr_label, bat_popup_lvl_label};
        for (int i = 0; i < 4; i++) {
            lv_obj_set_style_text_font(rows[i], &lv_font_montserrat_18_latin1, 0);
        }

        apply_battery_popup_theme();
    }

    update_battery_popup_text();
    lv_obj_remove_flag(bat_popup_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(bat_popup_backdrop);
}

} // namespace

void ui_status_init(lv_obj_t *parent)
{
    /* Icone de Musica / Audio interativo */
    music_icon_btn = lv_obj_create(parent);
    lv_obj_set_size(music_icon_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(music_icon_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_icon_btn, 0, 0);
    lv_obj_set_style_shadow_width(music_icon_btn, 0, 0);
    lv_obj_set_style_radius(music_icon_btn, 8, 0);
    lv_obj_set_style_pad_left(music_icon_btn, 8, 0);
    lv_obj_set_style_pad_right(music_icon_btn, 8, 0);
    lv_obj_set_style_pad_top(music_icon_btn, 8, 0);
    lv_obj_set_style_pad_bottom(music_icon_btn, 8, 0);
    lv_obj_set_style_margin_right(music_icon_btn, 4, 0);
    lv_obj_clear_flag(music_icon_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(music_icon_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(music_icon_btn, music_icon_click_cb, LV_EVENT_CLICKED, nullptr);

    music_icon_label = lv_label_create(music_icon_btn);
    lv_label_set_text(music_icon_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(music_icon_label, &lv_font_montserrat_18_latin1, 0);

    /* Icone Bluetooth interativo */
    bt_icon_btn = lv_obj_create(parent);
    lv_obj_set_size(bt_icon_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bt_icon_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bt_icon_btn, 0, 0);
    lv_obj_set_style_shadow_width(bt_icon_btn, 0, 0);
    lv_obj_set_style_radius(bt_icon_btn, 8, 0);
    lv_obj_set_style_pad_left(bt_icon_btn, 8, 0);
    lv_obj_set_style_pad_right(bt_icon_btn, 8, 0);
    lv_obj_set_style_pad_top(bt_icon_btn, 8, 0);
    lv_obj_set_style_pad_bottom(bt_icon_btn, 8, 0);
    lv_obj_set_style_margin_right(bt_icon_btn, 4, 0);
    lv_obj_clear_flag(bt_icon_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bt_icon_btn, bt_icon_click_cb, LV_EVENT_CLICKED, nullptr);

    bt_icon_label = lv_label_create(bt_icon_btn);
    lv_label_set_text(bt_icon_label, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(bt_icon_label, &lv_font_montserrat_18_latin1, 0);

    /* Icone Wi-Fi interativo alinhado ao lado do Bluetooth */
    wifi_icon_btn = lv_obj_create(parent);
    lv_obj_set_size(wifi_icon_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wifi_icon_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_icon_btn, 0, 0);
    lv_obj_set_style_shadow_width(wifi_icon_btn, 0, 0);
    lv_obj_set_style_radius(wifi_icon_btn, 8, 0);
    lv_obj_set_style_pad_left(wifi_icon_btn, 8, 0);
    lv_obj_set_style_pad_right(wifi_icon_btn, 8, 0);
    lv_obj_set_style_pad_top(wifi_icon_btn, 8, 0);
    lv_obj_set_style_pad_bottom(wifi_icon_btn, 8, 0);
    lv_obj_set_style_margin_right(wifi_icon_btn, 4, 0);
    lv_obj_clear_flag(wifi_icon_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(wifi_icon_btn, wifi_icon_click_cb, LV_EVENT_CLICKED, nullptr);

    wifi_icon_label = lv_label_create(wifi_icon_btn);
    lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon_label, &lv_font_montserrat_18_latin1, 0);

    /* Icone de bateria com percentual e popup de detalhes */
    bat_icon_btn = lv_obj_create(parent);
    lv_obj_set_size(bat_icon_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bat_icon_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bat_icon_btn, 0, 0);
    lv_obj_set_style_shadow_width(bat_icon_btn, 0, 0);
    lv_obj_set_style_radius(bat_icon_btn, 8, 0);
    lv_obj_set_style_pad_left(bat_icon_btn, 10, 0);
    lv_obj_set_style_pad_right(bat_icon_btn, 10, 0);
    lv_obj_set_style_pad_top(bat_icon_btn, 8, 0);
    lv_obj_set_style_pad_bottom(bat_icon_btn, 8, 0);
    lv_obj_set_style_pad_column(bat_icon_btn, 6, 0);
    lv_obj_set_style_margin_right(bat_icon_btn, 8, 0);
    lv_obj_set_flex_flow(bat_icon_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bat_icon_btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bat_icon_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bat_icon_btn, bat_icon_click_cb, LV_EVENT_CLICKED, nullptr);

    bat_icon_label = lv_label_create(bat_icon_btn);
    lv_label_set_text(bat_icon_label, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_style_text_font(bat_icon_label, &lv_font_montserrat_18_latin1, 0);

    bat_pct_label = lv_label_create(bat_icon_btn);
    lv_label_set_text(bat_pct_label, "");
    lv_obj_set_style_text_font(bat_pct_label, &lv_font_montserrat_18_latin1, 0);

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
