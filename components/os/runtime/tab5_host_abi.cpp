/**
 * @file tab5_host_abi.cpp
 * @brief Implementação da Tabela de Símbolos Nativos e Dispatcher do Host
 */

#include "tab5_host_abi.h"
#include "tab5_storage_sandbox.h"
#include "tab5_ui_host.h"
#include "tab5_sys_host.h"
#include "tab5_lifecycle_host.h"
#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM) || defined(LV_LVGL_H_INCLUDE_SIMPLE) || defined(LV_CONF_INCLUDE_SIMPLE) ||                   \
    defined(TAB5_SIMULATOR)
#include "lvgl.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#define HAVE_LVGL 1
#else
#define HAVE_LVGL 0
#endif

#if defined(ESP_PLATFORM)
#include "http_file_server.h"
#include "audio_recorder.h"
#include "terminal_cmd.h"
#include "music_player.h"
#include "wifi_mgr.h"
#include "bt_mgr.h"
#include "esp_wifi.h"
#endif

static tab5_app_context_t *s_active_app_ctx = nullptr;

tab5_err_t tab5_host_abi_init(void)
{
    s_active_app_ctx = nullptr;
    return TAB5_OK;
}

tab5_err_t tab5_host_set_active_app(tab5_app_context_t *ctx)
{
    s_active_app_ctx = ctx;
    return TAB5_OK;
}

tab5_app_context_t *tab5_host_get_active_app(void)
{
    return s_active_app_ctx;
}

void tab5_host_clear_active_app(void)
{
    s_active_app_ctx = nullptr;
}

bool tab5_host_has_permission(uint32_t permission_flag)
{
    if (s_active_app_ctx == nullptr) {
        return false;
    }
    return (s_active_app_ctx->permissions & permission_flag) != 0;
}

/* ========================================================================= */
/* Implementações dos Símbolos do SDK                                         */
/* ========================================================================= */

extern "C" {

tab5_err_t tab5_lifecycle_register(const tab5_lifecycle_callbacks_t *cbs)
{
    if (cbs == nullptr || s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    s_active_app_ctx->lifecycle = *cbs;
    return TAB5_OK;
}

tab5_ui_obj_t tab5_ui_get_screen(void)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_UI_INVALID_OBJ;
    }
    return tab5_ui_host_register_obj(s_active_app_ctx->root_screen);
}

tab5_err_t tab5_ui_app_bar_set_title(const char *title)
{
    if (title == nullptr || s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    if (s_active_app_ctx->app_bar != nullptr) {
        lv_obj_t *bar = (lv_obj_t *)s_active_app_ctx->app_bar;
        lv_obj_t *lbl = lv_obj_get_child(bar, 0);
        if (lbl != nullptr) {
            lv_label_set_text(lbl, title);
        }
    }
#endif
    strncpy(s_active_app_ctx->app_name, title, sizeof(s_active_app_ctx->app_name) - 1);
    s_active_app_ctx->app_name[sizeof(s_active_app_ctx->app_name) - 1] = '\0';
    return TAB5_OK;
}

tab5_ui_obj_t tab5_ui_app_bar_add_action_button(const char *symbol_or_text, void (*on_click)(void *user_data),
                                                void *user_data)
{
    if (s_active_app_ctx == nullptr || symbol_or_text == nullptr) {
        return TAB5_UI_INVALID_OBJ;
    }
#if HAVE_LVGL
    if (s_active_app_ctx->app_bar != nullptr) {
        lv_obj_t *bar = (lv_obj_t *)s_active_app_ctx->app_bar;
        lv_obj_t *actions_cont = lv_obj_get_child(bar, 1);
        if (actions_cont == nullptr) {
            actions_cont = bar;
        }

        const char *sym = symbol_or_text;
        static const struct {
            const char *name;
            const char *glyph;
        } sym_map[] = {
            {"LV_SYMBOL_AUDIO", LV_SYMBOL_AUDIO},
            {"LV_SYMBOL_VIDEO", LV_SYMBOL_VIDEO},
            {"LV_SYMBOL_LIST", LV_SYMBOL_LIST},
            {"LV_SYMBOL_OK", LV_SYMBOL_OK},
            {"LV_SYMBOL_CLOSE", LV_SYMBOL_CLOSE},
            {"LV_SYMBOL_POWER", LV_SYMBOL_POWER},
            {"LV_SYMBOL_SETTINGS", LV_SYMBOL_SETTINGS},
            {"LV_SYMBOL_HOME", LV_SYMBOL_HOME},
            {"LV_SYMBOL_DOWNLOAD", LV_SYMBOL_DOWNLOAD},
            {"LV_SYMBOL_DRIVE", LV_SYMBOL_DRIVE},
            {"LV_SYMBOL_REFRESH", LV_SYMBOL_REFRESH},
            {"LV_SYMBOL_MUTE", LV_SYMBOL_MUTE},
            {"LV_SYMBOL_VOLUME_MID", LV_SYMBOL_VOLUME_MID},
            {"LV_SYMBOL_VOLUME_MAX", LV_SYMBOL_VOLUME_MAX},
            {"LV_SYMBOL_IMAGE", LV_SYMBOL_IMAGE},
            {"LV_SYMBOL_PREV", LV_SYMBOL_PREV},
            {"LV_SYMBOL_PLAY", LV_SYMBOL_PLAY},
            {"LV_SYMBOL_PAUSE", LV_SYMBOL_PAUSE},
            {"LV_SYMBOL_STOP", LV_SYMBOL_STOP},
            {"LV_SYMBOL_NEXT", LV_SYMBOL_NEXT},
            {"LV_SYMBOL_LEFT", LV_SYMBOL_LEFT},
            {"LV_SYMBOL_RIGHT", LV_SYMBOL_RIGHT},
            {"LV_SYMBOL_PLUS", LV_SYMBOL_PLUS},
            {"LV_SYMBOL_MINUS", LV_SYMBOL_MINUS},
            {"LV_SYMBOL_EYE_OPEN", LV_SYMBOL_EYE_OPEN},
            {"LV_SYMBOL_EYE_CLOSE", LV_SYMBOL_EYE_CLOSE},
            {"LV_SYMBOL_WARNING", LV_SYMBOL_WARNING},
            {"LV_SYMBOL_SHUFFLE", LV_SYMBOL_SHUFFLE},
            {"LV_SYMBOL_UP", LV_SYMBOL_UP},
            {"LV_SYMBOL_DOWN", LV_SYMBOL_DOWN},
            {"LV_SYMBOL_LOOP", LV_SYMBOL_LOOP},
            {"LV_SYMBOL_DIRECTORY", LV_SYMBOL_DIRECTORY},
            {"LV_SYMBOL_UPLOAD", LV_SYMBOL_UPLOAD},
            {"LV_SYMBOL_CALL", LV_SYMBOL_CALL},
            {"LV_SYMBOL_CUT", LV_SYMBOL_CUT},
            {"LV_SYMBOL_COPY", LV_SYMBOL_COPY},
            {"LV_SYMBOL_SAVE", LV_SYMBOL_SAVE},
            {"LV_SYMBOL_BARS", LV_SYMBOL_BARS},
            {"LV_SYMBOL_ENVELOPE", LV_SYMBOL_ENVELOPE},
            {"LV_SYMBOL_CHARGE", LV_SYMBOL_CHARGE},
            {"LV_SYMBOL_BELL", LV_SYMBOL_BELL},
            {"LV_SYMBOL_KEYBOARD", LV_SYMBOL_KEYBOARD},
            {"LV_SYMBOL_FILE", LV_SYMBOL_FILE},
            {"LV_SYMBOL_WIFI", LV_SYMBOL_WIFI},
            {"LV_SYMBOL_BLUETOOTH", LV_SYMBOL_BLUETOOTH},
            {"LV_SYMBOL_TRASH", LV_SYMBOL_TRASH},
            {"LV_SYMBOL_EDIT", LV_SYMBOL_EDIT},
            {"LV_SYMBOL_BACKSPACE", LV_SYMBOL_BACKSPACE},
        };
        for (const auto &entry : sym_map) {
            if (strcmp(symbol_or_text, entry.name) == 0) {
                sym = entry.glyph;
                break;
            }
        }

        lv_obj_t *btn = lv_button_create(actions_cont);
        lv_obj_set_size(btn, 44, 36);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        struct action_cb_data_t {
            void (*cb)(void *);
            void *data;
            bool is_wasm;
        };
        auto *cb_data = new action_cb_data_t{on_click, user_data, s_active_app_ctx->is_wasm};
        lv_obj_add_event_cb(
            btn,
            [](lv_event_t *e) {
                auto *d = (action_cb_data_t *)lv_event_get_user_data(e);
                if (d && !d->is_wasm && d->cb) {
                    d->cb(d->data);
                }
            },
            LV_EVENT_CLICKED, cb_data);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, sym != nullptr ? sym : "");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_center(lbl);
        return tab5_ui_host_register_obj(btn);
    }
#endif
    return 0x3000;
}

