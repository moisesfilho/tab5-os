/**
 * @file tab5_host_abi.cpp
 * @brief Implementação da Tabela de Símbolos Nativos e Dispatcher do Host
 */

#include "tab5_host_abi.h"
#include "tab5_storage_sandbox.h"
#include "tab5_ui_host.h"
#include "tab5_sys_host.h"
#include "tab5_lifecycle_host.h"
#include "tab5_nvs_worker.h"
#include "terminal_cmd.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#if defined(ESP_PLATFORM) || defined(LV_LVGL_H_INCLUDE_SIMPLE) || defined(LV_CONF_INCLUDE_SIMPLE) ||                   \
    defined(TAB5_SIMULATOR)
#include "lvgl.h"
#include "bsp/m5stack_tab5.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#include "ui_theme.h"
#define HAVE_LVGL 1
#else
#define HAVE_LVGL 0
#endif

#if HAVE_LVGL
#define LV_LOCK() bsp_display_lock(pdMS_TO_TICKS(500))
#define LV_UNLOCK() bsp_display_unlock()
#else
#define LV_LOCK() (void)0
#define LV_UNLOCK() (void)0
#endif

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#include "http_file_server.h"
#include "audio_recorder.h"
#include "music_player.h"
#include "wifi_mgr.h"
#include "bt_mgr.h"
#include "esp_wifi.h"
#include "ai_client.h"
#include "ai_storage.h"
#include "file_assoc.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <vector>
#endif

/* The manager headers are ESP-IDF-only includes, but the public ABI also
 * exposes host/simulator scan entry points. Keep their defaults available in
 * every target while preserving the manager-provided values on IDF builds. */
#ifndef WIFI_SCAN_SYNC_TIMEOUT_MS
#define WIFI_SCAN_SYNC_TIMEOUT_MS 8000
#endif
#ifndef BT_SCAN_SYNC_TIMEOUT_MS
#define BT_SCAN_SYNC_TIMEOUT_MS 7000
#endif

static tab5_app_context_t *s_active_app_ctx = nullptr;
static std::mutex s_active_app_mutex;

tab5_err_t tab5_host_abi_init(void)
{
    std::lock_guard<std::mutex> lock(s_active_app_mutex);
    s_active_app_ctx = nullptr;
    return TAB5_OK;
}

tab5_err_t tab5_host_set_active_app(tab5_app_context_t *ctx)
{
    std::lock_guard<std::mutex> lock(s_active_app_mutex);
    s_active_app_ctx = ctx;
    return TAB5_OK;
}

tab5_app_context_t *tab5_host_get_active_app(void)
{
    std::lock_guard<std::mutex> lock(s_active_app_mutex);
    return s_active_app_ctx;
}

void tab5_host_clear_active_app(void)
{
    std::lock_guard<std::mutex> lock(s_active_app_mutex);
    s_active_app_ctx = nullptr;
}

bool tab5_host_get_active_app_snapshot(tab5_active_app_snapshot_t *out_snapshot)
{
    if (out_snapshot == nullptr)
        return false;
    std::lock_guard<std::mutex> lock(s_active_app_mutex);
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (s_active_app_ctx == nullptr)
        return false;
    out_snapshot->active = true;
    strncpy(out_snapshot->app_id, s_active_app_ctx->app_id, sizeof(out_snapshot->app_id) - 1);
    strncpy(out_snapshot->app_name, s_active_app_ctx->app_name, sizeof(out_snapshot->app_name) - 1);
    out_snapshot->is_wasm = s_active_app_ctx->is_wasm;
    out_snapshot->state = s_active_app_ctx->state;
    return true;
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
#if HAVE_LVGL
    if (!LV_LOCK()) {
        return TAB5_UI_INVALID_OBJ;
    }
#endif
    tab5_ui_obj_t screen = tab5_ui_host_register_obj(s_active_app_ctx->root_screen);
#if HAVE_LVGL
    LV_UNLOCK();
#endif
    return screen;
}

tab5_err_t tab5_ui_app_bar_set_title(const char *title)
{
    if (title == nullptr || s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    if (!LV_LOCK()) {
        return TAB5_ERR_TIMEOUT;
    }
    if (s_active_app_ctx->app_bar != nullptr) {
        lv_obj_t *bar = (lv_obj_t *)s_active_app_ctx->app_bar;
        lv_obj_t *lbl = lv_obj_get_child(bar, 0);
        if (lbl != nullptr) {
            lv_label_set_text(lbl, title);
        }
    }
    LV_UNLOCK();
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
#if !HAVE_LVGL
    (void)on_click;
    (void)user_data;
#endif
#if HAVE_LVGL
    if (!LV_LOCK()) {
        return TAB5_UI_INVALID_OBJ;
    }
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
        const ui_palette_t *pal = ui_theme_get();
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
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
        if (s_active_app_ctx->is_wasm) {
            lv_obj_add_event_cb(btn, tab5_ui_host_generic_widget_event_cb, LV_EVENT_ALL, nullptr);
        }

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, sym != nullptr ? sym : "");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(pal->text), 0);
        lv_obj_center(lbl);
        tab5_ui_obj_t handle = tab5_ui_host_register_obj(btn);
        LV_UNLOCK();
        return handle;
    }
    LV_UNLOCK();
#endif
    return 0x3000;
}

tab5_ui_obj_t tab5_ui_get_main_textarea(void)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_UI_INVALID_OBJ;
    }
#if HAVE_LVGL
    if (!LV_LOCK()) {
        return TAB5_UI_INVALID_OBJ;
    }
#endif
    tab5_ui_obj_t textarea = tab5_ui_host_register_obj(s_active_app_ctx->content_area);
#if HAVE_LVGL
    LV_UNLOCK();
#endif
    return textarea;
}

tab5_ui_obj_t tab5_ui_textarea_create(tab5_ui_obj_t parent)
{
    return tab5_ui_host_textarea_create(parent);
}

tab5_err_t tab5_ui_textarea_set_text(tab5_ui_obj_t ta, const char *text)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
#if !HAVE_LVGL
    obj = obj != nullptr ? obj : (void *)(uintptr_t)ta;
#endif
    return tab5_ui_host_textarea_set_text(obj, text);
}

int32_t tab5_ui_textarea_copy_text(tab5_ui_obj_t ta, char *buffer, uint32_t capacity)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
#if !HAVE_LVGL
    obj = obj != nullptr ? obj : (void *)(uintptr_t)ta;
#endif
    return tab5_ui_host_textarea_copy_text(obj, buffer, capacity);
}

