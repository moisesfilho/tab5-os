/**
 * @file tab5_host_abi.cpp
 * @brief Implementação da Tabela de Símbolos Nativos e Dispatcher do Host
 */

#include "tab5_host_abi.h"
#include "tab5_storage_sandbox.h"
#include "tab5_ui_host.h"
#include "tab5_sys_host.h"
#include "tab5_lifecycle_host.h"
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
        if (strcmp(symbol_or_text, "LV_SYMBOL_PLUS") == 0) {
            sym = LV_SYMBOL_PLUS;
        } else if (strcmp(symbol_or_text, "LV_SYMBOL_SAVE") == 0) {
            sym = LV_SYMBOL_SAVE;
        } else if (strcmp(symbol_or_text, "LV_SYMBOL_EDIT") == 0) {
            sym = LV_SYMBOL_EDIT;
        } else if (strcmp(symbol_or_text, "LV_SYMBOL_TRASH") == 0) {
            sym = LV_SYMBOL_TRASH;
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
#define HAVE_WAMR_ENV 1
#else
#define HAVE_WAMR_ENV 0
typedef void *wasm_exec_env_t;
typedef void *wasm_module_inst_t;
#define wasm_runtime_get_module_inst(env) ((void *)(env))
#define wasm_runtime_addr_app_to_native(inst, addr) ((void *)(uintptr_t)(addr))
#endif

namespace {

static inline void *wasm_to_native_ptr(wasm_exec_env_t exec_env, uint32_t wasm_offset)
{
    if (wasm_offset == 0) {
        return nullptr;
    }
#if HAVE_WAMR_ENV
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    if (module_inst == nullptr) {
        return nullptr;
    }
    return wasm_runtime_addr_app_to_native(module_inst, wasm_offset);
#else
    (void)exec_env;
    return (void *)(uintptr_t)wasm_offset;
#endif
}

static tab5_err_t wasm_tab5_lifecycle_register(wasm_exec_env_t exec_env, uint32_t cbs_offset)
{
    const auto *cbs = (const tab5_lifecycle_callbacks_t *)wasm_to_native_ptr(exec_env, cbs_offset);
    return tab5_lifecycle_register(cbs);
}

static tab5_err_t wasm_tab5_sound_play_beep(wasm_exec_env_t exec_env, uint32_t freq_hz, uint32_t duration_ms)
{
    (void)exec_env;
    return tab5_sound_play_beep(freq_hz, duration_ms);
}

static tab5_err_t wasm_tab5_storage_get_app_dir(wasm_exec_env_t exec_env, uint32_t out_buf_offset, size_t buf_size)
{
    char *out_buf = (char *)wasm_to_native_ptr(exec_env, out_buf_offset);
    return tab5_storage_get_app_dir(out_buf, buf_size);
}

static tab5_err_t wasm_tab5_storage_mkdir(wasm_exec_env_t exec_env, uint32_t path_offset)
{
    const char *path = (const char *)wasm_to_native_ptr(exec_env, path_offset);
    return tab5_storage_mkdir(path);
}

static tab5_err_t wasm_tab5_storage_path_resolve(wasm_exec_env_t exec_env, uint32_t in_offset, uint32_t out_offset,
                                                 size_t out_size, bool write_access)
{
    const char *in_path = (const char *)wasm_to_native_ptr(exec_env, in_offset);
    char *out_path = (char *)wasm_to_native_ptr(exec_env, out_offset);
    return tab5_storage_path_resolve(in_path, out_path, out_size, write_access);
}

static tab5_err_t wasm_tab5_storage_remove(wasm_exec_env_t exec_env, uint32_t path_offset)
{
    const char *path = (const char *)wasm_to_native_ptr(exec_env, path_offset);
    return tab5_storage_remove(path);
}

static tab5_err_t wasm_tab5_system_get_battery(wasm_exec_env_t exec_env, uint32_t out_offset)
{
    auto *out_info = (tab5_battery_info_t *)wasm_to_native_ptr(exec_env, out_offset);
    return tab5_system_get_battery(out_info);
}

static tab5_err_t wasm_tab5_system_get_bt_status(wasm_exec_env_t exec_env, uint32_t out_offset)
{
    auto *out_info = (tab5_bt_info_t *)wasm_to_native_ptr(exec_env, out_offset);
    return tab5_system_get_bt_status(out_info);
}

static tab5_err_t wasm_tab5_system_get_time(wasm_exec_env_t exec_env, uint32_t epoch_offset, uint32_t tm_offset)
{
    auto *out_epoch = (int64_t *)wasm_to_native_ptr(exec_env, epoch_offset);
    auto *out_time = (struct tm *)wasm_to_native_ptr(exec_env, tm_offset);
    return tab5_system_get_time(out_epoch, out_time);
}

static tab5_err_t wasm_tab5_system_get_wifi_status(wasm_exec_env_t exec_env, uint32_t out_offset)
{
    auto *out_info = (tab5_wifi_info_t *)wasm_to_native_ptr(exec_env, out_offset);
    return tab5_system_get_wifi_status(out_info);
}

static void wasm_tab5_system_log(wasm_exec_env_t exec_env, int level, uint32_t tag_offset, uint32_t msg_offset)
{
    const char *tag = (const char *)wasm_to_native_ptr(exec_env, tag_offset);
    const char *message = (const char *)wasm_to_native_ptr(exec_env, msg_offset);
    tab5_system_log(level, tag != nullptr ? tag : "wasm", message != nullptr ? message : "");
}

static tab5_ui_obj_t wasm_tab5_ui_app_bar_add_action_button(wasm_exec_env_t exec_env, uint32_t sym_offset,
                                                            void (*on_click)(void *user_data), void *user_data)
{
    const char *sym = (const char *)wasm_to_native_ptr(exec_env, sym_offset);
    return tab5_ui_app_bar_add_action_button(sym, on_click, user_data);
}

static tab5_err_t wasm_tab5_ui_app_bar_set_title(wasm_exec_env_t exec_env, uint32_t title_offset)
{
    const char *title = (const char *)wasm_to_native_ptr(exec_env, title_offset);
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

static tab5_err_t wasm_tab5_ui_show_toast(wasm_exec_env_t exec_env, uint32_t msg_offset, uint32_t duration_ms)
{
    const char *message = (const char *)wasm_to_native_ptr(exec_env, msg_offset);
    return tab5_ui_show_toast(message, duration_ms);
}

static const char *wasm_tab5_ui_textarea_get_text(wasm_exec_env_t exec_env, tab5_ui_obj_t ta)
{
    (void)exec_env;
    return tab5_ui_textarea_get_text(ta);
}

static tab5_err_t wasm_tab5_ui_textarea_set_placeholder(wasm_exec_env_t exec_env, tab5_ui_obj_t ta,
                                                        uint32_t placeholder_offset)
{
    const char *placeholder = (const char *)wasm_to_native_ptr(exec_env, placeholder_offset);
    return tab5_ui_textarea_set_placeholder(ta, placeholder);
}

static tab5_err_t wasm_tab5_ui_textarea_set_text(wasm_exec_env_t exec_env, tab5_ui_obj_t ta, uint32_t text_offset)
{
    const char *text = (const char *)wasm_to_native_ptr(exec_env, text_offset);
    return tab5_ui_textarea_set_text(ta, text);
}

} // namespace

/* ========================================================================= */
/* Tabela de Exportação de Símbolos Nativos para WAMR (ordenada alfabeticamente) */
/* ========================================================================= */

static const tab5_native_symbol_t s_native_symbols[] = {
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
    {"tab5_system_log", (void *)wasm_tab5_system_log, "(i$$)v", nullptr},
    {"tab5_ui_app_bar_add_action_button", (void *)wasm_tab5_ui_app_bar_add_action_button, "($*r)r", nullptr},
    {"tab5_ui_app_bar_set_title", (void *)wasm_tab5_ui_app_bar_set_title, "($)i", nullptr},
    {"tab5_ui_get_main_textarea", (void *)wasm_tab5_ui_get_main_textarea, "()r", nullptr},
    {"tab5_ui_get_screen", (void *)wasm_tab5_ui_get_screen, "()r", nullptr},
    {"tab5_ui_keyboard_hide", (void *)wasm_tab5_ui_keyboard_hide, "()i", nullptr},
    {"tab5_ui_keyboard_is_visible", (void *)wasm_tab5_ui_keyboard_is_visible, "()i", nullptr},
    {"tab5_ui_keyboard_show", (void *)wasm_tab5_ui_keyboard_show, "(r)i", nullptr},
    {"tab5_ui_show_toast", (void *)wasm_tab5_ui_show_toast, "($i)i", nullptr},
    {"tab5_ui_textarea_get_text", (void *)wasm_tab5_ui_textarea_get_text, "(r)$", nullptr},
    {"tab5_ui_textarea_set_placeholder", (void *)wasm_tab5_ui_textarea_set_placeholder, "(r$)i", nullptr},
    {"tab5_ui_textarea_set_text", (void *)wasm_tab5_ui_textarea_set_text, "(r$)i", nullptr}};

const tab5_native_symbol_t *tab5_host_abi_get_symbols(uint32_t *out_count)
{
    if (out_count != nullptr) {
        *out_count = sizeof(s_native_symbols) / sizeof(s_native_symbols[0]);
    }
    return s_native_symbols;
}
