#include "ui_gallery_view.h"
#include "camera_mgr.h"
#include "ui_bar.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "tab5_package_mgr.h"
#include "tjpgd.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>
#include <new>
#include <string>
#include <algorithm>

#include "bsp/m5stack_tab5.h"

static const char *TAG = "tab5_ui_gallery_view";

#define GALLERY_VIEW_W 640
#define GALLERY_VIEW_H 480

struct PhotoItem {
    std::string name;
    std::string fullpath;
    time_t mtime;
};

struct ui_gallery_view_s {
    lv_obj_t *parent = nullptr;
    ui_app_bar_t app_bar = {};

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

    std::vector<PhotoItem> photos;
    int current_index = -1;
    std::string current_dir = "/sdcard/imagens";
};

namespace {

void load_and_display_current_photo(ui_gallery_view_t *view);
void scan_directory(ui_gallery_view_t *view, const std::string &dir_path);

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

bool decode_bmp(FILE *fp, uint8_t *dst_rgb565, int max_w, int max_h, int *out_w, int *out_h)
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

    /* Reducao por potencias de 2 (como o caminho JPEG) ate caber no canvas */
    int step = 1;
    while ((width + step - 1) / step > max_w || (height + step - 1) / step > max_h) {
        step <<= 1;
    }
    int render_w = (width + step - 1) / step;
    int render_h = (height + step - 1) / step;

    fseek(fp, offset, SEEK_SET);
    int row_stride = ((width * (bpp / 8) + 3) / 4) * 4;
    std::vector<uint8_t> row_buf(row_stride);

    uint16_t *dst = (uint16_t *)dst_rgb565;
    memset(dst, 0, (size_t)max_w * max_h * 2);

    for (int y = 0; y < height; y++) {
        if (fread(row_buf.data(), 1, row_stride, fp) < (size_t)row_stride)
            break;
        if (y % step != 0) {
            continue;
        }
        int dst_y = flip ? (height - 1 - y) / step : y / step;
        for (int x = 0; x < width; x += step) {
            int px_idx = x * (bpp / 8);
            uint8_t b = row_buf[px_idx];
            uint8_t g = row_buf[px_idx + 1];
            uint8_t r = row_buf[px_idx + 2];
            uint16_t color = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            dst[dst_y * render_w + (x / step)] = color;
        }
    }

    if (out_w)
        *out_w = render_w;
    if (out_h)
        *out_h = render_h;
    return true;
}

bool decode_jpeg_to_canvas(const char *path, uint8_t *dst_rgb565, int *out_w, int *out_h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "falha ao abrir arquivo %s", path);
        return false;
    }

    uint8_t magic[2];
    if (fread(magic, 1, 2, fp) < 2 || magic[0] != 0xFF || magic[1] != 0xD8) {
        /* Se nao for JPEG valido, tenta decodificar como BMP */
        fseek(fp, 0, SEEK_SET);
        int max_w = (out_w && *out_w > 0) ? *out_w : 640;
        int max_h = (out_h && *out_h > 0) ? *out_h : 640;
        bool res = decode_bmp(fp, dst_rgb565, max_w, max_h, out_w, out_h);
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

    JpegDecodeContext ctx = {fp, (uint16_t *)dst_rgb565, 640, 640};

    JDEC jdec;
    JRESULT res = jd_prepare(&jdec, tjpgd_infunc, pool, pool_size, &ctx);
    if (res == JDR_OK) {
        uint8_t scale = 0;
        int dec_w = (int)jdec.width;
        int dec_h = (int)jdec.height;
        if (dec_w > 640 || dec_h > 640) {
            scale = 1; /* 1/2 escala */
            dec_w >>= 1;
            dec_h >>= 1;
        }
        if (dec_w > 640 || dec_h > 640) {
            scale = 2; /* 1/4 escala */
            dec_w >>= 1;
            dec_h >>= 1;
        }

        ctx.max_w = dec_w;
        ctx.max_h = dec_h;
        memset(dst_rgb565, 0, (size_t)dec_w * dec_h * 2);

        res = jd_decomp(&jdec, tjpgd_outfunc, scale);
        if (res == JDR_OK) {
            if (out_w)
                *out_w = dec_w;
            if (out_h)
                *out_h = dec_h;
            ESP_LOGI(TAG, "foto decodificada com sucesso (%dx%d, orig=%ux%u): %s", dec_w, dec_h, (unsigned)jdec.width,
                     (unsigned)jdec.height, path);
        } else {
            ESP_LOGW(TAG, "erro na descompactacao JPEG (jd_decomp res=%d): %s", (int)res, path);
        }
    } else {
        ESP_LOGW(TAG, "erro ao descompactar JPEG (jd_prepare res=%d): %s", (int)res, path);
    }

    free(pool);
    fclose(fp);
    return (res == JDR_OK);
}

