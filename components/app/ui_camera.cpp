#include "ui_camera.h"
#include "camera_mgr.h"
#include "ui_bar.h"
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
lv_obj_t *top_bar = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *close_btn = nullptr;
lv_obj_t *close_label = nullptr;

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
    if (!s_last_saved_filepath.empty()) {
        ui_shell_open_gallery_with_file(s_last_saved_filepath.c_str());
    } else {
        ui_shell_open_gallery();
    }
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

    char target_path[128] = {0};
    esp_err_t err = camera_mgr_capture_photo_async(target_path, sizeof(target_path), on_photo_saved_cb, nullptr);
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

    if (bsp_display_lock(pdMS_TO_TICKS(20))) {
        if (width == PREVIEW_W && height == PREVIEW_H) {
            memcpy(canvas_buf, frame_buf, PREVIEW_W * PREVIEW_H * 2);
            lv_obj_invalidate(preview_canvas);
        }
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
    if (top_bar != nullptr) {
        lv_obj_set_style_bg_color(top_bar, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(top_bar, lv_color_hex(pal->border), 0);
    }
    if (title_label != nullptr) {
        lv_obj_set_style_text_color(title_label, lv_color_hex(pal->text), 0);
    }
    if (close_btn != nullptr) {
        lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(close_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (close_label != nullptr) {
        lv_obj_set_style_text_color(close_label, lv_color_hex(pal->text), 0);
    }

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

    /* Barra superior do app (padrao com os outros apps do Tab5 OS) */
    top_bar = lv_obj_create(camera_scr);
    lv_obj_set_size(top_bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_pad_hor(top_bar, 12, 0);
    lv_obj_set_style_pad_ver(top_bar, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Titulo à esquerda */
    title_label = lv_label_create(top_bar);
    lv_label_set_text(title_label, "Câmera");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_flex_grow(title_label, 1);

    /* Botao Fechar [X] à direita (padrao do sistema) */
    close_btn = lv_obj_create(top_bar);
    lv_obj_set_size(close_btn, 36, 36);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_margin_right(close_btn, 6, 0);
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close_btn, close_click_cb, LV_EVENT_CLICKED, nullptr);

    close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(close_label);

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

    /* Canvas para renderizacao de frames RGB565 */
    size_t c_size = PREVIEW_W * PREVIEW_H * 2;
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
    lv_obj_set_style_text_font(gallery_label, &lv_font_montserrat_14_latin1, 0);
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
    lv_obj_set_style_text_font(toast_label, &lv_font_montserrat_14_latin1, 0);
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

    apply_camera_theme();
    return camera_scr;
}

void ui_camera_refresh_theme(void)
{
    apply_camera_theme();
}

void ui_camera_apply_layout(void)
{
    if (camera_scr != nullptr) {
        lv_obj_invalidate(camera_scr);
    }
}

void ui_camera_on_open(void)
{
    ESP_LOGI(TAG, "abrindo app Câmera");
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