tab5_ui_obj_t tab5_ui_get_main_textarea(void)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_UI_INVALID_OBJ;
    }
    return tab5_ui_host_register_obj(s_active_app_ctx->content_area);
}

tab5_err_t tab5_ui_textarea_set_text(tab5_ui_obj_t ta, const char *text)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
    return tab5_ui_host_textarea_set_text(obj != nullptr ? obj : (void *)(uintptr_t)ta, text);
}

const char *tab5_ui_textarea_get_text(tab5_ui_obj_t ta)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
    return tab5_ui_host_textarea_get_text(obj != nullptr ? obj : (void *)(uintptr_t)ta);
}

tab5_err_t tab5_ui_textarea_set_placeholder(tab5_ui_obj_t ta, const char *placeholder)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
    return tab5_ui_host_textarea_set_placeholder(obj != nullptr ? obj : (void *)(uintptr_t)ta, placeholder);
}

tab5_err_t tab5_ui_textarea_set_cursor_pos(tab5_ui_obj_t ta, int32_t pos)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
    return tab5_ui_host_textarea_set_cursor_pos(obj != nullptr ? obj : (void *)(uintptr_t)ta, pos);
}

int32_t tab5_ui_textarea_get_cursor_pos(tab5_ui_obj_t ta)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
    return tab5_ui_host_textarea_get_cursor_pos(obj != nullptr ? obj : (void *)(uintptr_t)ta);
}

tab5_err_t tab5_ui_textarea_set_password_mode(tab5_ui_obj_t ta, bool password_mode)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
    return tab5_ui_host_textarea_set_password_mode(obj != nullptr ? obj : (void *)(uintptr_t)ta, password_mode);
}

tab5_err_t tab5_ui_keyboard_show(tab5_ui_obj_t target_textarea)
{
    void *obj = tab5_ui_host_get_lv_obj(target_textarea);
    return tab5_ui_host_keyboard_show(obj != nullptr ? obj : (void *)(uintptr_t)target_textarea);
}

tab5_err_t tab5_ui_keyboard_hide(void)
{
    return tab5_ui_host_keyboard_hide();
}

bool tab5_ui_keyboard_is_visible(void)
{
    return tab5_ui_host_keyboard_is_visible();
}

tab5_err_t tab5_ui_show_toast(const char *message, uint32_t duration_ms)
{
    return tab5_ui_host_show_toast(message, duration_ms);
}

/* ========================================================================= */
/* Widgets e Layouts Genéricos                                               */
/* ========================================================================= */

tab5_ui_obj_t tab5_ui_container_create(tab5_ui_obj_t parent)
{
    return tab5_ui_host_container_create(parent);
}

tab5_err_t tab5_ui_obj_set_size(tab5_ui_obj_t obj, int32_t w, int32_t h)
{
    return tab5_ui_host_obj_set_size(obj, w, h);
}

tab5_err_t tab5_ui_obj_set_align(tab5_ui_obj_t obj, tab5_ui_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    return tab5_ui_host_obj_set_align(obj, align, x_ofs, y_ofs);
}

tab5_err_t tab5_ui_obj_set_flex_flow(tab5_ui_obj_t obj, tab5_ui_flex_flow_t flow)
{
    return tab5_ui_host_obj_set_flex_flow(obj, flow);
}

tab5_err_t tab5_ui_obj_set_pad(tab5_ui_obj_t obj, int32_t pad_all)
{
    return tab5_ui_host_obj_set_pad(obj, pad_all);
}

tab5_err_t tab5_ui_obj_set_gap(tab5_ui_obj_t obj, int32_t gap)
{
    return tab5_ui_host_obj_set_gap(obj, gap);
}

tab5_ui_obj_t tab5_ui_label_create(tab5_ui_obj_t parent, const char *text)
{
    return tab5_ui_host_label_create(parent, text);
}

tab5_err_t tab5_ui_label_set_text(tab5_ui_obj_t obj, const char *text)
{
    return tab5_ui_host_label_set_text(obj, text);
}

tab5_ui_obj_t tab5_ui_btn_create(tab5_ui_obj_t parent, const char *label_or_symbol)
{
    return tab5_ui_host_btn_create(parent, label_or_symbol);
}

tab5_ui_obj_t tab5_ui_switch_create(tab5_ui_obj_t parent)
{
    return tab5_ui_host_switch_create(parent);
}

tab5_err_t tab5_ui_switch_set_state(tab5_ui_obj_t obj, bool checked)
{
    return tab5_ui_host_switch_set_state(obj, checked);
}

bool tab5_ui_switch_get_state(tab5_ui_obj_t obj)
{
    return tab5_ui_host_switch_get_state(obj);
}

tab5_ui_obj_t tab5_ui_slider_create(tab5_ui_obj_t parent, int32_t min, int32_t max)
{
    return tab5_ui_host_slider_create(parent, min, max);
}

tab5_err_t tab5_ui_slider_set_value(tab5_ui_obj_t obj, int32_t val)
{
    return tab5_ui_host_slider_set_value(obj, val);
}

int32_t tab5_ui_slider_get_value(tab5_ui_obj_t obj)
{
    return tab5_ui_host_slider_get_value(obj);
}