/* =========================================================================
 * Manipulacao de Interface e Gestos
 * ========================================================================= */

void camera_click_cb(lv_event_t *e)
{
    (void)e;
    tab5_package_mgr_launch("com.tab5.camera", nullptr);
}

void prev_photo(ui_gallery_view_t *view)
{
    if (view->photos.empty())
        return;
    if (view->current_index > 0) {
        view->current_index--;
        load_and_display_current_photo(view);
    }
}

void next_photo(ui_gallery_view_t *view)
{
    if (view->photos.empty())
        return;
    if (view->current_index + 1 < (int)view->photos.size()) {
        view->current_index++;
        load_and_display_current_photo(view);
    }
}

void prev_click_cb(lv_event_t *e)
{
    ui_gallery_view_t *view = (ui_gallery_view_t *)lv_event_get_user_data(e);
    if (view != nullptr) {
        prev_photo(view);
    }
}

void next_click_cb(lv_event_t *e)
{
    ui_gallery_view_t *view = (ui_gallery_view_t *)lv_event_get_user_data(e);
    if (view != nullptr) {
        next_photo(view);
    }
}

void image_event_cb(lv_event_t *e)
{
    ui_gallery_view_t *view = (ui_gallery_view_t *)lv_event_get_user_data(e);
    if (view == nullptr) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_LEFT) {
            next_photo(view);
        } else if (dir == LV_DIR_RIGHT) {
            prev_photo(view);
        }
    } else if (code == LV_EVENT_CLICKED) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        int32_t screen_w = lv_display_get_horizontal_resolution(lv_display_get_default());
        if (point.x < (screen_w / 2)) {
            prev_photo(view);
        } else {
            next_photo(view);
        }
    }
}

void modal_btn_cb(lv_event_t *e)
{
    ui_gallery_view_t *view = (ui_gallery_view_t *)lv_event_get_user_data(e);
    if (view == nullptr) {
        return;
    }

    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    bool delete_photo = false;
    while (btn != nullptr) {
        lv_obj_t *child = lv_obj_get_child(btn, 0);
        if (child != nullptr) {
            const char *txt = lv_label_get_text(child);
            if (txt != nullptr && strcmp(txt, "Excluir") == 0) {
                delete_photo = true;
                break;
            }
        }
        btn = lv_obj_get_parent(btn);
    }

    if (view->confirm_modal != nullptr) {
        lv_obj_delete(view->confirm_modal);
        view->confirm_modal = nullptr;
    }

    if (delete_photo) {
        if (view->current_index >= 0 && view->current_index < (int)view->photos.size()) {
            const std::string &path = view->photos[view->current_index].fullpath;
            ESP_LOGI(TAG, "removendo foto física do SD: %s", path.c_str());
            unlink(path.c_str());
            view->photos.erase(view->photos.begin() + view->current_index);

            if (view->current_index >= (int)view->photos.size()) {
                view->current_index = (int)view->photos.size() - 1;
            }
            load_and_display_current_photo(view);
        }
    }
}

