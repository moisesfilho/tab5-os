/**
 * @file tab5_ui_host.cpp
 * @brief Implementação dos Bindings de UI e Shell para Apps Isoladas
 */

#include "tab5_ui_host.h"
#include "tab5_lifecycle_host.h"
#include "tab5_package_mgr.h"
#include "tab5_wasm_dispatcher.h"
#include <cstring>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

#if defined(ESP_PLATFORM) || defined(LV_LVGL_H_INCLUDE_SIMPLE) || defined(LV_CONF_INCLUDE_SIMPLE) ||                   \
    defined(TAB5_SIMULATOR)
#include "lvgl.h"
#include "bsp/m5stack_tab5.h"
#include "ui_theme.h"
#include "ui_app_bar.h"
#include "ui_bar.h"
#include "ui_font.h"
#include "ui_keyboard.h"
#include "ui_camera_view.h"
#include "ui_gallery_view.h"
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
#define MAX_UI_HANDLES 128

#if defined(ESP_PLATFORM)
static const char *TAG = "tab5_ui_host";
static bool s_display_size_diagnostic_logged = false;
#endif

#if HAVE_LVGL
#include "ui_shell.h"
static lv_obj_t *s_previous_screen = nullptr;
static lv_obj_t *s_handle_table[MAX_UI_HANDLES];
static uint32_t s_next_handle = 1;
static lv_obj_t *s_previous_handle_table[MAX_UI_HANDLES];
static uint32_t s_previous_next_handle = 1;
static bool s_has_previous_handles = false;
static ui_camera_view_t *s_active_camera_view = nullptr;
static ui_gallery_view_t *s_active_gallery_view = nullptr;
static tab5_app_context_t *s_active_camera_owner = nullptr;
static tab5_app_context_t *s_active_gallery_owner = nullptr;
static ui_camera_view_t *s_previous_camera_view = nullptr;
static ui_gallery_view_t *s_previous_gallery_view = nullptr;
static tab5_app_context_t *s_previous_camera_owner = nullptr;
static tab5_app_context_t *s_previous_gallery_owner = nullptr;
static bool s_has_previous_views = false;
static lv_timer_t *s_wasm_poll_timer = nullptr;
static tab5_app_context_t *s_wasm_poll_owner = nullptr;