tab5_ui_obj_t tab5_ui_list_create(tab5_ui_obj_t parent)
{
    return tab5_ui_host_list_create(parent);
}

tab5_ui_obj_t tab5_ui_list_add_btn(tab5_ui_obj_t list, const char *symbol, const char *text)
{
    return tab5_ui_host_list_add_btn(list, symbol, text);
}

tab5_err_t tab5_ui_obj_clean(tab5_ui_obj_t obj)
{
    return tab5_ui_host_obj_clean(obj);
}

tab5_err_t tab5_ui_clear_content(void)
{
    return tab5_ui_host_clear_app_content(s_active_app_ctx);
}

uint32_t tab5_ui_theme_get_color(tab5_ui_color_id_t color_id)
{
    return tab5_ui_host_theme_get_color((uint32_t)color_id);
}

tab5_err_t tab5_ui_obj_set_style_bg(tab5_ui_obj_t obj, uint32_t color_hex, uint8_t opa)
{
    return tab5_ui_host_obj_set_style_bg(obj, color_hex, opa);
}

tab5_err_t tab5_ui_obj_set_style_border(tab5_ui_obj_t obj, uint32_t border_hex, int32_t width)
{
    return tab5_ui_host_obj_set_style_border(obj, border_hex, width);
}

tab5_err_t tab5_ui_obj_set_style_text_color(tab5_ui_obj_t obj, uint32_t color_hex, uint8_t opa)
{
    return tab5_ui_host_obj_set_style_text_color(obj, color_hex, opa);
}

tab5_err_t tab5_ui_obj_set_style_radius(tab5_ui_obj_t obj, int32_t radius)
{
    return tab5_ui_host_obj_set_style_radius(obj, radius);
}

tab5_err_t tab5_ui_obj_set_flex_grow(tab5_ui_obj_t obj, uint8_t grow)
{
    return tab5_ui_host_obj_set_flex_grow(obj, grow);
}

tab5_err_t tab5_ui_obj_set_clickable(tab5_ui_obj_t obj, bool clickable)
{
    return tab5_ui_host_obj_set_clickable(obj, clickable);
}

tab5_err_t tab5_storage_get_app_dir(char *out_buf, size_t buf_size)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_STATE;
    }
    return tab5_storage_sandbox_get_app_dir(s_active_app_ctx->app_id, out_buf, buf_size);
}

tab5_err_t tab5_storage_path_resolve(const char *in_path, char *out_path, size_t out_size, bool write_access)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_STATE;
    }
    return tab5_storage_sandbox_resolve_path(in_path, out_path, out_size, s_active_app_ctx->app_id,
                                             s_active_app_ctx->permissions, write_access);
}

tab5_err_t tab5_storage_mkdir(const char *rel_or_abs_path)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_STATE;
    }
    return tab5_storage_sandbox_mkdir(rel_or_abs_path, s_active_app_ctx->app_id, s_active_app_ctx->permissions);
}

tab5_err_t tab5_storage_remove(const char *rel_or_abs_path)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_STATE;
    }
    return tab5_storage_sandbox_remove(rel_or_abs_path, s_active_app_ctx->app_id, s_active_app_ctx->permissions);
}

tab5_err_t tab5_storage_scandir(const char *rel_or_abs_path, tab5_dir_entry_t *entries, uint32_t max_entries,
                                uint32_t *out_count)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_STATE;
    }
    return tab5_storage_sandbox_scandir(rel_or_abs_path, entries, max_entries, out_count, s_active_app_ctx->app_id,
                                        s_active_app_ctx->permissions);
}

tab5_err_t tab5_system_get_battery(tab5_battery_info_t *out_info)
{
    return tab5_sys_host_get_battery(out_info);
}

tab5_err_t tab5_system_get_wifi_status(tab5_wifi_info_t *out_info)
{
    return tab5_sys_host_get_wifi(out_info);
}

tab5_err_t tab5_system_get_bt_status(tab5_bt_info_t *out_info)
{
    return tab5_sys_host_get_bt(out_info);
}

tab5_err_t tab5_system_get_time(int64_t *out_epoch_ms, struct tm *out_time)
{
    return tab5_sys_host_get_time(out_epoch_ms, out_time);
}

tab5_err_t tab5_sound_play_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    return tab5_sys_host_beep(freq_hz, duration_ms);
}

void tab5_system_log(int level, const char *tag, const char *message)
{
    tab5_sys_host_log(level, tag, message);
}

