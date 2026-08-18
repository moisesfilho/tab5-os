#include "ui_desktop.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_font.h"

namespace {

lv_obj_t *desktop_scr = nullptr;
lv_obj_t *app_tile = nullptr;
lv_obj_t *app_icon = nullptr;
lv_obj_t *app_icon_label = nullptr;
lv_obj_t *app_label = nullptr;
lv_obj_t *wifi_tile = nullptr;
lv_obj_t *wifi_icon = nullptr;
lv_obj_t *wifi_icon_label = nullptr;
lv_obj_t *wifi_label = nullptr;
lv_obj_t *files_tile = nullptr;
lv_obj_t *files_icon = nullptr;
lv_obj_t *files_icon_label = nullptr;
lv_obj_t *files_label = nullptr;
lv_obj_t *bt_tile = nullptr;
lv_obj_t *bt_icon = nullptr;
lv_obj_t *bt_icon_label = nullptr;
lv_obj_t *bt_label = nullptr;
lv_obj_t *term_tile = nullptr;
lv_obj_t *term_icon = nullptr;
lv_obj_t *term_icon_label = nullptr;
lv_obj_t *term_label = nullptr;
lv_obj_t *camera_tile = nullptr;
lv_obj_t *camera_icon = nullptr;
lv_obj_t *cam_body = nullptr;
lv_obj_t *cam_bump = nullptr;
lv_obj_t *cam_flash = nullptr;
lv_obj_t *cam_lens = nullptr;
lv_obj_t *cam_pupil = nullptr;
lv_obj_t *camera_label = nullptr;
lv_obj_t *gallery_tile = nullptr;
lv_obj_t *gallery_icon = nullptr;
lv_obj_t *gallery_icon_label = nullptr;
lv_obj_t *gallery_label = nullptr;
lv_obj_t *fileserver_tile = nullptr;
lv_obj_t *fileserver_icon = nullptr;
lv_obj_t *fileserver_icon_label = nullptr;
lv_obj_t *fileserver_label = nullptr;
lv_obj_t *recorder_tile = nullptr;
lv_obj_t *recorder_icon = nullptr;
lv_obj_t *recorder_icon_label = nullptr;
lv_obj_t *recorder_label = nullptr;
lv_obj_t *grid_cont = nullptr;

static void update_grid_padding(lv_obj_t *cont)
{
    if (cont == nullptr) {
        return;
    }
    int32_t scr_w = lv_display_get_horizontal_resolution(nullptr);
    if (scr_w <= 0) {
        scr_w = 1280;
    }

    const int32_t tile_w = 92;
    const int32_t gap = 20;
    const int32_t min_pad = 16;

    int cols = (scr_w - 2 * min_pad + gap) / (tile_w + gap);
    if (cols < 1) {
        cols = 1;
    }
    if (cols > 8) {
        cols = 8;
    }

    int32_t grid_w = cols * tile_w + (cols - 1) * gap;
    int32_t pad_hor = (scr_w - grid_w) / 2;
    if (pad_hor < min_pad) {
        pad_hor = min_pad;
    }

    lv_obj_set_style_pad_left(cont, pad_hor, 0);
    lv_obj_set_style_pad_right(cont, pad_hor, 0);
}

void app_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_notas();
}

void wifi_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_wifi();
}

void files_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_files();
}

void bt_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_bluetooth();
}

void term_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_terminal();
}

void camera_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_camera();
}

void gallery_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_gallery();
}

void fileserver_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_fileserver();
}

void recorder_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_recorder();
}

