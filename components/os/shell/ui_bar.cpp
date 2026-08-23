#include "ui_bar.h"
#include "ui_theme.h"
#include "ui_status.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_screensaver.h"
#include "ui_screen_off.h"
#include "ui_keyboard.h"
#include "display_storage.h"
#include "timezone_mgr.h"
#include "imu_reader.h"
#include "wifi_mgr.h"
#include "bt_mgr.h"
#include "music_player.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

namespace {

const char *TAG = "ui_bar";

enum menu_page_t {
    MENU_PAGE_MAIN,
    MENU_PAGE_THEME,
    MENU_PAGE_SCREENSAVER,
    MENU_PAGE_SCREEN_OFF,
    MENU_PAGE_POWER,
};

/* Acao escolhida no painel de energia e pendente de confirmacao. */
enum power_action_t {
    POWER_ACTION_REBOOT,
    POWER_ACTION_SHUTDOWN,
};

lv_obj_t *bar = nullptr;
lv_obj_t *power_btn = nullptr;
lv_obj_t *power_label = nullptr;
lv_obj_t *gear = nullptr;
lv_obj_t *gear_label = nullptr;
lv_obj_t *clock_label = nullptr;

/* Menu Configuracao (overlay + painel) */
lv_obj_t *menu_overlay = nullptr;
lv_obj_t *menu_panel = nullptr;
lv_obj_t *menu_header_label = nullptr;
lv_obj_t *menu_row_theme = nullptr;
lv_obj_t *menu_row_theme_label = nullptr;
lv_obj_t *menu_row_ss = nullptr;
lv_obj_t *menu_row_ss_label = nullptr;
lv_obj_t *menu_row_light = nullptr;
lv_obj_t *menu_row_light_label = nullptr;
lv_obj_t *menu_row_dark = nullptr;
lv_obj_t *menu_row_dark_label = nullptr;
lv_obj_t *menu_row_back = nullptr;
lv_obj_t *menu_row_back_label = nullptr;
lv_obj_t *menu_row_ss_off = nullptr;
lv_obj_t *menu_row_ss_off_label = nullptr;
lv_obj_t *menu_row_ss_1m = nullptr;
lv_obj_t *menu_row_ss_1m_label = nullptr;
lv_obj_t *menu_row_ss_2m = nullptr;
lv_obj_t *menu_row_ss_2m_label = nullptr;
lv_obj_t *menu_row_ss_5m = nullptr;
lv_obj_t *menu_row_ss_5m_label = nullptr;
lv_obj_t *menu_row_soff = nullptr;
lv_obj_t *menu_row_soff_label = nullptr;
lv_obj_t *menu_row_soff_off = nullptr;
lv_obj_t *menu_row_soff_off_label = nullptr;
lv_obj_t *menu_row_soff_30s = nullptr;
lv_obj_t *menu_row_soff_30s_label = nullptr;
lv_obj_t *menu_row_soff_1m = nullptr;
lv_obj_t *menu_row_soff_1m_label = nullptr;
lv_obj_t *menu_row_soff_2m = nullptr;
lv_obj_t *menu_row_soff_2m_label = nullptr;
lv_obj_t *menu_row_soff_5m = nullptr;
lv_obj_t *menu_row_soff_5m_label = nullptr;
lv_obj_t *menu_row_soff_10m = nullptr;
lv_obj_t *menu_row_soff_10m_label = nullptr;
lv_obj_t *menu_row_pw_screen = nullptr;
lv_obj_t *menu_row_pw_screen_label = nullptr;
lv_obj_t *menu_row_pw_reboot = nullptr;
lv_obj_t *menu_row_pw_reboot_label = nullptr;
lv_obj_t *menu_row_pw_off = nullptr;
lv_obj_t *menu_row_pw_off_label = nullptr;
lv_obj_t *menu_row_rotation_label = nullptr;
lv_obj_t *menu_row_rotation_switch = nullptr;
lv_obj_t *menu_row_wifi_label = nullptr;
lv_obj_t *menu_row_wifi_switch = nullptr;
lv_obj_t *menu_row_bt_label = nullptr;
lv_obj_t *menu_row_bt_switch = nullptr;
lv_obj_t *menu_row_brightness_label = nullptr;
lv_obj_t *menu_row_brightness_val_label = nullptr;
lv_obj_t *menu_row_brightness_slider = nullptr;
lv_obj_t *menu_row_volume_label = nullptr;
lv_obj_t *menu_row_volume_val_label = nullptr;
lv_obj_t *menu_row_volume_slider = nullptr;
lv_obj_t *menu_row_tz_label = nullptr;
lv_obj_t *menu_row_tz_val_label = nullptr;
lv_obj_t *menu_row_tz_minus_btn = nullptr;
lv_obj_t *menu_row_tz_minus_label = nullptr;
lv_obj_t *menu_row_tz_plus_btn = nullptr;
lv_obj_t *menu_row_tz_plus_label = nullptr;

/* Modal de confirmacao Reiniciar/Desligar (overlay + card) */
lv_obj_t *power_confirm_overlay = nullptr;
lv_obj_t *power_confirm_card = nullptr;
lv_obj_t *power_confirm_title_label = nullptr;
lv_obj_t *power_confirm_msg_label = nullptr;
lv_obj_t *power_confirm_btn_cancel = nullptr;
lv_obj_t *power_confirm_btn_cancel_label = nullptr;
lv_obj_t *power_confirm_btn_ok = nullptr;
lv_obj_t *power_confirm_btn_ok_label = nullptr;
power_action_t power_action = POWER_ACTION_REBOOT;

void close_menu(void);
void close_power_confirm(void);
void show_power_confirm(power_action_t action);
void clock_update(void);

void menu_overlay_cb(lv_event_t *event)
{
    (void)event;
    close_menu();
}

void close_power_confirm(void)
{
    if (power_confirm_overlay != nullptr) {
        lv_obj_delete(power_confirm_overlay);
        power_confirm_overlay = nullptr;
    }
    power_confirm_card = nullptr;
    power_confirm_title_label = nullptr;
    power_confirm_msg_label = nullptr;
    power_confirm_btn_cancel = nullptr;
    power_confirm_btn_cancel_label = nullptr;
    power_confirm_btn_ok = nullptr;
    power_confirm_btn_ok_label = nullptr;
}

void close_menu(void)
{
    close_power_confirm();
    if (menu_overlay != nullptr) {
        lv_obj_delete(menu_overlay);
        menu_overlay = nullptr;
    }
    if (menu_panel != nullptr) {
        lv_obj_delete(menu_panel);
        menu_panel = nullptr;
    }

    menu_header_label = nullptr;
    menu_row_theme = nullptr;
    menu_row_theme_label = nullptr;
    menu_row_ss = nullptr;
    menu_row_ss_label = nullptr;
    menu_row_light = nullptr;
    menu_row_light_label = nullptr;
    menu_row_dark = nullptr;
    menu_row_dark_label = nullptr;
    menu_row_back = nullptr;
    menu_row_back_label = nullptr;
    menu_row_ss_off = nullptr;
    menu_row_ss_off_label = nullptr;
    menu_row_ss_1m = nullptr;
    menu_row_ss_1m_label = nullptr;
    menu_row_ss_2m = nullptr;
    menu_row_ss_2m_label = nullptr;
    menu_row_ss_5m = nullptr;
    menu_row_ss_5m_label = nullptr;
    menu_row_soff = nullptr;
    menu_row_soff_label = nullptr;
    menu_row_soff_off = nullptr;
    menu_row_soff_off_label = nullptr;
    menu_row_soff_30s = nullptr;
    menu_row_soff_30s_label = nullptr;
    menu_row_soff_1m = nullptr;
    menu_row_soff_1m_label = nullptr;
    menu_row_soff_2m = nullptr;
    menu_row_soff_2m_label = nullptr;
    menu_row_soff_5m = nullptr;
    menu_row_soff_5m_label = nullptr;
    menu_row_soff_10m = nullptr;
    menu_row_soff_10m_label = nullptr;
    menu_row_pw_screen = nullptr;
    menu_row_pw_screen_label = nullptr;
    menu_row_pw_reboot = nullptr;
    menu_row_pw_reboot_label = nullptr;
    menu_row_pw_off = nullptr;
    menu_row_pw_off_label = nullptr;
    menu_row_rotation_label = nullptr;
    menu_row_rotation_switch = nullptr;
    menu_row_wifi_label = nullptr;
    menu_row_wifi_switch = nullptr;
    menu_row_bt_label = nullptr;
    menu_row_bt_switch = nullptr;
    menu_row_brightness_label = nullptr;
    menu_row_brightness_val_label = nullptr;
    menu_row_brightness_slider = nullptr;
    menu_row_volume_label = nullptr;
    menu_row_volume_val_label = nullptr;
    menu_row_volume_slider = nullptr;
    menu_row_tz_label = nullptr;
    menu_row_tz_val_label = nullptr;
    menu_row_tz_minus_btn = nullptr;
    menu_row_tz_minus_label = nullptr;
    menu_row_tz_plus_btn = nullptr;
    menu_row_tz_plus_label = nullptr;
}

void menu_header_create(const char *text)
{
    menu_header_label = lv_label_create(menu_panel);
    lv_label_set_text(menu_header_label, text);
    lv_obj_set_style_text_font(menu_header_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_left(menu_header_label, 14, 0);
    lv_obj_set_style_pad_right(menu_header_label, 14, 0);
    lv_obj_set_style_pad_top(menu_header_label, 12, 0);
    lv_obj_set_style_pad_bottom(menu_header_label, 4, 0);
}

/* Item do menu: linha clicavel com label. `chevron` adiciona uma seta
 * discreta a direita (usa LV_SYMBOL_RIGHT, coberto pela fonte default). */
void menu_row_create(const char *text, lv_event_cb_t cb, lv_obj_t **out_row, lv_obj_t **out_label, bool chevron)
{
    lv_obj_t *row = lv_obj_create(menu_panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14_latin1, 0);

    if (out_row != nullptr) {
        *out_row = row;
    }
    if (out_label != nullptr) {
        *out_label = label;
    }

    if (chevron) {
        lv_obj_t *ch_label = lv_label_create(row);
        lv_label_set_text(ch_label, LV_SYMBOL_RIGHT);
        lv_obj_set_align(ch_label, LV_ALIGN_RIGHT_MID);
        lv_obj_set_x(ch_label, -14);
        lv_obj_set_style_text_font(ch_label, &lv_font_montserrat_14_latin1, 0);
        lv_obj_set_style_text_color(ch_label, lv_color_hex(ui_theme_get()->text_muted), 0);
    }
}

/* Reestiliza um item de tema/opcao: o ativo ganha fundo accent_soft e texto
 * accent (indicador tipo radio); o inativo fica com texto normal. */
void apply_theme_item(lv_obj_t *row, lv_obj_t *label, bool active)
{
    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_opa(row, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(pal->accent_soft), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(active ? pal->accent : pal->text), 0);
}

/* Aplica a paleta atual ao modal de confirmacao (se estiver aberto).
 * Funcao propria: o modal vive fora do ciclo do menu (menu_panel ja foi
 * destruido quando ele abre), e apply_menu_theme retorna cedo nesse caso. */
void apply_power_confirm_theme(void)
{
    if (power_confirm_card == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(power_confirm_card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(power_confirm_card, lv_color_hex(pal->border), 0);
    if (power_confirm_title_label != nullptr) {
        lv_obj_set_style_text_color(power_confirm_title_label, lv_color_hex(pal->text), 0);
    }
    if (power_confirm_msg_label != nullptr) {
        lv_obj_set_style_text_color(power_confirm_msg_label, lv_color_hex(pal->text_muted), 0);
    }
    if (power_confirm_btn_cancel != nullptr) {
        lv_obj_set_style_bg_color(power_confirm_btn_cancel, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_bg_color(power_confirm_btn_cancel, lv_color_hex(pal->border), LV_STATE_PRESSED);
    }
    if (power_confirm_btn_cancel_label != nullptr) {
        lv_obj_set_style_text_color(power_confirm_btn_cancel_label, lv_color_hex(pal->text), 0);
    }
    if (power_confirm_btn_ok != nullptr) {
        lv_obj_set_style_bg_color(power_confirm_btn_ok, lv_color_hex(pal->accent), 0);
    }
    if (power_confirm_btn_ok_label != nullptr) {
        lv_obj_set_style_text_color(power_confirm_btn_ok_label, lv_color_white(), 0);
    }
}

/* Aplica a paleta atual ao painel do menu (se estiver aberto). */
void apply_menu_theme(void)
{
    apply_power_confirm_theme();

    if (menu_panel == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(menu_panel, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(menu_panel, lv_color_hex(pal->border), 0);

    if (menu_header_label != nullptr) {
        lv_obj_set_style_text_color(menu_header_label, lv_color_hex(pal->text_muted), 0);
    }
    if (menu_row_theme_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_theme_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_ss_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_ss_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_back_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_back_label, lv_color_hex(pal->text_muted), 0);
    }
    if (menu_row_light_label != nullptr && menu_row_light != nullptr) {
        apply_theme_item(menu_row_light, menu_row_light_label, !ui_theme_is_dark());
    }
    if (menu_row_dark_label != nullptr && menu_row_dark != nullptr) {
        apply_theme_item(menu_row_dark, menu_row_dark_label, ui_theme_is_dark());
    }

    uint32_t current_ss = ui_screensaver_get_timeout();
    if (menu_row_ss_off_label != nullptr && menu_row_ss_off != nullptr) {
        apply_theme_item(menu_row_ss_off, menu_row_ss_off_label, current_ss == 0);
    }
    if (menu_row_ss_1m_label != nullptr && menu_row_ss_1m != nullptr) {
        apply_theme_item(menu_row_ss_1m, menu_row_ss_1m_label, current_ss == 60);
    }
    if (menu_row_ss_2m_label != nullptr && menu_row_ss_2m != nullptr) {
        apply_theme_item(menu_row_ss_2m, menu_row_ss_2m_label, current_ss == 120);
    }
    if (menu_row_ss_5m_label != nullptr && menu_row_ss_5m != nullptr) {
        apply_theme_item(menu_row_ss_5m, menu_row_ss_5m_label, current_ss == 300);
    }

    if (menu_row_soff_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_soff_label, lv_color_hex(pal->text), 0);
    }

    uint32_t current_soff = ui_screen_off_get_timeout();
    if (menu_row_soff_off_label != nullptr && menu_row_soff_off != nullptr) {
        apply_theme_item(menu_row_soff_off, menu_row_soff_off_label, current_soff == 0);
    }
    if (menu_row_soff_30s_label != nullptr && menu_row_soff_30s != nullptr) {
        apply_theme_item(menu_row_soff_30s, menu_row_soff_30s_label, current_soff == 30);
    }
    if (menu_row_soff_1m_label != nullptr && menu_row_soff_1m != nullptr) {
        apply_theme_item(menu_row_soff_1m, menu_row_soff_1m_label, current_soff == 60);
    }
    if (menu_row_soff_2m_label != nullptr && menu_row_soff_2m != nullptr) {
        apply_theme_item(menu_row_soff_2m, menu_row_soff_2m_label, current_soff == 120);
    }
    if (menu_row_soff_5m_label != nullptr && menu_row_soff_5m != nullptr) {
        apply_theme_item(menu_row_soff_5m, menu_row_soff_5m_label, current_soff == 300);
    }
    if (menu_row_soff_10m_label != nullptr && menu_row_soff_10m != nullptr) {
        apply_theme_item(menu_row_soff_10m, menu_row_soff_10m_label, current_soff == 600);
    }

    if (menu_row_pw_screen_label != nullptr && menu_row_pw_screen != nullptr) {
        lv_obj_set_style_text_color(menu_row_pw_screen_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_pw_reboot_label != nullptr && menu_row_pw_reboot != nullptr) {
        lv_obj_set_style_text_color(menu_row_pw_reboot_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_pw_off_label != nullptr && menu_row_pw_off != nullptr) {
        lv_obj_set_style_text_color(menu_row_pw_off_label, lv_color_hex(pal->text), 0);
    }

    if (menu_row_rotation_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_rotation_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_rotation_switch != nullptr) {
        lv_obj_set_style_bg_color(menu_row_rotation_switch, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(menu_row_rotation_switch, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(menu_row_rotation_switch, lv_color_hex(pal->accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(menu_row_rotation_switch, lv_color_hex(pal->text), LV_PART_KNOB);
    }
    if (menu_row_wifi_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_wifi_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_wifi_switch != nullptr) {
        lv_obj_set_style_bg_color(menu_row_wifi_switch, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(menu_row_wifi_switch, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(menu_row_wifi_switch, lv_color_hex(pal->accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(menu_row_wifi_switch, lv_color_hex(pal->text), LV_PART_KNOB);
    }
    if (menu_row_bt_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_bt_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_bt_switch != nullptr) {
        lv_obj_set_style_bg_color(menu_row_bt_switch, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(menu_row_bt_switch, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(menu_row_bt_switch, lv_color_hex(pal->accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(menu_row_bt_switch, lv_color_hex(pal->text), LV_PART_KNOB);
    }
    if (menu_row_brightness_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_brightness_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_brightness_val_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_brightness_val_label, lv_color_hex(pal->text_muted), 0);
    }
    if (menu_row_brightness_slider != nullptr) {
        lv_obj_set_style_bg_color(menu_row_brightness_slider, lv_color_hex(pal->border), LV_PART_MAIN);
        lv_obj_set_style_bg_color(menu_row_brightness_slider, lv_color_hex(pal->accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(menu_row_brightness_slider, lv_color_hex(pal->text), LV_PART_KNOB);
    }
    if (menu_row_volume_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_volume_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_volume_val_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_volume_val_label, lv_color_hex(pal->text_muted), 0);
    }
    if (menu_row_volume_slider != nullptr) {
        lv_obj_set_style_bg_color(menu_row_volume_slider, lv_color_hex(pal->border), LV_PART_MAIN);
        lv_obj_set_style_bg_color(menu_row_volume_slider, lv_color_hex(pal->accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(menu_row_volume_slider, lv_color_hex(pal->text), LV_PART_KNOB);
    }
    if (menu_row_tz_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_tz_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_tz_val_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_tz_val_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_tz_minus_btn != nullptr) {
        lv_obj_set_style_bg_color(menu_row_tz_minus_btn, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(menu_row_tz_minus_btn, lv_color_hex(pal->border), 0);
        lv_obj_set_style_border_width(menu_row_tz_minus_btn, 1, 0);
    }
    if (menu_row_tz_minus_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_tz_minus_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_tz_plus_btn != nullptr) {
        lv_obj_set_style_bg_color(menu_row_tz_plus_btn, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(menu_row_tz_plus_btn, lv_color_hex(pal->border), 0);
        lv_obj_set_style_border_width(menu_row_tz_plus_btn, 1, 0);
    }
    if (menu_row_tz_plus_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_tz_plus_label, lv_color_hex(pal->text), 0);
    }
}

void open_menu(menu_page_t page);

void menu_theme_cb(lv_event_t *event)
{
    (void)event;
    open_menu(MENU_PAGE_THEME);
}

void menu_screensaver_cb(lv_event_t *event)
{
    (void)event;
    open_menu(MENU_PAGE_SCREENSAVER);
}

void menu_screen_off_cb(lv_event_t *event)
{
    (void)event;
    open_menu(MENU_PAGE_SCREEN_OFF);
}

void menu_light_cb(lv_event_t *event)
{
    (void)event;
    ui_theme_set(false);
    close_menu();
}

void menu_dark_cb(lv_event_t *event)
{
    (void)event;
    ui_theme_set(true);
    close_menu();
}

void menu_ss_off_cb(lv_event_t *event)
{
    (void)event;
    ui_screensaver_set_timeout(0);
    close_menu();
}

void menu_ss_1m_cb(lv_event_t *event)
{
    (void)event;
    ui_screensaver_set_timeout(60);
    close_menu();
}

void menu_ss_2m_cb(lv_event_t *event)
{
    (void)event;
    ui_screensaver_set_timeout(120);
    close_menu();
}

void menu_ss_5m_cb(lv_event_t *event)
{
    (void)event;
    ui_screensaver_set_timeout(300);
    close_menu();
}

void menu_soff_off_cb(lv_event_t *event)
{
    (void)event;
    ui_screen_off_set_timeout(0);
    close_menu();
}

void menu_soff_30s_cb(lv_event_t *event)
{
    (void)event;
    ui_screen_off_set_timeout(30);
    close_menu();
}

void menu_soff_1m_cb(lv_event_t *event)
{
    (void)event;
    ui_screen_off_set_timeout(60);
    close_menu();
}

void menu_soff_2m_cb(lv_event_t *event)
{
    (void)event;
    ui_screen_off_set_timeout(120);
    close_menu();
}

void menu_soff_5m_cb(lv_event_t *event)
{
    (void)event;
    ui_screen_off_set_timeout(300);
    close_menu();
}

void menu_soff_10m_cb(lv_event_t *event)
{
    (void)event;
    ui_screen_off_set_timeout(600);
    close_menu();
}

void menu_back_cb(lv_event_t *event)
{
    (void)event;
    open_menu(MENU_PAGE_MAIN);
}

/* Painel de energia: desliga apenas o backlight pelo mesmo caminho do
 * timeout automatico (duplo toque desperta, brilho anterior restaurado). */
void power_screen_off_cb(lv_event_t *event)
{
    (void)event;
    close_menu();
    ui_screen_off_show();
}

void power_reboot_cb(lv_event_t *event)
{
    (void)event;
    close_menu();
    show_power_confirm(POWER_ACTION_REBOOT);
}

void power_shutdown_cb(lv_event_t *event)
{
    (void)event;
    close_menu();
    show_power_confirm(POWER_ACTION_SHUTDOWN);
}

void power_confirm_cancel_cb(lv_event_t *event)
{
    (void)event;
    close_power_confirm();
}

/* Confirma a acao pendente. Reiniciar: esp_restart(). Desligar: deep sleep
 * sem fontes de despertar configuradas (~uA); o botao fisico do Tab5
 * reinicia o sistema (boot limpo). O backlight apaga antes nos dois casos,
 * escondendo o reset do painel MIPI-DSI (evita o flash azul/branco). */
void power_confirm_ok_cb(lv_event_t *event)
{
    (void)event;
    bool reboot = (power_action == POWER_ACTION_REBOOT);
    close_power_confirm();

    if (music_player_is_playing()) {
        music_player_stop();
    }
    bsp_display_brightness_set(0);
    ui_keyboard_hide();

    if (reboot) {
        ESP_LOGI(TAG, "reiniciando o dispositivo");
        vTaskDelay(pdMS_TO_TICKS(150));
        esp_restart();
    }

    ESP_LOGI(TAG, "desligando o dispositivo (deep sleep)");
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_deep_sleep_start();
}

/* Modal de confirmacao no layer_top: overlay escuro que cancela ao tocar
 * fora e card centralizado (flex coluna) com titulo, mensagem e botoes.
 * Toda label usa montserrat_14_latin1 explicita: sem herdar a fonte default
 * (ASCII), que renderiza acentos como glifos invalidos. */
void show_power_confirm(power_action_t action)
{
    close_power_confirm();
    power_action = action;

    const ui_palette_t *pal = ui_theme_get();

    power_confirm_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(power_confirm_overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(power_confirm_overlay);
    lv_obj_set_style_bg_color(power_confirm_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(power_confirm_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(power_confirm_overlay, 0, 0);
    lv_obj_set_style_shadow_width(power_confirm_overlay, 0, 0);
    lv_obj_clear_flag(power_confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(power_confirm_overlay, power_confirm_cancel_cb, LV_EVENT_CLICKED, nullptr);

    power_confirm_card = lv_obj_create(power_confirm_overlay);
    lv_obj_set_size(power_confirm_card, 420, 210);
    lv_obj_center(power_confirm_card);
    lv_obj_set_style_bg_color(power_confirm_card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_bg_opa(power_confirm_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(power_confirm_card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(power_confirm_card, 2, 0);
    lv_obj_set_style_radius(power_confirm_card, 12, 0);
    lv_obj_set_style_shadow_width(power_confirm_card, 30, 0);
    lv_obj_set_style_shadow_opa(power_confirm_card, LV_OPA_60, 0);
    lv_obj_set_style_pad_left(power_confirm_card, 16, 0);
    lv_obj_set_style_pad_right(power_confirm_card, 16, 0);
    lv_obj_set_style_pad_top(power_confirm_card, 12, 0);
    lv_obj_set_style_pad_bottom(power_confirm_card, 12, 0);
    lv_obj_clear_flag(power_confirm_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(power_confirm_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(power_confirm_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    power_confirm_title_label = lv_label_create(power_confirm_card);
    lv_label_set_text(power_confirm_title_label, action == POWER_ACTION_REBOOT ? "Reiniciar?" : "Desligar?");
    lv_obj_set_style_text_font(power_confirm_title_label, &lv_font_montserrat_14_latin1, 0);

    power_confirm_msg_label = lv_label_create(power_confirm_card);
    lv_label_set_long_mode(power_confirm_msg_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(power_confirm_msg_label, 388);
    if (action == POWER_ACTION_REBOOT) {
        lv_label_set_text(power_confirm_msg_label, "O dispositivo será reiniciado. Deseja continuar?");
    } else {
        lv_label_set_text(power_confirm_msg_label,
                          "O dispositivo entrará em modo de baixo consumo. Use o botão físico para ligar novamente.");
    }
    lv_obj_set_style_text_font(power_confirm_msg_label, &lv_font_montserrat_14_latin1, 0);

    lv_obj_t *btn_row = lv_obj_create(power_confirm_card);
    lv_obj_set_size(btn_row, 388, 44);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_shadow_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    power_confirm_btn_cancel = lv_button_create(btn_row);
    lv_obj_set_size(power_confirm_btn_cancel, 120, 38);
    lv_obj_set_style_bg_color(power_confirm_btn_cancel, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_radius(power_confirm_btn_cancel, 8, 0);
    lv_obj_add_event_cb(power_confirm_btn_cancel, power_confirm_cancel_cb, LV_EVENT_CLICKED, nullptr);
    power_confirm_btn_cancel_label = lv_label_create(power_confirm_btn_cancel);
    lv_label_set_text(power_confirm_btn_cancel_label, "Cancelar");
    lv_obj_set_style_text_font(power_confirm_btn_cancel_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(power_confirm_btn_cancel_label);

    power_confirm_btn_ok = lv_button_create(btn_row);
    lv_obj_set_size(power_confirm_btn_ok, 120, 38);
    lv_obj_set_style_bg_color(power_confirm_btn_ok, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_radius(power_confirm_btn_ok, 8, 0);
    lv_obj_add_event_cb(power_confirm_btn_ok, power_confirm_ok_cb, LV_EVENT_CLICKED, nullptr);
    power_confirm_btn_ok_label = lv_label_create(power_confirm_btn_ok);
    lv_label_set_text(power_confirm_btn_ok_label, "Confirmar");
    lv_obj_set_style_text_font(power_confirm_btn_ok_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(power_confirm_btn_ok_label);

    apply_power_confirm_theme();
}

/* Repassa o estado do switch de rotacao para o modulo do IMU. */
void rotation_switch_cb(lv_event_t *event)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(event);
    imu_reader_set_rotation_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

/* Repassa o estado do switch de Wi-Fi para o modulo wifi_mgr. */
void wifi_switch_cb(lv_event_t *event)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(event);
    wifi_mgr_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

/* Repassa o estado do switch de Bluetooth para o modulo bt_mgr. */
void bt_switch_cb(lv_event_t *event)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(event);
    bt_mgr_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

/* Row "Rotação" com switch de liga/desliga no estilo SO. */
void menu_rotation_row_create(void)
{
    lv_obj_t *row = lv_obj_create(menu_panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);

    menu_row_rotation_label = lv_label_create(row);
    lv_label_set_text(menu_row_rotation_label, "Rotação");
    lv_obj_set_style_text_font(menu_row_rotation_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(menu_row_rotation_label, LV_ALIGN_LEFT_MID, 14, 0);

    menu_row_rotation_switch = lv_switch_create(row);
    lv_obj_set_size(menu_row_rotation_switch, 44, 24);
    lv_obj_align(menu_row_rotation_switch, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_radius(menu_row_rotation_switch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(menu_row_rotation_switch, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(menu_row_rotation_switch, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(menu_row_rotation_switch, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(menu_row_rotation_switch, rotation_switch_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    if (imu_reader_is_rotation_enabled()) {
        lv_obj_add_state(menu_row_rotation_switch, LV_STATE_CHECKED);
    }
}

/* Row "Wi-Fi" com switch de liga/desliga no estilo SO. */
void menu_wifi_row_create(void)
{
    lv_obj_t *row = lv_obj_create(menu_panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);

    menu_row_wifi_label = lv_label_create(row);
    lv_label_set_text(menu_row_wifi_label, "Wi-Fi");
    lv_obj_set_style_text_font(menu_row_wifi_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(menu_row_wifi_label, LV_ALIGN_LEFT_MID, 14, 0);

    menu_row_wifi_switch = lv_switch_create(row);
    lv_obj_set_size(menu_row_wifi_switch, 44, 24);
    lv_obj_align(menu_row_wifi_switch, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_radius(menu_row_wifi_switch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(menu_row_wifi_switch, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(menu_row_wifi_switch, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(menu_row_wifi_switch, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(menu_row_wifi_switch, wifi_switch_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    if (wifi_mgr_is_enabled()) {
        lv_obj_add_state(menu_row_wifi_switch, LV_STATE_CHECKED);
    }
}

/* Row "Bluetooth" com switch de liga/desliga no estilo SO. */
void menu_bluetooth_row_create(void)
{
    lv_obj_t *row = lv_obj_create(menu_panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);

    menu_row_bt_label = lv_label_create(row);
    lv_label_set_text(menu_row_bt_label, "Bluetooth");
    lv_obj_set_style_text_font(menu_row_bt_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(menu_row_bt_label, LV_ALIGN_LEFT_MID, 14, 0);

    menu_row_bt_switch = lv_switch_create(row);
    lv_obj_set_size(menu_row_bt_switch, 44, 24);
    lv_obj_align(menu_row_bt_switch, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_radius(menu_row_bt_switch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(menu_row_bt_switch, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(menu_row_bt_switch, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(menu_row_bt_switch, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(menu_row_bt_switch, bt_switch_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    if (bt_mgr_is_enabled()) {
        lv_obj_add_state(menu_row_bt_switch, LV_STATE_CHECKED);
    }
}

void brightness_slider_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(event);
    int val = (int)lv_slider_get_value(slider);
    if (val < DISPLAY_MIN_BRIGHTNESS) {
        val = DISPLAY_MIN_BRIGHTNESS;
    }
    if (val > DISPLAY_MAX_BRIGHTNESS) {
        val = DISPLAY_MAX_BRIGHTNESS;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        if (menu_row_brightness_val_label != nullptr) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", val);
            lv_label_set_text(menu_row_brightness_val_label, buf);
        }
        bsp_display_brightness_set(val);
    } else if (code == LV_EVENT_RELEASED) {
        display_storage_save_brightness(val);
    }
}

/* Row "Brilho" com slider horizontal no menu de configuracao. */
void menu_brightness_row_create(void)
{
    lv_obj_t *row = lv_obj_create(menu_panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Flex coluna para empilhar o header e o slider verticalmente sem sobreposicao */
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row, 8, 0);

    lv_obj_t *header_box = lv_obj_create(row);
    lv_obj_set_width(header_box, lv_pct(100));
    lv_obj_set_height(header_box, 18);
    lv_obj_set_style_bg_opa(header_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_box, 0, 0);
    lv_obj_set_style_shadow_width(header_box, 0, 0);
    lv_obj_set_style_pad_all(header_box, 0, 0);
    lv_obj_clear_flag(header_box, LV_OBJ_FLAG_SCROLLABLE);

    menu_row_brightness_label = lv_label_create(header_box);
    lv_label_set_text(menu_row_brightness_label, "Brilho");
    lv_obj_set_style_text_font(menu_row_brightness_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(menu_row_brightness_label, LV_ALIGN_LEFT_MID, 0, 0);

    int cur_br = DISPLAY_DEFAULT_BRIGHTNESS;
    display_storage_load_brightness(&cur_br);

    menu_row_brightness_val_label = lv_label_create(header_box);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", cur_br);
    lv_label_set_text(menu_row_brightness_val_label, buf);
    lv_obj_set_style_text_font(menu_row_brightness_val_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(menu_row_brightness_val_label, LV_ALIGN_RIGHT_MID, 0, 0);

    menu_row_brightness_slider = lv_slider_create(row);
    lv_obj_set_width(menu_row_brightness_slider, lv_pct(100));
    lv_obj_set_height(menu_row_brightness_slider, 8);
    lv_obj_set_style_radius(menu_row_brightness_slider, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(menu_row_brightness_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(menu_row_brightness_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(menu_row_brightness_slider, 3, LV_PART_KNOB);
    lv_slider_set_range(menu_row_brightness_slider, DISPLAY_MIN_BRIGHTNESS, DISPLAY_MAX_BRIGHTNESS);
    lv_slider_set_value(menu_row_brightness_slider, cur_br, LV_ANIM_OFF);

    lv_obj_add_event_cb(menu_row_brightness_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(menu_row_brightness_slider, brightness_slider_cb, LV_EVENT_RELEASED, nullptr);
}

void volume_slider_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(event);
    int val = (int)lv_slider_get_value(slider);
    if (val < 0) {
        val = 0;
    }
    if (val > 100) {
        val = 100;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        if (menu_row_volume_val_label != nullptr) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", val);
            lv_label_set_text(menu_row_volume_val_label, buf);
        }
        music_player_set_volume(val);
    }
}

/* Row "Volume" com slider horizontal no menu de configuracao. */
void menu_volume_row_create(void)
{
    lv_obj_t *row = lv_obj_create(menu_panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Flex coluna para empilhar o header e o slider verticalmente */
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row, 8, 0);

    lv_obj_t *header_box = lv_obj_create(row);
    lv_obj_set_width(header_box, lv_pct(100));
    lv_obj_set_height(header_box, 18);
    lv_obj_set_style_bg_opa(header_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_box, 0, 0);
    lv_obj_set_style_shadow_width(header_box, 0, 0);
    lv_obj_set_style_pad_all(header_box, 0, 0);
    lv_obj_clear_flag(header_box, LV_OBJ_FLAG_SCROLLABLE);

    menu_row_volume_label = lv_label_create(header_box);
    lv_label_set_text(menu_row_volume_label, "Volume");
    lv_obj_set_style_text_font(menu_row_volume_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(menu_row_volume_label, LV_ALIGN_LEFT_MID, 0, 0);

    int cur_vol = music_player_get_volume();

    menu_row_volume_val_label = lv_label_create(header_box);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", cur_vol);
    lv_label_set_text(menu_row_volume_val_label, buf);
    lv_obj_set_style_text_font(menu_row_volume_val_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(menu_row_volume_val_label, LV_ALIGN_RIGHT_MID, 0, 0);

    menu_row_volume_slider = lv_slider_create(row);
    lv_obj_set_width(menu_row_volume_slider, lv_pct(100));
    lv_obj_set_height(menu_row_volume_slider, 8);
    lv_obj_set_style_radius(menu_row_volume_slider, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(menu_row_volume_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(menu_row_volume_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(menu_row_volume_slider, 3, LV_PART_KNOB);
    lv_slider_set_range(menu_row_volume_slider, 0, 100);
    lv_slider_set_value(menu_row_volume_slider, cur_vol, LV_ANIM_OFF);

    lv_obj_add_event_cb(menu_row_volume_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void tz_btn_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int cur = timezone_mgr_get_offset();
    int next_val = cur + delta;
    if (next_val >= TIMEZONE_MIN_OFFSET && next_val <= TIMEZONE_MAX_OFFSET) {
        timezone_mgr_set_offset(next_val);
        if (menu_row_tz_val_label != nullptr) {
            char buf[16];
            timezone_mgr_format_offset(next_val, buf, sizeof(buf));
            lv_label_set_text(menu_row_tz_val_label, buf);
        }
        clock_update();
    }
}

/* Row "Fuso Horário" com botões [-] e [+] para ajuste rápido. */
void menu_timezone_row_create(void)
{
    lv_obj_t *row = lv_obj_create(menu_panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    lv_obj_set_style_pad_bottom(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    menu_row_tz_label = lv_label_create(row);
    lv_label_set_text(menu_row_tz_label, "Fuso Horário");
    lv_obj_set_style_text_font(menu_row_tz_label, &lv_font_montserrat_14_latin1, 0);

    lv_obj_t *ctrl_box = lv_obj_create(row);
    lv_obj_set_size(ctrl_box, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_bg_opa(ctrl_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_box, 0, 0);
    lv_obj_set_style_pad_all(ctrl_box, 0, 0);
    lv_obj_clear_flag(ctrl_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ctrl_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_box, 4, 0);

    menu_row_tz_minus_btn = lv_button_create(ctrl_box);
    lv_obj_set_size(menu_row_tz_minus_btn, 28, 28);
    lv_obj_set_style_radius(menu_row_tz_minus_btn, 6, 0);
    lv_obj_add_event_cb(menu_row_tz_minus_btn, tz_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    menu_row_tz_minus_label = lv_label_create(menu_row_tz_minus_btn);
    lv_label_set_text(menu_row_tz_minus_label, LV_SYMBOL_MINUS);
    lv_obj_center(menu_row_tz_minus_label);

    int cur_offset = timezone_mgr_get_offset();
    char tz_buf[16];
    timezone_mgr_format_offset(cur_offset, tz_buf, sizeof(tz_buf));

    menu_row_tz_val_label = lv_label_create(ctrl_box);
    lv_label_set_text(menu_row_tz_val_label, tz_buf);
    lv_obj_set_style_text_font(menu_row_tz_val_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_width(menu_row_tz_val_label, 32);
    lv_obj_set_style_text_align(menu_row_tz_val_label, LV_TEXT_ALIGN_CENTER, 0);

    menu_row_tz_plus_btn = lv_button_create(ctrl_box);
    lv_obj_set_size(menu_row_tz_plus_btn, 28, 28);
    lv_obj_set_style_radius(menu_row_tz_plus_btn, 6, 0);
    lv_obj_add_event_cb(menu_row_tz_plus_btn, tz_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    menu_row_tz_plus_label = lv_label_create(menu_row_tz_plus_btn);
    lv_label_set_text(menu_row_tz_plus_label, LV_SYMBOL_PLUS);
    lv_obj_center(menu_row_tz_plus_label);
}

void gear_click_cb(lv_event_t *event)
{
    (void)event;
    open_menu(MENU_PAGE_MAIN);
}

void power_click_cb(lv_event_t *event)
{
    (void)event;
    open_menu(MENU_PAGE_POWER);
}

/* Popover ancorado sob a engrenagem, com overlay transparente que fecha
 * ao tocar fora. `page` escolhe a pagina inicial do painel. */
void open_menu(menu_page_t page)
{
    close_menu();

    lv_obj_t *layer = lv_layer_top();

    menu_overlay = lv_obj_create(layer);
    lv_obj_set_size(menu_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(menu_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_overlay, 0, 0);
    lv_obj_set_style_shadow_width(menu_overlay, 0, 0);
    lv_obj_clear_flag(menu_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(menu_overlay, menu_overlay_cb, LV_EVENT_CLICKED, nullptr);

    menu_panel = lv_obj_create(layer);
    lv_obj_set_size(menu_panel, 230, LV_SIZE_CONTENT);
    lv_obj_set_align(menu_panel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(menu_panel, 10, UI_BAR_HEIGHT + 8);
    lv_obj_set_style_bg_opa(menu_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(menu_panel, 1, 0);
    lv_obj_set_style_radius(menu_panel, 12, 0);
    lv_obj_set_style_shadow_width(menu_panel, 12, 0);
    lv_obj_set_style_shadow_opa(menu_panel, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(menu_panel, 4, 0);
    lv_obj_clear_flag(menu_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Empilha header e itens verticalmente. */
    lv_obj_set_flex_flow(menu_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (page == MENU_PAGE_MAIN) {
        menu_header_create("Configuração");
        menu_row_create("Tema", menu_theme_cb, &menu_row_theme, &menu_row_theme_label, true);
        menu_row_create("Protetor de Tela", menu_screensaver_cb, &menu_row_ss, &menu_row_ss_label, true);
        menu_row_create("Desligar Tela", menu_screen_off_cb, &menu_row_soff, &menu_row_soff_label, true);
        menu_rotation_row_create();
        menu_wifi_row_create();
        menu_bluetooth_row_create();
        menu_brightness_row_create();
        menu_volume_row_create();
        menu_timezone_row_create();
    } else if (page == MENU_PAGE_THEME) {
        menu_header_create("Tema");
        menu_row_create("Claro", menu_light_cb, &menu_row_light, &menu_row_light_label, false);
        menu_row_create("Escuro", menu_dark_cb, &menu_row_dark, &menu_row_dark_label, false);
        menu_row_create("Voltar", menu_back_cb, &menu_row_back, &menu_row_back_label, false);
    } else if (page == MENU_PAGE_SCREENSAVER) {
        menu_header_create("Protetor de Tela");
        menu_row_create("Desativado", menu_ss_off_cb, &menu_row_ss_off, &menu_row_ss_off_label, false);
        menu_row_create("1 minuto", menu_ss_1m_cb, &menu_row_ss_1m, &menu_row_ss_1m_label, false);
        menu_row_create("2 minutos", menu_ss_2m_cb, &menu_row_ss_2m, &menu_row_ss_2m_label, false);
        menu_row_create("5 minutos", menu_ss_5m_cb, &menu_row_ss_5m, &menu_row_ss_5m_label, false);
        menu_row_create("Voltar", menu_back_cb, &menu_row_back, &menu_row_back_label, false);
    } else if (page == MENU_PAGE_SCREEN_OFF) {
        menu_header_create("Desligar Tela");
        menu_row_create("Desativado", menu_soff_off_cb, &menu_row_soff_off, &menu_row_soff_off_label, false);
        menu_row_create("30 segundos", menu_soff_30s_cb, &menu_row_soff_30s, &menu_row_soff_30s_label, false);
        menu_row_create("1 minuto", menu_soff_1m_cb, &menu_row_soff_1m, &menu_row_soff_1m_label, false);
        menu_row_create("2 minutos", menu_soff_2m_cb, &menu_row_soff_2m, &menu_row_soff_2m_label, false);
        menu_row_create("5 minutos", menu_soff_5m_cb, &menu_row_soff_5m, &menu_row_soff_5m_label, false);
        menu_row_create("10 minutos", menu_soff_10m_cb, &menu_row_soff_10m, &menu_row_soff_10m_label, false);
        menu_row_create("Voltar", menu_back_cb, &menu_row_back, &menu_row_back_label, false);
    } else if (page == MENU_PAGE_POWER) {
        menu_header_create("Energia");
        menu_row_create("Desligar Tela", power_screen_off_cb, &menu_row_pw_screen, &menu_row_pw_screen_label, false);
        menu_row_create("Reiniciar", power_reboot_cb, &menu_row_pw_reboot, &menu_row_pw_reboot_label, false);
        menu_row_create("Desligar", power_shutdown_cb, &menu_row_pw_off, &menu_row_pw_off_label, false);
    }

    apply_menu_theme();
}

void clock_update(void)
{
    if (clock_label == nullptr) {
        return;
    }

    struct tm t;
    if (timezone_mgr_get_localtime(&t) != nullptr) {
        char buf[32];
        strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &t);
        lv_label_set_text(clock_label, buf);
    }
}

void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    clock_update();
}

} // namespace

void ui_bar_refresh_theme(void)
{
    if (bar == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(bar, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(pal->border), 0);

    if (power_label != nullptr) {
        lv_obj_set_style_text_color(power_label, lv_color_hex(pal->text), 0);
        lv_obj_set_style_text_color(power_label, lv_color_hex(pal->accent), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(power_label, lv_color_hex(pal->accent), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(power_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }

    if (gear_label != nullptr) {
        lv_obj_set_style_text_color(gear_label, lv_color_hex(pal->text), 0);
        lv_obj_set_style_text_color(gear_label, lv_color_hex(pal->accent), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(gear_label, lv_color_hex(pal->accent), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(gear, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }

    if (clock_label != nullptr) {
        lv_obj_set_style_text_color(clock_label, lv_color_hex(pal->text), 0);
    }

    apply_menu_theme();

    /* O tema tambem alcanca as telas do shell (desktop e apps). */
    ui_shell_refresh_theme();
}

void ui_bar_init(lv_obj_t *parent)
{
    /* A barra vive no layer top; o fundo da tela e do shell/tema. */
    bar = lv_obj_create(parent);
    lv_obj_set_size(bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_set_align(bar, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Layout flex: energia e engrenagem a esquerda, espaco flexivel,
     * status e relogio a direita. */
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Botao de energia: abre o painel Desligar Tela / Reiniciar / Desligar.
     * Primeiro item da barra (ponta esquerda), antes das configuracoes.
     * Pad/margens iguais aos icones de status (6/4 + 2px) para o espacamento
     * ficar coerente com o resto da barra. */
    power_btn = lv_obj_create(bar);
    lv_obj_set_size(power_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(power_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(power_btn, 0, 0);
    lv_obj_set_style_shadow_width(power_btn, 0, 0);
    lv_obj_set_style_radius(power_btn, 8, 0);
    lv_obj_set_style_pad_left(power_btn, 6, 0);
    lv_obj_set_style_pad_right(power_btn, 6, 0);
    lv_obj_set_style_pad_top(power_btn, 4, 0);
    lv_obj_set_style_pad_bottom(power_btn, 4, 0);
    lv_obj_set_style_margin_left(power_btn, 6, 0);
    lv_obj_set_style_margin_right(power_btn, 2, 0);
    /* Sem scroll: remove os scrollbars e o arraste do botao. */
    lv_obj_clear_flag(power_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(power_btn, power_click_cb, LV_EVENT_CLICKED, nullptr);

    power_label = lv_label_create(power_btn);
    lv_label_set_text(power_label, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(power_label, &lv_font_montserrat_14_latin1, 0);

    /* Engrenagem: abre o menu de configuracao. Mesmo pad dos icones de
     * status para manter o ritmo de espacamento da barra. */
    gear = lv_obj_create(bar);
    lv_obj_set_size(gear, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(gear, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gear, 0, 0);
    lv_obj_set_style_shadow_width(gear, 0, 0);
    lv_obj_set_style_radius(gear, 8, 0);
    lv_obj_set_style_pad_left(gear, 6, 0);
    lv_obj_set_style_pad_right(gear, 6, 0);
    lv_obj_set_style_pad_top(gear, 4, 0);
    lv_obj_set_style_pad_bottom(gear, 4, 0);
    /* Sem scroll: remove os scrollbars e o arraste da engrenagem. */
    lv_obj_clear_flag(gear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(gear, gear_click_cb, LV_EVENT_CLICKED, nullptr);

    gear_label = lv_label_create(gear);
    lv_label_set_text(gear_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gear_label, &lv_font_montserrat_14_latin1, 0);

    /* Espacador flexivel empurra badge e relogio para a direita. */
    lv_obj_t *spacer = lv_obj_create(bar);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_shadow_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    /* Badge de orientacao integrado a barra (compacto). */
    ui_status_init(bar);

    /* Relogio no formato brasileiro (dd/mm/aaaa hh:mm). Fonte monoespacada
     * + largura fixa + alinhamento a direita: nenhuma mudanca de valor
     * desloca o texto ou empurra os icones. */
    clock_label = lv_label_create(bar);
    lv_obj_set_style_text_font(clock_label, &lv_font_jetbrains_mono_14_clock, 0);
    lv_point_t clock_max = {0, 0};
    lv_text_get_size(&clock_max, "88/88/8888 88:88", &lv_font_jetbrains_mono_14_clock, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    lv_obj_set_width(clock_label, clock_max.x);
    lv_obj_set_style_text_align(clock_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_margin_right(clock_label, 12, 0);
    lv_timer_create(clock_timer_cb, 1000, nullptr);
    clock_update();

    ui_bar_refresh_theme();
}

void ui_bar_set_visible(bool visible)
{
    if (bar == nullptr) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        close_menu();
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
}
