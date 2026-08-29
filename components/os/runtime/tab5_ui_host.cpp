/**
 * @file tab5_ui_host.cpp
 * @brief Implementação dos Bindings de UI e Shell para Apps Isoladas
 */

#include "tab5_ui_host.h"
#include "tab5_lifecycle_host.h"
#include "tab5_package_mgr.h"
#include <cstring>

#if defined(ESP_PLATFORM) || defined(LV_LVGL_H_INCLUDE_SIMPLE) || defined(LV_CONF_INCLUDE_SIMPLE) ||                   \
    defined(TAB5_SIMULATOR)
#include "lvgl.h"
#include "ui_theme.h"
#include "ui_app_bar.h"
#include "ui_keyboard.h"
#define HAVE_LVGL 1
#else
#define HAVE_LVGL 0
#endif

#if HAVE_LVGL
static lv_obj_t *s_previous_screen = nullptr;

static void on_app_bar_close_clicked(lv_event_t *e)
{
    (void)e;
    tab5_package_mgr_close_active();
}
#endif

tab5_err_t tab5_ui_host_create_app_screen(const char *app_name, tab5_app_context_t *ctx)
{
    if (ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    s_previous_screen = lv_disp_get_scr_act(NULL);
    lv_obj_t *scr = lv_obj_create(NULL);
    if (scr == nullptr) {
        return TAB5_ERR_NO_MEM;
    }

    const ui_palette_t *palette = ui_theme_get();
    lv_obj_set_style_bg_color(scr, lv_color_hex(palette->background), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    ui_app_bar_t bar = ui_app_bar_create(scr, app_name != nullptr ? app_name : "App", on_app_bar_close_clicked, ctx);

    ctx->root_screen = (tab5_ui_obj_t)scr;
    ctx->app_bar = (tab5_ui_obj_t)bar.bar;

    lv_disp_load_scr(scr);
    return TAB5_OK;
#else
    (void)app_name;
    // Ambiente de teste de host sem display gráfico
    ctx->root_screen = (tab5_ui_obj_t)(uintptr_t)0x1000;
    ctx->app_bar = (tab5_ui_obj_t)(uintptr_t)0x2000;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_destroy_app_screen(tab5_app_context_t *ctx)
{
    if (ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    if (ctx->root_screen != nullptr) {
        lv_obj_t *scr = (lv_obj_t *)ctx->root_screen;
        ctx->root_screen = nullptr;
        ctx->app_bar = nullptr;

        ui_keyboard_hide();
        lv_obj_t *act = lv_disp_get_scr_act(NULL);
        if (act == scr) {
            lv_obj_t *target =
                (s_previous_screen != nullptr && s_previous_screen != scr) ? s_previous_screen : lv_screen_active();
            if (target != nullptr && target != scr) {
                lv_disp_load_scr(target);
            }
        }
        s_previous_screen = nullptr;
        lv_obj_delete_async(scr);
    }
#else
    ctx->root_screen = nullptr;
    ctx->app_bar = nullptr;
#endif

    return TAB5_OK;
}

tab5_err_t tab5_ui_host_keyboard_show(tab5_ui_obj_t target_textarea)
{
    if (target_textarea == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    ui_keyboard_attach((lv_obj_t *)target_textarea);
    return TAB5_OK;
#else
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_keyboard_hide(void)
{
#if HAVE_LVGL
    ui_keyboard_hide();
    return TAB5_OK;
#else
    return TAB5_OK;
#endif
}

bool tab5_ui_host_keyboard_is_visible(void)
{
#if HAVE_LVGL
    return ui_keyboard_is_visible();
#else
    return false;
#endif
}

tab5_err_t tab5_ui_host_show_toast(const char *message, uint32_t duration_ms)
{
    if (message == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    lv_obj_t *top_layer = lv_layer_top();
    if (top_layer == nullptr) {
        return TAB5_ERR_FAIL;
    }

    const ui_palette_t *palette = ui_theme_get();

    lv_obj_t *toast = lv_obj_create(top_layer);
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(toast, lv_color_hex(palette->surface), 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(palette->accent), 0);
    lv_obj_set_style_border_width(toast, 2, 0);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_pad_all(toast, 12, 0);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *lbl = lv_label_create(toast);
    lv_label_set_text(lbl, message);
    lv_obj_set_style_text_color(lbl, lv_color_hex(palette->text), 0);

    // Auto-delete toast após a duração
    if (duration_ms > 0) {
        lv_obj_delete_delayed(toast, duration_ms);
    }
    return TAB5_OK;
#else
    (void)duration_ms;
    return TAB5_OK;
#endif
}
