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
        };
        auto *cb_data = new action_cb_data_t{on_click, user_data};
        lv_obj_add_event_cb(
            btn,
            [](lv_event_t *e) {
                auto *d = (action_cb_data_t *)lv_event_get_user_data(e);
                if (d && d->cb) {
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
/* Tabela de Exportação de Símbolos Nativos para WAMR                         */
/* ========================================================================= */

static const tab5_native_symbol_t s_native_symbols[] = {
    {"tab5_lifecycle_register", (void *)tab5_lifecycle_register, "(*)i"},
    {"tab5_ui_get_screen", (void *)tab5_ui_get_screen, "()r"},
    {"tab5_ui_get_main_textarea", (void *)tab5_ui_get_main_textarea, "()r"},
    {"tab5_ui_textarea_set_text", (void *)tab5_ui_textarea_set_text, "(r$)i"},
    {"tab5_ui_textarea_get_text", (void *)tab5_ui_textarea_get_text, "(r)$"},
    {"tab5_ui_textarea_set_placeholder", (void *)tab5_ui_textarea_set_placeholder, "(r$)i"},
    {"tab5_ui_app_bar_set_title", (void *)tab5_ui_app_bar_set_title, "($)i"},
    {"tab5_ui_app_bar_add_action_button", (void *)tab5_ui_app_bar_add_action_button, "($*r)r"},
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