static void tab5_ui_host_apply_layout_locked(void)
{
    tab5_app_context_t *ctx = tab5_host_get_active_app();
    if (ctx == nullptr || ctx->root_screen == nullptr) {
        return;
    }

    int32_t w = lv_display_get_horizontal_resolution(NULL);
    int32_t h = lv_display_get_vertical_resolution(NULL);
    int32_t kb_h = ui_keyboard_is_visible() ? ui_keyboard_get_height() : 0;

    if (ctx->app_bar != nullptr) {
        lv_obj_set_width((lv_obj_t *)ctx->app_bar, w);
    }
    if (ctx->app_id[0] && (strcmp(ctx->app_id, "com.tab5.camera") == 0 || strcmp(ctx->app_id, "camera") == 0)) {
        if (s_active_camera_view != nullptr) {
            ui_camera_view_apply_layout(s_active_camera_view);
        }
        return;
    }
    if (ctx->app_id[0] && (strcmp(ctx->app_id, "com.tab5.gallery") == 0 || strcmp(ctx->app_id, "gallery") == 0)) {
        if (s_active_gallery_view != nullptr) {
            ui_gallery_view_apply_layout(s_active_gallery_view);
        }
        return;
    }

    lv_obj_t *ta = (lv_obj_t *)ctx->content_area;
    if (ta == nullptr) {
        return;
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
}

static tab5_err_t call_wasm_callback(tab5_wasm_app_instance_t *wasm_inst, const char *primary, const char *alias,
                                     uint32_t argc, uint32_t *argv)
{
    tab5_err_t err = tab5_wasm_call_function(wasm_inst, primary, argc, argv);
    if (err == TAB5_ERR_NOT_FOUND) {
        err = tab5_wasm_call_function(wasm_inst, alias, argc, argv);
    }
    return err;
}

static bool is_descendant_of(lv_obj_t *obj, lv_obj_t *ancestor)
{
    while (obj != nullptr) {
        if (obj == ancestor) {
            return true;
        }
        obj = lv_obj_get_parent(obj);
    }
    return false;
}

static void clear_handles_for_screen(lv_obj_t *screen)
{
    if (screen == nullptr) {
        return;
    }
    for (uint32_t i = 1; i < MAX_UI_HANDLES; ++i) {
        if (s_handle_table[i] != nullptr && is_descendant_of(s_handle_table[i], screen)) {
            s_handle_table[i] = nullptr;
        }
    }
}

static void destroy_owned_views(tab5_app_context_t *owner)
{
    if (s_active_camera_view != nullptr && s_active_camera_owner == owner) {
        ui_camera_view_destroy(s_active_camera_view);
        s_active_camera_view = nullptr;
        s_active_camera_owner = nullptr;
    }
    if (s_active_gallery_view != nullptr && s_active_gallery_owner == owner) {
        ui_gallery_view_destroy(s_active_gallery_view);
        s_active_gallery_view = nullptr;
        s_active_gallery_owner = nullptr;
    }
}

static void destroy_snapshot_owned_views(tab5_app_context_t *owner)
{
    if (s_previous_camera_view != nullptr && s_previous_camera_owner == owner) {
        if (s_previous_camera_view != s_active_camera_view) {
            ui_camera_view_destroy(s_previous_camera_view);
        }
        s_previous_camera_view = nullptr;
        s_previous_camera_owner = nullptr;
    }
    if (s_previous_gallery_view != nullptr && s_previous_gallery_owner == owner) {
        if (s_previous_gallery_view != s_active_gallery_view) {
            ui_gallery_view_destroy(s_previous_gallery_view);
        }
        s_previous_gallery_view = nullptr;
        s_previous_gallery_owner = nullptr;
    }
}

void tab5_ui_host_generic_widget_event_cb(lv_event_t *e);

static void on_app_bar_close_clicked(lv_event_t *e)
{
    (void)e;
    tab5_package_mgr_close_active();
}

static bool is_chat_app_id(const char *app_id)
{
    return app_id != nullptr && app_id[0] != '\0' &&
           (strcmp(app_id, "com.tab5.chat") == 0 || strcmp(app_id, "chat") == 0);
}

static void on_wasm_poll_timer(lv_timer_t *timer)
{
    (void)timer;
    tab5_app_context_t *app_ctx = tab5_host_get_active_app();
    if (app_ctx != s_wasm_poll_owner) {
        return;
    }
    if (app_ctx == nullptr || !app_ctx->is_wasm || app_ctx->wasm_instance == nullptr ||
        !is_chat_app_id(app_ctx->app_id)) {
        return;
    }
    tab5_wasm_app_instance_t *wasm_inst = (tab5_wasm_app_instance_t *)app_ctx->wasm_instance;
    uint32_t argv[3] = {0u, 0u, 0u};
    // dispatch post: never execute WASM on the LVGL callback stack.
    (void)tab5_wasm_dispatch_post_call(wasm_inst, "tab5_app_on_ui_event", "on_ui_event", 3, argv);
}

static void delete_wasm_poll_timer()
{
    if (s_wasm_poll_timer != nullptr) {
        lv_timer_delete(s_wasm_poll_timer);
        s_wasm_poll_timer = nullptr;
    }
    s_wasm_poll_owner = nullptr;
}

static void sync_wasm_poll_timer_to_active_app()
{
    tab5_app_context_t *active_ctx = tab5_host_get_active_app();
    if (active_ctx == nullptr || !active_ctx->is_wasm || !is_chat_app_id(active_ctx->app_id)) {
        delete_wasm_poll_timer();
        return;
    }

    if (s_wasm_poll_timer == nullptr) {
        s_wasm_poll_timer = lv_timer_create(on_wasm_poll_timer, 200, nullptr);
    }
    s_wasm_poll_owner = s_wasm_poll_timer != nullptr ? active_ctx : nullptr;
}
#endif

#if defined(ESP_PLATFORM)
static void tab5_ui_host_log_display_diagnostic(int32_t physical_w, int32_t physical_h)
{
    if (s_display_size_diagnostic_logged) {
        return;
    }
    s_display_size_diagnostic_logged = true;
    ESP_LOGI(TAG, "display size diagnostic: logical=%ldx%ld physical=%ldx%ld rotation=%d",
             (long)lv_display_get_horizontal_resolution(nullptr), (long)lv_display_get_vertical_resolution(nullptr),
             (long)physical_w, (long)physical_h, (int)lv_display_get_rotation(nullptr));
}
#endif

tab5_err_t tab5_ui_host_create_app_screen(const char *app_name, tab5_app_context_t *ctx)
{
    if (ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    if (!LV_LOCK()) {
        return TAB5_ERR_TIMEOUT;
    }
    memcpy(s_previous_handle_table, s_handle_table, sizeof(s_handle_table));
    s_previous_next_handle = s_next_handle;
    s_has_previous_handles = true;
    s_previous_camera_view = s_active_camera_view;
    s_previous_gallery_view = s_active_gallery_view;
    s_previous_camera_owner = s_active_camera_owner;
    s_previous_gallery_owner = s_active_gallery_owner;
    s_has_previous_views = true;
    s_previous_screen = lv_disp_get_scr_act(NULL);
    lv_obj_t *scr = lv_obj_create(NULL);
    if (scr == nullptr) {
        LV_UNLOCK();
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
    lv_textarea_set_placeholder_text(ta, "");
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

    tab5_ui_host_register_obj(ta);
    lv_obj_add_event_cb(ta, tab5_ui_host_generic_widget_event_cb, LV_EVENT_ALL, nullptr);

    bool is_camera =
        ctx->app_id[0] && (strcmp(ctx->app_id, "com.tab5.camera") == 0 || strcmp(ctx->app_id, "camera") == 0);
    bool is_files = ctx->app_id[0] && (strcmp(ctx->app_id, "com.tab5.files") == 0 || strcmp(ctx->app_id, "files") == 0);
    if (is_camera || is_files) {
        lv_obj_add_flag(ta, LV_OBJ_FLAG_HIDDEN);
    }
    if (is_camera) {
        ui_camera_view_t *candidate_view = ui_camera_view_create(scr, bar);
        if (candidate_view != nullptr) {
            s_active_camera_view = candidate_view;
            s_active_camera_owner = ctx;
        }
    } else if (ctx->app_id[0] &&
               (strcmp(ctx->app_id, "com.tab5.gallery") == 0 || strcmp(ctx->app_id, "gallery") == 0)) {
        lv_obj_add_flag(ta, LV_OBJ_FLAG_HIDDEN);
        ui_gallery_view_t *candidate_view = ui_gallery_view_create(scr, bar);
        if (candidate_view != nullptr) {
            s_active_gallery_view = candidate_view;
            s_active_gallery_owner = ctx;
        }
    }

    ctx->root_screen = (void *)scr;
    ctx->app_bar = (void *)bar.bar;
    ctx->app_bar_handle = new ui_app_bar_t(bar);
    ctx->content_area = (void *)ta;

    if (ctx->is_wasm && is_chat_app_id(ctx->app_id) && s_wasm_poll_timer == nullptr) {
        s_wasm_poll_timer = lv_timer_create(on_wasm_poll_timer, 200, nullptr);
        s_wasm_poll_owner = s_wasm_poll_timer != nullptr ? ctx : nullptr;
    }

    lv_disp_load_scr(scr);
    tab5_ui_host_apply_layout_locked();
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)app_name;
    // Ambiente de teste de host sem display gráfico
    ctx->root_screen = (void *)(uintptr_t)0x1000;
    ctx->app_bar = (void *)(uintptr_t)0x2000;
    ctx->content_area = (void *)(uintptr_t)0x3000;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_destroy_app_screen(tab5_app_context_t *ctx)
{
    if (ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    if (!LV_LOCK()) {
        return TAB5_ERR_TIMEOUT;
    }
    // Durante uma troca bem-sucedida, o contexto novo já é o ativo quando o
    // contexto antigo chega aqui. Recursos globais de UI pertencem à tela
    // ativa, portanto não podem ser limpos ao destruir a tela antiga.
    const bool destroying_active = tab5_host_get_active_app() == ctx;
    if (destroying_active) {
        delete_wasm_poll_timer();

        destroy_owned_views(ctx);
        // A final close cannot leave a view owned by a context that is no
        // longer active, even if a malformed caller supplied stale ownership.
        if (s_active_camera_view != nullptr) {
            ui_camera_view_destroy(s_active_camera_view);
            s_active_camera_view = nullptr;
            s_active_camera_owner = nullptr;
        }
        if (s_active_gallery_view != nullptr) {
            ui_gallery_view_destroy(s_active_gallery_view);
            s_active_gallery_view = nullptr;
            s_active_gallery_owner = nullptr;
        }

        tab5_ui_host_clear_handles();
    }

    if (ctx->root_screen != nullptr) {
        lv_obj_t *scr = (lv_obj_t *)ctx->root_screen;
        if (!destroying_active) {
            clear_handles_for_screen(scr);
            destroy_snapshot_owned_views(ctx);
            destroy_owned_views(ctx);
            sync_wasm_poll_timer_to_active_app();
        }
        ctx->root_screen = nullptr;
        ctx->app_bar = nullptr;
        ctx->content_area = nullptr;
        if (ctx->app_bar_handle != nullptr) {
            delete (ui_app_bar_t *)ctx->app_bar_handle;
            ctx->app_bar_handle = nullptr;
        }

        if (destroying_active) {
            ui_keyboard_hide();
        }
        lv_obj_t *act = lv_disp_get_scr_act(NULL);
        if (act == scr) {
            lv_obj_t *target = (s_previous_screen != nullptr && s_previous_screen != scr)
                                   ? s_previous_screen
                                   : ui_shell_get_desktop_screen();
            if (target != nullptr && target != scr) {
                lv_disp_load_scr(target);
            }
        }
        // A successful replacement leaves the old screen in this slot. It is
        // being deleted now, so never let a later close target that stale
        // pointer.
        if (s_previous_screen == scr || destroying_active) {
            s_previous_screen = nullptr;
        }
        s_has_previous_handles = false;
        s_has_previous_views = false;
        // A screen owns all of its children.  Delete it synchronously while
        // the display lock is held; deferred deletion leaves stale pointers
        // reachable by the next app switch.
        lv_obj_delete(scr);
    }
    LV_UNLOCK();
#else
    ctx->root_screen = nullptr;
    ctx->app_bar = nullptr;
#endif

    return TAB5_OK;
}

tab5_err_t tab5_ui_host_abort_app_screen(tab5_app_context_t *ctx)
{
    if (ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    if (!LV_LOCK()) {
        return TAB5_ERR_TIMEOUT;
    }
    if (s_wasm_poll_timer != nullptr && s_wasm_poll_owner == ctx) {
        delete_wasm_poll_timer();
    }
    destroy_owned_views(ctx);

    lv_obj_t *scr = (lv_obj_t *)ctx->root_screen;
    ctx->root_screen = nullptr;
    ctx->app_bar = nullptr;
    ctx->content_area = nullptr;
    if (ctx->app_bar_handle != nullptr) {
        delete (ui_app_bar_t *)ctx->app_bar_handle;
        ctx->app_bar_handle = nullptr;
    }
    ui_keyboard_hide();

    // Remove a tela candidata de forma síncrona antes de restaurar qualquer
    // handle/view pertencente ao contexto anterior.
    if (scr != nullptr) {
        lv_obj_delete(scr);
    }

    if (s_previous_screen != nullptr && s_previous_screen != scr) {
        lv_disp_load_scr(s_previous_screen);
    }

    if (s_has_previous_handles) {
        memcpy(s_handle_table, s_previous_handle_table, sizeof(s_handle_table));
        s_next_handle = s_previous_next_handle;
    } else {
        tab5_ui_host_clear_handles();
    }
    if (s_has_previous_views) {
        s_active_camera_view = s_previous_camera_view;
        s_active_gallery_view = s_previous_gallery_view;
        s_active_camera_owner = s_previous_camera_owner;
        s_active_gallery_owner = s_previous_gallery_owner;
    }
    s_previous_screen = nullptr;
    s_has_previous_handles = false;
    s_has_previous_views = false;
    LV_UNLOCK();
#else
    ctx->root_screen = nullptr;
    ctx->app_bar = nullptr;
    ctx->app_bar_handle = nullptr;
#endif
    return TAB5_OK;
}

tab5_err_t tab5_ui_host_keyboard_show(void *target_textarea)
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

int32_t tab5_ui_host_keyboard_get_height(void)
{
#if HAVE_LVGL
    return ui_keyboard_is_visible() ? ui_keyboard_get_height() : 0;
#else
    return 0;
#endif
}

void tab5_ui_host_get_display_size(int32_t *out_w, int32_t *out_h)
{
    if (out_w == nullptr && out_h == nullptr) {
        return;
    }
#if HAVE_LVGL
    int32_t w = lv_display_get_physical_horizontal_resolution(nullptr);
    int32_t h = lv_display_get_physical_vertical_resolution(nullptr);
#if defined(ESP_PLATFORM)
    tab5_ui_host_log_display_diagnostic(w, h);
#endif
    if (out_w != nullptr) {
        *out_w = w;
    }
    if (out_h != nullptr) {
        *out_h = h;
    }
#else
    if (out_w != nullptr) {
        *out_w = 720;
    }
    if (out_h != nullptr) {
        *out_h = 1280;
    }
#endif
}

tab5_ui_obj_t tab5_ui_host_textarea_create(tab5_ui_obj_t parent_handle)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *parent = (lv_obj_t *)tab5_ui_host_get_lv_obj(parent_handle);
    if (parent == nullptr) {
        tab5_app_context_t *ctx = tab5_host_get_active_app();
        if (ctx != nullptr) {
            parent = (lv_obj_t *)ctx->root_screen;
        }
    }
    if (parent == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }

    const ui_palette_t *palette = ui_theme_get();
    lv_obj_t *ta = lv_textarea_create(parent);
    if (ta == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_cursor_click_pos(ta, true);
    lv_obj_set_style_anim_duration(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_18_latin1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, lv_color_hex(palette->surface_alt), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(palette->text), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(palette->border), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_pad_hor(ta, 12, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(palette->text_muted), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, lv_color_hex(palette->accent), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);

    lv_obj_add_event_cb(
        ta,
        [](lv_event_t *e) {
            lv_obj_t *target_ta = (lv_obj_t *)lv_event_get_target(e);
            if (target_ta != nullptr) {
                lv_group_focus_obj(target_ta);
                ui_keyboard_attach(target_ta);
            }
        },
        LV_EVENT_CLICKED, nullptr);

    tab5_ui_obj_t res = tab5_ui_host_register_obj(ta);
    lv_obj_add_event_cb(ta, tab5_ui_host_generic_widget_event_cb, LV_EVENT_ALL, nullptr);
    LV_UNLOCK();
    return res;
#else
    (void)parent_handle;
    return 1;
#endif
}

tab5_err_t tab5_ui_host_show_toast(const char *message, uint32_t duration_ms)
{
    if (message == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_LVGL
    if (!LV_LOCK()) {
        return TAB5_ERR_TIMEOUT;
    }
    lv_obj_t *top_layer = lv_layer_top();
    if (top_layer == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_FAIL;
    }

    const ui_palette_t *palette = ui_theme_get();

    lv_obj_t *toast = lv_obj_create(top_layer);
    if (toast == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_NO_MEM;
    }
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(toast, lv_color_hex(palette->surface), 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(palette->accent), 0);
    lv_obj_set_style_border_width(toast, 2, 0);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_pad_all(toast, 12, 0);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *lbl = lv_label_create(toast);
    if (lbl != nullptr) {
        lv_label_set_text(lbl, message);
        lv_obj_set_style_text_color(lbl, lv_color_hex(palette->text), 0);
    }

    // Auto-delete toast após a duração
    if (duration_ms > 0) {
        lv_obj_delete_delayed(toast, duration_ms);
    }
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)duration_ms;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_textarea_set_text(void *ta, const char *text)
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

const char *tab5_ui_host_textarea_get_text(void *ta)
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

tab5_err_t tab5_ui_host_textarea_set_placeholder(void *ta, const char *placeholder)
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

tab5_err_t tab5_ui_host_textarea_set_cursor_pos(void *ta, int32_t pos)
{
    if (ta == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if HAVE_LVGL
    if (pos >= TAB5_UI_CURSOR_LAST) {
        lv_textarea_set_cursor_pos((lv_obj_t *)ta, LV_TEXTAREA_CURSOR_LAST);
    } else {
        lv_textarea_set_cursor_pos((lv_obj_t *)ta, pos);
    }
    return TAB5_OK;
#else
    (void)pos;
    return TAB5_OK;
#endif
}

int32_t tab5_ui_host_textarea_get_cursor_pos(void *ta)
{
    if (ta == nullptr) {
        return 0;
    }
#if HAVE_LVGL
    return (int32_t)lv_textarea_get_cursor_pos((lv_obj_t *)ta);
#else
    return 0;
#endif
}

tab5_err_t tab5_ui_host_textarea_set_password_mode(void *ta, bool password_mode)
{
    if (ta == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
#if HAVE_LVGL
    LV_LOCK();
    lv_textarea_set_password_mode((lv_obj_t *)ta, password_mode);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)password_mode;
    return TAB5_OK;
#endif
}

void tab5_ui_host_apply_layout(void)
{
#if HAVE_LVGL
    if (!LV_LOCK()) {
        return;
    }
    tab5_ui_host_apply_layout_locked();
    LV_UNLOCK();
#endif
}

void tab5_ui_host_refresh_theme(void)
{
#if HAVE_LVGL
    if (!LV_LOCK()) {
        return;
    }
    if (s_active_camera_view != nullptr) {
        ui_camera_view_refresh_theme(s_active_camera_view);
    }
    if (s_active_gallery_view != nullptr) {
        ui_gallery_view_refresh_theme(s_active_gallery_view);
    }

    tab5_app_context_t *ctx = tab5_host_get_active_app();
    if (ctx == nullptr) {
        LV_UNLOCK();
        return;
    }
    if (ctx->app_bar_handle != nullptr) {
        ui_app_bar_refresh_theme((ui_app_bar_t *)ctx->app_bar_handle);
    }
    tab5_wasm_app_instance_t *wasm_inst = nullptr;
    if (ctx->is_wasm && ctx->wasm_instance != nullptr) {
        wasm_inst = (tab5_wasm_app_instance_t *)ctx->wasm_instance;
    }
    uint32_t theme_arg = ui_theme_is_dark() ? 1u : 0u;
    LV_UNLOCK();

    if (wasm_inst != nullptr) {
        // dispatch post: return immediately after releasing the display lock.
        (void)tab5_wasm_dispatch_post_call(wasm_inst, "tab5_app_on_theme_changed", "on_theme_changed", 1, &theme_arg);
    }
#endif
}

void tab5_ui_host_resume_app(tab5_app_context_t *ctx)
{
#if HAVE_LVGL
    if (ctx == nullptr) {
        return;
    }
    if (!LV_LOCK()) {
        return;
    }
    if (ctx->app_id[0] && (strcmp(ctx->app_id, "com.tab5.camera") == 0 || strcmp(ctx->app_id, "camera") == 0)) {
        if (s_active_camera_view != nullptr) {
            ui_camera_view_start(s_active_camera_view);
        }
    } else if (ctx->app_id[0] &&
               (strcmp(ctx->app_id, "com.tab5.gallery") == 0 || strcmp(ctx->app_id, "gallery") == 0)) {
        if (s_active_gallery_view != nullptr) {
            ui_gallery_view_start(s_active_gallery_view);
        }
    }
    LV_UNLOCK();
#else
    (void)ctx;
#endif
}

void tab5_ui_host_open_file(tab5_app_context_t *ctx, const char *path)
{
#if HAVE_LVGL
    if (ctx == nullptr || path == nullptr) {
        return;
    }
    if (!LV_LOCK()) {
        return;
    }
    if (ctx->app_id[0] && (strcmp(ctx->app_id, "com.tab5.gallery") == 0 || strcmp(ctx->app_id, "gallery") == 0)) {
        if (s_active_gallery_view != nullptr) {
            ui_gallery_view_open_file(s_active_gallery_view, path);
        }
    }
    LV_UNLOCK();
#else
    (void)ctx;
    (void)path;
#endif
}

/* ========================================================================= */
/* Manipulação de Handles de UI e Widgets Genéricos                          */
/* ========================================================================= */

#if HAVE_LVGL
void tab5_ui_host_generic_widget_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
    tab5_app_context_t *app_ctx = tab5_host_get_active_app();
    if (app_ctx == nullptr) {
        return;
    }
    if (!app_ctx->is_wasm && app_ctx->lifecycle.on_ui_event == nullptr) {
        return;
    }

    tab5_ui_obj_t handle = TAB5_UI_INVALID_OBJ;
    for (lv_obj_t *candidate = target; candidate != nullptr && handle == TAB5_UI_INVALID_OBJ;
         candidate = lv_obj_get_parent(candidate)) {
        for (uint32_t i = 1; i < MAX_UI_HANDLES; ++i) {
            if (s_handle_table[i] == candidate) {
                handle = i;
                break;
            }
        }
    }

    if (handle == TAB5_UI_INVALID_OBJ) {
        return;
    }

    uint32_t event_type = 0;
    int32_t event_val = 0;

    if (code == LV_EVENT_CLICKED) {
        event_type = TAB5_UI_EVENT_CLICKED;
    } else if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_READY) {
        event_type = TAB5_UI_EVENT_VALUE_CHANGED;
        if (lv_obj_check_type(target, &lv_slider_class)) {
            event_val = (int32_t)lv_slider_get_value(target);
        } else if (lv_obj_check_type(target, &lv_switch_class)) {
            event_val = lv_obj_has_state(target, LV_STATE_CHECKED) ? 1 : 0;
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        event_type = TAB5_UI_EVENT_LONG_PRESSED;
    } else if (code == LV_EVENT_FOCUSED) {
        event_type = TAB5_UI_EVENT_FOCUSED;
    } else if (code == LV_EVENT_DEFOCUSED) {
        event_type = TAB5_UI_EVENT_DEFOCUSED;
    }

    if (event_type != 0) {
        if (app_ctx->is_wasm && app_ctx->wasm_instance != nullptr) {
            tab5_wasm_app_instance_t *wasm_inst = (tab5_wasm_app_instance_t *)app_ctx->wasm_instance;
            uint32_t argv[3] = {(uint32_t)handle, (uint32_t)event_type, (uint32_t)event_val};
            // dispatch post: argv is copied into the bounded job.
            (void)tab5_wasm_dispatch_post_call(wasm_inst, "tab5_app_on_ui_event", "on_ui_event", 3, argv);
        } else if (app_ctx->lifecycle.on_ui_event != nullptr) {
            app_ctx->lifecycle.on_ui_event(handle, event_type, event_val);
        }
    }
}
#endif

tab5_ui_obj_t tab5_ui_host_register_obj(void *lv_obj)
{
#if HAVE_LVGL
    if (lv_obj == nullptr) {
        return TAB5_UI_INVALID_OBJ;
    }
    // Verifica se já registrado
    for (uint32_t i = 1; i < MAX_UI_HANDLES; ++i) {
        if (s_handle_table[i] == (lv_obj_t *)lv_obj) {
            return i;
        }
    }
    for (uint32_t count = 0; count < MAX_UI_HANDLES - 1; ++count) {
        if (s_next_handle >= MAX_UI_HANDLES) {
            s_next_handle = 1;
        }
        uint32_t h = s_next_handle++;
        if (s_handle_table[h] == nullptr) {
            s_handle_table[h] = (lv_obj_t *)lv_obj;
            return h;
        }
    }
    return TAB5_UI_INVALID_OBJ;
#else
    (void)lv_obj;
    return 1;
#endif
}

void *tab5_ui_host_get_lv_obj(tab5_ui_obj_t handle)
{
#if HAVE_LVGL
    if (handle == TAB5_UI_INVALID_OBJ || handle >= MAX_UI_HANDLES) {
        return nullptr;
    }
    return (void *)s_handle_table[handle];
#else
    (void)handle;
    return nullptr;
#endif
}

void tab5_ui_host_clear_handles(void)
{
#if HAVE_LVGL
    memset(s_handle_table, 0, sizeof(s_handle_table));
    s_next_handle = 1;
#endif
}

tab5_ui_obj_t tab5_ui_host_container_create(tab5_ui_obj_t parent_handle)
{
#if HAVE_LVGL
    LV_LOCK();
    tab5_app_context_t *ctx = tab5_host_get_active_app();
    lv_obj_t *parent = (lv_obj_t *)tab5_ui_host_get_lv_obj(parent_handle);
    if (parent == nullptr) {
        if (ctx != nullptr) {
            parent = (lv_obj_t *)ctx->root_screen;
        }
    }
    if (parent == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }

    // Se o aplicativo criou um contêiner diretamente na tela raiz, oculta o textarea padrão
    if (ctx != nullptr && ctx->content_area != nullptr && (parent == (lv_obj_t *)ctx->root_screen)) {
        lv_obj_add_flag((lv_obj_t *)ctx->content_area, LV_OBJ_FLAG_HIDDEN);
    }

    const ui_palette_t *palette = ui_theme_get();
    lv_obj_t *cont = lv_obj_create(parent);
    if (cont == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }
    lv_obj_set_style_bg_color(cont, lv_color_hex(palette->surface), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_add_flag(cont, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_SCROLL_CHAIN_HOR));

    tab5_ui_obj_t res = tab5_ui_host_register_obj(cont);
    LV_UNLOCK();
    return res;
#else
    (void)parent_handle;
    return 1;
#endif
}

static int32_t resolve_size(int32_t val)
{
#if HAVE_LVGL
    if (val == TAB5_UI_SIZE_CONTENT) {
        return LV_SIZE_CONTENT;
    }
    if (val <= -1000) {
        int pct = -(val + 1000);
        return lv_pct(pct);
    }
    return val;
#else
    return val;
#endif
}

tab5_err_t tab5_ui_host_obj_set_size(tab5_ui_obj_t obj_handle, int32_t w, int32_t h)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_set_size(obj, resolve_size(w), resolve_size(h));
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)w;
    (void)h;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_scrollable(tab5_ui_obj_t obj_handle, bool scrollable)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    if (scrollable) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    }
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)scrollable;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_scroll_to_bottom(tab5_ui_obj_t obj_handle, bool animated)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_scroll_to_y(obj, LV_COORD_MAX, animated ? LV_ANIM_ON : LV_ANIM_OFF);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)animated;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_scroll_to_top(tab5_ui_obj_t obj_handle, bool animated)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_scroll_to_y(obj, 0, animated ? LV_ANIM_ON : LV_ANIM_OFF);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)animated;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_align(tab5_ui_obj_t obj_handle, tab5_ui_align_t align, int32_t x_ofs, int32_t y_ofs)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_align_t lv_al = LV_ALIGN_DEFAULT;
    switch (align) {
    case TAB5_UI_ALIGN_TOP_LEFT:
        lv_al = LV_ALIGN_TOP_LEFT;
        break;
    case TAB5_UI_ALIGN_TOP_MID:
        lv_al = LV_ALIGN_TOP_MID;
        break;
    case TAB5_UI_ALIGN_TOP_RIGHT:
        lv_al = LV_ALIGN_TOP_RIGHT;
        break;
    case TAB5_UI_ALIGN_BOTTOM_LEFT:
        lv_al = LV_ALIGN_BOTTOM_LEFT;
        break;
    case TAB5_UI_ALIGN_BOTTOM_MID:
        lv_al = LV_ALIGN_BOTTOM_MID;
        break;
    case TAB5_UI_ALIGN_BOTTOM_RIGHT:
        lv_al = LV_ALIGN_BOTTOM_RIGHT;
        break;
    case TAB5_UI_ALIGN_LEFT_MID:
        lv_al = LV_ALIGN_LEFT_MID;
        break;
    case TAB5_UI_ALIGN_RIGHT_MID:
        lv_al = LV_ALIGN_RIGHT_MID;
        break;
    case TAB5_UI_ALIGN_CENTER:
        lv_al = LV_ALIGN_CENTER;
        break;
    default:
        lv_al = LV_ALIGN_DEFAULT;
        break;
    }
    lv_obj_align(obj, lv_al, x_ofs, y_ofs);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)align;
    (void)x_ofs;
    (void)y_ofs;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_flex_flow(tab5_ui_obj_t obj_handle, tab5_ui_flex_flow_t flow)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_flex_flow_t lv_flow = LV_FLEX_FLOW_ROW;
    switch (flow) {
    case TAB5_UI_FLEX_FLOW_COLUMN:
        lv_flow = LV_FLEX_FLOW_COLUMN;
        break;
    case TAB5_UI_FLEX_FLOW_ROW_WRAP:
        lv_flow = LV_FLEX_FLOW_ROW_WRAP;
        break;
    case TAB5_UI_FLEX_FLOW_COLUMN_WRAP:
        lv_flow = LV_FLEX_FLOW_COLUMN_WRAP;
        break;
    default:
        lv_flow = LV_FLEX_FLOW_ROW;
        break;
    }
    lv_obj_set_flex_flow(obj, lv_flow);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)flow;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_pad(tab5_ui_obj_t obj_handle, int32_t pad_all)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_set_style_pad_all(obj, pad_all, 0);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)pad_all;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_gap(tab5_ui_obj_t obj_handle, int32_t gap)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_set_style_pad_row(obj, gap, 0);
    lv_obj_set_style_pad_column(obj, gap, 0);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)gap;
    return TAB5_OK;