tab5_err_t tab5_ui_textarea_set_placeholder(tab5_ui_obj_t ta, const char *placeholder)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
#if !HAVE_LVGL
    obj = obj != nullptr ? obj : (void *)(uintptr_t)ta;
#endif
    return tab5_ui_host_textarea_set_placeholder(obj, placeholder);
}

tab5_err_t tab5_ui_textarea_set_cursor_pos(tab5_ui_obj_t ta, int32_t pos)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
#if !HAVE_LVGL
    obj = obj != nullptr ? obj : (void *)(uintptr_t)ta;
#endif
    return tab5_ui_host_textarea_set_cursor_pos(obj, pos);
}

int32_t tab5_ui_textarea_get_cursor_pos(tab5_ui_obj_t ta)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
#if !HAVE_LVGL
    obj = obj != nullptr ? obj : (void *)(uintptr_t)ta;
#endif
    return tab5_ui_host_textarea_get_cursor_pos(obj);
}

tab5_err_t tab5_ui_textarea_set_password_mode(tab5_ui_obj_t ta, bool password_mode)
{
    void *obj = tab5_ui_host_get_lv_obj(ta);
#if !HAVE_LVGL
    obj = obj != nullptr ? obj : (void *)(uintptr_t)ta;
#endif
    return tab5_ui_host_textarea_set_password_mode(obj, password_mode);
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

int32_t tab5_ui_keyboard_get_height(void)
{
    return tab5_ui_host_keyboard_get_height();
}

void tab5_ui_get_display_size(int32_t *out_w, int32_t *out_h)
{
    tab5_ui_host_get_display_size(out_w, out_h);
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

tab5_err_t tab5_ui_obj_set_scrollable(tab5_ui_obj_t obj, bool scrollable)
{
    return tab5_ui_host_obj_set_scrollable(obj, scrollable);
}

tab5_err_t tab5_ui_obj_scroll_to_bottom(tab5_ui_obj_t obj, bool animated)
{
    return tab5_ui_host_obj_scroll_to_bottom(obj, animated);
}

tab5_err_t tab5_ui_obj_scroll_to_top(tab5_ui_obj_t obj, bool animated)
{
    return tab5_ui_host_obj_scroll_to_top(obj, animated);
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

tab5_err_t tab5_ui_label_set_wrap(tab5_ui_obj_t obj, bool wrap)
{
    return tab5_ui_host_label_set_wrap(obj, wrap);
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

tab5_err_t tab5_ui_obj_clean_deferred(tab5_ui_obj_t obj)
{
    return tab5_ui_host_obj_clean_deferred(obj);
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

tab5_err_t tab5_ui_obj_set_style_text_size(tab5_ui_obj_t obj, int32_t size_px)
{
    return tab5_ui_host_obj_set_style_text_size(obj, size_px);
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

tab5_err_t tab5_storage_write_file(const char *rel_or_abs_path, const char *data, size_t data_len)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_STATE;
    }
    return tab5_storage_sandbox_write_file(rel_or_abs_path, data, data_len, s_active_app_ctx->app_id,
                                           s_active_app_ctx->permissions);
}

tab5_err_t tab5_storage_read_file(const char *rel_or_abs_path, char *data, size_t data_size, size_t *out_len)
{
    if (s_active_app_ctx == nullptr) {
        return TAB5_ERR_INVALID_STATE;
    }
    return tab5_storage_sandbox_read_file(rel_or_abs_path, data, data_size, out_len, s_active_app_ctx->app_id,
                                          s_active_app_ctx->permissions);
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

tab5_err_t tab5_system_get_timestamp(char *out_buf, size_t buf_size)
{
    return tab5_sys_host_get_timestamp(out_buf, buf_size);
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
#else
    static std::string s_terminal_cwd = "/tmp";
    std::string output = terminal_exec(cmd, s_terminal_cwd);
#endif
    const size_t copy_len = output.size() < buf_size - 1 ? output.size() : buf_size - 1;
    memcpy(out_buf, output.data(), copy_len);
    out_buf[copy_len] = '\0';
    return TAB5_OK;
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

tab5_err_t tab5_wifi_scan_with_timeout(tab5_wifi_ap_t *out_aps, uint32_t max_aps, uint32_t *out_count,
                                       uint32_t timeout_ms)
{
    if (out_aps == nullptr || max_aps == 0 || out_count == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    *out_count = 0;
#if defined(ESP_PLATFORM)
    if (!wifi_mgr_is_enabled()) {
        return TAB5_ERR_INVALID_STATE;
    }
    struct wifi_sync_context_t {
        SemaphoreHandle_t done;
        tab5_wifi_ap_t *out;
        uint32_t capacity;
        uint32_t count;
        bool active;
    };
    static SemaphoreHandle_t done = nullptr;
    static std::mutex sync_mutex;
    static std::mutex callback_mutex;
    std::lock_guard<std::mutex> guard(sync_mutex);
    if (done == nullptr) {
        done = xSemaphoreCreateBinary();
        if (done == nullptr) {
            return TAB5_ERR_NO_MEM;
        }
    }
    auto *context = new wifi_sync_context_t{};
    if (context == nullptr) {
        return TAB5_ERR_NO_MEM;
    }
    context->done = done;
    context->out = out_aps;
    context->capacity = max_aps;
    context->count = 0;
    context->active = true;
    auto callback = [](const wifi_ap_record_t *aps, int count, void *opaque) {
        wifi_sync_context_t *ctx = static_cast<wifi_sync_context_t *>(opaque);
        std::lock_guard<std::mutex> callback_guard(callback_mutex);
        if (!ctx->active) {
            delete ctx;
            return;
        }
        if (aps != nullptr && count > 0) {
            const uint32_t copy_count = (uint32_t)count < ctx->capacity ? (uint32_t)count : ctx->capacity;
            for (uint32_t i = 0; i < copy_count; ++i) {
                strncpy(ctx->out[i].ssid, (const char *)aps[i].ssid, sizeof(ctx->out[i].ssid) - 1);
                ctx->out[i].ssid[sizeof(ctx->out[i].ssid) - 1] = '\0';
                ctx->out[i].rssi = aps[i].rssi;
                ctx->out[i].authmode = (uint8_t)aps[i].authmode;
            }
            ctx->count = copy_count;
        }
        xSemaphoreGive(ctx->done);
        ctx->active = false;
    };
    while (xSemaphoreTake(done, 0) == pdTRUE) {
    }
    if (timeout_ms == 0) {
        timeout_ms = WIFI_SCAN_SYNC_TIMEOUT_MS;
    }
    bool scan_started = false;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        const esp_err_t err = wifi_mgr_scan(callback, context);
        if (err == ESP_OK) {
            scan_started = true;
            const TickType_t now = xTaskGetTickCount();
            const TickType_t remaining = (int32_t)(now - deadline) >= 0 ? 0 : deadline - now;
            if (xSemaphoreTake(done, remaining) != pdTRUE) {
                std::lock_guard<std::mutex> callback_guard(callback_mutex);
                context->active = false;
                const bool callback_pending = wifi_mgr_cancel_scan(callback, context);
                *out_count = 0;
                if (!callback_pending) {
                    delete context;
                }
                return TAB5_ERR_TIMEOUT;
            }
            *out_count = context->count;
            delete context;
            return TAB5_OK;
        }
        if (err != ESP_ERR_INVALID_STATE || (int32_t)(xTaskGetTickCount() - deadline) >= 0) {
            std::lock_guard<std::mutex> callback_guard(callback_mutex);
            context->active = false;
            const bool callback_pending = scan_started && wifi_mgr_cancel_scan(callback, context);
            if (!callback_pending) {
                delete context;
            }
            return err == ESP_ERR_INVALID_STATE ? TAB5_ERR_TIMEOUT : TAB5_ERR_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
#else
    (void)timeout_ms;
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

tab5_err_t tab5_wifi_scan(tab5_wifi_ap_t *out_aps, uint32_t max_aps, uint32_t *out_count)
{
    return tab5_wifi_scan_with_timeout(out_aps, max_aps, out_count, WIFI_SCAN_SYNC_TIMEOUT_MS);
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

tab5_err_t tab5_bt_scan_with_timeout(tab5_bt_dev_t *out_devs, uint32_t max_devs, uint32_t *out_count,
                                     uint32_t timeout_ms)
{
    if (out_devs == nullptr || max_devs == 0 || out_count == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    *out_count = 0;
#if defined(ESP_PLATFORM)
    if (!bt_mgr_is_enabled()) {
        return TAB5_ERR_INVALID_STATE;
    }
    struct bt_sync_context_t {
        SemaphoreHandle_t done;
        tab5_bt_dev_t *out;
        uint32_t capacity;
        uint32_t count;
        bool active;
    };
    static SemaphoreHandle_t done = nullptr;
    static std::mutex sync_mutex;
    static std::mutex callback_mutex;
    std::lock_guard<std::mutex> guard(sync_mutex);
    if (done == nullptr) {
        done = xSemaphoreCreateBinary();
        if (done == nullptr) {
            return TAB5_ERR_NO_MEM;
        }
    }
    auto *context = new bt_sync_context_t{};
    if (context == nullptr) {
        return TAB5_ERR_NO_MEM;
    }
    context->done = done;
    context->out = out_devs;
    context->capacity = max_devs;
    context->count = 0;
    context->active = true;
    auto callback = [](const bt_device_info_t *devices, int count, void *opaque) {
        bt_sync_context_t *ctx = static_cast<bt_sync_context_t *>(opaque);
        std::lock_guard<std::mutex> callback_guard(callback_mutex);
        if (!ctx->active) {
            delete ctx;
            return;
        }
        if (devices != nullptr && count > 0) {
            const uint32_t copy_count = (uint32_t)count < ctx->capacity ? (uint32_t)count : ctx->capacity;
            for (uint32_t i = 0; i < copy_count; ++i) {
                strncpy(ctx->out[i].mac, devices[i].mac, sizeof(ctx->out[i].mac) - 1);
                strncpy(ctx->out[i].name, devices[i].name, sizeof(ctx->out[i].name) - 1);
                ctx->out[i].mac[sizeof(ctx->out[i].mac) - 1] = '\0';
                ctx->out[i].name[sizeof(ctx->out[i].name) - 1] = '\0';
                ctx->out[i].rssi = devices[i].rssi;
                ctx->out[i].type = (uint8_t)devices[i].type;
                ctx->out[i].connected = devices[i].connected;
                ctx->out[i].paired = devices[i].paired;
            }
            ctx->count = copy_count;
        }
        xSemaphoreGive(ctx->done);
        ctx->active = false;
    };
    while (xSemaphoreTake(done, 0) == pdTRUE) {
    }
    if (timeout_ms == 0) {
        timeout_ms = BT_SCAN_SYNC_TIMEOUT_MS;
    }
    bool scan_started = false;
    auto abandon_context = [&]() {
        std::lock_guard<std::mutex> callback_guard(callback_mutex);
        context->active = false;
        /* Once accepted by bt_mgr_scan, ownership moves to its eventual
         * callback.  Before that point no callback can arrive, so release the
         * context here instead of leaking it on an early error/timeout. */
        if (!scan_started) {
            delete context;
        }
    };
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    while ((int32_t)(xTaskGetTickCount() - deadline) < 0) {
        err = bt_mgr_scan(callback, context);
        if (err == ESP_OK) {
            break;
        }
        if (err != ESP_ERR_INVALID_STATE) {
            abandon_context();
            return err == ESP_ERR_TIMEOUT ? TAB5_ERR_TIMEOUT : TAB5_ERR_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (err != ESP_OK) {
        abandon_context();
        return TAB5_ERR_TIMEOUT;
    }
    scan_started = true;
    const TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - deadline) >= 0) {
        std::lock_guard<std::mutex> callback_guard(callback_mutex);
        context->active = false;
        return TAB5_ERR_TIMEOUT;
    }
    const TickType_t remaining = deadline - now;
    if (xSemaphoreTake(done, remaining) != pdTRUE) {
        std::lock_guard<std::mutex> callback_guard(callback_mutex);
        context->active = false;
        return TAB5_ERR_TIMEOUT;
    }
    *out_count = context->count;
    delete context;
    return TAB5_OK;
#else
    (void)timeout_ms;
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

tab5_err_t tab5_bt_scan(tab5_bt_dev_t *out_devs, uint32_t max_devs, uint32_t *out_count)
{
    return tab5_bt_scan_with_timeout(out_devs, max_devs, out_count, BT_SCAN_SYNC_TIMEOUT_MS);
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

/* ========================================================================= */
/* Cliente AI                                                                */
/* ========================================================================= */

static char s_ai_response_buf[4096] = {0};
static char s_ai_error_buf[256] = {0};
static volatile int s_ai_state = 0;

#if defined(ESP_PLATFORM)
static std::vector<ai_msg_t> s_ai_history;

static void on_ai_response_cb(const char *response_text, void *user_data)
{
    (void)user_data;
    if (response_text != nullptr) {
        strncpy(s_ai_response_buf, response_text, sizeof(s_ai_response_buf) - 1);
        s_ai_response_buf[sizeof(s_ai_response_buf) - 1] = '\0';
    }
    if (!s_ai_history.empty()) {
        s_ai_history.push_back({"assistant", response_text ? response_text : ""});
    }
    s_ai_state = 2;
}

static void on_ai_state_cb(ai_state_t state, const char *status_msg, void *user_data)
{
    (void)user_data;
    (void)status_msg;
    if (state == AI_STATE_CONNECTING || state == AI_STATE_SENDING || state == AI_STATE_RECEIVING) {
        s_ai_state = 1;
    } else if (state == AI_STATE_ERROR) {
        s_ai_state = 3;
        if (status_msg != nullptr) {
            strncpy(s_ai_error_buf, status_msg, sizeof(s_ai_error_buf) - 1);
            s_ai_error_buf[sizeof(s_ai_error_buf) - 1] = '\0';
        }
    } else if (state == AI_STATE_CANCELLED) {
        s_ai_state = 4;
    }
}
#endif

tab5_err_t tab5_ai_send(const char *prompt)
{
#if defined(ESP_PLATFORM)
    if (prompt == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    if (ai_client_is_busy()) {
        return TAB5_ERR_INVALID_STATE;
    }

    ai_cfg_t cfg;
    ai_storage_load(&cfg);

    s_ai_response_buf[0] = '\0';
    s_ai_error_buf[0] = '\0';
    s_ai_state = 1;

    std::string trimmed = prompt;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' ')) {
        trimmed.pop_back();
    }
    while (!trimmed.empty() && (trimmed.front() == '\n' || trimmed.front() == '\r' || trimmed.front() == ' ')) {
        trimmed.erase(trimmed.begin());
    }

    if (trimmed.empty()) {
        s_ai_state = 0;
        return TAB5_ERR_INVALID_ARG;
    }

    s_ai_history.push_back({"user", trimmed});

    esp_err_t err = ai_client_send(&cfg, s_ai_history, on_ai_response_cb, on_ai_state_cb, nullptr);
    if (err != ESP_OK) {
        s_ai_state = 3;
        strncpy(s_ai_error_buf, "Falha ao iniciar comunicacao", sizeof(s_ai_error_buf) - 1);
        return TAB5_ERR_FAIL;
    }
    return TAB5_OK;
#else
    (void)prompt;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ai_cancel(void)
{
#if defined(ESP_PLATFORM)
    ai_client_cancel();
    s_ai_state = 4;
    return TAB5_OK;
#else
    return TAB5_OK;
#endif
}

bool tab5_ai_is_busy(void)
{
#if defined(ESP_PLATFORM)
    return ai_client_is_busy();
#else
    return false;
#endif
}

tab5_err_t tab5_ai_config_load(tab5_ai_config_t *out_cfg)
{
    if (out_cfg == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    ai_cfg_t internal;
    ai_storage_load(&internal);
    memset(out_cfg, 0, sizeof(*out_cfg));
    strncpy(out_cfg->base_url, internal.base_url, sizeof(out_cfg->base_url) - 1);
    strncpy(out_cfg->token, internal.token, sizeof(out_cfg->token) - 1);
    strncpy(out_cfg->model, internal.model, sizeof(out_cfg->model) - 1);
    out_cfg->max_tokens = internal.max_tokens;
    return TAB5_OK;
#else
    memset(out_cfg, 0, sizeof(*out_cfg));
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ai_config_save(const tab5_ai_config_t *cfg)
{
    if (cfg == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    ai_cfg_t internal;
    ai_storage_get_default(&internal);
    strncpy(internal.base_url, cfg->base_url, sizeof(internal.base_url) - 1);
    strncpy(internal.token, cfg->token, sizeof(internal.token) - 1);
    strncpy(internal.model, cfg->model, sizeof(internal.model) - 1);
    internal.max_tokens = cfg->max_tokens;
    return ai_storage_save(&internal) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
#else
    (void)cfg;
    return TAB5_OK;
#endif
}

int32_t tab5_ai_get_state(void)
{
    return (int32_t)s_ai_state;
}

const char *tab5_ai_get_response(void)
{
    return s_ai_response_buf[0] != '\0' ? s_ai_response_buf : nullptr;
}

const char *tab5_ai_get_error(void)
{
    return s_ai_error_buf[0] != '\0' ? s_ai_error_buf : nullptr;
}

void tab5_ai_consume_response(void)
{
    s_ai_response_buf[0] = '\0';
    s_ai_error_buf[0] = '\0';
    s_ai_state = 0;
}

/* ========================================================================= */
/* Associacao de Arquivos                                                    */
/* ========================================================================= */

tab5_err_t tab5_file_assoc_open(const char *path)
{
    if (path == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    file_assoc_open(path);
    return TAB5_OK;
#else
    return TAB5_OK;
#endif
}

/* ========================================================================= */
/* NVS Generico                                                              */
/* ========================================================================= */

tab5_err_t tab5_nvs_get_u8(const char *ns, const char *key, uint8_t *out_val)
{
    if (ns == nullptr || key == nullptr || out_val == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    return tab5_nvs_worker_get_u8(ns, key, out_val);
#else
    *out_val = 0;
    return TAB5_ERR_NOT_FOUND;
#endif
}

tab5_err_t tab5_nvs_set_u8(const char *ns, const char *key, uint8_t val)
{
    if (ns == nullptr || key == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if defined(ESP_PLATFORM)
    return tab5_nvs_worker_set_u8(ns, key, val);
#else
    (void)val;
    return TAB5_OK;
#endif
}

} // extern "C"

/* ========================================================================= */
/* Wrappers de Exportação para WAMR (recebem wasm_exec_env_t no 1º argumento) */
/* ========================================================================= */

#if defined(ESP_PLATFORM) || defined(TAB5_SIM) || defined(TAB5_SIMULATOR)
#include "wasm_export.h"
#if defined(ESP_PLATFORM)
#include "esp_rom_sys.h"
#endif
#define HAVE_WAMR_ENV 1
#else
#define HAVE_WAMR_ENV 0
typedef void *wasm_exec_env_t;
typedef void *wasm_module_inst_t;
#define wasm_runtime_get_module_inst(env) ((void *)(env))
#define wasm_runtime_addr_app_to_native(inst, addr) ((void *)(uintptr_t)(addr))
#endif

namespace {

#if HAVE_WAMR_ENV
static wasm_module_inst_t wasm_module(wasm_exec_env_t exec_env)
{
    return wasm_runtime_get_module_inst(exec_env);
}
#endif

template <typename T> static T *wasm_arg(wasm_exec_env_t env, T *app_ptr)
{
    (void)env;
    /* WAMR already translates parameters declared as '*' or '$' in the
     * native symbol signature. Converting them again treats a native pointer
     * as a WASM offset and silently invalidates otherwise valid arguments. */
    return app_ptr;
}

static const char *wasm_string(wasm_exec_env_t env, const char *app_ptr)
{
    (void)env;
    return app_ptr;
}

static tab5_err_t wasm_tab5_lifecycle_register(wasm_exec_env_t exec_env, const tab5_lifecycle_callbacks_t *cbs)
{
    const tab5_lifecycle_callbacks_t *native_cbs = wasm_arg(exec_env, const_cast<tab5_lifecycle_callbacks_t *>(cbs));
    if (native_cbs == nullptr)
        return TAB5_ERR_INVALID_ARG;
    return tab5_lifecycle_register(native_cbs);
}

static tab5_err_t wasm_tab5_sound_play_beep(wasm_exec_env_t exec_env, uint32_t freq_hz, uint32_t duration_ms)
{
    (void)exec_env;
    return tab5_sound_play_beep(freq_hz, duration_ms);
}

static tab5_err_t wasm_tab5_storage_get_app_dir(wasm_exec_env_t exec_env, char *out_buf, size_t buf_size)
{
    char *native_buf = wasm_arg(exec_env, out_buf);
    return native_buf != nullptr ? tab5_storage_get_app_dir(native_buf, buf_size) : TAB5_ERR_INVALID_ARG;
}

static tab5_err_t wasm_tab5_storage_mkdir(wasm_exec_env_t exec_env, const char *path)
{
    return tab5_storage_mkdir(wasm_string(exec_env, path));
}

static tab5_err_t wasm_tab5_storage_write_file(wasm_exec_env_t exec_env, const char *path, const char *data,
                                               size_t data_len)
{
    return tab5_storage_write_file(wasm_string(exec_env, path), wasm_string(exec_env, data), data_len);
}

static tab5_err_t wasm_tab5_storage_read_file(wasm_exec_env_t exec_env, const char *path, char *data, size_t data_size,
                                              size_t *out_len)
{
    return tab5_storage_read_file(wasm_string(exec_env, path), wasm_arg(exec_env, data), data_size,
                                  wasm_arg(exec_env, out_len));
}

static tab5_err_t wasm_tab5_storage_path_resolve(wasm_exec_env_t exec_env, const char *in_path, char *out_path,
                                                 size_t out_size, bool write_access)
{
    const char *native_in = wasm_string(exec_env, in_path);
    char *native_out = wasm_arg(exec_env, out_path);
    if (native_in == nullptr || native_out == nullptr)
        return TAB5_ERR_INVALID_ARG;
    return tab5_storage_path_resolve(native_in, native_out, out_size, write_access);
}

static tab5_err_t wasm_tab5_storage_remove(wasm_exec_env_t exec_env, const char *path)
{
    return tab5_storage_remove(wasm_string(exec_env, path));
}

static tab5_err_t wasm_tab5_storage_scandir(wasm_exec_env_t exec_env, const char *rel_or_abs_path,
                                            tab5_dir_entry_t *entries, uint32_t max_entries, uint32_t *out_count)
{
    return tab5_storage_scandir(wasm_string(exec_env, rel_or_abs_path), wasm_arg(exec_env, entries), max_entries,
                                wasm_arg(exec_env, out_count));
}

static tab5_err_t wasm_tab5_system_get_battery(wasm_exec_env_t exec_env, tab5_battery_info_t *out_info)
{
    return tab5_system_get_battery(wasm_arg(exec_env, out_info));
}

static tab5_err_t wasm_tab5_system_get_bt_status(wasm_exec_env_t exec_env, tab5_bt_info_t *out_info)
{
    return tab5_system_get_bt_status(wasm_arg(exec_env, out_info));
}

static tab5_err_t wasm_tab5_system_get_time(wasm_exec_env_t exec_env, int64_t *out_epoch, struct tm *out_time)
{
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_module(exec_env);
    if (module_inst != nullptr) {
        int64_t *native_epoch =
            out_epoch ? (int64_t *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_epoch)
                      : nullptr;
        struct tm *native_time =
            out_time ? (struct tm *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_time)
                     : nullptr;
        return tab5_system_get_time(native_epoch, native_time);
    }
#else
    (void)exec_env;
#endif
    return tab5_system_get_time(wasm_arg(exec_env, out_epoch), wasm_arg(exec_env, out_time));
}

static tab5_err_t wasm_tab5_system_get_timestamp(wasm_exec_env_t exec_env, char *out_buf, size_t buf_size)
{
    char *native_buf = wasm_arg(exec_env, out_buf);
    return native_buf != nullptr ? tab5_system_get_timestamp(native_buf, buf_size) : TAB5_ERR_INVALID_ARG;
}

static tab5_err_t wasm_tab5_system_get_wifi_status(wasm_exec_env_t exec_env, tab5_wifi_info_t *out_info)
{
    return tab5_system_get_wifi_status(wasm_arg(exec_env, out_info));
}

static void wasm_tab5_system_log(wasm_exec_env_t exec_env, int level, const char *tag, const char *message)
{
    tab5_system_log(level, wasm_string(exec_env, tag) != nullptr ? wasm_string(exec_env, tag) : "wasm",
                    wasm_string(exec_env, message) != nullptr ? wasm_string(exec_env, message) : "");
}

static tab5_ui_obj_t wasm_tab5_ui_app_bar_add_action_button(wasm_exec_env_t exec_env, const char *sym,
                                                            void (*on_click)(void *user_data), void *user_data)
{
    (void)exec_env;
    return tab5_ui_app_bar_add_action_button(wasm_string(exec_env, sym), on_click, user_data);
}

static tab5_err_t wasm_tab5_ui_app_bar_set_title(wasm_exec_env_t exec_env, const char *title)
{
    return tab5_ui_app_bar_set_title(wasm_string(exec_env, title));
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

static int32_t wasm_tab5_ui_keyboard_get_height(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_ui_keyboard_get_height();
}

static void wasm_tab5_ui_get_display_size(wasm_exec_env_t exec_env, int32_t *out_w, int32_t *out_h)
{
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_module(exec_env);
    if (module_inst != nullptr) {
        int32_t *native_w =
            out_w ? (int32_t *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_w) : nullptr;
        int32_t *native_h =
            out_h ? (int32_t *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_h) : nullptr;
        tab5_ui_get_display_size(native_w, native_h);
        return;
    }
#else
    (void)exec_env;
#endif
    tab5_ui_get_display_size(wasm_arg(exec_env, out_w), wasm_arg(exec_env, out_h));
}

static tab5_ui_obj_t wasm_tab5_ui_textarea_create(wasm_exec_env_t exec_env, tab5_ui_obj_t parent)
{
    (void)exec_env;
    return tab5_ui_textarea_create(parent);
}

static tab5_err_t wasm_tab5_ui_keyboard_show(wasm_exec_env_t exec_env, tab5_ui_obj_t target_textarea)
{
    (void)exec_env;
    return tab5_ui_keyboard_show(target_textarea);
}

static tab5_err_t wasm_tab5_ui_show_toast(wasm_exec_env_t exec_env, const char *message, uint32_t duration_ms)
{
    (void)exec_env;
    return tab5_ui_show_toast(wasm_string(exec_env, message), duration_ms);
}

/* The WASM import is (i,i,i)->i: a C char * is an i32 offset in wasm32.
 * Keep the wrapper argument wide enough for the no-WAMR simulator, where the
 * same C import is called with a native host pointer.  On WAMR it remains an
 * app offset and is narrowed only at the single validation/conversion point. */
static int32_t wasm_tab5_ui_textarea_copy_text(wasm_exec_env_t exec_env, tab5_ui_obj_t ta, uintptr_t buffer_ptr,
                                               uint32_t capacity)
{
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_module(exec_env);
    if (module_inst == nullptr || (capacity > 0 && buffer_ptr == 0) || buffer_ptr > UINT32_MAX) {
        return TAB5_ERR_INVALID_ARG;
    }
    const uint32_t app_buffer_ptr = (uint32_t)buffer_ptr;
    if (capacity > 0 && !wasm_runtime_validate_app_addr(module_inst, app_buffer_ptr, capacity)) {
        return TAB5_ERR_INVALID_ARG;
    }
    char *native_buffer = capacity > 0 ? (char *)wasm_runtime_addr_app_to_native(module_inst, app_buffer_ptr) : nullptr;
    if (capacity > 0 && native_buffer == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    return tab5_ui_textarea_copy_text(ta, native_buffer, capacity);
#else
    (void)exec_env;
    return tab5_ui_textarea_copy_text(ta, (char *)buffer_ptr, capacity);
#endif
}

static tab5_err_t wasm_tab5_ui_textarea_set_placeholder(wasm_exec_env_t exec_env, tab5_ui_obj_t ta,
                                                        const char *placeholder)
{
    (void)exec_env;
    return tab5_ui_textarea_set_placeholder(ta, wasm_string(exec_env, placeholder));
}

static tab5_err_t wasm_tab5_ui_textarea_set_text(wasm_exec_env_t exec_env, tab5_ui_obj_t ta, const char *text)
{
    (void)exec_env;
    return tab5_ui_textarea_set_text(ta, wasm_string(exec_env, text));
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

static tab5_err_t wasm_tab5_ui_obj_set_scrollable(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, bool scrollable)
{
    (void)exec_env;
    return tab5_ui_obj_set_scrollable(obj, scrollable);
}

static tab5_err_t wasm_tab5_ui_obj_scroll_to_bottom(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, bool animated)
{
    (void)exec_env;
    return tab5_ui_obj_scroll_to_bottom(obj, animated);
}

static tab5_err_t wasm_tab5_ui_obj_scroll_to_top(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, bool animated)
{
    (void)exec_env;
    return tab5_ui_obj_scroll_to_top(obj, animated);
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
    return tab5_ui_label_create(parent, wasm_string(exec_env, text));
}

static tab5_err_t wasm_tab5_ui_label_set_text(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, const char *text)
{
    (void)exec_env;
    return tab5_ui_label_set_text(obj, wasm_string(exec_env, text));
}

static tab5_err_t wasm_tab5_ui_label_set_wrap(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, bool wrap)
{
    (void)exec_env;
    return tab5_ui_label_set_wrap(obj, wrap);
}

static tab5_ui_obj_t wasm_tab5_ui_btn_create(wasm_exec_env_t exec_env, tab5_ui_obj_t parent,
                                             const char *label_or_symbol)
{
    (void)exec_env;
    return tab5_ui_btn_create(parent, wasm_string(exec_env, label_or_symbol));
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
    return tab5_ui_list_add_btn(list, wasm_string(exec_env, symbol), wasm_string(exec_env, text));
}

static tab5_err_t wasm_tab5_ui_obj_clean(wasm_exec_env_t exec_env, tab5_ui_obj_t obj)
{
    (void)exec_env;
    return tab5_ui_obj_clean(obj);
}

static tab5_err_t wasm_tab5_ui_obj_clean_deferred(wasm_exec_env_t exec_env, tab5_ui_obj_t obj)
{
    (void)exec_env;
    return tab5_ui_obj_clean_deferred(obj);
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

static tab5_err_t wasm_tab5_ui_obj_set_style_text_size(wasm_exec_env_t exec_env, tab5_ui_obj_t obj, int32_t size_px)
{
    (void)exec_env;
    return tab5_ui_obj_set_style_text_size(obj, size_px);
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
    wasm_module_inst_t module_inst = wasm_module(exec_env);
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
    return tab5_recorder_play(wasm_string(exec_env, path));
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
    char *native_buf = wasm_arg(exec_env, out_buf);
    const char *native_cmd = wasm_string(exec_env, cmd);
    if (native_buf == nullptr || native_cmd == nullptr)
        return TAB5_ERR_INVALID_ARG;
    return tab5_terminal_exec(native_cmd, native_buf, buf_size);
}

/* ========================================================================= */
/* Player de Música Wrappers                                                 */
/* ========================================================================= */

static tab5_err_t wasm_tab5_music_play(wasm_exec_env_t exec_env, const char *filepath)
{
    return tab5_music_play(wasm_string(exec_env, filepath));
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
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_module(exec_env);
    if (module_inst != nullptr && out_status != nullptr) {
        tab5_music_status_t *native_status =
            (tab5_music_status_t *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_status);
        if (native_status != nullptr) {
            return tab5_music_get_status(native_status);
        }
        return TAB5_ERR_INVALID_ARG;
    }
#else
    (void)exec_env;
#endif
    return tab5_music_get_status(out_status);
}

/* ========================================================================= */
/* Wi-Fi Wrappers                                                            */
/* ========================================================================= */

static tab5_err_t wasm_tab5_wifi_scan(wasm_exec_env_t exec_env, tab5_wifi_ap_t *out_aps, uint32_t max_aps,
                                      uint32_t *out_count)
{
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_module(exec_env);
    if (module_inst != nullptr) {
        tab5_wifi_ap_t *native_aps =
            out_aps ? (tab5_wifi_ap_t *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_aps)
                    : nullptr;
        uint32_t *native_count =
            out_count ? (uint32_t *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_count)
                      : nullptr;
        if ((out_aps != nullptr && native_aps == nullptr) || (out_count != nullptr && native_count == nullptr))
            return TAB5_ERR_INVALID_ARG;
        return tab5_wifi_scan(native_aps, max_aps, native_count);
    }
#else
    (void)exec_env;
#endif
    return tab5_wifi_scan(out_aps, max_aps, out_count);
}

static tab5_err_t wasm_tab5_wifi_connect(wasm_exec_env_t exec_env, const char *ssid, const char *password)
{
    return tab5_wifi_connect(wasm_string(exec_env, ssid), wasm_string(exec_env, password));
}

static tab5_err_t wasm_tab5_wifi_disconnect(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_wifi_disconnect();
}

static tab5_err_t wasm_tab5_wifi_forget(wasm_exec_env_t exec_env, const char *ssid)
{
    return tab5_wifi_forget(wasm_string(exec_env, ssid));
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
    /* The WAMR signature (*i*)i already supplies native pointers here.  Do
     * not translate them a second time: a native address is not a WASM
     * linear-memory offset.  The old conversion rejected valid buffers (and
     * could fault) before bt_mgr_scan was reached. */
    return tab5_bt_scan(out_devs, max_devs, out_count);
}

static tab5_err_t wasm_tab5_bt_connect(wasm_exec_env_t exec_env, const char *mac, const char *name, uint32_t dev_type)
{
    return tab5_bt_connect(wasm_string(exec_env, mac), wasm_string(exec_env, name), dev_type);
}

static tab5_err_t wasm_tab5_bt_disconnect(wasm_exec_env_t exec_env, const char *mac)
{
    (void)exec_env;
    return tab5_bt_disconnect(wasm_string(exec_env, mac));
}

static tab5_err_t wasm_tab5_bt_forget(wasm_exec_env_t exec_env, const char *mac)
{
    (void)exec_env;
    return tab5_bt_forget(wasm_string(exec_env, mac));
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

/* ========================================================================= */
/* AI Client Wrappers                                                        */
/* ========================================================================= */

static tab5_err_t wasm_tab5_ai_send(wasm_exec_env_t exec_env, const char *prompt)
{
    return tab5_ai_send(wasm_string(exec_env, prompt));
}

static tab5_err_t wasm_tab5_ai_cancel(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_ai_cancel();
}

static bool wasm_tab5_ai_is_busy(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_ai_is_busy();
}

static tab5_err_t wasm_tab5_ai_config_load(wasm_exec_env_t exec_env, tab5_ai_config_t *out_cfg)
{
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_module(exec_env);
    if (module_inst != nullptr && out_cfg != nullptr) {
        tab5_ai_config_t *native_cfg =
            (tab5_ai_config_t *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)out_cfg);
        if (native_cfg != nullptr) {
            return tab5_ai_config_load(native_cfg);
        }
        return TAB5_ERR_INVALID_ARG;
    }
#else
    (void)exec_env;
#endif
    return tab5_ai_config_load(out_cfg);
}

static tab5_err_t wasm_tab5_ai_config_save(wasm_exec_env_t exec_env, const tab5_ai_config_t *cfg)
{
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_module(exec_env);
    if (module_inst != nullptr && cfg != nullptr) {
        const tab5_ai_config_t *native_cfg =
            (const tab5_ai_config_t *)wasm_runtime_addr_app_to_native(module_inst, (uint32_t)(uintptr_t)cfg);
        if (native_cfg != nullptr) {
            return tab5_ai_config_save(native_cfg);
        }
        return TAB5_ERR_INVALID_ARG;
    }
#else
    (void)exec_env;
#endif
    return tab5_ai_config_save(cfg);
}

static int32_t wasm_tab5_ai_get_state(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return tab5_ai_get_state();
}

static uint32_t wasm_tab5_ai_get_response(wasm_exec_env_t exec_env)
{
    const char *text = tab5_ai_get_response();
    if (text == nullptr) {
        return 0;
    }
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_module(exec_env);
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
    // cppcheck-suppress CastAddressToIntegerAtReturn -- simulator fallback ABI
    return (uint32_t)(uintptr_t)text;
#endif
}

static uint32_t wasm_tab5_ai_get_error(wasm_exec_env_t exec_env)
{
    const char *text = tab5_ai_get_error();
    if (text == nullptr) {
        return 0;
    }
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_module(exec_env);
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
    // cppcheck-suppress CastAddressToIntegerAtReturn -- simulator fallback ABI
    return (uint32_t)(uintptr_t)text;
#endif
}

static void wasm_tab5_ai_consume_response(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    tab5_ai_consume_response();
}

static tab5_err_t wasm_tab5_file_assoc_open(wasm_exec_env_t exec_env, const char *path)
{
    (void)exec_env;
    return tab5_file_assoc_open(path);
}

static tab5_err_t wasm_tab5_nvs_get_u8(wasm_exec_env_t exec_env, const char *ns, const char *key, uint8_t *out_val)
{
    return tab5_nvs_get_u8(wasm_string(exec_env, ns), wasm_string(exec_env, key), wasm_arg(exec_env, out_val));
}

static tab5_err_t wasm_tab5_nvs_set_u8(wasm_exec_env_t exec_env, const char *ns, const char *key, uint8_t val)
{
    (void)exec_env;
    return tab5_nvs_set_u8(ns, key, val);
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
    {"tab5_storage_write_file", (void *)wasm_tab5_storage_write_file, "($$i)i", nullptr},
    {"tab5_storage_read_file", (void *)wasm_tab5_storage_read_file, "($*i*)i", nullptr},
    {"tab5_storage_path_resolve", (void *)wasm_tab5_storage_path_resolve, "($*ii)i", nullptr},
    {"tab5_storage_remove", (void *)wasm_tab5_storage_remove, "($)i", nullptr},
    {"tab5_storage_scandir", (void *)wasm_tab5_storage_scandir, "($*i*)i", nullptr},
    {"tab5_system_get_battery", (void *)wasm_tab5_system_get_battery, "(*)i", nullptr},
    {"tab5_system_get_bt_status", (void *)wasm_tab5_system_get_bt_status, "(*)i", nullptr},
    {"tab5_system_get_time", (void *)wasm_tab5_system_get_time, "(**)i", nullptr},
    {"tab5_system_get_timestamp", (void *)wasm_tab5_system_get_timestamp, "(*i)i", nullptr},
    {"tab5_system_get_wifi_status", (void *)wasm_tab5_system_get_wifi_status, "(*)i", nullptr},
    {"tab5_system_log", (void *)wasm_tab5_system_log, "(i$$)", nullptr},
    {"tab5_ui_app_bar_add_action_button", (void *)wasm_tab5_ui_app_bar_add_action_button, "($ii)i", nullptr},
    {"tab5_ui_app_bar_set_title", (void *)wasm_tab5_ui_app_bar_set_title, "($)i", nullptr},
    {"tab5_ui_get_main_textarea", (void *)wasm_tab5_ui_get_main_textarea, "()i", nullptr},
    {"tab5_ui_get_screen", (void *)wasm_tab5_ui_get_screen, "()i", nullptr},
    {"tab5_ui_keyboard_hide", (void *)wasm_tab5_ui_keyboard_hide, "()i", nullptr},
    {"tab5_ui_keyboard_is_visible", (void *)wasm_tab5_ui_keyboard_is_visible, "()i", nullptr},
    {"tab5_ui_keyboard_show", (void *)wasm_tab5_ui_keyboard_show, "(i)i", nullptr},
    {"tab5_ui_keyboard_get_height", (void *)wasm_tab5_ui_keyboard_get_height, "()i", nullptr},
    {"tab5_ui_get_display_size", (void *)wasm_tab5_ui_get_display_size, "(**)", nullptr},
    {"tab5_ui_show_toast", (void *)wasm_tab5_ui_show_toast, "($i)i", nullptr},
    {"tab5_ui_textarea_create", (void *)wasm_tab5_ui_textarea_create, "(i)i", nullptr},
    {"tab5_ui_textarea_copy_text", (void *)wasm_tab5_ui_textarea_copy_text, "(iii)i", nullptr},
    {"tab5_ui_textarea_set_placeholder", (void *)wasm_tab5_ui_textarea_set_placeholder, "(i$)i", nullptr},
    {"tab5_ui_textarea_set_text", (void *)wasm_tab5_ui_textarea_set_text, "(i$)i", nullptr},
    {"tab5_ui_textarea_set_cursor_pos", (void *)wasm_tab5_ui_textarea_set_cursor_pos, "(ii)i", nullptr},
    {"tab5_ui_textarea_get_cursor_pos", (void *)wasm_tab5_ui_textarea_get_cursor_pos, "(i)i", nullptr},
    {"tab5_ui_textarea_set_password_mode", (void *)wasm_tab5_ui_textarea_set_password_mode, "(ii)i", nullptr},
    {"tab5_ui_container_create", (void *)wasm_tab5_ui_container_create, "(i)i", nullptr},
    {"tab5_ui_obj_set_size", (void *)wasm_tab5_ui_obj_set_size, "(iii)i", nullptr},
    {"tab5_ui_obj_set_scrollable", (void *)wasm_tab5_ui_obj_set_scrollable, "(ii)i", nullptr},
    {"tab5_ui_obj_scroll_to_bottom", (void *)wasm_tab5_ui_obj_scroll_to_bottom, "(ii)i", nullptr},
    {"tab5_ui_obj_scroll_to_top", (void *)wasm_tab5_ui_obj_scroll_to_top, "(ii)i", nullptr},
    {"tab5_ui_obj_set_align", (void *)wasm_tab5_ui_obj_set_align, "(iiii)i", nullptr},
    {"tab5_ui_obj_set_flex_flow", (void *)wasm_tab5_ui_obj_set_flex_flow, "(ii)i", nullptr},
    {"tab5_ui_obj_set_pad", (void *)wasm_tab5_ui_obj_set_pad, "(ii)i", nullptr},
    {"tab5_ui_obj_set_gap", (void *)wasm_tab5_ui_obj_set_gap, "(ii)i", nullptr},
    {"tab5_ui_label_create", (void *)wasm_tab5_ui_label_create, "(i$)i", nullptr},
    {"tab5_ui_label_set_text", (void *)wasm_tab5_ui_label_set_text, "(i$)i", nullptr},
    {"tab5_ui_label_set_wrap", (void *)wasm_tab5_ui_label_set_wrap, "(ii)i", nullptr},
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
    {"tab5_ui_obj_clean_deferred", (void *)wasm_tab5_ui_obj_clean_deferred, "(i)i", nullptr},
    {"tab5_ui_clear_content", (void *)wasm_tab5_ui_clear_content, "()i", nullptr},
    {"tab5_ui_theme_get_color", (void *)wasm_tab5_ui_theme_get_color, "(i)i", nullptr},
    {"tab5_ui_obj_set_style_bg", (void *)wasm_tab5_ui_obj_set_style_bg, "(iii)i", nullptr},
    {"tab5_ui_obj_set_style_border", (void *)wasm_tab5_ui_obj_set_style_border, "(iii)i", nullptr},
    {"tab5_ui_obj_set_style_text_color", (void *)wasm_tab5_ui_obj_set_style_text_color, "(iii)i", nullptr},
    {"tab5_ui_obj_set_style_text_size", (void *)wasm_tab5_ui_obj_set_style_text_size, "(ii)i", nullptr},
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
    {"tab5_bt_is_enabled", (void *)wasm_tab5_bt_is_enabled, "()i", nullptr},
    {"tab5_ai_send", (void *)wasm_tab5_ai_send, "($)i", nullptr},
    {"tab5_ai_cancel", (void *)wasm_tab5_ai_cancel, "()i", nullptr},
    {"tab5_ai_is_busy", (void *)wasm_tab5_ai_is_busy, "()i", nullptr},
    {"tab5_ai_config_load", (void *)wasm_tab5_ai_config_load, "(*)i", nullptr},
    {"tab5_ai_config_save", (void *)wasm_tab5_ai_config_save, "(*)i", nullptr},
    {"tab5_ai_get_state", (void *)wasm_tab5_ai_get_state, "()i", nullptr},
    {"tab5_ai_get_response", (void *)wasm_tab5_ai_get_response, "()i", nullptr},
    {"tab5_ai_get_error", (void *)wasm_tab5_ai_get_error, "()i", nullptr},
    {"tab5_ai_consume_response", (void *)wasm_tab5_ai_consume_response, "()", nullptr},
    {"tab5_file_assoc_open", (void *)wasm_tab5_file_assoc_open, "($)i", nullptr},
    {"tab5_nvs_get_u8", (void *)wasm_tab5_nvs_get_u8, "($$*)i", nullptr},
    {"tab5_nvs_set_u8", (void *)wasm_tab5_nvs_set_u8, "($$i)i", nullptr}};

const tab5_native_symbol_t *tab5_host_abi_get_symbols(uint32_t *out_count)
{
    if (out_count != nullptr) {
        *out_count = sizeof(s_native_symbols) / sizeof(s_native_symbols[0]);
    }
    return s_native_symbols;
}
