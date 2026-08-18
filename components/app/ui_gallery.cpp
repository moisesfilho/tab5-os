#include "ui_gallery.h"
#include "ui_bar.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "tjpgd.h"
#include "camera_mgr.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>
#include <string>
#include <algorithm>

static const char *TAG = "tab5_ui_gallery";

#define GALLERY_VIEW_W 640
#define GALLERY_VIEW_H 480

namespace {

struct PhotoItem {
    std::string filename;
    std::string fullpath;
    time_t mtime;
};

lv_obj_t *gallery_scr = nullptr;
lv_obj_t *top_bar = nullptr;
lv_obj_t *back_btn = nullptr;
lv_obj_t *back_label = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *trash_btn = nullptr;
lv_obj_t *trash_label = nullptr;
lv_obj_t *camera_btn = nullptr;
lv_obj_t *camera_label = nullptr;

lv_obj_t *image_container = nullptr;
lv_obj_t *image_canvas = nullptr;
uint8_t *gallery_canvas_buf = nullptr;

lv_obj_t *prev_btn = nullptr;
lv_obj_t *prev_label = nullptr;
lv_obj_t *next_btn = nullptr;
lv_obj_t *next_label = nullptr;

lv_obj_t *empty_container = nullptr;
lv_obj_t *empty_icon = nullptr;
lv_obj_t *empty_label = nullptr;

lv_obj_t *confirm_modal = nullptr;

std::vector<PhotoItem> s_photos;
int s_current_index = -1;
std::string s_current_dir = "/sdcard/imagens";

void load_and_display_current_photo(void);
void scan_directory(const std::string &dir_path);

/* =========================================================================
 * Decodificador Real JPEG / BMP para Renderizacao no Canvas
 * ========================================================================= */

struct JpegDecodeContext {
    FILE *fp;
    uint16_t *dst_canvas;
    int max_w;
    int max_h;
};

static size_t tjpgd_infunc(JDEC *jd, uint8_t *buff, size_t ndata)
{
    JpegDecodeContext *ctx = (JpegDecodeContext *)jd->device;
    if (!ctx || !ctx->fp)
        return 0;
    if (buff) {
        return fread(buff, 1, ndata, ctx->fp);
    } else {
        return (fseek(ctx->fp, (long)ndata, SEEK_CUR) == 0) ? ndata : 0;
    }
}

static int tjpgd_outfunc(JDEC *jd, void *bitmap, JRECT *rect)
{
    JpegDecodeContext *ctx = (JpegDecodeContext *)jd->device;
    if (!ctx || !ctx->dst_canvas)
        return 0;

    const uint16_t *src_rgb565 = (const uint16_t *)bitmap;
    int w = rect->right - rect->left + 1;
    int h = rect->bottom - rect->top + 1;

    for (int y = 0; y < h; y++) {
        int py = rect->top + y;
        if (py >= ctx->max_h)
            break;
        for (int x = 0; x < w; x++) {
            int px = rect->left + x;
            if (px >= ctx->max_w)
                break;
            ctx->dst_canvas[py * ctx->max_w + px] = src_rgb565[y * w + x];
        }
    }
    return 1;
}

bool decode_bmp(FILE *fp, uint8_t *dst_rgb565, int max_w, int max_h)
{
    uint8_t header[54];
    if (fread(header, 1, 54, fp) < 54)
        return false;
    if (header[0] != 'B' || header[1] != 'M')
        return false;

    int width = *(int32_t *)&header[18];
    int height = *(int32_t *)&header[22];
    int bpp = *(uint16_t *)&header[28];
    int offset = *(uint32_t *)&header[10];

    bool flip = true;
    if (height < 0) {
        height = -height;
        flip = false;
    }

    if (bpp != 24 && bpp != 32)
        return false;

    fseek(fp, offset, SEEK_SET);
    int row_stride = ((width * (bpp / 8) + 3) / 4) * 4;
    std::vector<uint8_t> row_buf(row_stride);

    uint16_t *dst = (uint16_t *)dst_rgb565;
    memset(dst, 0, max_w * max_h * 2);

    int render_w = std::min(width, max_w);
    int render_h = std::min(height, max_h);

    for (int y = 0; y < height; y++) {
        if (fread(row_buf.data(), 1, row_stride, fp) < (size_t)row_stride)
            break;
        int dst_y = flip ? (height - 1 - y) : y;
        if (dst_y < render_h) {
            for (int x = 0; x < render_w; x++) {
                int px_idx = x * (bpp / 8);
                uint8_t b = row_buf[px_idx];
                uint8_t g = row_buf[px_idx + 1];
                uint8_t r = row_buf[px_idx + 2];
                uint16_t color = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                dst[dst_y * max_w + x] = color;
            }
        }
    }
    return true;
}

bool decode_jpeg_to_canvas(const char *filepath, uint8_t *dst_rgb565, int max_w, int max_h)
{
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "falha ao abrir arquivo %s", filepath);
        return false;
    }

    uint8_t magic[2];
    if (fread(magic, 1, 2, fp) < 2 || magic[0] != 0xFF || magic[1] != 0xD8) {
        /* Se nao for JPEG valido, tenta decodificar como BMP */
        fseek(fp, 0, SEEK_SET);
        bool res = decode_bmp(fp, dst_rgb565, max_w, max_h);
        fclose(fp);
        return res;
    }
    fseek(fp, 0, SEEK_SET);

    size_t pool_size = 24576;
    void *pool = malloc(pool_size);
    if (!pool) {
        fclose(fp);
        return false;
    }

    JpegDecodeContext ctx = {fp, (uint16_t *)dst_rgb565, max_w, max_h};
    memset(dst_rgb565, 0, max_w * max_h * 2);

    JDEC jdec;
    JRESULT res = jd_prepare(&jdec, tjpgd_infunc, pool, pool_size, &ctx);
    if (res == JDR_OK) {
        jd_decomp(&jdec, tjpgd_outfunc, 0);
        ESP_LOGI(TAG, "foto decodificada com sucesso (%ux%u): %s", jdec.width, jdec.height, filepath);
    } else {
        ESP_LOGW(TAG, "erro ao descompactar JPEG (jd_prepare res=%d): %s", (int)res, filepath);
    }

    free(pool);
    fclose(fp);
    return (res == JDR_OK);
}

