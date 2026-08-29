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
#include "ui_bar.h"
#include "ui_font.h"
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

    // Cria a área de texto padrão para aplicativos
    lv_obj_t *ta = lv_textarea_create(scr);
    lv_obj_set_width(ta, lv_pct(100));
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 2 * UI_BAR_HEIGHT);
    lv_textarea_set_placeholder_text(ta, "Escreva sua nota...");
    lv_textarea_set_cursor_click_pos(ta, true);
    lv_obj_set_style_anim_duration(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_18_latin1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, lv_color_hex(palette->surface), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(palette->text), 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_pad_all(ta, 14, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(palette->text_muted), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, lv_color_hex(palette->accent), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);

    // Abre teclado ao clicar na área de texto
    lv_obj_add_event_cb(
        ta,
        [](lv_event_t *e) {
            lv_obj_t *target_ta = (lv_obj_t *)lv_event_get_target(e);
            if (target_ta != nullptr) {
                lv_obj_set_style_bg_opa(target_ta, LV_OPA_COVER, LV_PART_CURSOR);
                lv_group_focus_obj(target_ta);
                ui_keyboard_attach(target_ta);
            }
        },
        LV_EVENT_CLICKED, nullptr);

    ctx->root_screen = (tab5_ui_obj_t)scr;
    ctx->app_bar = (tab5_ui_obj_t)bar.bar;
    ctx->content_area = (tab5_ui_obj_t)ta;

    lv_disp_load_scr(scr);
    tab5_ui_host_apply_layout();
    return TAB5_OK;
#else
    (void)app_name;
    // Ambiente de teste de host sem display gráfico
    ctx->root_screen = (tab5_ui_obj_t)(uintptr_t)0x1000;
    ctx->app_bar = (tab5_ui_obj_t)(uintptr_t)0x2000;
    ctx->content_area = (tab5_ui_obj_t)(uintptr_t)0x3000;
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

tab5_err_t tab5_ui_host_textarea_set_text(tab5_ui_obj_t ta, const char *text)
{
    if (ta == nullptr || text == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if HAVE_LVGL
    lv_textarea_set_text((lv_obj_t *)ta, text);
    return TAB5_OK;
#else
    return TAB5_OK;
#endif
}

const char *tab5_ui_host_textarea_get_text(tab5_ui_obj_t ta)
{
    if (ta == nullptr) {
        return "";
    }
#if HAVE_LVGL
    const char *txt = lv_textarea_get_text((lv_obj_t *)ta);
    return txt != nullptr ? txt : "";
#else
    return "";
#endif
}

tab5_err_t tab5_ui_host_textarea_set_placeholder(tab5_ui_obj_t ta, const char *placeholder)
{
    if (ta == nullptr || placeholder == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if HAVE_LVGL
    lv_textarea_set_placeholder_text((lv_obj_t *)ta, placeholder);
    return TAB5_OK;
#else
    return TAB5_OK;
#endif
}

void tab5_ui_host_apply_layout(void)
{
#if HAVE_LVGL
    tab5_app_context_t *ctx = tab5_host_get_active_app();
    if (ctx == nullptr || ctx->root_screen == nullptr) {
        return;
    }

    lv_obj_t *ta = (lv_obj_t *)ctx->content_area;
    if (ta == nullptr) {
        return;
    }

    int32_t w = lv_display_get_horizontal_resolution(NULL);
    int32_t h = lv_display_get_vertical_resolution(NULL);
    int32_t kb_h = ui_keyboard_is_visible() ? ui_keyboard_get_height() : 0;

    if (ctx->app_bar != nullptr) {
        lv_obj_set_width((lv_obj_t *)ctx->app_bar, w);
    }

    lv_obj_set_width(ta, w);
    int32_t available_h = h - 2 * UI_BAR_HEIGHT - kb_h - 8;
    if (available_h < 40) {
        available_h = 40;
    }
    lv_obj_set_height(ta, available_h);
    if (ui_keyboard_is_visible()) {
        lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
    }
#endif
}
