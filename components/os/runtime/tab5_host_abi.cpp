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
        ui_app_bar_t bar_struct;
        bar_struct.bar = (lv_obj_t *)s_active_app_ctx->app_bar;
        // Atualiza título se a struct puder ser manipulada
    }
#endif
    strncpy(s_active_app_ctx->app_name, title, sizeof(s_active_app_ctx->app_name) - 1);
    s_active_app_ctx->app_name[sizeof(s_active_app_ctx->app_name) - 1] = '\0';
    return TAB5_OK;
}

tab5_ui_obj_t tab5_ui_app_bar_add_action_button(const char *symbol_or_text, void (*on_click)(void *user_data),
                                                void *user_data)
{
    (void)symbol_or_text;
    (void)on_click;
    (void)user_data;
    if (s_active_app_ctx == nullptr) {
        return nullptr;
    }
    // Suporte a adicionar botão de ação nativo
    return (tab5_ui_obj_t)0x3000;
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
/* Tabela de Exportação de Símbolos Nativos para WAMR                         */
/* ========================================================================= */

static const tab5_native_symbol_t s_native_symbols[] = {
    {"tab5_lifecycle_register", (void *)tab5_lifecycle_register, "(*)i"},
    {"tab5_ui_get_screen", (void *)tab5_ui_get_screen, "()r"},
    {"tab5_ui_app_bar_set_title", (void *)tab5_ui_app_bar_set_title, "($)i"},
    {"tab5_ui_keyboard_show", (void *)tab5_ui_keyboard_show, "(r)i"},
    {"tab5_ui_keyboard_hide", (void *)tab5_ui_keyboard_hide, "()i"},
    {"tab5_ui_keyboard_is_visible", (void *)tab5_ui_keyboard_is_visible, "()i"},
    {"tab5_ui_show_toast", (void *)tab5_ui_show_toast, "($i)i"},
    {"tab5_storage_get_app_dir", (void *)tab5_storage_get_app_dir, "(*i)i"},
    {"tab5_storage_path_resolve", (void *)tab5_storage_path_resolve, "($*ii)i"},
    {"tab5_storage_mkdir", (void *)tab5_storage_mkdir, "($)i"},
    {"tab5_storage_remove", (void *)tab5_storage_remove, "($)i"},
    {"tab5_system_get_battery", (void *)tab5_system_get_battery, "(*)i"},
    {"tab5_system_get_wifi_status", (void *)tab5_system_get_wifi_status, "(*)i"},
    {"tab5_system_get_bt_status", (void *)tab5_system_get_bt_status, "(*)i"},
    {"tab5_system_get_time", (void *)tab5_system_get_time, "(**)i"},
    {"tab5_sound_play_beep", (void *)tab5_sound_play_beep, "(ii)i"},
    {"tab5_system_log", (void *)tab5_system_log, "(i$$)v"}};

const tab5_native_symbol_t *tab5_host_abi_get_symbols(uint32_t *out_count)
{
    if (out_count != nullptr) {
        *out_count = sizeof(s_native_symbols) / sizeof(s_native_symbols[0]);
    }
    return s_native_symbols;
}