tab5_err_t tab5_fileserver_start(void)
{
#if defined(ESP_PLATFORM)
    return http_file_server_start() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_fileserver_stop(void)
{
#if defined(ESP_PLATFORM)
    return http_file_server_stop() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

bool tab5_fileserver_is_running(void)
{
#if defined(ESP_PLATFORM)
    return http_file_server_is_running();
#else
    return false;
#endif
}

uint16_t tab5_fileserver_get_port(void)
{
#if defined(ESP_PLATFORM)
    return http_file_server_get_port();
#else
    return 8080;
#endif
}

tab5_err_t tab5_recorder_start(char *out_path, size_t out_len)
{
#if defined(ESP_PLATFORM)
    return audio_recorder_start_recording(out_path, out_len) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    if (out_path && out_len > 0) {
        strncpy(out_path, "/sdcard/gravacoes/rec_mock.wav", out_len - 1);
    }
    return TAB5_OK;
#endif
}

tab5_err_t tab5_recorder_stop(void)
{
#if defined(ESP_PLATFORM)
    return audio_recorder_stop_recording() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_recorder_play(const char *path)
{
#if defined(ESP_PLATFORM)
    return audio_recorder_start_playback(path) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    (void)path;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_recorder_pause(void)
{
#if defined(ESP_PLATFORM)
    return audio_recorder_pause_playback() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_recorder_resume(void)
{
#if defined(ESP_PLATFORM)
    return audio_recorder_resume_playback() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_recorder_stop_play(void)
{
#if defined(ESP_PLATFORM)
    return audio_recorder_stop_playback() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

bool tab5_recorder_is_recording(void)
{
#if defined(ESP_PLATFORM)
    return audio_recorder_is_recording();
#else
    return false;
#endif
}

bool tab5_recorder_is_playing(void)
{
#if defined(ESP_PLATFORM)
    return audio_recorder_is_playing();
#else
    return false;
#endif
}

tab5_err_t tab5_terminal_exec(const char *cmd, char *out_buf, size_t buf_size)
{
    if (cmd == nullptr || out_buf == nullptr || buf_size == 0) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    static std::string s_terminal_cwd = "/sdcard";
    std::string output = terminal_exec(cmd, s_terminal_cwd);
    strncpy(out_buf, output.c_str(), buf_size - 1);
    out_buf[buf_size - 1] = '\0';
    return TAB5_OK;
#else
    snprintf(out_buf, buf_size, "Output of '%s'\n", cmd);
    return TAB5_OK;
#endif
}

/* ========================================================================= */
/* Player de Música                                                          */
/* ========================================================================= */

tab5_err_t tab5_music_play(const char *filepath)
{
    if (filepath == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    return music_player_start(filepath) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_music_pause(void)
{
#if defined(ESP_PLATFORM)
    return music_player_pause() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_music_resume(void)
{
#if defined(ESP_PLATFORM)
    return music_player_resume() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_music_stop(void)
{
#if defined(ESP_PLATFORM)
    return music_player_stop() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

bool tab5_music_is_playing(void)
{
#if defined(ESP_PLATFORM)
    return music_player_is_playing();
#else
    return false;
#endif
}

tab5_err_t tab5_music_set_volume(int32_t volume)
{
#if defined(ESP_PLATFORM)
    return music_player_set_volume((int)volume) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    (void)volume;
    return TAB5_OK;
#endif
}

int32_t tab5_music_get_volume(void)
{
#if defined(ESP_PLATFORM)
    return (int32_t)music_player_get_volume();
#else
    return 75;
#endif
}

tab5_err_t tab5_music_get_status(tab5_music_status_t *out_status)
{
    if (out_status == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    memset(out_status, 0, sizeof(*out_status));
#if defined(ESP_PLATFORM)
    music_player_status_t st = {};
    music_player_get_status(&st);
    out_status->state = (uint32_t)st.state;
    out_status->current_time_sec = st.current_time_sec;
    out_status->total_time_sec = st.total_time_sec;
    strncpy(out_status->current_filepath, st.current_filepath, sizeof(out_status->current_filepath) - 1);
    return TAB5_OK;
#else
    out_status->state = 0;
    out_status->current_time_sec = 0;
    out_status->total_time_sec = 180;
    strncpy(out_status->current_filepath, "/sdcard/musica/track1.mp3", sizeof(out_status->current_filepath) - 1);
    return TAB5_OK;
#endif
}

/* ========================================================================= */
/* Gerenciamento de Rede Wi-Fi                                               */
/* ========================================================================= */

tab5_err_t tab5_wifi_scan(tab5_wifi_ap_t *out_aps, uint32_t max_aps, uint32_t *out_count)
{
    if (out_aps == nullptr || max_aps == 0 || out_count == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    *out_count = 0;
#if defined(ESP_PLATFORM)
    if (!wifi_mgr_is_enabled()) {
        return TAB5_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err == ESP_OK) {
        uint16_t num_aps = (uint16_t)max_aps;
        wifi_ap_record_t *records = (wifi_ap_record_t *)calloc(max_aps, sizeof(wifi_ap_record_t));
        if (records != nullptr) {
            if (esp_wifi_scan_get_ap_records(&num_aps, records) == ESP_OK) {
                for (uint16_t i = 0; i < num_aps; i++) {
                    strncpy(out_aps[i].ssid, (const char *)records[i].ssid, sizeof(out_aps[i].ssid) - 1);
                    out_aps[i].ssid[sizeof(out_aps[i].ssid) - 1] = '\0';
                    out_aps[i].rssi = records[i].rssi;
                    out_aps[i].authmode = (uint8_t)records[i].authmode;
                }
                *out_count = num_aps;
            }
            free(records);
        }
    }
    return TAB5_OK;
#else
    strncpy(out_aps[0].ssid, "Tab5_WiFi_5G", sizeof(out_aps[0].ssid) - 1);
    out_aps[0].rssi = -45;
    out_aps[0].authmode = 3;
    strncpy(out_aps[1].ssid, "Office_Guest", sizeof(out_aps[1].ssid) - 1);
    out_aps[1].rssi = -68;
    out_aps[1].authmode = 0;
    *out_count = 2;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_wifi_connect(const char *ssid, const char *password)
{
    if (ssid == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    return wifi_mgr_connect(ssid, password) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    (void)password;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_wifi_disconnect(void)
{
#if defined(ESP_PLATFORM)
    return wifi_mgr_disconnect() == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_wifi_forget(const char *ssid)
{
    if (ssid == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    return wifi_mgr_forget(ssid) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_wifi_set_enabled(bool enabled)
{
#if defined(ESP_PLATFORM)
    return wifi_mgr_set_enabled(enabled) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    (void)enabled;
    return TAB5_OK;
#endif
}

bool tab5_wifi_is_enabled(void)
{
#if defined(ESP_PLATFORM)
    return wifi_mgr_is_enabled();
#else
    return true;
#endif
}

/* ========================================================================= */
/* Gerenciamento de Dispositivos Bluetooth BLE                               */
/* ========================================================================= */

tab5_err_t tab5_bt_scan(tab5_bt_dev_t *out_devs, uint32_t max_devs, uint32_t *out_count)
{
    if (out_devs == nullptr || max_devs == 0 || out_count == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    *out_count = 0;
#if defined(ESP_PLATFORM)
    // No ESP32, scan usa callback assíncrono interno; para a Host ABI retornamos dispositivos conhecidos/descobertos
    *out_count = 0;
    return TAB5_OK;
#else
    strncpy(out_devs[0].mac, "AA:BB:CC:DD:EE:01", sizeof(out_devs[0].mac) - 1);
    strncpy(out_devs[0].name, "Bluetooth Keyboard", sizeof(out_devs[0].name) - 1);
    out_devs[0].rssi = -55;
    out_devs[0].type = 1;
    out_devs[0].connected = 0;
    out_devs[0].paired = 1;
    *out_count = 1;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_bt_connect(const char *mac, const char *name, uint32_t dev_type)
{
    if (mac == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    return bt_mgr_connect(mac, name ? name : "", (bt_dev_type_t)dev_type) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    (void)name;
    (void)dev_type;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_bt_disconnect(const char *mac)
{
    (void)mac;
#if defined(ESP_PLATFORM)
    return TAB5_OK;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_bt_forget(const char *mac)
{
    if (mac == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    return TAB5_OK;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_bt_set_enabled(bool enabled)
{
    (void)enabled;
    return TAB5_OK;
}

bool tab5_bt_is_enabled(void)
{
    return true;
}

} // extern "C"

/* ========================================================================= */
/* Wrappers de Exportação para WAMR (recebem wasm_exec_env_t no 1º argumento) */
/* ========================================================================= */

#ifdef ESP_PLATFORM
#include "wasm_export.h"
#include "esp_rom_sys.h"
#define HAVE_WAMR_ENV 1
#else
#define HAVE_WAMR_ENV 0
typedef void *wasm_exec_env_t;
typedef void *wasm_module_inst_t;
#define wasm_runtime_get_module_inst(env) ((void *)(env))
#define wasm_runtime_addr_app_to_native(inst, addr) ((void *)(uintptr_t)(addr))
#endif

namespace {

static tab5_err_t wasm_tab5_lifecycle_register(wasm_exec_env_t exec_env, const tab5_lifecycle_callbacks_t *cbs)
{
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    if (module_inst != nullptr && cbs != nullptr) {
        const tab5_lifecycle_callbacks_t *native_cbs =
            (const tab5_lifecycle_callbacks_t *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)cbs);
        if (native_cbs != nullptr) {
            tab5_app_context_t *ctx = s_active_app_ctx;
            if (ctx != nullptr) {
                ctx->lifecycle = *native_cbs;
                return TAB5_OK;
            }
        }
    }
#endif
    (void)exec_env;
    return tab5_lifecycle_register(cbs);
}

static tab5_err_t wasm_tab5_sound_play_beep(wasm_exec_env_t exec_env, uint32_t freq_hz, uint32_t duration_ms)
{
    (void)exec_env;
    return tab5_sound_play_beep(freq_hz, duration_ms);
}

static tab5_err_t wasm_tab5_storage_get_app_dir(wasm_exec_env_t exec_env, char *out_buf, size_t buf_size)
{
    (void)exec_env;
    return tab5_storage_get_app_dir(out_buf, buf_size);
}

static tab5_err_t wasm_tab5_storage_mkdir(wasm_exec_env_t exec_env, const char *path)
{
    (void)exec_env;
    return tab5_storage_mkdir(path);
}

static tab5_err_t wasm_tab5_storage_path_resolve(wasm_exec_env_t exec_env, const char *in_path, char *out_path,
                                                 size_t out_size, bool write_access)
{
    (void)exec_env;
    return tab5_storage_path_resolve(in_path, out_path, out_size, write_access);
}

static tab5_err_t wasm_tab5_storage_remove(wasm_exec_env_t exec_env, const char *path)
{
    (void)exec_env;
    return tab5_storage_remove(path);
}

static tab5_err_t wasm_tab5_storage_scandir(wasm_exec_env_t exec_env, const char *rel_or_abs_path,
                                            tab5_dir_entry_t *entries, uint32_t max_entries, uint32_t *out_count)
{
    (void)exec_env;
    return tab5_storage_scandir(rel_or_abs_path, entries, max_entries, out_count);
}

static tab5_err_t wasm_tab5_system_get_battery(wasm_exec_env_t exec_env, tab5_battery_info_t *out_info)
{
    (void)exec_env;
    return tab5_system_get_battery(out_info);
}

static tab5_err_t wasm_tab5_system_get_bt_status(wasm_exec_env_t exec_env, tab5_bt_info_t *out_info)
{
    (void)exec_env;
    return tab5_system_get_bt_status(out_info);
}

static tab5_err_t wasm_tab5_system_get_time(wasm_exec_env_t exec_env, int64_t *out_epoch, struct tm *out_time)
{
    (void)exec_env;
    return tab5_system_get_time(out_epoch, out_time);
}

static tab5_err_t wasm_tab5_system_get_wifi_status(wasm_exec_env_t exec_env, tab5_wifi_info_t *out_info)
{
    (void)exec_env;
    return tab5_system_get_wifi_status(out_info);
}

static void wasm_tab5_system_log(wasm_exec_env_t exec_env, int level, const char *tag, const char *message)
{
    (void)exec_env;
    tab5_system_log(level, tag != nullptr ? tag : "wasm", message != nullptr ? message : "");
}

static tab5_ui_obj_t wasm_tab5_ui_app_bar_add_action_button(wasm_exec_env_t exec_env, const char *sym,
                                                            void (*on_click)(void *user_data), void *user_data)
{
    (void)exec_env;
    return tab5_ui_app_bar_add_action_button(sym, on_click, user_data);
}

static tab5_err_t wasm_tab5_ui_app_bar_set_title(wasm_exec_env_t exec_env, const char *title)
{
    (void)exec_env;
    return tab5_ui_app_bar_set_title(title);
}

static tab5_ui_obj_t wasm_tab5_ui_get_main_textarea(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_ui_get_main_textarea();
}

static tab5_ui_obj_t wasm_tab5_ui_get_screen(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_ui_get_screen();
}

static tab5_err_t wasm_tab5_ui_keyboard_hide(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_ui_keyboard_hide();
}

static bool wasm_tab5_ui_keyboard_is_visible(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_ui_keyboard_is_visible();
}

static tab5_err_t wasm_tab5_ui_keyboard_show(wasm_exec_env_t exec_env, tab5_ui_obj_t target_textarea)
{
    (void)exec_env;
    return tab5_ui_keyboard_show(target_textarea);
}

static tab5_err_t wasm_tab5_ui_show_toast(wasm_exec_env_t exec_env, const char *message, uint32_t duration_ms)
{
    (void)exec_env;
    return tab5_ui_show_toast(message, duration_ms);
}

static uint32_t wasm_tab5_ui_textarea_get_text(wasm_exec_env_t exec_env, tab5_ui_obj_t ta)
{
    const char *text = tab5_ui_textarea_get_text(ta);
    if (text == nullptr) {
        return 0;
    }
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    if (module_inst == nullptr) {
        return 0;
    }
    size_t len = strlen(text) + 1;
    uint32_t offset = wasm_runtime_module_malloc(module_inst, len, nullptr);
    if (offset != 0) {
        char *dest = (char *)wasm_runtime_addr_app_to_native(module_inst, offset);
        if (dest != nullptr) {
            memcpy(dest, text, len);
        }
    }
    return offset;
#else
    (void)exec_env;
    uintptr_t addr = (uintptr_t)text;
    return (uint32_t)addr;
#endif
}

static tab5_err_t wasm_tab5_ui_textarea_set_placeholder(wasm_exec_env_t exec_env, tab5_ui_obj_t ta,
                                                        const char *placeholder)
{
    (void)exec_env;
    return tab5_ui_textarea_set_placeholder(ta, placeholder);
}

static tab5_err_t wasm_tab5_ui_textarea_set_text(wasm_exec_env_t exec_env, tab5_ui_obj_t ta, const char *text)
{
    (void)exec_env;
    return tab5_ui_textarea_set_text(ta, text);
}

static tab5_err_t wasm_tab5_ui_textarea_set_cursor_pos(wasm_exec_env_t exec_env, tab5_ui_obj_t ta, int32_t pos)
{
    (void)exec_env;
    return tab5_ui_textarea_set_cursor_pos(ta, pos);
}

static int32_t wasm_tab5_ui_textarea_get_cursor_pos(wasm_exec_env_t exec_env, tab5_ui_obj_t ta)
{
    (void)exec_env;
    return tab5_ui_textarea_get_cursor_pos(ta);
}

static tab5_err_t wasm_tab5_ui_textarea_set_password_mode(wasm_exec_env_t exec_env, tab5_ui_obj_t ta,
                                                          bool password_mode)
{
    (void)exec_env;
    return tab5_ui_textarea_set_password_mode(ta, password_mode);
}

static tab5_ui_obj_t wasm_tab5_ui_container_create(wasm_exec_env_t exec_env, tab5_ui_obj_t parent)
{
    (void)exec_env;
    return tab5_ui_container_create(parent);
}

static tab5_err_t wasm_tab5_ui_obj_set_size(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, int32_t w, int32_t h)
{
    (void)exec_env;
    return tab5_ui_obj_set_size(obj, w, h);
}

static tab5_err_t wasm_tab5_ui_obj_set_align(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, uint32_t align, int32_t x_ofs,
                                             int32_t y_ofs)
{
    (void)exec_env;
    return tab5_ui_obj_set_align(obj, (tab5_ui_align_t)align, x_ofs, y_ofs);
}

static tab5_err_t wasm_tab5_ui_obj_set_flex_flow(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, uint32_t flow)
{
    (void)exec_env;
    return tab5_ui_obj_set_flex_flow(obj, (tab5_ui_flex_flow_t)flow);
}

static tab5_err_t wasm_tab5_ui_obj_set_pad(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, int32_t pad_all)
{
    (void)exec_env;
    return tab5_ui_obj_set_pad(obj, pad_all);
}

static tab5_err_t wasm_tab5_ui_obj_set_gap(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, int32_t gap)
{
    (void)exec_env;
    return tab5_ui_obj_set_gap(obj, gap);
}

static tab5_ui_obj_t wasm_tab5_ui_label_create(wasm_exec_env_t exec_env, tab5_ui_obj_t parent, const char *text)
{
    (void)exec_env;
    return tab5_ui_label_create(parent, text);
}

static tab5_err_t wasm_tab5_ui_label_set_text(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, const char *text)
{
    (void)exec_env;
    return tab5_ui_label_set_text(obj, text);
}

static tab5_ui_obj_t wasm_tab5_ui_btn_create(wasm_exec_env_t exec_env, tab5_ui_obj_t parent,
                                             const char *label_or_symbol)
{
    (void)exec_env;
    return tab5_ui_btn_create(parent, label_or_symbol);
}

static tab5_ui_obj_t wasm_tab5_ui_switch_create(wasm_exec_env_t exec_env, tab5_ui_obj_t parent)
{
    (void)exec_env;
    return tab5_ui_switch_create(parent);
}

static tab5_err_t wasm_tab5_ui_switch_set_state(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, bool checked)
{
    (void)exec_env;
    return tab5_ui_switch_set_state(obj, checked);
}

static bool wasm_tab5_ui_switch_get_state(wasm_exec_env_t exec_env, tab5_ui_obj_t obj)
{
    (void)exec_env;
    return tab5_ui_switch_get_state(obj);
}

static tab5_ui_obj_t wasm_tab5_ui_slider_create(wasm_exec_env_t exec_env, tab5_ui_obj_t parent, int32_t min,
                                                int32_t max)
{
    (void)exec_env;
    return tab5_ui_slider_create(parent, min, max);
}

static tab5_err_t wasm_tab5_ui_slider_set_value(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, int32_t val)
{
    (void)exec_env;
    return tab5_ui_slider_set_value(obj, val);
}

static int32_t wasm_tab5_ui_slider_get_value(wasm_exec_env_t exec_env, tab5_ui_obj_t obj)
{
    (void)exec_env;
    return tab5_ui_slider_get_value(obj);
}

static tab5_ui_obj_t wasm_tab5_ui_list_create(wasm_exec_env_t exec_env, tab5_ui_obj_t parent)
{
    (void)exec_env;
    return tab5_ui_list_create(parent);
}

static tab5_ui_obj_t wasm_tab5_ui_list_add_btn(wasm_exec_env_t exec_env, tab5_ui_obj_t list, const char *symbol,
                                               const char *text)
{
    (void)exec_env;
    return tab5_ui_list_add_btn(list, symbol, text);
}

static tab5_err_t wasm_tab5_ui_obj_clean(wasm_exec_env_t exec_env, tab5_ui_obj_t obj)
{
    (void)exec_env;
    return tab5_ui_obj_clean(obj);
}

static tab5_err_t wasm_tab5_ui_clear_content(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_ui_clear_content();
}

static uint32_t wasm_tab5_ui_theme_get_color(wasm_exec_env_t exec_env, uint32_t color_id)
{
    (void)exec_env;
    return tab5_ui_theme_get_color((tab5_ui_color_id_t)color_id);
}

static tab5_err_t wasm_tab5_ui_obj_set_style_bg(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, uint32_t color_hex,
                                                uint32_t opa)
{
    (void)exec_env;
    return tab5_ui_obj_set_style_bg(obj, color_hex, (uint8_t)opa);
}

static tab5_err_t wasm_tab5_ui_obj_set_style_border(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, uint32_t border_hex,
                                                    int32_t width)
{
    (void)exec_env;
    return tab5_ui_obj_set_style_border(obj, border_hex, width);
}

static tab5_err_t wasm_tab5_ui_obj_set_style_text_color(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, uint32_t color_hex,
                                                        uint32_t opa)
{
    (void)exec_env;
    return tab5_ui_obj_set_style_text_color(obj, color_hex, (uint8_t)opa);
}

static tab5_err_t wasm_tab5_ui_obj_set_style_radius(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, int32_t radius)
{
    (void)exec_env;
    return tab5_ui_obj_set_style_radius(obj, radius);
}

static tab5_err_t wasm_tab5_ui_obj_set_flex_grow(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, uint32_t grow)
{
    (void)exec_env;
    return tab5_ui_obj_set_flex_grow(obj, (uint8_t)grow);
}

static tab5_err_t wasm_tab5_ui_obj_set_clickable(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, bool clickable)
{
    (void)exec_env;
    return tab5_ui_obj_set_clickable(obj, clickable);
}

static tab5_err_t wasm_tab5_fileserver_start(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_fileserver_start();
}

static tab5_err_t wasm_tab5_fileserver_stop(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_fileserver_stop();
}

static bool wasm_tab5_fileserver_is_running(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_fileserver_is_running();
}

static uint32_t wasm_tab5_fileserver_get_port(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (uint32_t)tab5_fileserver_get_port();
}

static tab5_err_t wasm_tab5_recorder_start(wasm_exec_env_t exec_env, char *out_path, uint32_t out_len)
{
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    if (module_inst != nullptr && out_path != nullptr) {
        char *native_buf = (char *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_path);
        if (native_buf != nullptr) {
            return tab5_recorder_start(native_buf, out_len);
        }
    }
#endif
    (void)exec_env;
    return tab5_recorder_start(out_path, out_len);
}

static tab5_err_t wasm_tab5_recorder_stop(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_recorder_stop();
}

static tab5_err_t wasm_tab5_recorder_play(wasm_exec_env_t exec_env, const char *path)
{
    (void)exec_env;
    return tab5_recorder_play(path);
}

static tab5_err_t wasm_tab5_recorder_pause(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_recorder_pause();
}

static tab5_err_t wasm_tab5_recorder_resume(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_recorder_resume();
}

static tab5_err_t wasm_tab5_recorder_stop_play(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_recorder_stop_play();
}

static bool wasm_tab5_recorder_is_recording(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_recorder_is_recording();
}

static bool wasm_tab5_recorder_is_playing(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_recorder_is_playing();
}

static tab5_err_t wasm_tab5_terminal_exec(wasm_exec_env_t exec_env, const char *cmd, char *out_buf, uint32_t buf_size)
{
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    if (module_inst != nullptr && out_buf != nullptr) {
        char *native_buf = (char *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_buf);
        if (native_buf != nullptr) {
            return tab5_terminal_exec(cmd, native_buf, buf_size);
        }
    }
#endif
    (void)exec_env;
    return tab5_terminal_exec(cmd, out_buf, buf_size);
}

/* ========================================================================= */
/* Player de Música Wrappers                                                 */
/* ========================================================================= */

static tab5_err_t wasm_tab5_music_play(wasm_exec_env_t exec_env, const char *filepath)
{
    (void)exec_env;
    return tab5_music_play(filepath);
}

static tab5_err_t wasm_tab5_music_pause(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_music_pause();
}

static tab5_err_t wasm_tab5_music_resume(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_music_resume();
}

static tab5_err_t wasm_tab5_music_stop(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_music_stop();
}

static bool wasm_tab5_music_is_playing(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_music_is_playing();
}

static tab5_err_t wasm_tab5_music_set_volume(wasm_exec_env_t exec_env, int32_t volume)
{
    (void)exec_env;
    return tab5_music_set_volume(volume);
}

static int32_t wasm_tab5_music_get_volume(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_music_get_volume();
}

static tab5_err_t wasm_tab5_music_get_status(wasm_exec_env_t exec_env, tab5_music_status_t *out_status)
{
    (void)exec_env;
    return tab5_music_get_status(out_status);
}

/* ========================================================================= */
/* Wi-Fi Wrappers                                                            */
/* ========================================================================= */

static tab5_err_t wasm_tab5_wifi_scan(wasm_exec_env_t exec_env, tab5_wifi_ap_t *out_aps, uint32_t max_aps,
                                      uint32_t *out_count)
{
    (void)exec_env;
    return tab5_wifi_scan(out_aps, max_aps, out_count);
}

static tab5_err_t wasm_tab5_wifi_connect(wasm_exec_env_t exec_env, const char *ssid, const char *password)
{
    (void)exec_env;
    return tab5_wifi_connect(ssid, password);
}

static tab5_err_t wasm_tab5_wifi_disconnect(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_wifi_disconnect();
}

static tab5_err_t wasm_tab5_wifi_forget(wasm_exec_env_t exec_env, const char *ssid)
{
    (void)exec_env;
    return tab5_wifi_forget(ssid);
}

static tab5_err_t wasm_tab5_wifi_set_enabled(wasm_exec_env_t exec_env, bool enabled)
{
    (void)exec_env;
    return tab5_wifi_set_enabled(enabled);
}

static bool wasm_tab5_wifi_is_enabled(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_wifi_is_enabled();
}

/* ========================================================================= */
/* Bluetooth Wrappers                                                        */
/* ========================================================================= */

static tab5_err_t wasm_tab5_bt_scan(wasm_exec_env_t exec_env, tab5_bt_dev_t *out_devs, uint32_t max_devs,
                                    uint32_t *out_count)
{
    (void)exec_env;
    return tab5_bt_scan(out_devs, max_devs, out_count);
}

static tab5_err_t wasm_tab5_bt_connect(wasm_exec_env_t exec_env, const char *mac, const char *name, uint32_t dev_type)
{
    (void)exec_env;
    return tab5_bt_connect(mac, name, dev_type);
}

static tab5_err_t wasm_tab5_bt_disconnect(wasm_exec_env_t exec_env, const char *mac)
{
    (void)exec_env;
    return tab5_bt_disconnect(mac);
}

static tab5_err_t wasm_tab5_bt_forget(wasm_exec_env_t exec_env, const char *mac)
{
    (void)exec_env;
    return tab5_bt_forget(mac);
}

static tab5_err_t wasm_tab5_bt_set_enabled(wasm_exec_env_t exec_env, bool enabled)
{
    (void)exec_env;
    return tab5_bt_set_enabled(enabled);
}

static bool wasm_tab5_bt_is_enabled(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_bt_is_enabled();
}

} // namespace

/* ========================================================================= */
/* Tabela de Exportação de Símbolos para WAMR                                */
/* ========================================================================= */

static tab5_native_symbol_t s_native_symbols[] = {
    {"tab5_lifecycle_register", (void *)wasm_tab5_lifecycle_register, "(*)i", nullptr},
    {"tab5_sound_play_beep", (void *)wasm_tab5_sound_play_beep, "(ii)i", nullptr},
    {"tab5_storage_get_app_dir", (void *)wasm_tab5_storage_get_app_dir, "(*i)i", nullptr},
    {"tab5_storage_mkdir", (void *)wasm_tab5_storage_mkdir, "($)i", nullptr},
    {"tab5_storage_path_resolve", (void *)wasm_tab5_storage_path_resolve, "($*ii)i", nullptr},
    {"tab5_storage_remove", (void *)wasm_tab5_storage_remove, "($)i", nullptr},
    {"tab5_storage_scandir", (void *)wasm_tab5_storage_scandir, "($*i*)i", nullptr},
    {"tab5_system_get_battery", (void *)wasm_tab5_system_get_battery, "(*)i", nullptr},
    {"tab5_system_get_bt_status", (void *)wasm_tab5_system_get_bt_status, "(*)i", nullptr},
    {"tab5_system_get_time", (void *)wasm_tab5_system_get_time, "(**)i", nullptr},
    {"tab5_system_get_wifi_status", (void *)wasm_tab5_system_get_wifi_status, "(*)i", nullptr},
    {"tab5_system_log", (void *)wasm_tab5_system_log, "(i$$)", nullptr},
    {"tab5_ui_app_bar_add_action_button", (void *)wasm_tab5_ui_app_bar_add_action_button, "($ii)i", nullptr},
    {"tab5_ui_app_bar_set_title", (void *)wasm_tab5_ui_app_bar_set_title, "($)i", nullptr},
    {"tab5_ui_get_main_textarea", (void *)wasm_tab5_ui_get_main_textarea, "()i", nullptr},
    {"tab5_ui_get_screen", (void *)wasm_tab5_ui_get_screen, "()i", nullptr},
    {"tab5_ui_keyboard_hide", (void *)wasm_tab5_ui_keyboard_hide, "()i", nullptr},
    {"tab5_ui_keyboard_is_visible", (void *)wasm_tab5_ui_keyboard_is_visible, "()i", nullptr},
    {"tab5_ui_keyboard_show", (void *)wasm_tab5_ui_keyboard_show, "(i)i", nullptr},
    {"tab5_ui_show_toast", (void *)wasm_tab5_ui_show_toast, "($i)i", nullptr},
    {"tab5_ui_textarea_get_text", (void *)wasm_tab5_ui_textarea_get_text, "(i)i", nullptr},
    {"tab5_ui_textarea_set_placeholder", (void *)wasm_tab5_ui_textarea_set_placeholder, "(i$)i", nullptr},
    {"tab5_ui_textarea_set_text", (void *)wasm_tab5_ui_textarea_set_text, "(i$)i", nullptr},
    {"tab5_ui_textarea_set_cursor_pos", (void *)wasm_tab5_ui_textarea_set_cursor_pos, "(ii)i", nullptr},
    {"tab5_ui_textarea_get_cursor_pos", (void *)wasm_tab5_ui_textarea_get_cursor_pos, "(i)i", nullptr},
    {"tab5_ui_textarea_set_password_mode", (void *)wasm_tab5_ui_textarea_set_password_mode, "(ii)i", nullptr},
    {"tab5_ui_container_create", (void *)wasm_tab5_ui_container_create, "(i)i", nullptr},
    {"tab5_ui_obj_set_size", (void *)wasm_tab5_ui_obj_set_size, "(iii)i", nullptr},
    {"tab5_ui_obj_set_align", (void *)wasm_tab5_ui_obj_set_align, "(iiii)i", nullptr},
    {"tab5_ui_obj_set_flex_flow", (void *)wasm_tab5_ui_obj_set_flex_flow, "(ii)i", nullptr},
    {"tab5_ui_obj_set_pad", (void *)wasm_tab5_ui_obj_set_pad, "(ii)i", nullptr},
    {"tab5_ui_obj_set_gap", (void *)wasm_tab5_ui_obj_set_gap, "(ii)i", nullptr},
    {"tab5_ui_label_create", (void *)wasm_tab5_ui_label_create, "(i$)i", nullptr},
    {"tab5_ui_label_set_text", (void *)wasm_tab5_ui_label_set_text, "(i$)i", nullptr},
    {"tab5_ui_btn_create", (void *)wasm_tab5_ui_btn_create, "(i$)i", nullptr},
    {"tab5_ui_switch_create", (void *)wasm_tab5_ui_switch_create, "(i)i", nullptr},
    {"tab5_ui_switch_set_state", (void *)wasm_tab5_ui_switch_set_state, "(ii)i", nullptr},
    {"tab5_ui_switch_get_state", (void *)wasm_tab5_ui_switch_get_state, "(i)i", nullptr},
    {"tab5_ui_slider_create", (void *)wasm_tab5_ui_slider_create, "(iii)i", nullptr},
    {"tab5_ui_slider_set_value", (void *)wasm_tab5_ui_slider_set_value, "(ii)i", nullptr},
    {"tab5_ui_slider_get_value", (void *)wasm_tab5_ui_slider_get_value, "(i)i", nullptr},
    {"tab5_ui_list_create", (void *)wasm_tab5_ui_list_create, "(i)i", nullptr},
    {"tab5_ui_list_add_btn", (void *)wasm_tab5_ui_list_add_btn, "(i$$)i", nullptr},
    {"tab5_ui_obj_clean", (void *)wasm_tab5_ui_obj_clean, "(i)i", nullptr},
    {"tab5_ui_clear_content", (void *)wasm_tab5_ui_clear_content, "()i", nullptr},
    {"tab5_ui_theme_get_color", (void *)wasm_tab5_ui_theme_get_color, "(i)i", nullptr},
    {"tab5_ui_obj_set_style_bg", (void *)wasm_tab5_ui_obj_set_style_bg, "(iii)i", nullptr},
    {"tab5_ui_obj_set_style_border", (void *)wasm_tab5_ui_obj_set_style_border, "(iii)i", nullptr},
    {"tab5_ui_obj_set_style_text_color", (void *)wasm_tab5_ui_obj_set_style_text_color, "(iii)i", nullptr},
    {"tab5_ui_obj_set_style_radius", (void *)wasm_tab5_ui_obj_set_style_radius, "(ii)i", nullptr},
    {"tab5_ui_obj_set_flex_grow", (void *)wasm_tab5_ui_obj_set_flex_grow, "(ii)i", nullptr},
    {"tab5_ui_obj_set_clickable", (void *)wasm_tab5_ui_obj_set_clickable, "(ii)i", nullptr},
    {"tab5_fileserver_start", (void *)wasm_tab5_fileserver_start, "()i", nullptr},
    {"tab5_fileserver_stop", (void *)wasm_tab5_fileserver_stop, "()i", nullptr},
    {"tab5_fileserver_is_running", (void *)wasm_tab5_fileserver_is_running, "()i", nullptr},
    {"tab5_fileserver_get_port", (void *)wasm_tab5_fileserver_get_port, "()i", nullptr},
    {"tab5_recorder_start", (void *)wasm_tab5_recorder_start, "(*i)i", nullptr},
    {"tab5_recorder_stop", (void *)wasm_tab5_recorder_stop, "()i", nullptr},
    {"tab5_recorder_play", (void *)wasm_tab5_recorder_play, "($)i", nullptr},
    {"tab5_recorder_pause", (void *)wasm_tab5_recorder_pause, "()i", nullptr},
    {"tab5_recorder_resume", (void *)wasm_tab5_recorder_resume, "()i", nullptr},
    {"tab5_recorder_stop_play", (void *)wasm_tab5_recorder_stop_play, "()i", nullptr},
    {"tab5_recorder_is_recording", (void *)wasm_tab5_recorder_is_recording, "()i", nullptr},
    {"tab5_recorder_is_playing", (void *)wasm_tab5_recorder_is_playing, "()i", nullptr},
    {"tab5_terminal_exec", (void *)wasm_tab5_terminal_exec, "($*i)i", nullptr},
    {"tab5_music_play", (void *)wasm_tab5_music_play, "($)i", nullptr},
    {"tab5_music_pause", (void *)wasm_tab5_music_pause, "()i", nullptr},
    {"tab5_music_resume", (void *)wasm_tab5_music_resume, "()i", nullptr},
    {"tab5_music_stop", (void *)wasm_tab5_music_stop, "()i", nullptr},
    {"tab5_music_is_playing", (void *)wasm_tab5_music_is_playing, "()i", nullptr},
    {"tab5_music_set_volume", (void *)wasm_tab5_music_set_volume, "(i)i", nullptr},
    {"tab5_music_get_volume", (void *)wasm_tab5_music_get_volume, "()i", nullptr},
    {"tab5_music_get_status", (void *)wasm_tab5_music_get_status, "(*)i", nullptr},
    {"tab5_wifi_scan", (void *)wasm_tab5_wifi_scan, "(*i*)i", nullptr},
    {"tab5_wifi_connect", (void *)wasm_tab5_wifi_connect, "($$)i", nullptr},
    {"tab5_wifi_disconnect", (void *)wasm_tab5_wifi_disconnect, "()i", nullptr},
    {"tab5_wifi_forget", (void *)wasm_tab5_wifi_forget, "($)i", nullptr},
    {"tab5_wifi_set_enabled", (void *)wasm_tab5_wifi_set_enabled, "(i)i", nullptr},
    {"tab5_wifi_is_enabled", (void *)wasm_tab5_wifi_is_enabled, "()i", nullptr},
    {"tab5_bt_scan", (void *)wasm_tab5_bt_scan, "(*i*)i", nullptr},
    {"tab5_bt_connect", (void *)wasm_tab5_bt_connect, "($$i)i", nullptr},
    {"tab5_bt_disconnect", (void *)wasm_tab5_bt_disconnect, "($)i", nullptr},
    {"tab5_bt_forget", (void *)wasm_tab5_bt_forget, "($)i", nullptr},
    {"tab5_bt_set_enabled", (void *)wasm_tab5_bt_set_enabled, "(i)i", nullptr},
    {"tab5_bt_is_enabled", (void *)wasm_tab5_bt_is_enabled, "()i", nullptr}};

const tab5_native_symbol_t *tab5_host_abi_get_symbols(uint32_t *out_count)
{
    if (out_count != nullptr) {
        *out_count = sizeof(s_native_symbols) / sizeof(s_native_symbols[0]);
    }
    return s_native_symbols;
}
