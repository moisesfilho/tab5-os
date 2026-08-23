#include "ui_screen_off.h"
#include "ui_screensaver.h"
#include "ui_bar.h"
#include "ui_keyboard.h"
#include "ui_mouse.h"
#include "display_storage.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <stdint.h>

static const char *TAG = "tab5_screen_off";

#define NVS_NAMESPACE "tab5"
#define NVS_KEY_TIMEOUT "screen_off"
#define DEFAULT_TIMEOUT_SEC 120
#define DOUBLE_CLICK_MS 400

namespace {

lv_obj_t *s_off_scr = nullptr;
lv_obj_t *s_prev_scr = nullptr;

bool s_active = false;
uint32_t s_timeout_sec = DEFAULT_TIMEOUT_SEC;
int s_prev_brightness = DISPLAY_DEFAULT_BRIGHTNESS;

uint32_t s_last_click_tick = 0;

void load_nvs_timeout(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint32_t val = 0;
        if (nvs_get_u32(nvs, NVS_KEY_TIMEOUT, &val) == ESP_OK) {
            s_timeout_sec = val;
            ESP_LOGI(TAG, "timeout carregado da NVS: %lu s", (unsigned long)s_timeout_sec);
        }
        nvs_close(nvs);
    }
}

void save_nvs_timeout(uint32_t sec)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, NVS_KEY_TIMEOUT, sec);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "timeout salvo na NVS: %lu s", (unsigned long)sec);
    }
}

/* Desperta apenas com duplo toque na tela. O primeiro toque é engolido
 * (evita clique fantasma no app por baixo) e apenas registra o instante. */
void off_scr_click_cb(lv_event_t *event)
{
    (void)event;
    uint32_t now = lv_tick_get();
    if (s_last_click_tick != 0 && (now - s_last_click_tick) <= DOUBLE_CLICK_MS) {
        s_last_click_tick = 0;
        ui_screen_off_hide();
    } else {
        s_last_click_tick = now;
    }
}

} // namespace

void ui_screen_off_init(void)
{
    load_nvs_timeout();

    s_off_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_off_scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_off_scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_off_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_off_scr, 0, 0);
    lv_obj_set_style_pad_all(s_off_scr, 0, 0);
    lv_obj_clear_flag(s_off_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_off_scr, off_scr_click_cb, LV_EVENT_CLICKED, nullptr);

    ESP_LOGI(TAG, "módulo inicializado (timeout=%lu s, duplo toque=%u ms)", (unsigned long)s_timeout_sec,
             (unsigned)DOUBLE_CLICK_MS);
}

void ui_screen_off_show(void)
{
    if (s_active) {
        return;
    }

    /* Se o screensaver estiver ativo, o desligamento assume: esconde o
     * protetor (restaura a tela do app) antes de apagar o backlight. */
    if (ui_screensaver_is_active()) {
        ui_screensaver_hide();
    }

    lv_obj_t *current = lv_disp_get_scr_act(NULL);
    if (current != nullptr && current != s_off_scr) {
        s_prev_scr = current;
    }

    s_active = true;

    display_storage_load_brightness(&s_prev_brightness);
    bsp_display_brightness_set(0);

    ui_bar_set_visible(false);
    ui_keyboard_hide();
    ui_mouse_set_cursor_visible(false);

    lv_disp_load_scr(s_off_scr);
    ESP_LOGI(TAG, "tela desligada (brilho anterior=%d%%)", s_prev_brightness);
}

void ui_screen_off_hide(void)
{
    if (!s_active) {
        return;
    }

    s_active = false;
    s_last_click_tick = 0;

    bsp_display_brightness_set(s_prev_brightness);

    ui_bar_set_visible(true);
    ui_mouse_set_cursor_visible(true);

    lv_obj_t *target = (s_prev_scr != nullptr && s_prev_scr != s_off_scr) ? s_prev_scr : lv_disp_get_scr_act(NULL);
    if (target != nullptr && target != s_off_scr) {
        lv_disp_load_scr(target);
    }

    lv_display_trigger_activity(NULL);
    ESP_LOGI(TAG, "tela religada (brilho=%d%%)", s_prev_brightness);
}

bool ui_screen_off_is_active(void)
{
    return s_active;
}

void ui_screen_off_set_timeout(uint32_t seconds)
{
    s_timeout_sec = seconds;
    save_nvs_timeout(seconds);
    lv_display_trigger_activity(NULL);
}

uint32_t ui_screen_off_get_timeout(void)
{
    return s_timeout_sec;
}

void ui_screen_off_check_inactivity(void)
{
    if (s_timeout_sec == 0 || s_active) {
        return;
    }

    uint32_t inactive_ms = lv_display_get_inactive_time(NULL);
    if (inactive_ms >= (s_timeout_sec * 1000)) {
        ESP_LOGI(TAG, "inatividade detectada (%lu ms >= %lu ms) -> desligando", (unsigned long)inactive_ms,
                 (unsigned long)(s_timeout_sec * 1000));
        ui_screen_off_show();
    }
}

void ui_screen_off_wake_up(void)
{
    if (s_active) {
        ui_screen_off_hide();
    } else {
        lv_display_trigger_activity(NULL);
    }
}