#endif
}

tab5_ui_obj_t tab5_ui_host_label_create(tab5_ui_obj_t parent_handle, const char *text)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *parent = (lv_obj_t *)tab5_ui_host_get_lv_obj(parent_handle);
    if (parent == nullptr) {
        tab5_app_context_t *ctx = tab5_host_get_active_app();
        if (ctx != nullptr) {
            parent = (lv_obj_t *)ctx->root_screen;
        }
    }
    if (parent == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }
    const ui_palette_t *palette = ui_theme_get();
    lv_obj_t *lbl = lv_label_create(parent);
    if (lbl == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }
    lv_label_set_text(lbl, text != nullptr ? text : "");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(palette->text), 0);
    tab5_ui_obj_t res = tab5_ui_host_register_obj(lbl);
    LV_UNLOCK();
    return res;
#else
    (void)parent_handle;
    (void)text;
    return 1;
#endif
}

tab5_err_t tab5_ui_host_label_set_text(tab5_ui_obj_t obj_handle, const char *text)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    const char *resolved_text = text != nullptr ? text : "";
    if (strcmp(resolved_text, "LV_SYMBOL_LIST") == 0) {
        resolved_text = LV_SYMBOL_LIST;
    } else if (strcmp(resolved_text, "LV_SYMBOL_IMAGE") == 0) {
        resolved_text = LV_SYMBOL_IMAGE;
    }
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_label_set_text(obj, resolved_text);
    } else if (lv_obj_get_child_cnt(obj) > 0) {
        lv_obj_t *child = lv_obj_get_child(obj, 0);
        if (child != nullptr && lv_obj_check_type(child, &lv_label_class)) {
            lv_label_set_text(child, resolved_text);
        }
    }
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)text;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_label_set_wrap(tab5_ui_obj_t obj_handle, bool wrap)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_label_long_mode_t mode = wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_CLIP;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_label_set_long_mode(obj, mode);
    } else if (lv_obj_get_child_cnt(obj) > 0) {
        lv_obj_t *child = lv_obj_get_child(obj, 0);
        if (child != nullptr && lv_obj_check_type(child, &lv_label_class)) {
            lv_label_set_long_mode(child, mode);
        }
    }
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)wrap;
    return TAB5_OK;
