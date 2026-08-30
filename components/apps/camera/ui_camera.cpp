#include "ui_camera.h"
#include "app_registry.h"
#include "camera_mgr.h"
#include "ui_bar.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <cstdio>
#include <cstring>
#include <string>

static const char *TAG = "tab5_ui_camera";

#define PREVIEW_W 640
#define PREVIEW_H 480

namespace {

static std::string s_last_saved_filepath;

lv_obj_t *camera_scr = nullptr;
ui_app_bar_t camera_app_bar = {};

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

void toast_timer_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    toast_timer = nullptr;
    if (toast_label != nullptr) {
        lv_obj_add_flag(toast_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void show_toast(const char *text)
{
    if (toast_label == nullptr) {
        return;
    }
    lv_label_set_text(toast_label, text);
    lv_obj_clear_flag(toast_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_label);
    if (toast_timer != nullptr) {
        lv_timer_delete(toast_timer);
    }
    toast_timer = lv_timer_create(toast_timer_cb, 3000, nullptr);
}

void flash_timer_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    if (flash_overlay != nullptr) {
        lv_obj_add_flag(flash_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void trigger_shutter_flash(void)
{
    if (flash_overlay != nullptr) {
        lv_obj_clear_flag(flash_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_timer_create(flash_timer_cb, 80, nullptr);
    }
}

void close_click_cb(lv_event_t *e)
{
    (void)e;
    ui_shell_close_camera();
}

void gallery_click_cb(lv_event_t *e)
{
    (void)e;
    ui_shell_open_gallery();
}

void on_photo_saved_cb(esp_err_t res, const char *filepath, void *user_data)
{
    (void)user_data;
    if (bsp_display_lock(pdMS_TO_TICKS(100))) {
        if (res == ESP_OK && filepath != nullptr) {
            s_last_saved_filepath = filepath;
            const char *basename = strrchr(filepath, '/');
            char msg[140];
            snprintf(msg, sizeof(msg), "Foto salva: %s", basename ? basename + 1 : filepath);
            show_toast(msg);
        } else {
            show_toast("Falha ao salvar foto");
        }
        bsp_display_unlock();
    }
}

void shutter_click_cb(lv_event_t *e)
{
    (void)e;
    trigger_shutter_flash();
    show_toast("Salvando foto...");

    lv_display_t *disp = lv_display_get_default();
    lv_disp_rotation_t rot = disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0;

    char target_path[128] = {0};
    esp_err_t err = camera_mgr_capture_photo_with_rotation_async(target_path, sizeof(target_path), (int)rot,
                                                                 on_photo_saved_cb, nullptr);
    if (err == ESP_OK && target_path[0] != '\0') {
        s_last_saved_filepath = target_path;
    }
}

void on_camera_frame(const uint8_t *frame_buf, uint16_t width, uint16_t height, void *user_data)
{
    (void)user_data;
    static uint32_t s_ui_frames = 0;
    if (++s_ui_frames % 30 == 1) {
        ESP_LOGI(TAG, "UI frame recebido #%lu (%ux%u)", (unsigned long)s_ui_frames, (unsigned)width, (unsigned)height);
    }
    if (preview_canvas == nullptr || canvas_buf == nullptr) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    lv_disp_rotation_t rot = disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0;

    if (bsp_display_lock(pdMS_TO_TICKS(20))) {
        if (rot == LV_DISPLAY_ROTATION_0) {
            /* Retrato normal (0): rotaciona 90 CW para orientar em pe (480x640) */
            camera_mgr_rotate_rgb565_90((const uint16_t *)frame_buf, width, height, (uint16_t *)canvas_buf);
        } else if (rot == LV_DISPLAY_ROTATION_180) {
            /* Retrato invertido (180): rotaciona 270 CW (480x640) */
            camera_mgr_rotate_rgb565_270((const uint16_t *)frame_buf, width, height, (uint16_t *)canvas_buf);
        } else if (rot == LV_DISPLAY_ROTATION_270) {
            /* Paisagem B (270): rotaciona 180 (640x480) */
            camera_mgr_rotate_rgb565_180((const uint16_t *)frame_buf, width, height, (uint16_t *)canvas_buf);
        } else {
            /* Paisagem A (90): copia direta sem rotacao (640x480) */
            if (width == PREVIEW_W && height == PREVIEW_H) {
                memcpy(canvas_buf, frame_buf, (size_t)PREVIEW_W * PREVIEW_H * 2);
            }
        }
        lv_obj_invalidate(preview_canvas);
        bsp_display_unlock();
    }
}

void apply_camera_theme(void)
{
    if (camera_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(camera_scr, lv_color_hex(pal->background), 0);

    /* Barra Superior */
    ui_app_bar_refresh_theme(&camera_app_bar);

    /* Barra Inferior */
    if (bottom_bar != nullptr) {
        lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(bottom_bar, lv_color_hex(pal->border), 0);
    }
    if (gallery_btn != nullptr) {
        lv_obj_set_style_bg_color(gallery_btn, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(gallery_btn, lv_color_hex(pal->border), 0);
    }
    if (gallery_label != nullptr) {
        lv_obj_set_style_text_color(gallery_label, lv_color_hex(pal->accent), 0);
    }
    if (shutter_btn != nullptr) {
        lv_obj_set_style_bg_color(shutter_btn, lv_color_hex(pal->accent), 0);
    }
    if (shutter_inner != nullptr) {
        lv_obj_set_style_bg_color(shutter_inner, lv_color_hex(pal->surface), 0);
    }
    if (toast_label != nullptr) {
        lv_obj_set_style_bg_opa(toast_label, LV_OPA_90, 0);
        lv_obj_set_style_bg_color(toast_label, lv_color_hex(0x181825), 0);
        lv_obj_set_style_text_color(toast_label, lv_color_white(), 0);
        lv_obj_set_style_border_color(toast_label, lv_color_hex(pal->accent), 0);
        lv_obj_set_style_border_width(toast_label, 2, 0);
        lv_obj_set_style_pad_hor(toast_label, 24, 0);
        lv_obj_set_style_pad_ver(toast_label, 12, 0);
        lv_obj_set_style_radius(toast_label, 14, 0);
        lv_obj_set_style_shadow_width(toast_label, 16, 0);
        lv_obj_set_style_shadow_color(toast_label, lv_color_black(), 0);
    }
}

} // namespace

lv_obj_t *ui_camera_create(void)
{
    camera_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(camera_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Barra superior padronizada do app */
    camera_app_bar = ui_app_bar_create(camera_scr, "Câmera", close_click_cb, nullptr);

    /* Container Central de Preview */
    preview_container = lv_obj_create(camera_scr);
    lv_obj_set_size(preview_container, PREVIEW_W, PREVIEW_H);
    lv_obj_align(preview_container, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 16);
    lv_obj_set_style_bg_color(preview_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(preview_container, 0, 0);
    lv_obj_set_style_pad_all(preview_container, 0, 0);
    lv_obj_set_style_radius(preview_container, 12, 0);
    lv_obj_set_style_clip_corner(preview_container, true, 0);
    lv_obj_clear_flag(preview_container, LV_OBJ_FLAG_SCROLLABLE);

    /* Canvas para renderizacao de frames RGB565 (aloca tamanho maximo 640x480x2) */
    size_t c_size = (size_t)PREVIEW_W * PREVIEW_H * 2;
    canvas_buf = (uint8_t *)heap_caps_malloc(c_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!canvas_buf) {
        canvas_buf = (uint8_t *)malloc(c_size);
    }
    if (canvas_buf) {
        memset(canvas_buf, 0x18, c_size);
    }

    preview_canvas = lv_canvas_create(preview_container);
    if (canvas_buf) {
        lv_canvas_set_buffer(preview_canvas, canvas_buf, PREVIEW_W, PREVIEW_H, LV_COLOR_FORMAT_RGB565);
    }
    lv_obj_center(preview_canvas);

    /* Barra Inferior com Disparador e Atalho da Galeria */
    bottom_bar = lv_obj_create(camera_scr);
    lv_obj_set_size(bottom_bar, lv_pct(100), 96);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bottom_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bottom_bar, 1, 0);
    lv_obj_set_style_radius(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 8, 0);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Botao Atalho para a Galeria / Preview de Fotos (embaixo, proximo ao botao de tirar foto) */
    gallery_btn = lv_obj_create(bottom_bar);
    lv_obj_set_size(gallery_btn, 50, 50);
    lv_obj_align(gallery_btn, LV_ALIGN_CENTER, -130, 0);
    lv_obj_set_style_radius(gallery_btn, 12, 0);
    lv_obj_set_style_border_width(gallery_btn, 1, 0);
    lv_obj_clear_flag(gallery_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(gallery_btn, gallery_click_cb, LV_EVENT_CLICKED, nullptr);

    gallery_label = lv_label_create(gallery_btn);
    lv_label_set_text(gallery_label, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(gallery_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(gallery_label);

    /* Botao circular central de disparo */
    shutter_btn = lv_obj_create(bottom_bar);
    lv_obj_set_size(shutter_btn, 72, 72);
    lv_obj_align(shutter_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(shutter_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(shutter_btn, 0, 0);
    lv_obj_set_style_shadow_width(shutter_btn, 4, 0);
    lv_obj_clear_flag(shutter_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(shutter_btn, shutter_click_cb, LV_EVENT_CLICKED, nullptr);

    shutter_inner = lv_obj_create(shutter_btn);
    lv_obj_set_size(shutter_inner, 54, 54);
    lv_obj_align(shutter_inner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(shutter_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(shutter_inner, 0, 0);
    lv_obj_clear_flag(shutter_inner, LV_OBJ_FLAG_CLICKABLE);

    /* Toast informativo */
    toast_label = lv_label_create(camera_scr);
    lv_obj_align(toast_label, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 24);
    lv_obj_set_style_radius(toast_label, 16, 0);
    lv_obj_set_style_pad_hor(toast_label, 16, 0);
    lv_obj_set_style_pad_ver(toast_label, 8, 0);
    lv_obj_set_style_border_width(toast_label, 1, 0);
    lv_obj_set_style_text_font(toast_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_add_flag(toast_label, LV_OBJ_FLAG_HIDDEN);

    /* Overlay de flash ao disparar */
    flash_overlay = lv_obj_create(camera_scr);
    lv_obj_set_size(flash_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(flash_overlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(flash_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(flash_overlay, 0, 0);
    lv_obj_clear_flag(flash_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(flash_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(flash_overlay, LV_OBJ_FLAG_HIDDEN);

    /* Reage a mudancas de orientacao */
    lv_obj_add_event_cb(
        camera_scr,
        [](lv_event_t *e) {
            (void)e;
            ui_camera_apply_layout();
        },
        LV_EVENT_SIZE_CHANGED, nullptr);

    apply_camera_theme();
    ui_camera_apply_layout();
    return camera_scr;
}

void ui_camera_refresh_theme(void)
{
    apply_camera_theme();
}

void ui_camera_apply_layout(void)
{
    if (camera_scr == nullptr || preview_container == nullptr || preview_canvas == nullptr) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    lv_disp_rotation_t rot = disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0;
    bool is_portrait = (rot == LV_DISPLAY_ROTATION_0 || rot == LV_DISPLAY_ROTATION_180);

    if (is_portrait) {
        /* Modo Retrato: preview em pe (480 largura x 640 altura) */
        lv_obj_set_size(preview_container, 480, 640);
        lv_obj_align(preview_container, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 30);
        if (canvas_buf != nullptr) {
            lv_canvas_set_buffer(preview_canvas, canvas_buf, 480, 640, LV_COLOR_FORMAT_RGB565);
        }
        lv_obj_set_size(preview_canvas, 480, 640);
        lv_obj_center(preview_canvas);

        if (bottom_bar != nullptr) {
            lv_obj_set_size(bottom_bar, lv_pct(100), 96);
            lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
    } else {
        /* Modo Paisagem: preview horizontal (640 largura x 480 altura) */
        lv_obj_set_size(preview_container, 640, 480);
        lv_obj_align(preview_container, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 10);
        if (canvas_buf != nullptr) {
            lv_canvas_set_buffer(preview_canvas, canvas_buf, 640, 480, LV_COLOR_FORMAT_RGB565);
        }
        lv_obj_set_size(preview_canvas, 640, 480);
        lv_obj_center(preview_canvas);

        if (bottom_bar != nullptr) {
            lv_obj_set_size(bottom_bar, lv_pct(100), 80);
            lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
    }
    lv_obj_invalidate(camera_scr);
}

void ui_camera_on_open(void)
{
    ESP_LOGI(TAG, "abrindo app Câmera");
    ui_camera_apply_layout();
    camera_mgr_start_preview(on_camera_frame, nullptr);
}

void ui_camera_on_close(void)
{
    ESP_LOGI(TAG, "fechando app Câmera");
    camera_mgr_stop_preview();
    if (toast_timer != nullptr) {
        lv_timer_delete(toast_timer);
        toast_timer = nullptr;
    }
}

static void ui_camera_build_icon(lv_obj_t *icon_box)
{
    const ui_palette_t *pal = ui_theme_get();
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(icon_box, LV_SCROLLBAR_MODE_OFF);

    /* Corpo da camera fotografica */
    lv_obj_t *cam_body = lv_obj_create(icon_box);
    lv_obj_set_size(cam_body, 44, 30);
    lv_obj_align(cam_body, LV_ALIGN_CENTER, 0, 3);
    lv_obj_set_style_radius(cam_body, 6, 0);
    lv_obj_set_style_border_width(cam_body, 0, 0);
    lv_obj_set_style_pad_all(cam_body, 0, 0);
    lv_obj_set_style_bg_color(cam_body, lv_color_hex(pal->accent), 0);
    lv_obj_clear_flag(cam_body, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_body, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *cam_bump = lv_obj_create(icon_box);
    lv_obj_set_size(cam_bump, 14, 6);
    lv_obj_align(cam_bump, LV_ALIGN_CENTER, -6, -13);
    lv_obj_set_style_radius(cam_bump, 2, 0);
    lv_obj_set_style_border_width(cam_bump, 0, 0);
    lv_obj_set_style_bg_color(cam_bump, lv_color_hex(pal->accent), 0);
    lv_obj_clear_flag(cam_bump, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_bump, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_bump, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *cam_flash = lv_obj_create(icon_box);
    lv_obj_set_size(cam_flash, 5, 5);
    lv_obj_align(cam_flash, LV_ALIGN_CENTER, 12, -12);
    lv_obj_set_style_radius(cam_flash, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cam_flash, 0, 0);
    lv_obj_set_style_bg_color(cam_flash, lv_color_hex(pal->accent), 0);
    lv_obj_clear_flag(cam_flash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_flash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_flash, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *cam_lens = lv_obj_create(cam_body);
    lv_obj_set_size(cam_lens, 20, 20);
    lv_obj_align(cam_lens, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(cam_lens, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cam_lens, 2, 0);
    lv_obj_set_style_border_color(cam_lens, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_bg_color(cam_lens, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_pad_all(cam_lens, 0, 0);
    lv_obj_clear_flag(cam_lens, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_lens, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_lens, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *cam_pupil = lv_obj_create(cam_lens);
    lv_obj_set_size(cam_pupil, 8, 8);
    lv_obj_align(cam_pupil, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(cam_pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cam_pupil, 0, 0);
    lv_obj_set_style_bg_color(cam_pupil, lv_color_hex(pal->surface), 0);
    lv_obj_clear_flag(cam_pupil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_pupil, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_pupil, LV_SCROLLBAR_MODE_OFF);
}

static void ui_camera_theme_icon(lv_obj_t *icon_box)
{
    const ui_palette_t *pal = ui_theme_get();
    if (icon_box == nullptr) {
        return;
    }
    uint32_t count = lv_obj_get_child_count(icon_box);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *c = lv_obj_get_child(icon_box, i);
        if (c == nullptr) {
            continue;
        }
        if (lv_obj_get_width(c) == 44) {
            lv_obj_set_style_bg_color(c, lv_color_hex(pal->accent), 0);
            uint32_t inner_count = lv_obj_get_child_count(c);
            for (uint32_t j = 0; j < inner_count; j++) {
                lv_obj_t *inner = lv_obj_get_child(c, j);
                if (inner == nullptr) {
                    continue;
                }
                lv_obj_set_style_bg_color(inner, lv_color_hex(pal->accent), 0);
                lv_obj_set_style_border_color(inner, lv_color_hex(pal->surface), 0);
                uint32_t p_count = lv_obj_get_child_count(inner);
                for (uint32_t k = 0; k < p_count; k++) {
                    lv_obj_t *pupil = lv_obj_get_child(inner, k);
                    if (pupil != nullptr) {
                        lv_obj_set_style_bg_color(pupil, lv_color_hex(pal->surface), 0);
                    }
                }
            }
        } else {
            lv_obj_set_style_bg_color(c, lv_color_hex(pal->accent), 0);
        }
    }
}

void ui_camera_register(void)
{
    static const app_desc_t s_camera_desc = {
        .id = "camera",
        .name = "Câmera",
        .icon_symbol = nullptr,
        .icon_bg_color = nullptr,
        .icon_builder = ui_camera_build_icon,
        .icon_theme_refresh = ui_camera_theme_icon,
        .on_launch = ui_shell_open_camera,
        .file_extensions = nullptr,
        .on_open_file = nullptr,
    };
    app_registry_register(&s_camera_desc);
}