/* =========================================================================
 * Manipulacao de Interface e Gestos
 * ========================================================================= */

void back_click_cb(lv_event_t *e)
{
    (void)e;
    ui_shell_close_gallery();
}

void camera_click_cb(lv_event_t *e)
{
    (void)e;
    ui_shell_open_camera();
}

void prev_photo(void)
{
    if (s_photos.empty())
        return;
    if (s_current_index > 0) {
        s_current_index--;
        load_and_display_current_photo();
    }
}

void next_photo(void)
{
    if (s_photos.empty())
        return;
    if (s_current_index + 1 < (int)s_photos.size()) {
        s_current_index++;
        load_and_display_current_photo();
    }
}

void prev_click_cb(lv_event_t *e)
{
    (void)e;
    prev_photo();
}

void next_click_cb(lv_event_t *e)
{
    (void)e;
    next_photo();
}

void image_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_LEFT) {
            next_photo();
        } else if (dir == LV_DIR_RIGHT) {
            prev_photo();
        }
    } else if (code == LV_EVENT_CLICKED) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        if (point.x < 360) {
            prev_photo();
        } else {
            next_photo();
        }
    }
}

void modal_btn_cb(lv_event_t *e)
{
    const char *txt = (const char *)lv_event_get_user_data(e);

    if (confirm_modal != nullptr) {
        lv_obj_delete(confirm_modal);
        confirm_modal = nullptr;
    }

    if (txt != nullptr && strcmp(txt, "Excluir") == 0) {
        if (s_current_index >= 0 && s_current_index < (int)s_photos.size()) {
            const std::string &path = s_photos[s_current_index].fullpath;
            ESP_LOGI(TAG, "removendo foto física do SD: %s", path.c_str());
            unlink(path.c_str());
            s_photos.erase(s_photos.begin() + s_current_index);

            if (s_current_index >= (int)s_photos.size()) {
                s_current_index = (int)s_photos.size() - 1;
            }
            load_and_display_current_photo();
        }
    }
}