#endif
}

tab5_ui_obj_t tab5_ui_host_btn_create(tab5_ui_obj_t parent_handle, const char *label_or_symbol)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *parent = (lv_obj_t *)tab5_ui_host_get_lv_obj(parent_handle);
    if (parent == nullptr) {
        tab5_app_context_t *ctx = tab5_host_get_active_app();
        if (ctx != nullptr) {
            parent = (lv_obj_t *)ctx->root_screen;
        }
    }
    if (parent == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }

    const ui_palette_t *palette = ui_theme_get();
    lv_obj_t *btn = lv_btn_create(parent);
    if (btn == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(palette->accent), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_all(btn, 8, 0);

    if (label_or_symbol != nullptr && label_or_symbol[0] != '\0') {
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label_or_symbol);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(lbl);
    }

    lv_obj_add_event_cb(btn, tab5_ui_host_generic_widget_event_cb, LV_EVENT_ALL, nullptr);
    tab5_ui_obj_t res = tab5_ui_host_register_obj(btn);
    LV_UNLOCK();
    return res;
#else
    (void)parent_handle;
    (void)label_or_symbol;
    return 1;
#endif
}

tab5_ui_obj_t tab5_ui_host_switch_create(tab5_ui_obj_t parent_handle)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *parent = (lv_obj_t *)tab5_ui_host_get_lv_obj(parent_handle);
    if (parent == nullptr) {
        tab5_app_context_t *ctx = tab5_host_get_active_app();
        if (ctx != nullptr) {
            parent = (lv_obj_t *)ctx->root_screen;
        }
    }
    if (parent == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }

    const ui_palette_t *palette = ui_theme_get();
    lv_obj_t *sw = lv_switch_create(parent);
    if (sw == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }
    lv_obj_set_style_bg_color(sw, lv_color_hex(palette->accent), LV_PART_INDICATOR);
    lv_obj_add_event_cb(sw, tab5_ui_host_generic_widget_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    tab5_ui_obj_t res = tab5_ui_host_register_obj(sw);
    LV_UNLOCK();
    return res;
#else
    (void)parent_handle;
    return 1;
#endif
}

