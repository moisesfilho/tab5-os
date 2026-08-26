#include "ui_screensaver.h"
#include "ui_bar.h"
#include "ui_keyboard.h"
#include "ui_mouse.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "music_player.h"
#include "timezone_mgr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "tab5_screensaver";

#define NVS_NAMESPACE "tab5"
#define NVS_KEY_SS_TIMEOUT "ss_timeout"
#define DEFAULT_TIMEOUT_SEC 120
#define MARGIN_PX 20
#define TRACK_TITLE_MAX 128

namespace {

lv_obj_t *s_screensaver_scr = nullptr;
lv_obj_t *s_prev_scr = nullptr;
lv_obj_t *s_box = nullptr;
lv_obj_t *s_version_label = nullptr;
lv_obj_t *s_time_label = nullptr;
lv_obj_t *s_date_label = nullptr;
lv_obj_t *s_track_label = nullptr;

char s_last_track_title[TRACK_TITLE_MAX] = "";
int32_t s_track_max_w = 0;

lv_timer_t *s_clock_timer = nullptr;
lv_timer_t *s_reloc_timer = nullptr;

bool s_is_active = false;
uint32_t s_timeout_sec = DEFAULT_TIMEOUT_SEC;

const char *WEEKDAYS_PT[7] = {
    "Domingo", "Segunda-feira", "Terça-feira", "Quarta-feira", "Quinta-feira", "Sexta-feira", "Sábado",
};

const char *MONTHS_PT[12] = {
    "Janeiro", "Fevereiro", "Março",    "Abril",   "Maio",     "Junho",
    "Julho",   "Agosto",    "Setembro", "Outubro", "Novembro", "Dezembro",
};

void load_nvs_timeout(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint32_t val = 0;
        if (nvs_get_u32(nvs, NVS_KEY_SS_TIMEOUT, &val) == ESP_OK) {
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
        nvs_set_u32(nvs, NVS_KEY_SS_TIMEOUT, sec);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "timeout salvo na NVS: %lu s", (unsigned long)sec);
    }
}

void update_clock_and_date(void)
{
    if (s_time_label == nullptr || s_date_label == nullptr) {
        return;
    }

    struct tm t;
    if (timezone_mgr_get_localtime(&t) == nullptr) {
        return;
    }

    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    lv_label_set_text(s_time_label, time_buf);

    int wday = t.tm_wday;
    if (wday < 0 || wday > 6) {
        wday = 0;
    }

    int mon = t.tm_mon;
    if (mon < 0 || mon > 11) {
        mon = 0;
    }

    char date_buf[64];
    snprintf(date_buf, sizeof(date_buf), "%s, %d de %s de %04d", WEEKDAYS_PT[wday], t.tm_mday, MONTHS_PT[mon],
             t.tm_year + 1900);
    lv_label_set_text(s_date_label, date_buf);
}

void relocate_box(void)
{
    if (s_box == nullptr || s_screensaver_scr == nullptr) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    if (disp == nullptr) {
        return;
    }

    int32_t screen_w = lv_display_get_horizontal_resolution(disp);
    int32_t screen_h = lv_display_get_vertical_resolution(disp);

    lv_obj_update_layout(s_box);
    int32_t box_w = lv_obj_get_width(s_box);
    int32_t box_h = lv_obj_get_height(s_box);

    if (box_w <= 0) {
        box_w = 260;
    }
    if (box_h <= 0) {
        box_h = 90;
    }

    int32_t max_x = screen_w - box_w - MARGIN_PX;
    int32_t max_y = screen_h - box_h - MARGIN_PX;

    if (max_x < MARGIN_PX) {
        max_x = MARGIN_PX;
    }
    if (max_y < MARGIN_PX) {
        max_y = MARGIN_PX;
    }

    int32_t range_x = max_x - MARGIN_PX + 1;
    int32_t range_y = max_y - MARGIN_PX + 1;

    int32_t new_x = MARGIN_PX + (int32_t)(esp_random() % (uint32_t)range_x);
    int32_t new_y = MARGIN_PX + (int32_t)(esp_random() % (uint32_t)range_y);

    lv_obj_set_pos(s_box, new_x, new_y);
    ESP_LOGD(TAG, "box relocado para (%ld, %ld) [tela: %ldx%ld, box: %ldx%ld]", (long)new_x, (long)new_y,
             (long)screen_w, (long)screen_h, (long)box_w, (long)box_h);
}

int32_t track_max_width(void)
{
    lv_display_t *disp = lv_display_get_default();
    int32_t w = (disp != nullptr) ? lv_display_get_horizontal_resolution(disp) : 1280;
    return (w * 9) / 10;
}

void update_track_label(void)
{
    if (s_track_label == nullptr) {
        return;
    }

    int32_t max_w = track_max_width();
    if (max_w != s_track_max_w) {
        s_track_max_w = max_w;
        lv_obj_set_width(s_track_label, max_w);
    }

    music_player_status_t status = {};
    music_player_get_status(&status);

    if (status.state != MUSIC_PLAYER_STATE_PLAYING || status.current_filepath[0] == '\0') {
        if (s_last_track_title[0] != '\0') {
            s_last_track_title[0] = '\0';
            lv_obj_add_flag(s_track_label, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    const char *basename = strrchr(status.current_filepath, '/');
    basename = (basename != nullptr) ? basename + 1 : status.current_filepath;

    const char *dot = strrchr(basename, '.');
    size_t len = (dot != nullptr && dot != basename) ? (size_t)(dot - basename) : strlen(basename);
    if (len >= sizeof(s_last_track_title)) {
        len = sizeof(s_last_track_title) - 1;
    }

    char title[TRACK_TITLE_MAX];
    snprintf(title, sizeof(title), LV_SYMBOL_AUDIO " %.*s", (int)len, basename);

    if (strcmp(title, s_last_track_title) != 0) {
        snprintf(s_last_track_title, sizeof(s_last_track_title), "%s", title);
        lv_label_set_text(s_track_label, title);
    }
    lv_obj_clear_flag(s_track_label, LV_OBJ_FLAG_HIDDEN);
}

void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_is_active) {
        update_clock_and_date();
        update_track_label();
    }
}

void reloc_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_is_active) {
        relocate_box();
    }
}