void trash_click_cb(lv_event_t *e)
{
    ui_gallery_view_t *view = (ui_gallery_view_t *)lv_event_get_user_data(e);
    if (view == nullptr) {
        return;
    }
    if (view->photos.empty() || view->current_index < 0) {
        return;
    }

    if (view->confirm_modal != nullptr) {
        lv_obj_delete(view->confirm_modal);
        view->confirm_modal = nullptr;
    }

    view->confirm_modal = lv_obj_create(view->parent);
    lv_obj_set_size(view->confirm_modal, 360, 200);
    lv_obj_center(view->confirm_modal);
    lv_obj_set_style_bg_color(view->confirm_modal, lv_color_hex(ui_theme_get()->surface), 0);
    lv_obj_set_style_border_color(view->confirm_modal, lv_color_hex(ui_theme_get()->border), 0);
    lv_obj_set_style_border_width(view->confirm_modal, 1, 0);
    lv_obj_set_style_radius(view->confirm_modal, 16, 0);
    lv_obj_set_style_shadow_width(view->confirm_modal, 12, 0);
    lv_obj_clear_flag(view->confirm_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *m_title = lv_label_create(view->confirm_modal);
    lv_label_set_text(m_title, "Excluir Foto?");
    lv_obj_set_style_text_font(m_title, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(m_title, lv_color_hex(ui_theme_get()->text), 0);
    lv_obj_align(m_title, LV_ALIGN_TOP_LEFT, 16, 16);

    lv_obj_t *m_text = lv_label_create(view->confirm_modal);
    lv_label_set_text(m_text, "Deseja excluir permanentemente este\narquivo do cartão microSD?");
    lv_obj_set_style_text_color(m_text, lv_color_hex(ui_theme_get()->text_muted), 0);
    lv_obj_align(m_text, LV_ALIGN_TOP_LEFT, 16, 50);

    /* Botao Cancelar */
    lv_obj_t *btn_cancel = lv_button_create(view->confirm_modal);
    lv_obj_set_size(btn_cancel, 130, 44);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(ui_theme_get()->surface_alt), 0);
    lv_obj_add_event_cb(btn_cancel, modal_btn_cb, LV_EVENT_CLICKED, view);
    lv_obj_t *lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, "Cancelar");
    lv_obj_set_style_text_color(lbl_c, lv_color_hex(ui_theme_get()->text), 0);
    lv_obj_center(lbl_c);

    /* Botao Excluir */
    lv_obj_t *btn_del = lv_button_create(view->confirm_modal);
    lv_obj_set_size(btn_del, 130, 44);
    lv_obj_align(btn_del, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    lv_obj_set_style_bg_color(btn_del, lv_color_hex(ui_theme_get()->accent), 0);
    lv_obj_add_event_cb(btn_del, modal_btn_cb, LV_EVENT_CLICKED, view);
    lv_obj_t *lbl_d = lv_label_create(btn_del);
    lv_label_set_text(lbl_d, "Excluir");
    lv_obj_set_style_text_color(lbl_d, lv_color_white(), 0);
    lv_obj_center(lbl_d);
}

void load_and_display_current_photo(ui_gallery_view_t *view)
{
    if (view == nullptr) {
        return;
    }
    if (view->photos.empty() || view->current_index < 0 || view->current_index >= (int)view->photos.size()) {
        lv_obj_add_flag(view->image_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view->empty_container, LV_OBJ_FLAG_HIDDEN);
        ui_app_bar_set_title(&view->app_bar, "Galeria (0/0)");
        if (view->prev_btn)
            lv_obj_add_flag(view->prev_btn, LV_OBJ_FLAG_HIDDEN);
        if (view->next_btn)
            lv_obj_add_flag(view->next_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(view->image_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->empty_container, LV_OBJ_FLAG_HIDDEN);

    if (view->prev_btn) {
        if (view->current_index > 0) {
            lv_obj_clear_flag(view->prev_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(view->prev_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (view->next_btn) {
        if (view->current_index + 1 < (int)view->photos.size()) {
            lv_obj_clear_flag(view->next_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(view->next_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const PhotoItem &photo = view->photos[view->current_index];
    char header_str[128];
    snprintf(header_str, sizeof(header_str), "Galeria - %s (%d/%d)", photo.name.c_str(), view->current_index + 1,
             (int)view->photos.size());
    ui_app_bar_set_title(&view->app_bar, header_str);

    if (view->gallery_canvas_buf != nullptr && view->image_canvas != nullptr) {
        int img_w = 640;
        int img_h = 640;
        if (decode_jpeg_to_canvas(photo.fullpath.c_str(), view->gallery_canvas_buf, &img_w, &img_h)) {
            lv_canvas_set_buffer(view->image_canvas, view->gallery_canvas_buf, img_w, img_h, LV_COLOR_FORMAT_RGB565);
            lv_obj_set_size(view->image_canvas, img_w, img_h);
            lv_obj_set_size(view->image_container, img_w, img_h);
            lv_obj_center(view->image_canvas);
            ui_gallery_view_apply_layout(view);
            lv_obj_invalidate(view->image_canvas);
        }
    }
}

void scan_directory(ui_gallery_view_t *view, const std::string &dir_path)
{
    view->photos.clear();
    view->current_dir = dir_path;

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
                        view->photos.push_back({entry->d_name, fullpath, mt});
                    }
                }
            }
        }
        closedir(d);
    }

    /* Fallback: se nao houver fotos em /sdcard/imagens e estavamos olhando la, procura em /sdcard */
    if (view->photos.empty() && dir_path == "/sdcard/imagens") {
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
                            view->photos.push_back({entry->d_name, fullpath, mt});
                        }
                    }
                }
            }
            closedir(d2);
        }
    }

    /* Ordenacao: fotos mais recentes no disco primeiro (mtime decrescente, desempate por nome decrescente) */
    std::sort(view->photos.begin(), view->photos.end(), [](const PhotoItem &a, const PhotoItem &b) {
        if (a.mtime != b.mtime && a.mtime > 1000000 && b.mtime > 1000000) {
            return a.mtime > b.mtime;
        }
        return a.name > b.name;
    });

    ESP_LOGI(TAG, "scan_directory encontrou %d fotos", (int)view->photos.size());
    if (!view->photos.empty()) {
        view->current_index = 0;
    } else {
        view->current_index = -1;
    }
}

void apply_gallery_theme(ui_gallery_view_t *view)
{
    if (view == nullptr || view->parent == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(view->parent, lv_color_hex(pal->background), 0);

    /* Barra Superior */
    ui_app_bar_refresh_theme(&view->app_bar);

    if (view->empty_icon != nullptr) {
        lv_obj_set_style_text_color(view->empty_icon, lv_color_hex(pal->text_muted), 0);
    }
    if (view->empty_label != nullptr) {
        lv_obj_set_style_text_color(view->empty_label, lv_color_hex(pal->text_muted), 0);
    }

    if (view->prev_btn != nullptr) {
        lv_obj_set_style_bg_color(view->prev_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(view->prev_btn, lv_color_hex(pal->border), 0);
    }
    if (view->prev_label != nullptr) {
        lv_obj_set_style_text_color(view->prev_label, lv_color_hex(pal->text), 0);
    }
    if (view->next_btn != nullptr) {
        lv_obj_set_style_bg_color(view->next_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(view->next_btn, lv_color_hex(pal->border), 0);
    }
    if (view->next_label != nullptr) {
        lv_obj_set_style_text_color(view->next_label, lv_color_hex(pal->text), 0);
    }
}

} // namespace

ui_gallery_view_t *ui_gallery_view_create(lv_obj_t *parent, ui_app_bar_t app_bar)
{
    ui_gallery_view_t *view = new (std::nothrow) ui_gallery_view_t();
    if (view == nullptr) {
        return nullptr;
    }

    view->parent = parent;
    view->app_bar = app_bar;

    /* Barra padronizada do app com acoes de Excluir e Atalho para Camera */
    view->trash_btn =
        ui_app_bar_add_action_button(&view->app_bar, LV_SYMBOL_TRASH, trash_click_cb, view, &view->trash_label);
    view->camera_btn =
        ui_app_bar_add_action_button(&view->app_bar, LV_SYMBOL_IMAGE, camera_click_cb, view, &view->camera_label);

    /* Area Central da Imagem */
    view->image_container = lv_obj_create(parent);
    lv_obj_set_size(view->image_container, GALLERY_VIEW_W, GALLERY_VIEW_H);
    lv_obj_align(view->image_container, LV_ALIGN_CENTER, 0, 15);
    lv_obj_set_style_bg_color(view->image_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(view->image_container, 0, 0);
    lv_obj_set_style_pad_all(view->image_container, 0, 0);
    lv_obj_set_style_radius(view->image_container, 12, 0);
    lv_obj_set_style_clip_corner(view->image_container, true, 0);
    lv_obj_clear_flag(view->image_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(view->image_container, image_event_cb, LV_EVENT_GESTURE, view);
    lv_obj_add_event_cb(view->image_container, image_event_cb, LV_EVENT_CLICKED, view);

    size_t c_size = (size_t)640 * 640 * 2;
    view->gallery_canvas_buf = (uint8_t *)heap_caps_malloc(c_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!view->gallery_canvas_buf) {
        view->gallery_canvas_buf = (uint8_t *)malloc(c_size);
    }
    if (view->gallery_canvas_buf) {
        memset(view->gallery_canvas_buf, 0, c_size);
    }

    view->image_canvas = lv_canvas_create(view->image_container);
    if (view->gallery_canvas_buf) {
        lv_canvas_set_buffer(view->image_canvas, view->gallery_canvas_buf, GALLERY_VIEW_W, GALLERY_VIEW_H,
                             LV_COLOR_FORMAT_RGB565);
    }
    lv_obj_center(view->image_canvas);
    lv_obj_clear_flag(view->image_canvas, LV_OBJ_FLAG_CLICKABLE);

    /* Botao Navegacao Anterior [<] */
    view->prev_btn = lv_obj_create(parent);
    lv_obj_set_size(view->prev_btn, 48, 64);
    lv_obj_align(view->prev_btn, LV_ALIGN_LEFT_MID, 12, 15);
    lv_obj_set_style_radius(view->prev_btn, 8, 0);
    lv_obj_set_style_border_width(view->prev_btn, 1, 0);
    lv_obj_set_style_bg_opa(view->prev_btn, LV_OPA_80, 0);
    lv_obj_clear_flag(view->prev_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(view->prev_btn, prev_click_cb, LV_EVENT_CLICKED, view);
    view->prev_label = lv_label_create(view->prev_btn);
    lv_label_set_text(view->prev_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(view->prev_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(view->prev_label);

    /* Botao Navegacao Proxima [>] */
    view->next_btn = lv_obj_create(parent);
    lv_obj_set_size(view->next_btn, 48, 64);
    lv_obj_align(view->next_btn, LV_ALIGN_RIGHT_MID, -12, 15);
    lv_obj_set_style_radius(view->next_btn, 8, 0);
    lv_obj_set_style_border_width(view->next_btn, 1, 0);
    lv_obj_set_style_bg_opa(view->next_btn, LV_OPA_80, 0);
    lv_obj_clear_flag(view->next_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(view->next_btn, next_click_cb, LV_EVENT_CLICKED, view);
    view->next_label = lv_label_create(view->next_btn);
    lv_label_set_text(view->next_label, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(view->next_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(view->next_label);

    /* Container de Estado Vazio */
    view->empty_container = lv_obj_create(parent);
    lv_obj_set_size(view->empty_container, lv_pct(80), 240);
    lv_obj_center(view->empty_container);
    lv_obj_set_style_bg_opa(view->empty_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->empty_container, 0, 0);
    lv_obj_clear_flag(view->empty_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(view->empty_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view->empty_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    view->empty_icon = lv_label_create(view->empty_container);
    lv_label_set_text(view->empty_icon, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(view->empty_icon, &lv_font_montserrat_28_latin1, 0);

    view->empty_label = lv_label_create(view->empty_container);
    lv_label_set_text(view->empty_label, "Nenhuma imagem encontrada\nno cartão microSD (/sdcard/imagens)");
    lv_obj_set_style_text_font(view->empty_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_align(view->empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(view->empty_label, 12, 0);

    /* Reage a mudanca de rotacao */
    lv_obj_add_event_cb(
        parent,
        [](lv_event_t *e) {
            ui_gallery_view_t *v = (ui_gallery_view_t *)lv_event_get_user_data(e);
            if (v != nullptr) {
                ui_gallery_view_apply_layout(v);
            }
        },
        LV_EVENT_SIZE_CHANGED, view);

    apply_gallery_theme(view);
    ui_gallery_view_apply_layout(view);
    return view;
}

void ui_gallery_view_refresh_theme(ui_gallery_view_t *view)
{
    apply_gallery_theme(view);
}

void ui_gallery_view_apply_layout(ui_gallery_view_t *view)
{
    if (view == nullptr || view->parent == nullptr || view->image_container == nullptr) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    lv_disp_rotation_t rot = disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0;
    bool is_portrait = (rot == LV_DISPLAY_ROTATION_0 || rot == LV_DISPLAY_ROTATION_180);

    if (is_portrait) {
        lv_obj_align(view->image_container, LV_ALIGN_CENTER, 0, 15);
        if (view->prev_btn)
            lv_obj_align(view->prev_btn, LV_ALIGN_LEFT_MID, 10, 15);
        if (view->next_btn)
            lv_obj_align(view->next_btn, LV_ALIGN_RIGHT_MID, -10, 15);
    } else {
        lv_obj_align(view->image_container, LV_ALIGN_CENTER, 0, 15);
        if (view->prev_btn)
            lv_obj_align(view->prev_btn, LV_ALIGN_LEFT_MID, 16, 15);
        if (view->next_btn)
            lv_obj_align(view->next_btn, LV_ALIGN_RIGHT_MID, -16, 15);
    }
    lv_obj_invalidate(view->parent);
}

void ui_gallery_view_open_file(ui_gallery_view_t *view, const char *path)
{
    if (view == nullptr) {
        return;
    }
    if (camera_mgr_is_saving()) {
        ESP_LOGI(TAG, "aguardando foto em gravacao finalizar antes de abrir...");
        camera_mgr_wait_save_done(3500);
    }

    if (path == nullptr) {
        scan_directory(view, "/sdcard/imagens");
        view->current_index = view->photos.empty() ? -1 : 0;
        load_and_display_current_photo(view);
        return;
    }

    std::string path_str = path;
    size_t last_slash = path_str.find_last_of('/');
    std::string dir =
        (last_slash != std::string::npos && last_slash > 0) ? path_str.substr(0, last_slash) : "/sdcard/imagens";
    std::string fname = (last_slash != std::string::npos) ? path_str.substr(last_slash + 1) : path_str;

    /* Encontra o indice da foto aberta com retry para garantir visibilidade no VFS */
    view->current_index = -1;
    for (int retry = 0; retry < 10; retry++) {
        scan_directory(view, dir);
        for (size_t i = 0; i < view->photos.size(); i++) {
            if (view->photos[i].name == fname || view->photos[i].fullpath == path_str) {
                view->current_index = (int)i;
                break;
            }
        }
        if (view->current_index >= 0) {
            ESP_LOGI(TAG, "foto encontrada no indice %d: %s", view->current_index, fname.c_str());
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (view->current_index < 0 && !view->photos.empty()) {
        ESP_LOGW(TAG, "foto %s nao encontrada; exibindo foto mais recente", fname.c_str());
        view->current_index = 0;
    }

    load_and_display_current_photo(view);
}

void ui_gallery_view_start(ui_gallery_view_t *view)
{
    if (view == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "abrindo app Galeria");
    if (camera_mgr_is_saving()) {
        ESP_LOGI(TAG, "aguardando foto em gravacao finalizar antes de abrir galeria...");
        camera_mgr_wait_save_done(3500);
    }
    scan_directory(view, "/sdcard/imagens");
    view->current_index = view->photos.empty() ? -1 : 0;
    load_and_display_current_photo(view);
}

void ui_gallery_view_destroy(ui_gallery_view_t *view)
{
    if (view == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "fechando app Galeria");
    if (view->confirm_modal != nullptr) {
        lv_obj_delete(view->confirm_modal);
        view->confirm_modal = nullptr;
    }
    if (view->gallery_canvas_buf != nullptr) {
        free(view->gallery_canvas_buf);
        view->gallery_canvas_buf = nullptr;
    }
    delete view;
}