/* Reaplica a paleta ativa na area de trabalho. */
void apply_desktop_theme(void)
{
    if (desktop_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(desktop_scr, lv_color_hex(pal->background), 0);

    if (app_icon != nullptr) {
        lv_obj_set_style_bg_color(app_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (app_icon_label != nullptr) {
        lv_obj_set_style_text_color(app_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (app_label != nullptr) {
        lv_obj_set_style_text_color(app_label, lv_color_hex(pal->text), 0);
    }
    if (wifi_icon != nullptr) {
        lv_obj_set_style_bg_color(wifi_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (wifi_icon_label != nullptr) {
        lv_obj_set_style_text_color(wifi_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (wifi_label != nullptr) {
        lv_obj_set_style_text_color(wifi_label, lv_color_hex(pal->text), 0);
    }
    if (files_icon != nullptr) {
        lv_obj_set_style_bg_color(files_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (files_icon_label != nullptr) {
        lv_obj_set_style_text_color(files_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (files_label != nullptr) {
        lv_obj_set_style_text_color(files_label, lv_color_hex(pal->text), 0);
    }
    if (bt_icon != nullptr) {
        lv_obj_set_style_bg_color(bt_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (bt_icon_label != nullptr) {
        lv_obj_set_style_text_color(bt_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (bt_label != nullptr) {
        lv_obj_set_style_text_color(bt_label, lv_color_hex(pal->text), 0);
    }
    if (term_icon != nullptr) {
        lv_obj_set_style_bg_color(term_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (term_icon_label != nullptr) {
        lv_obj_set_style_text_color(term_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (term_label != nullptr) {
        lv_obj_set_style_text_color(term_label, lv_color_hex(pal->text), 0);
    }
    if (camera_icon != nullptr) {
        lv_obj_set_style_bg_color(camera_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (cam_body != nullptr) {
        lv_obj_set_style_bg_color(cam_body, lv_color_hex(pal->accent), 0);
    }
    if (cam_bump != nullptr) {
        lv_obj_set_style_bg_color(cam_bump, lv_color_hex(pal->accent), 0);
    }
    if (cam_flash != nullptr) {
        lv_obj_set_style_bg_color(cam_flash, lv_color_hex(pal->accent), 0);
    }
    if (cam_lens != nullptr) {
        lv_obj_set_style_bg_color(cam_lens, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(cam_lens, lv_color_hex(pal->background), 0);
    }
    if (cam_pupil != nullptr) {
        lv_obj_set_style_bg_color(cam_pupil, lv_color_hex(pal->accent), 0);
    }
    if (camera_label != nullptr) {
        lv_obj_set_style_text_color(camera_label, lv_color_hex(pal->text), 0);
    }
    if (gallery_icon != nullptr) {
        lv_obj_set_style_bg_color(gallery_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (gallery_icon_label != nullptr) {
        lv_obj_set_style_text_color(gallery_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (gallery_label != nullptr) {
        lv_obj_set_style_text_color(gallery_label, lv_color_hex(pal->text), 0);
    }
    if (fileserver_icon != nullptr) {
        lv_obj_set_style_bg_color(fileserver_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (fileserver_icon_label != nullptr) {
        lv_obj_set_style_text_color(fileserver_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (fileserver_label != nullptr) {
        lv_obj_set_style_text_color(fileserver_label, lv_color_hex(pal->text), 0);
    }
    if (recorder_icon != nullptr) {
        lv_obj_set_style_bg_color(recorder_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (recorder_icon_label != nullptr) {
        lv_obj_set_style_text_color(recorder_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (recorder_label != nullptr) {
        lv_obj_set_style_text_color(recorder_label, lv_color_hex(pal->text), 0);
    }
    if (grid_cont != nullptr) {
        update_grid_padding(grid_cont);
    }
}

} // namespace

void ui_desktop_create(lv_obj_t *scr)
{
    desktop_scr = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    /* Container responsivo para os tiles de aplicativos (Grid centralizado, preenchimento esquerda->direita) */
    grid_cont = lv_obj_create(scr);
    lv_obj_set_size(grid_cont, lv_pct(100), lv_pct(100));
    lv_obj_align(grid_cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(grid_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid_cont, 0, 0);
    lv_obj_set_style_shadow_width(grid_cont, 0, 0);
    lv_obj_set_style_pad_top(grid_cont, UI_BAR_HEIGHT + 16, 0);
    lv_obj_set_style_pad_bottom(grid_cont, 16, 0);
    lv_obj_set_style_pad_row(grid_cont, 20, 0);
    lv_obj_set_style_pad_column(grid_cont, 20, 0);
    lv_obj_clear_flag(grid_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(grid_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(grid_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    update_grid_padding(grid_cont);

    lv_obj_add_event_cb(
        grid_cont, [](lv_event_t *e) { update_grid_padding((lv_obj_t *)lv_event_get_target(e)); },
        LV_EVENT_SIZE_CHANGED, nullptr);

    /* Tile do app Notas */
    app_tile = lv_obj_create(grid_cont);
    lv_obj_set_size(app_tile, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(app_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(app_tile, 0, 0);
    lv_obj_set_style_shadow_width(app_tile, 0, 0);
    lv_obj_set_style_pad_all(app_tile, 8, 0);
    lv_obj_clear_flag(app_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(app_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(app_tile, app_tile_cb, LV_EVENT_CLICKED, nullptr);

    app_icon = lv_obj_create(app_tile);
    lv_obj_set_size(app_icon, 76, 76);
    lv_obj_set_style_radius(app_icon, 18, 0);
    lv_obj_set_style_border_width(app_icon, 0, 0);
    lv_obj_set_style_shadow_width(app_icon, 0, 0);
    lv_obj_clear_flag(app_icon, LV_OBJ_FLAG_CLICKABLE);

    app_icon_label = lv_label_create(app_icon);
    lv_label_set_text(app_icon_label, LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(app_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(app_icon_label);

    app_label = lv_label_create(app_tile);
    lv_label_set_text(app_label, "Notas");
    lv_obj_set_style_text_font(app_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(app_label, 6, 0);

    /* Tile do app WiFi */
    wifi_tile = lv_obj_create(grid_cont);
    lv_obj_set_size(wifi_tile, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wifi_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_tile, 0, 0);
    lv_obj_set_style_shadow_width(wifi_tile, 0, 0);
    lv_obj_set_style_pad_all(wifi_tile, 8, 0);
    lv_obj_clear_flag(wifi_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(wifi_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(wifi_tile, wifi_tile_cb, LV_EVENT_CLICKED, nullptr);

    wifi_icon = lv_obj_create(wifi_tile);
    lv_obj_set_size(wifi_icon, 76, 76);
    lv_obj_set_style_radius(wifi_icon, 18, 0);
    lv_obj_set_style_border_width(wifi_icon, 0, 0);
    lv_obj_set_style_shadow_width(wifi_icon, 0, 0);
    lv_obj_clear_flag(wifi_icon, LV_OBJ_FLAG_CLICKABLE);
    wifi_icon_label = lv_label_create(wifi_icon);
    lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(wifi_icon_label);

    wifi_label = lv_label_create(wifi_tile);
    lv_label_set_text(wifi_label, "WiFi");
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(wifi_label, 6, 0);

    /* Tile do app Arquivos */
    files_tile = lv_obj_create(grid_cont);
    lv_obj_set_size(files_tile, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(files_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(files_tile, 0, 0);
    lv_obj_set_style_shadow_width(files_tile, 0, 0);
    lv_obj_set_style_pad_all(files_tile, 8, 0);
    lv_obj_clear_flag(files_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(files_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(files_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(files_tile, files_tile_cb, LV_EVENT_CLICKED, nullptr);

    files_icon = lv_obj_create(files_tile);
    lv_obj_set_size(files_icon, 76, 76);
    lv_obj_set_style_radius(files_icon, 18, 0);
    lv_obj_set_style_border_width(files_icon, 0, 0);
    lv_obj_set_style_shadow_width(files_icon, 0, 0);
    lv_obj_clear_flag(files_icon, LV_OBJ_FLAG_CLICKABLE);
    files_icon_label = lv_label_create(files_icon);
    lv_label_set_text(files_icon_label, LV_SYMBOL_DIRECTORY);
    lv_obj_set_style_text_font(files_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(files_icon_label);

    files_label = lv_label_create(files_tile);
    lv_label_set_text(files_label, "Arquivos");
    lv_obj_set_style_text_font(files_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(files_label, 6, 0);

    /* Tile do app Bluetooth */
    bt_tile = lv_obj_create(grid_cont);
    lv_obj_set_size(bt_tile, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bt_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bt_tile, 0, 0);
    lv_obj_set_style_shadow_width(bt_tile, 0, 0);
    lv_obj_set_style_pad_all(bt_tile, 8, 0);
    lv_obj_clear_flag(bt_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bt_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bt_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(bt_tile, bt_tile_cb, LV_EVENT_CLICKED, nullptr);

    bt_icon = lv_obj_create(bt_tile);
    lv_obj_set_size(bt_icon, 76, 76);
    lv_obj_set_style_radius(bt_icon, 18, 0);
    lv_obj_set_style_border_width(bt_icon, 0, 0);
    lv_obj_set_style_shadow_width(bt_icon, 0, 0);
    lv_obj_clear_flag(bt_icon, LV_OBJ_FLAG_CLICKABLE);
    bt_icon_label = lv_label_create(bt_icon);
    lv_label_set_text(bt_icon_label, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(bt_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(bt_icon_label);

    bt_label = lv_label_create(bt_tile);
    lv_label_set_text(bt_label, "Bluetooth");
    lv_obj_set_style_text_font(bt_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(bt_label, 6, 0);

    /* Tile do app Terminal */
    term_tile = lv_obj_create(grid_cont);
    lv_obj_set_size(term_tile, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(term_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(term_tile, 0, 0);
    lv_obj_set_style_shadow_width(term_tile, 0, 0);
    lv_obj_set_style_pad_all(term_tile, 8, 0);
    lv_obj_clear_flag(term_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(term_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(term_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(term_tile, term_tile_cb, LV_EVENT_CLICKED, nullptr);

    term_icon = lv_obj_create(term_tile);
    lv_obj_set_size(term_icon, 76, 76);
    lv_obj_set_style_radius(term_icon, 18, 0);
    lv_obj_set_style_border_width(term_icon, 0, 0);
    lv_obj_set_style_shadow_width(term_icon, 0, 0);
    lv_obj_clear_flag(term_icon, LV_OBJ_FLAG_CLICKABLE);
    term_icon_label = lv_label_create(term_icon);
    lv_label_set_text(term_icon_label, ">_");
    lv_obj_set_style_text_font(term_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(term_icon_label);

    term_label = lv_label_create(term_tile);
    lv_label_set_text(term_label, "Terminal");
    lv_obj_set_style_text_font(term_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(term_label, 6, 0);

    /* Tile do app Câmera */
    camera_tile = lv_obj_create(grid_cont);
    lv_obj_set_size(camera_tile, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(camera_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(camera_tile, 0, 0);
    lv_obj_set_style_shadow_width(camera_tile, 0, 0);
    lv_obj_set_style_pad_all(camera_tile, 8, 0);
    lv_obj_clear_flag(camera_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(camera_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(camera_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(camera_tile, camera_tile_cb, LV_EVENT_CLICKED, nullptr);

    camera_icon = lv_obj_create(camera_tile);
    lv_obj_set_size(camera_icon, 76, 76);
    lv_obj_set_style_radius(camera_icon, 18, 0);
    lv_obj_set_style_border_width(camera_icon, 0, 0);
    lv_obj_set_style_shadow_width(camera_icon, 0, 0);
    lv_obj_set_style_pad_all(camera_icon, 0, 0);
    lv_obj_clear_flag(camera_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(camera_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(camera_icon, LV_SCROLLBAR_MODE_OFF);

    /* Corpo da camera fotografica 📷 */
    cam_body = lv_obj_create(camera_icon);
    lv_obj_set_size(cam_body, 44, 30);
    lv_obj_align(cam_body, LV_ALIGN_CENTER, 0, 3);
    lv_obj_set_style_radius(cam_body, 6, 0);
    lv_obj_set_style_border_width(cam_body, 0, 0);
    lv_obj_set_style_pad_all(cam_body, 0, 0);
    lv_obj_clear_flag(cam_body, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_body, LV_SCROLLBAR_MODE_OFF);

    cam_bump = lv_obj_create(camera_icon);
    lv_obj_set_size(cam_bump, 14, 6);
    lv_obj_align(cam_bump, LV_ALIGN_CENTER, -6, -13);
    lv_obj_set_style_radius(cam_bump, 2, 0);
    lv_obj_set_style_border_width(cam_bump, 0, 0);
    lv_obj_clear_flag(cam_bump, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_bump, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_bump, LV_SCROLLBAR_MODE_OFF);

    cam_flash = lv_obj_create(camera_icon);
    lv_obj_set_size(cam_flash, 5, 5);
    lv_obj_align(cam_flash, LV_ALIGN_CENTER, 12, -12);
    lv_obj_set_style_radius(cam_flash, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cam_flash, 0, 0);
    lv_obj_clear_flag(cam_flash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_flash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_flash, LV_SCROLLBAR_MODE_OFF);

    cam_lens = lv_obj_create(cam_body);
    lv_obj_set_size(cam_lens, 20, 20);
    lv_obj_align(cam_lens, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(cam_lens, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cam_lens, 2, 0);
    lv_obj_set_style_pad_all(cam_lens, 0, 0);
    lv_obj_clear_flag(cam_lens, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_lens, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_lens, LV_SCROLLBAR_MODE_OFF);

    cam_pupil = lv_obj_create(cam_lens);
    lv_obj_set_size(cam_pupil, 8, 8);
    lv_obj_align(cam_pupil, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(cam_pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cam_pupil, 0, 0);
    lv_obj_clear_flag(cam_pupil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cam_pupil, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(cam_pupil, LV_SCROLLBAR_MODE_OFF);

    camera_label = lv_label_create(camera_tile);
    lv_label_set_text(camera_label, "Câmera");
    lv_obj_set_style_text_font(camera_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(camera_label, 6, 0);

    /* Tile do app Galeria */
    gallery_tile = lv_obj_create(grid_cont);
    lv_obj_set_size(gallery_tile, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(gallery_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gallery_tile, 0, 0);
    lv_obj_set_style_shadow_width(gallery_tile, 0, 0);
    lv_obj_set_style_pad_all(gallery_tile, 8, 0);
    lv_obj_clear_flag(gallery_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(gallery_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gallery_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(gallery_tile, gallery_tile_cb, LV_EVENT_CLICKED, nullptr);

    gallery_icon = lv_obj_create(gallery_tile);
    lv_obj_set_size(gallery_icon, 76, 76);
    lv_obj_set_style_radius(gallery_icon, 18, 0);
    lv_obj_set_style_border_width(gallery_icon, 0, 0);
    lv_obj_set_style_shadow_width(gallery_icon, 0, 0);
    lv_obj_clear_flag(gallery_icon, LV_OBJ_FLAG_CLICKABLE);
    gallery_icon_label = lv_label_create(gallery_icon);
    lv_label_set_text(gallery_icon_label, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(gallery_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(gallery_icon_label);

    gallery_label = lv_label_create(gallery_tile);
    lv_label_set_text(gallery_label, "Galeria");
    lv_obj_set_style_text_font(gallery_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(gallery_label, 6, 0);

    /* Tile do app Servidor */
    fileserver_tile = lv_obj_create(grid_cont);
    lv_obj_set_size(fileserver_tile, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(fileserver_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fileserver_tile, 0, 0);
    lv_obj_set_style_shadow_width(fileserver_tile, 0, 0);
    lv_obj_set_style_pad_all(fileserver_tile, 8, 0);
    lv_obj_clear_flag(fileserver_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(fileserver_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(fileserver_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(fileserver_tile, fileserver_tile_cb, LV_EVENT_CLICKED, nullptr);

    fileserver_icon = lv_obj_create(fileserver_tile);
    lv_obj_set_size(fileserver_icon, 76, 76);
    lv_obj_set_style_radius(fileserver_icon, 18, 0);
    lv_obj_set_style_border_width(fileserver_icon, 0, 0);
    lv_obj_set_style_shadow_width(fileserver_icon, 0, 0);
    lv_obj_clear_flag(fileserver_icon, LV_OBJ_FLAG_CLICKABLE);
    fileserver_icon_label = lv_label_create(fileserver_icon);
    lv_label_set_text(fileserver_icon_label, LV_SYMBOL_DRIVE);
    lv_obj_set_style_text_font(fileserver_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(fileserver_icon_label);

    fileserver_label = lv_label_create(fileserver_tile);
    lv_label_set_text(fileserver_label, "Servidor");
    lv_obj_set_style_text_font(fileserver_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(fileserver_label, 6, 0);

    /* Tile do app Gravador */
    recorder_tile = lv_obj_create(grid_cont);
    lv_obj_set_size(recorder_tile, 92, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(recorder_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(recorder_tile, 0, 0);
    lv_obj_set_style_shadow_width(recorder_tile, 0, 0);
    lv_obj_set_style_pad_all(recorder_tile, 8, 0);
    lv_obj_clear_flag(recorder_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(recorder_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(recorder_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(recorder_tile, recorder_tile_cb, LV_EVENT_CLICKED, nullptr);

    recorder_icon = lv_obj_create(recorder_tile);
    lv_obj_set_size(recorder_icon, 76, 76);
    lv_obj_set_style_radius(recorder_icon, 18, 0);
    lv_obj_set_style_border_width(recorder_icon, 0, 0);
    lv_obj_set_style_shadow_width(recorder_icon, 0, 0);
    lv_obj_clear_flag(recorder_icon, LV_OBJ_FLAG_CLICKABLE);
    recorder_icon_label = lv_label_create(recorder_icon);
    lv_label_set_text(recorder_icon_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(recorder_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(recorder_icon_label);

    recorder_label = lv_label_create(recorder_tile);
    lv_label_set_text(recorder_label, "Gravador");
    lv_obj_set_style_text_font(recorder_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(recorder_label, 6, 0);

    apply_desktop_theme();
}

void ui_desktop_refresh_theme(void)
{
    apply_desktop_theme();
}