void screensaver_click_cb(lv_event_t *event)
{
    (void)event;
    ui_screensaver_hide();
}

} // namespace

void ui_screensaver_init(void)
{
    load_nvs_timeout();
    s_prev_scr = lv_disp_get_scr_act(NULL);

    s_screensaver_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_screensaver_scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_screensaver_scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_screensaver_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screensaver_scr, 0, 0);
    lv_obj_set_style_pad_all(s_screensaver_scr, 0, 0);
    lv_obj_clear_flag(s_screensaver_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screensaver_scr, screensaver_click_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(
        s_screensaver_scr,
        [](lv_event_t *e) {
            (void)e;
            if (s_is_active) {
                relocate_box();
            }
        },
        LV_EVENT_SIZE_CHANGED, nullptr);

    s_box = lv_obj_create(s_screensaver_scr);
    lv_obj_set_size(s_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_box, 0, 0);
    lv_obj_set_style_shadow_width(s_box, 0, 0);
    lv_obj_set_style_pad_all(s_box, 0, 0);
    lv_obj_clear_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_box, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_set_flex_flow(s_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_version_label = lv_label_create(s_box);
    lv_label_set_text(s_version_label, "tab5-os v0.4.0");
    lv_obj_set_style_text_font(s_version_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_set_style_text_color(s_version_label, lv_color_hex(0xA0A0A0), 0);
    lv_obj_set_style_pad_bottom(s_version_label, 4, 0);
    lv_obj_add_flag(s_version_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_time_label = lv_label_create(s_box);
    lv_label_set_text(s_time_label, "00:00:00");
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_56_latin1, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xFFFFFFFF), 0);
    lv_obj_set_style_pad_bottom(s_time_label, 4, 0);
    lv_obj_add_flag(s_time_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_date_label = lv_label_create(s_box);
    lv_label_set_text(s_date_label, "");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(0xD0D0D0), 0);
    lv_obj_add_flag(s_date_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    const ui_palette_t *pal = ui_theme_get();
    s_track_label = lv_label_create(s_box);
    lv_label_set_text(s_track_label, "");
    lv_obj_set_style_text_font(s_track_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_set_style_text_color(s_track_label, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_pad_top(s_track_label, 8, 0);
    lv_label_set_long_mode(s_track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_track_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_track_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_track_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_clock_timer = lv_timer_create(clock_timer_cb, 1000, nullptr);
    s_reloc_timer = lv_timer_create(reloc_timer_cb, 30000, nullptr);

    lv_timer_pause(s_clock_timer);
    lv_timer_pause(s_reloc_timer);

    ESP_LOGI(TAG, "módulo inicializado (timeout=%lu s)", (unsigned long)s_timeout_sec);
}

void ui_screensaver_show(void)
{
    if (s_is_active) {
        return;
    }

    lv_obj_t *current = lv_disp_get_scr_act(NULL);
    if (current != s_screensaver_scr && current != nullptr) {
        s_prev_scr = current;
    }

    s_is_active = true;
    ui_bar_set_visible(false);
    ui_keyboard_hide();
    ui_mouse_set_cursor_visible(false);

    update_clock_and_date();
    update_track_label();
    relocate_box();

    lv_timer_resume(s_clock_timer);
    lv_timer_resume(s_reloc_timer);

    lv_disp_load_scr(s_screensaver_scr);
    ESP_LOGI(TAG, "screensaver ativado");
}

void ui_screensaver_hide(void)
{
    if (!s_is_active) {
        return;
    }

    s_is_active = false;
    lv_timer_pause(s_clock_timer);
    lv_timer_pause(s_reloc_timer);

    ui_bar_set_visible(true);
    ui_mouse_set_cursor_visible(true);
    lv_display_trigger_activity(NULL);

    lv_obj_t *target =
        (s_prev_scr != nullptr && s_prev_scr != s_screensaver_scr) ? s_prev_scr : lv_disp_get_scr_act(NULL);
    if (target != nullptr && target != s_screensaver_scr) {
        lv_disp_load_scr(target);
    }
    ESP_LOGI(TAG, "screensaver desativado (retornando a %p)", target);
}

bool ui_screensaver_is_active(void)
{
    return s_is_active;
}

void ui_screensaver_set_timeout(uint32_t seconds)
{
    s_timeout_sec = seconds;
    save_nvs_timeout(seconds);
    lv_display_trigger_activity(NULL);
}

uint32_t ui_screensaver_get_timeout(void)
{
    return s_timeout_sec;
}

void ui_screensaver_check_inactivity(void)
{
    if (s_timeout_sec == 0 || s_is_active) {
        return;
    }

    uint32_t inactive_ms = lv_display_get_inactive_time(NULL);
    if (inactive_ms >= (s_timeout_sec * 1000)) {
        ESP_LOGI(TAG, "inatividade detectada (%lu ms >= %lu ms) -> ativando", (unsigned long)inactive_ms,
                 (unsigned long)(s_timeout_sec * 1000));
        ui_screensaver_show();
    }
}

void ui_screensaver_wake_up(void)
{
    if (s_is_active) {
        ui_screensaver_hide();
    } else {
        lv_display_trigger_activity(NULL);
    }
}