tab5_err_t tab5_ui_host_switch_set_state(tab5_ui_obj_t obj_handle, bool checked)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    if (checked) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_CHECKED);
    }
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)checked;
    return TAB5_OK;
#endif
}

bool tab5_ui_host_switch_get_state(tab5_ui_obj_t obj_handle)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return false;
    }
    bool res = lv_obj_has_state(obj, LV_STATE_CHECKED);
    LV_UNLOCK();
    return res;
#else
    (void)obj_handle;
    return false;
#endif
}

tab5_ui_obj_t tab5_ui_host_slider_create(tab5_ui_obj_t parent_handle, int32_t min, int32_t max)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *parent = (lv_obj_t *)tab5_ui_host_get_lv_obj(parent_handle);
    if (parent == nullptr) {
        tab5_app_context_t *ctx = tab5_host_get_active_app();
        if (ctx != nullptr) {
            parent = (lv_obj_t *)ctx->root_screen;
        }
    }
    if (parent == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }

    const ui_palette_t *palette = ui_theme_get();
    lv_obj_t *slider = lv_slider_create(parent);
    if (slider == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }
    lv_slider_set_range(slider, min, max);
    lv_obj_set_style_bg_color(slider, lv_color_hex(palette->text_muted), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(palette->accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(palette->accent), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, tab5_ui_host_generic_widget_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    tab5_ui_obj_t res = tab5_ui_host_register_obj(slider);
    LV_UNLOCK();
    return res;
#else
    (void)parent_handle;
    (void)min;
    (void)max;
    return 1;
#endif
}

tab5_err_t tab5_ui_host_slider_set_value(tab5_ui_obj_t obj_handle, int32_t val)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_slider_set_value(obj, val, LV_ANIM_OFF);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)val;
    return TAB5_OK;
