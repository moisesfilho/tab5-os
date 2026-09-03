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
        return nullptr;
    }
    return s_active_app_ctx->root_screen;
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
        return nullptr;
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
        return (tab5_ui_obj_t)btn;
    }
#endif
    return (tab5_ui_obj_t)0x3000;
}

tab5_ui_obj_t tab5_ui_get_main_textarea(void)
{
    if (s_active_app_ctx == nullptr) {
        return nullptr;
    }
    return s_active_app_ctx->content_area;
}

tab5_err_t tab5_ui_textarea_set_text(tab5_ui_obj_t ta, const char *text)
{
    return tab5_ui_host_textarea_set_text(ta, text);
}

const char *tab5_ui_textarea_get_text(tab5_ui_obj_t ta)
{
    return tab5_ui_host_textarea_get_text(ta);
}

tab5_err_t tab5_ui_textarea_set_placeholder(tab5_ui_obj_t ta, const char *placeholder)
{
    return tab5_ui_host_textarea_set_placeholder(ta, placeholder);
}

tab5_err_t tab5_ui_keyboard_show(tab5_ui_obj_t target_textarea)
{
    return tab5_ui_host_keyboard_show(target_textarea);
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
    {"tab5_ui_textarea_set_text", (void *)wasm_tab5_ui_textarea_set_text, "(i$)i", nullptr}};

const tab5_native_symbol_t *tab5_host_abi_get_symbols(uint32_t *out_count)
{
    if (out_count != nullptr) {
        *out_count = sizeof(s_native_symbols) / sizeof(s_native_symbols[0]);
    }
    return s_native_symbols;
}