void trash_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_photos.empty() || s_current_index < 0) {
        return;
    }

    if (confirm_modal != nullptr) {
        lv_obj_delete(confirm_modal);
        confirm_modal = nullptr;
    }

    confirm_modal = lv_obj_create(gallery_scr);
    lv_obj_set_size(confirm_modal, 360, 200);
    lv_obj_center(confirm_modal);
    lv_obj_set_style_bg_color(confirm_modal, lv_color_hex(ui_theme_get()->surface), 0);
    lv_obj_set_style_border_color(confirm_modal, lv_color_hex(ui_theme_get()->border), 0);
    lv_obj_set_style_border_width(confirm_modal, 1, 0);
    lv_obj_set_style_radius(confirm_modal, 16, 0);
    lv_obj_set_style_shadow_width(confirm_modal, 12, 0);
    lv_obj_clear_flag(confirm_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *m_title = lv_label_create(confirm_modal);
    lv_label_set_text(m_title, "Excluir Foto?");
    lv_obj_set_style_text_font(m_title, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(m_title, lv_color_hex(ui_theme_get()->text), 0);
    lv_obj_align(m_title, LV_ALIGN_TOP_LEFT, 16, 16);

    lv_obj_t *m_text = lv_label_create(confirm_modal);
    lv_label_set_text(m_text, "Deseja excluir permanentemente este\narquivo do cartão microSD?");
    lv_obj_set_style_text_color(m_text, lv_color_hex(ui_theme_get()->text_muted), 0);
    lv_obj_align(m_text, LV_ALIGN_TOP_LEFT, 16, 50);

    /* Botao Cancelar */
    lv_obj_t *btn_cancel = lv_button_create(confirm_modal);
    lv_obj_set_size(btn_cancel, 130, 44);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(ui_theme_get()->surface_alt), 0);
    lv_obj_add_event_cb(btn_cancel, modal_btn_cb, LV_EVENT_CLICKED, (void *)"Cancelar");
    lv_obj_t *lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, "Cancelar");
    lv_obj_set_style_text_color(lbl_c, lv_color_hex(ui_theme_get()->text), 0);
    lv_obj_center(lbl_c);

    /* Botao Excluir */
    lv_obj_t *btn_del = lv_button_create(confirm_modal);
    lv_obj_set_size(btn_del, 130, 44);
    lv_obj_align(btn_del, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    lv_obj_set_style_bg_color(btn_del, lv_color_hex(ui_theme_get()->accent), 0);
    lv_obj_add_event_cb(btn_del, modal_btn_cb, LV_EVENT_CLICKED, (void *)"Excluir");
    lv_obj_t *lbl_d = lv_label_create(btn_del);
    lv_label_set_text(lbl_d, "Excluir");
    lv_obj_set_style_text_color(lbl_d, lv_color_white(), 0);
    lv_obj_center(lbl_d);
}