#endif
}

int32_t tab5_ui_host_slider_get_value(tab5_ui_obj_t obj_handle)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return 0;
    }
    int32_t res = (int32_t)lv_slider_get_value(obj);
    LV_UNLOCK();
    return res;
#else
    (void)obj_handle;
    return 0;
#endif
}

tab5_ui_obj_t tab5_ui_host_list_create(tab5_ui_obj_t parent_handle)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *parent = (lv_obj_t *)tab5_ui_host_get_lv_obj(parent_handle);
    if (parent == nullptr) {
        tab5_app_context_t *ctx = tab5_host_get_active_app();
        if (ctx != nullptr) {
            parent = (lv_obj_t *)ctx->root_screen;
        }
    }
    if (parent == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }

    const ui_palette_t *palette = ui_theme_get();
    lv_obj_t *list = lv_list_create(parent);
    if (list == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }
    lv_obj_set_style_bg_color(list, lv_color_hex(palette->surface), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 8, 0);

    tab5_ui_obj_t res = tab5_ui_host_register_obj(list);
    LV_UNLOCK();
    return res;
#else
    (void)parent_handle;
    return 1;
#endif
}

tab5_ui_obj_t tab5_ui_host_list_add_btn(tab5_ui_obj_t list_handle, const char *symbol, const char *text)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *list = (lv_obj_t *)tab5_ui_host_get_lv_obj(list_handle);
    if (list == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }

    const ui_palette_t *palette = ui_theme_get();
    lv_obj_t *btn = lv_list_add_btn(list, symbol, text);
    if (btn == nullptr) {
        LV_UNLOCK();
        return TAB5_UI_INVALID_OBJ;
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(palette->surface), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(palette->text), 0);
    lv_obj_add_event_cb(btn, tab5_ui_host_generic_widget_event_cb, LV_EVENT_CLICKED, nullptr);

    tab5_ui_obj_t res = tab5_ui_host_register_obj(btn);
    LV_UNLOCK();
    return res;
