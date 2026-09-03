#include "ui_camera_view.h"
#include "camera_mgr.h"
#include "ui_bar.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "tab5_package_mgr.h"
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string>

static const char *TAG = "tab5_ui_camera_view";

#define PREVIEW_W 640
#define PREVIEW_H 480

struct ui_camera_view_s {
    lv_obj_t *parent = nullptr;
    ui_app_bar_t app_bar = {};

    lv_obj_t *preview_container = nullptr;
    lv_obj_t *preview_canvas = nullptr;
    uint8_t *canvas_buf = nullptr;

    lv_obj_t *bottom_bar = nullptr;
    lv_obj_t *gallery_btn = nullptr;
    lv_obj_t *gallery_label = nullptr;
    lv_obj_t *shutter_btn = nullptr;
    lv_obj_t *shutter_inner = nullptr;
    lv_obj_t *toast_label = nullptr;
    lv_timer_t *toast_timer = nullptr;

    lv_obj_t *flash_overlay = nullptr;

    std::string last_saved_filepath;
};

namespace {
void toast_timer_cb(lv_timer_t *timer)
{
    ui_camera_view_t *view = (ui_camera_view_t *)lv_timer_get_user_data(timer);
    lv_timer_delete(timer);
    if (view != nullptr) {
        view->toast_timer = nullptr;
        if (view->toast_label != nullptr) {
            lv_obj_add_flag(view->toast_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void flash_timer_cb(lv_timer_t *timer)
{
    ui_camera_view_t *view = (ui_camera_view_t *)lv_timer_get_user_data(timer);
    lv_timer_delete(timer);
    if (view != nullptr && view->flash_overlay != nullptr) {
        lv_obj_add_flag(view->flash_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void show_toast(ui_camera_view_t *view, const char *text)
{
    if (view == nullptr || view->toast_label == nullptr) {
        return;
    }
    lv_label_set_text(view->toast_label, text);
    lv_obj_clear_flag(view->toast_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(view->toast_label);
    if (view->toast_timer != nullptr) {
        lv_timer_del(view->toast_timer);
    }
    view->toast_timer = lv_timer_create(toast_timer_cb, 3000, view);
}

void trigger_shutter_flash(ui_camera_view_t *view)
{
    if (view != nullptr && view->flash_overlay != nullptr) {
        lv_obj_clear_flag(view->flash_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_timer_create(flash_timer_cb, 80, view);
    }
}

void gallery_click_cb(lv_event_t *e)
{
    (void)e;
    tab5_package_mgr_launch("com.tab5.gallery", nullptr);
}

void on_photo_saved_cb(esp_err_t res, const char *path, void *user_data)
{
    ui_camera_view_t *view = (ui_camera_view_t *)user_data;
    if (bsp_display_lock(pdMS_TO_TICKS(100))) {
        if (res == ESP_OK && path != nullptr) {
            view->last_saved_filepath = path;
            const char *basename = strrchr(path, '/');
            char msg[140];
            snprintf(msg, sizeof(msg), "Foto salva: %s", basename ? basename + 1 : path);
            show_toast(view, msg);
        } else {
            show_toast(view, "Falha ao salvar foto");
        }
        bsp_display_unlock();
    }
}

void shutter_click_cb(lv_event_t *e)
{
    ui_camera_view_t *view = (ui_camera_view_t *)lv_event_get_user_data(e);
    if (view == nullptr) {
        return;
    }
    trigger_shutter_flash(view);
    show_toast(view, "Salvando foto...");

    lv_display_t *disp = lv_display_get_default();
    lv_disp_rotation_t rot = disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0;

    char target_path[128] = {0};
    esp_err_t err = camera_mgr_capture_photo_with_rotation_async(target_path, sizeof(target_path), (int)rot,
                                                                 on_photo_saved_cb, view);
    if (err == ESP_OK && target_path[0] != '\0') {
        view->last_saved_filepath = target_path;
    }
}

void on_camera_frame(const uint8_t *frame_buf, uint16_t width, uint16_t height, void *user_data)
{
    ui_camera_view_t *view = (ui_camera_view_t *)user_data;
    if (view == nullptr) {
        return;
    }
    static uint32_t s_ui_frames = 0;
    if (++s_ui_frames % 30 == 1) {
        ESP_LOGI(TAG, "UI frame recebido #%lu (%ux%u)", (unsigned long)s_ui_frames, (unsigned)width, (unsigned)height);
    }
    if (view->preview_canvas == nullptr || view->canvas_buf == nullptr) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    lv_disp_rotation_t rot = disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0;

    if (bsp_display_lock(pdMS_TO_TICKS(20))) {
        if (rot == LV_DISPLAY_ROTATION_0) {
            /* Retrato normal (0): rotaciona 90 CW para orientar em pe (480x640) */
            camera_mgr_rotate_rgb565_90((const uint16_t *)frame_buf, width, height, (uint16_t *)view->canvas_buf);
        } else if (rot == LV_DISPLAY_ROTATION_180) {
            /* Retrato invertido (180): rotaciona 270 CW (480x640) */
            camera_mgr_rotate_rgb565_270((const uint16_t *)frame_buf, width, height, (uint16_t *)view->canvas_buf);
        } else if (rot == LV_DISPLAY_ROTATION_270) {
            /* Paisagem B (270): rotaciona 180 (640x480) */
            camera_mgr_rotate_rgb565_180((const uint16_t *)frame_buf, width, height, (uint16_t *)view->canvas_buf);
        } else {
            /* Paisagem A (90): copia direta sem rotacao (640x480) */
            if (width == PREVIEW_W && height == PREVIEW_H) {
                memcpy(view->canvas_buf, frame_buf, (size_t)PREVIEW_W * PREVIEW_H * 2);
            }
        }
        lv_obj_invalidate(view->preview_canvas);
        bsp_display_unlock();
    }
}

} // namespace

ui_camera_view_t *ui_camera_view_create(lv_obj_t *parent, ui_app_bar_t app_bar)
{
    ui_camera_view_t *view = new (std::nothrow) ui_camera_view_t();
    if (view == nullptr) {
        return nullptr;
    }

    view->parent = parent;
    view->app_bar = app_bar;

    /* Container Central de Preview */
    view->preview_container = lv_obj_create(parent);
    lv_obj_set_size(view->preview_container, PREVIEW_W, PREVIEW_H);
    lv_obj_align(view->preview_container, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 16);
    lv_obj_set_style_bg_color(view->preview_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(view->preview_container, 0, 0);
    lv_obj_set_style_pad_all(view->preview_container, 0, 0);
    lv_obj_set_style_radius(view->preview_container, 12, 0);
    lv_obj_set_style_clip_corner(view->preview_container, true, 0);
    lv_obj_clear_flag(view->preview_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(view->preview_container, view);

    /* Canvas para renderizacao de frames RGB565 (aloca tamanho maximo 640x480x2) */
    size_t c_size = (size_t)PREVIEW_W * PREVIEW_H * 2;
    view->canvas_buf = (uint8_t *)heap_caps_malloc(c_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!view->canvas_buf) {
        view->canvas_buf = (uint8_t *)malloc(c_size);
    }
    if (view->canvas_buf) {
        memset(view->canvas_buf, 0x18, c_size);
        view->preview_canvas = lv_canvas_create(view->preview_container);
        lv_canvas_set_buffer(view->preview_canvas, view->canvas_buf, PREVIEW_W, PREVIEW_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_center(view->preview_canvas);
        lv_obj_set_user_data(view->preview_canvas, view);
    }

    /* Barra Inferior com Disparador e Atalho da Galeria */
    view->bottom_bar = lv_obj_create(parent);
    lv_obj_set_size(view->bottom_bar, lv_pct(100), 96);
    lv_obj_align(view->bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(view->bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(view->bottom_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(view->bottom_bar, 1, 0);
    lv_obj_set_style_radius(view->bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(view->bottom_bar, 8, 0);
    lv_obj_clear_flag(view->bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(view->bottom_bar, view);

    /* Botao Atalho para a Galeria / Preview de Fotos (embaixo, proximo ao botao de tirar foto) */
    view->gallery_btn = lv_obj_create(view->bottom_bar);
    lv_obj_set_size(view->gallery_btn, 50, 50);
    lv_obj_align(view->gallery_btn, LV_ALIGN_CENTER, -130, 0);
    lv_obj_set_style_radius(view->gallery_btn, 12, 0);
    lv_obj_set_style_border_width(view->gallery_btn, 1, 0);
    lv_obj_clear_flag(view->gallery_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(view->gallery_btn, gallery_click_cb, LV_EVENT_CLICKED, view);
    lv_obj_set_user_data(view->gallery_btn, view);

    view->gallery_label = lv_label_create(view->gallery_btn);
    lv_label_set_text(view->gallery_label, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(view->gallery_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(view->gallery_label);

    /* Botao circular central de disparo */
    view->shutter_btn = lv_obj_create(view->bottom_bar);
    lv_obj_set_size(view->shutter_btn, 72, 72);
    lv_obj_align(view->shutter_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(view->shutter_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(view->shutter_btn, 0, 0);
    lv_obj_set_style_shadow_width(view->shutter_btn, 4, 0);
    lv_obj_clear_flag(view->shutter_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(view->shutter_btn, shutter_click_cb, LV_EVENT_CLICKED, view);
    lv_obj_set_user_data(view->shutter_btn, view);

    view->shutter_inner = lv_obj_create(view->shutter_btn);
    lv_obj_set_size(view->shutter_inner, 54, 54);
    lv_obj_align(view->shutter_inner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(view->shutter_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(view->shutter_inner, 0, 0);
    lv_obj_clear_flag(view->shutter_inner, LV_OBJ_FLAG_CLICKABLE);

    /* Toast informativo */
    view->toast_label = lv_label_create(parent);
    lv_obj_align(view->toast_label, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 24);
    lv_obj_set_style_radius(view->toast_label, 16, 0);
    lv_obj_set_style_pad_hor(view->toast_label, 16, 0);
    lv_obj_set_style_pad_ver(view->toast_label, 8, 0);
    lv_obj_set_style_border_width(view->toast_label, 1, 0);
    lv_obj_set_style_text_font(view->toast_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_add_flag(view->toast_label, LV_OBJ_FLAG_HIDDEN);

    /* Overlay de flash ao disparar */
    view->flash_overlay = lv_obj_create(parent);
    lv_obj_set_size(view->flash_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(view->flash_overlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(view->flash_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(view->flash_overlay, 0, 0);
    lv_obj_clear_flag(view->flash_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(view->flash_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(view->flash_overlay, LV_OBJ_FLAG_HIDDEN);

    /* Reage a mudancas de orientacao */
    lv_obj_add_event_cb(
        parent,
        [](lv_event_t *e) {
            ui_camera_view_t *v = (ui_camera_view_t *)lv_event_get_user_data(e);
            if (v != nullptr) {
                ui_camera_view_apply_layout(v);
            }
        },
        LV_EVENT_SIZE_CHANGED, view);

    ui_camera_view_refresh_theme(view);
    ui_camera_view_apply_layout(view);
    return view;
}

void ui_camera_view_refresh_theme(ui_camera_view_t *view)
{
    if (view == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    if (view->parent != nullptr) {
        lv_obj_set_style_bg_color(view->parent, lv_color_hex(pal->background), 0);
    }

    ui_app_bar_refresh_theme(&view->app_bar);

    if (view->bottom_bar != nullptr) {
        lv_obj_set_style_bg_color(view->bottom_bar, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(view->bottom_bar, lv_color_hex(pal->border), 0);
    }
    if (view->gallery_btn != nullptr) {
        lv_obj_set_style_bg_color(view->gallery_btn, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(view->gallery_btn, lv_color_hex(pal->border), 0);
    }
    if (view->gallery_label != nullptr) {
        lv_obj_set_style_text_color(view->gallery_label, lv_color_hex(pal->accent), 0);
    }
    if (view->shutter_btn != nullptr) {
        lv_obj_set_style_bg_color(view->shutter_btn, lv_color_hex(pal->accent), 0);
    }
    if (view->shutter_inner != nullptr) {
        lv_obj_set_style_bg_color(view->shutter_inner, lv_color_hex(pal->surface), 0);
    }
    if (view->toast_label != nullptr) {
        lv_obj_set_style_bg_opa(view->toast_label, LV_OPA_90, 0);
        lv_obj_set_style_bg_color(view->toast_label, lv_color_hex(0x181825), 0);
        lv_obj_set_style_text_color(view->toast_label, lv_color_white(), 0);
        lv_obj_set_style_border_color(view->toast_label, lv_color_hex(pal->accent), 0);
        lv_obj_set_style_border_width(view->toast_label, 2, 0);
        lv_obj_set_style_pad_hor(view->toast_label, 24, 0);
        lv_obj_set_style_pad_ver(view->toast_label, 12, 0);
        lv_obj_set_style_radius(view->toast_label, 14, 0);
        lv_obj_set_style_shadow_width(view->toast_label, 16, 0);
        lv_obj_set_style_shadow_color(view->toast_label, lv_color_black(), 0);
    }
}

void ui_camera_view_apply_layout(ui_camera_view_t *view)
{
    if (view == nullptr || view->preview_container == nullptr || view->preview_canvas == nullptr) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    lv_disp_rotation_t rot = disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0;
    bool is_portrait = (rot == LV_DISPLAY_ROTATION_0 || rot == LV_DISPLAY_ROTATION_180);

    if (is_portrait) {
        /* Modo Retrato: preview em pe (480 largura x 640 altura) */
        lv_obj_set_size(view->preview_container, 480, 640);
        lv_obj_align(view->preview_container, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 30);
        if (view->canvas_buf != nullptr) {
            lv_canvas_set_buffer(view->preview_canvas, view->canvas_buf, 480, 640, LV_COLOR_FORMAT_RGB565);
        }
        lv_obj_set_size(view->preview_canvas, 480, 640);
        lv_obj_center(view->preview_canvas);

        if (view->bottom_bar != nullptr) {
            lv_obj_set_size(view->bottom_bar, lv_pct(100), 96);
            lv_obj_align(view->bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
    } else {
        /* Modo Paisagem: preview horizontal (640 largura x 480 altura) */
        lv_obj_set_size(view->preview_container, 640, 480);
        lv_obj_align(view->preview_container, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 10);
        if (view->canvas_buf != nullptr) {
            lv_canvas_set_buffer(view->preview_canvas, view->canvas_buf, 640, 480, LV_COLOR_FORMAT_RGB565);
        }
        lv_obj_set_size(view->preview_canvas, 640, 480);
        lv_obj_center(view->preview_canvas);

        if (view->bottom_bar != nullptr) {
            lv_obj_set_size(view->bottom_bar, lv_pct(100), 80);
            lv_obj_align(view->bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
    }
    lv_obj_invalidate(view->parent);
}

void ui_camera_view_start(ui_camera_view_t *view)
{
    if (view == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "iniciando preview da câmera");
    ui_camera_view_apply_layout(view);
    camera_mgr_start_preview(on_camera_frame, view);
}

void ui_camera_view_stop(ui_camera_view_t *view)
{
    if (view == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "parando preview da câmera");
    camera_mgr_stop_preview();
    if (view->toast_timer != nullptr) {
        lv_timer_del(view->toast_timer);
        view->toast_timer = nullptr;
    }
}

void ui_camera_view_destroy(ui_camera_view_t *view)
{
    if (view == nullptr) {
        return;
    }
    ui_camera_view_stop(view);
    if (view->canvas_buf != nullptr) {
        free(view->canvas_buf);
        view->canvas_buf = nullptr;
    }
    delete view;
}