void load_and_display_current_photo(void)
{
    if (s_photos.empty() || s_current_index < 0 || s_current_index >= (int)s_photos.size()) {
        lv_obj_add_flag(image_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(empty_container, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(title_label, "Galeria (0/0)");
        if (prev_btn)
            lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
        if (next_btn)
            lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(image_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(empty_container, LV_OBJ_FLAG_HIDDEN);

    if (prev_btn) {
        if (s_current_index > 0) {
            lv_obj_clear_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (next_btn) {
        if (s_current_index + 1 < (int)s_photos.size()) {
            lv_obj_clear_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const PhotoItem &photo = s_photos[s_current_index];
    char header_str[128];
    snprintf(header_str, sizeof(header_str), "%s (%d/%d)", photo.filename.c_str(), s_current_index + 1,
             (int)s_photos.size());
    lv_label_set_text(title_label, header_str);

    if (gallery_canvas_buf != nullptr && image_canvas != nullptr) {
        decode_jpeg_to_canvas(photo.fullpath.c_str(), gallery_canvas_buf, GALLERY_VIEW_W, GALLERY_VIEW_H);
        lv_obj_invalidate(image_canvas);
    }
}

void scan_directory(const std::string &dir_path)
{
    s_photos.clear();
    s_current_dir = dir_path;

    DIR *d = opendir(dir_path.c_str());
    if (!d) {
        ESP_LOGW(TAG, "nao foi possivel abrir diretorio: %s", dir_path.c_str());
    } else {
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
                const char *dot = strrchr(entry->d_name, '.');
                if (dot) {
                    std::string ext = dot + 1;
                    for (char &c : ext)
                        c = (char)tolower((unsigned char)c);
                    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp") {
                        std::string fullpath = dir_path;
                        if (fullpath.back() != '/')
                            fullpath += '/';
                        fullpath += entry->d_name;
                        struct stat st;
                        time_t mt = 0;
                        if (stat(fullpath.c_str(), &st) == 0) {
                            mt = st.st_mtime;
                        }
                        s_photos.push_back({entry->d_name, fullpath, mt});
                    }
                }
            }
        }
        closedir(d);
    }

    /* Fallback: se nao houver fotos em /sdcard/imagens e estavamos olhando la, procura em /sdcard */
    if (s_photos.empty() && dir_path == "/sdcard/imagens") {
        DIR *d2 = opendir("/sdcard");
        if (d2) {
            struct dirent *entry;
            while ((entry = readdir(d2)) != nullptr) {
                if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
                    const char *dot = strrchr(entry->d_name, '.');
                    if (dot) {
                        std::string ext = dot + 1;
                        for (char &c : ext)
                            c = (char)tolower((unsigned char)c);
                        if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp") {
                            std::string fullpath = std::string("/sdcard/") + entry->d_name;
                            struct stat st;
                            time_t mt = 0;
                            if (stat(fullpath.c_str(), &st) == 0) {
                                mt = st.st_mtime;
                            }
                            s_photos.push_back({entry->d_name, fullpath, mt});
                        }
                    }
                }
            }
            closedir(d2);
        }
    }

    /* Ordenacao: fotos mais recentes no disco primeiro (mtime decrescente) */
    std::sort(s_photos.begin(), s_photos.end(), [](const PhotoItem &a, const PhotoItem &b) {
        if (a.mtime != b.mtime) {
            return a.mtime > b.mtime;
        }
        return a.filename > b.filename;
    });

    ESP_LOGI(TAG, "scan_directory encontrou %d fotos", (int)s_photos.size());
    if (!s_photos.empty()) {
        s_current_index = 0;
    } else {
        s_current_index = -1;
    }
}

void apply_gallery_theme(void)
{
    if (gallery_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(gallery_scr, lv_color_hex(pal->background), 0);

    /* Barra Superior */
    if (top_bar != nullptr) {
        lv_obj_set_style_bg_color(top_bar, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(top_bar, lv_color_hex(pal->border), 0);
    }
    if (title_label != nullptr) {
        lv_obj_set_style_text_color(title_label, lv_color_hex(pal->text), 0);
    }
    if (trash_btn != nullptr) {
        lv_obj_set_style_bg_color(trash_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(trash_btn, lv_color_hex(pal->border), 0);
    }
    if (trash_label != nullptr) {
        lv_obj_set_style_text_color(trash_label, lv_color_hex(pal->accent), 0);
    }
    if (camera_btn != nullptr) {
        lv_obj_set_style_bg_color(camera_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(camera_btn, lv_color_hex(pal->border), 0);
    }
    if (camera_label != nullptr) {
        lv_obj_set_style_text_color(camera_label, lv_color_hex(pal->accent), 0);
    }
    if (back_btn != nullptr) {
        lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(back_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(back_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (back_label != nullptr) {
        lv_obj_set_style_text_color(back_label, lv_color_hex(pal->text), 0);
    }

    if (empty_icon != nullptr) {
        lv_obj_set_style_text_color(empty_icon, lv_color_hex(pal->text_muted), 0);
    }
    if (empty_label != nullptr) {
        lv_obj_set_style_text_color(empty_label, lv_color_hex(pal->text_muted), 0);
    }

    if (prev_btn != nullptr) {
        lv_obj_set_style_bg_color(prev_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(prev_btn, lv_color_hex(pal->border), 0);
    }
    if (prev_label != nullptr) {
        lv_obj_set_style_text_color(prev_label, lv_color_hex(pal->text), 0);
    }
    if (next_btn != nullptr) {
        lv_obj_set_style_bg_color(next_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(next_btn, lv_color_hex(pal->border), 0);
    }
    if (next_label != nullptr) {
        lv_obj_set_style_text_color(next_label, lv_color_hex(pal->text), 0);
    }
}

} // namespace

lv_obj_t *ui_gallery_create(void)
{
    gallery_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(gallery_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Barra Superior (posicionada abaixo da barra de status do SO) */
    top_bar = lv_obj_create(gallery_scr);
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

    /* Titulo com nome do arquivo e contador à esquerda */
    title_label = lv_label_create(top_bar);
    lv_label_set_text(title_label, "Galeria");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_flex_grow(title_label, 1);

    /* Botao Excluir */
    trash_btn = lv_obj_create(top_bar);
    lv_obj_set_size(trash_btn, 36, 36);
    lv_obj_set_style_radius(trash_btn, 8, 0);
    lv_obj_set_style_border_width(trash_btn, 1, 0);
    lv_obj_set_style_margin_right(trash_btn, 6, 0);
    lv_obj_clear_flag(trash_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(trash_btn, trash_click_cb, LV_EVENT_CLICKED, nullptr);

    trash_label = lv_label_create(trash_btn);
    lv_label_set_text(trash_label, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(trash_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(trash_label);

    /* Botao Atalho Câmera */
    camera_btn = lv_obj_create(top_bar);
    lv_obj_set_size(camera_btn, 36, 36);
    lv_obj_set_style_radius(camera_btn, 8, 0);
    lv_obj_set_style_border_width(camera_btn, 1, 0);
    lv_obj_set_style_margin_right(camera_btn, 6, 0);
    lv_obj_clear_flag(camera_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(camera_btn, camera_click_cb, LV_EVENT_CLICKED, nullptr);

    camera_label = lv_label_create(camera_btn);
    lv_label_set_text(camera_label, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(camera_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(camera_label);

    /* Botao Fechar [X] à direita */
    back_btn = lv_obj_create(top_bar);
    lv_obj_set_size(back_btn, 36, 36);
    lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_margin_right(back_btn, 6, 0);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, back_click_cb, LV_EVENT_CLICKED, nullptr);

    back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(back_label);

    /* Area Central da Imagem */
    image_container = lv_obj_create(gallery_scr);
    lv_obj_set_size(image_container, GALLERY_VIEW_W, GALLERY_VIEW_H);
    lv_obj_align(image_container, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 16);
    lv_obj_set_style_bg_color(image_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(image_container, 0, 0);
    lv_obj_set_style_pad_all(image_container, 0, 0);
    lv_obj_set_style_radius(image_container, 12, 0);
    lv_obj_set_style_clip_corner(image_container, true, 0);
    lv_obj_clear_flag(image_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(image_container, image_event_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(image_container, image_event_cb, LV_EVENT_CLICKED, nullptr);

    size_t c_size = GALLERY_VIEW_W * GALLERY_VIEW_H * 2;
    gallery_canvas_buf = (uint8_t *)heap_caps_malloc(c_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gallery_canvas_buf) {
        gallery_canvas_buf = (uint8_t *)malloc(c_size);
    }
    if (gallery_canvas_buf) {
        memset(gallery_canvas_buf, 0, c_size);
    }

    image_canvas = lv_canvas_create(image_container);
    if (gallery_canvas_buf) {
        lv_canvas_set_buffer(image_canvas, gallery_canvas_buf, GALLERY_VIEW_W, GALLERY_VIEW_H, LV_COLOR_FORMAT_RGB565);
    }
    lv_obj_center(image_canvas);
    lv_obj_clear_flag(image_canvas, LV_OBJ_FLAG_CLICKABLE);

    /* Botao Navegacao Anterior [<] */
    prev_btn = lv_obj_create(gallery_scr);
    lv_obj_set_size(prev_btn, 48, 64);
    lv_obj_align(prev_btn, LV_ALIGN_LEFT_MID, 8, UI_BAR_HEIGHT);
    lv_obj_set_style_radius(prev_btn, 8, 0);
    lv_obj_set_style_border_width(prev_btn, 1, 0);
    lv_obj_set_style_bg_opa(prev_btn, LV_OPA_80, 0);
    lv_obj_clear_flag(prev_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(prev_btn, prev_click_cb, LV_EVENT_CLICKED, nullptr);
    prev_label = lv_label_create(prev_btn);
    lv_label_set_text(prev_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(prev_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(prev_label);

    /* Botao Navegacao Proxima [>] */
    next_btn = lv_obj_create(gallery_scr);
    lv_obj_set_size(next_btn, 48, 64);
    lv_obj_align(next_btn, LV_ALIGN_RIGHT_MID, -8, UI_BAR_HEIGHT);
    lv_obj_set_style_radius(next_btn, 8, 0);
    lv_obj_set_style_border_width(next_btn, 1, 0);
    lv_obj_set_style_bg_opa(next_btn, LV_OPA_80, 0);
    lv_obj_clear_flag(next_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(next_btn, next_click_cb, LV_EVENT_CLICKED, nullptr);
    next_label = lv_label_create(next_btn);
    lv_label_set_text(next_label, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(next_label);

    /* Container de Estado Vazio */
    empty_container = lv_obj_create(gallery_scr);
    lv_obj_set_size(empty_container, lv_pct(80), 240);
    lv_obj_center(empty_container);
    lv_obj_set_style_bg_opa(empty_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(empty_container, 0, 0);
    lv_obj_clear_flag(empty_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(empty_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(empty_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    empty_icon = lv_label_create(empty_container);
    lv_label_set_text(empty_icon, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(empty_icon, &lv_font_montserrat_28_latin1, 0);

    empty_label = lv_label_create(empty_container);
    lv_label_set_text(empty_label, "Nenhuma imagem encontrada\nno cartão microSD (/sdcard/imagens)");
    lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(empty_label, 12, 0);

    apply_gallery_theme();
    return gallery_scr;
}

void ui_gallery_refresh_theme(void)
{
    apply_gallery_theme();
}

void ui_gallery_apply_layout(void)
{
    if (gallery_scr != nullptr) {
        lv_obj_invalidate(gallery_scr);
    }
}

void ui_gallery_open_file(const char *filepath)
{
    if (filepath == nullptr) {
        scan_directory("/sdcard/imagens");
        load_and_display_current_photo();
        return;
    }

    std::string path_str = filepath;
    size_t last_slash = path_str.find_last_of('/');
    std::string dir =
        (last_slash != std::string::npos && last_slash > 0) ? path_str.substr(0, last_slash) : "/sdcard/imagens";
    std::string fname = (last_slash != std::string::npos) ? path_str.substr(last_slash + 1) : path_str;

    if (camera_mgr_is_saving()) {
        ESP_LOGI(TAG, "aguardando foto em gravacao finalizar antes de abrir...");
        camera_mgr_wait_save_done(2500);
    }

    scan_directory(dir);

    /* Encontra o indice da foto aberta (com retry rapido se necessario) */
    s_current_index = -1;
    for (int retry = 0; retry < 5; retry++) {
        for (size_t i = 0; i < s_photos.size(); i++) {
            if (s_photos[i].filename == fname || s_photos[i].fullpath == path_str) {
                s_current_index = (int)i;
                break;
            }
        }
        if (s_current_index >= 0)
            break;
        vTaskDelay(pdMS_TO_TICKS(100));
        scan_directory(dir);
    }

    if (s_current_index < 0 && !s_photos.empty()) {
        s_current_index = 0;
    }

    load_and_display_current_photo();
}

void ui_gallery_on_open(void)
{
    ESP_LOGI(TAG, "abrindo app Galeria");
    if (camera_mgr_is_saving()) {
        ESP_LOGI(TAG, "aguardando foto em gravacao finalizar antes de abrir galeria...");
        camera_mgr_wait_save_done(1500);
    }
    scan_directory("/sdcard/imagens");
    load_and_display_current_photo();
}

void ui_gallery_on_close(void)
{
    ESP_LOGI(TAG, "fechando app Galeria");
    if (confirm_modal != nullptr) {
        lv_obj_delete(confirm_modal);
        confirm_modal = nullptr;
    }
}