#else
    (void)list_handle;
    (void)symbol;
    (void)text;
    return 1;
#endif
}

tab5_err_t tab5_ui_host_obj_clean(tab5_ui_obj_t obj_handle)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_clean(obj);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_clean_deferred(tab5_ui_obj_t obj_handle)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    for (uint32_t i = 1; i < MAX_UI_HANDLES; i++) {
        if (s_handle_table[i] != nullptr && s_handle_table[i] != obj && is_descendant_of(s_handle_table[i], obj)) {
            s_handle_table[i] = nullptr;
        }
    }
    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = child_count; i > 0; i--) {
        lv_obj_t *child = lv_obj_get_child(obj, i - 1);
        if (child == nullptr) {
            continue;
        }
        lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(child);
    }
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_clear_app_content(tab5_app_context_t *ctx)
{
#if HAVE_LVGL
    if (ctx == nullptr || ctx->root_screen == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    LV_LOCK();
    lv_obj_t *scr = (lv_obj_t *)ctx->root_screen;
    lv_obj_t *keep_bar = (lv_obj_t *)ctx->app_bar;
    lv_obj_t *keep_ta = (lv_obj_t *)ctx->content_area;
    uint32_t n = lv_obj_get_child_cnt(scr);
    for (uint32_t i = n; i > 0; i--) {
        lv_obj_t *child = lv_obj_get_child(scr, i - 1);
        if (child == keep_bar || child == keep_ta) {
            continue;
        }
        lv_obj_delete(child);
    }
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)ctx;
    return TAB5_OK;
#endif
}

uint32_t tab5_ui_host_theme_get_color(uint32_t color_id)
{
#if HAVE_LVGL
    const ui_palette_t *pal = ui_theme_get();
    if (pal == nullptr) {
        return 0xFFFFFF;
    }
    switch (color_id) {
    case 0:
        return pal->accent;
    case 1:
        return pal->accent;
    case 2:
        return pal->accent_soft;
    case 3:
        return pal->surface;
    case 4:
        return pal->surface_alt;
    case 5:
        return pal->border;
    case 6:
        return pal->text;
    case 7:
        return pal->text_muted;
    case 8:
        return pal->background;
    default:
        return pal->text;
    }
#else
    (void)color_id;
    return 0xFFFFFF;
#endif
}

tab5_err_t tab5_ui_host_obj_set_style_bg(tab5_ui_obj_t obj_handle, uint32_t color_hex, uint8_t opa)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_set_style_bg_color(obj, lv_color_hex(color_hex), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)color_hex;
    (void)opa;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_style_border(tab5_ui_obj_t obj_handle, uint32_t border_hex, int32_t width)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_set_style_border_color(obj, lv_color_hex(border_hex), 0);
    lv_obj_set_style_border_width(obj, width, 0);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)border_hex;
    (void)width;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_style_text_color(tab5_ui_obj_t obj_handle, uint32_t color_hex, uint8_t opa)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_set_style_text_color(obj, lv_color_hex(color_hex), 0);
    lv_obj_set_style_text_opa(obj, opa, 0);
    if (lv_obj_get_child_cnt(obj) > 0) {
        lv_obj_t *child = lv_obj_get_child(obj, 0);
        if (child != nullptr && lv_obj_check_type(child, &lv_label_class)) {
            lv_obj_set_style_text_color(child, lv_color_hex(color_hex), 0);
            lv_obj_set_style_text_opa(child, opa, 0);
        }
    }
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)color_hex;
    (void)opa;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_style_text_size(tab5_ui_obj_t obj_handle, int32_t size_px)
{
#if HAVE_LVGL
    const lv_font_t *font = nullptr;
    if (size_px == 14) {
        font = &lv_font_montserrat_14_latin1;
    } else if (size_px == 18) {
        font = &lv_font_montserrat_18_latin1;
    } else if (size_px == 28) {
        font = &lv_font_montserrat_28_latin1;
    } else if (size_px == 56) {
        font = &lv_font_montserrat_56_latin1;
    } else {
        return TAB5_ERR_INVALID_ARG;
    }

    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_set_style_text_font(obj, font, 0);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)size_px;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_style_radius(tab5_ui_obj_t obj_handle, int32_t radius)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_set_style_radius(obj, radius, 0);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)radius;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_flex_grow(tab5_ui_obj_t obj_handle, uint8_t grow)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    lv_obj_set_flex_grow(obj, grow);
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)grow;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_ui_host_obj_set_clickable(tab5_ui_obj_t obj_handle, bool clickable)
{
#if HAVE_LVGL
    LV_LOCK();
    lv_obj_t *obj = (lv_obj_t *)tab5_ui_host_get_lv_obj(obj_handle);
    if (obj == nullptr) {
        LV_UNLOCK();
        return TAB5_ERR_INVALID_ARG;
    }
    if (clickable) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj, tab5_ui_host_generic_widget_event_cb, LV_EVENT_ALL, nullptr);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
    LV_UNLOCK();
    return TAB5_OK;
#else
    (void)obj_handle;
    (void)clickable;
    return TAB5_OK;
#endif
}
