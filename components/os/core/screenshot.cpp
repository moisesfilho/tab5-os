#include "screenshot.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "timezone_mgr.h"
#include "ui_theme.h"
#include "ui_font.h"

#define BMP_HEADER_SIZE 54

static const char *TAG = "screenshot";

static bool s_busy = false;
static uint16_t *s_shot_buf = nullptr;

static lv_obj_t *s_flash = nullptr;
static lv_obj_t *s_toast_label = nullptr;
static lv_timer_t *s_toast_timer = nullptr;

static void flash_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, v, 0);
}

static void flash_delete_cb(lv_anim_t *a)
{
    lv_obj_t *flash = (lv_obj_t *)a->var;
    if (s_flash == flash) {
        s_flash = nullptr;
    }
    lv_obj_delete(flash);
}

static void show_flash(void)
{
    if (s_flash != nullptr) {
        lv_obj_delete(s_flash);
        s_flash = nullptr;
    }

    s_flash = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_flash, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(s_flash, 0, 0);
    lv_obj_set_style_bg_color(s_flash, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_flash, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_flash, 0, 0);
    lv_obj_set_style_radius(s_flash, 0, 0);
    lv_obj_set_style_shadow_width(s_flash, 0, 0);
    lv_obj_set_style_pad_all(s_flash, 0, 0);
    lv_obj_clear_flag(s_flash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_flash, LV_OBJ_FLAG_SCROLLABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_flash);
    lv_anim_set_exec_cb(&a, flash_opa_cb);
    lv_anim_set_values(&a, LV_OPA_60, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_delay(&a, 60);
    lv_anim_set_ready_cb(&a, flash_delete_cb);
    lv_anim_start(&a);
}

static void toast_hide_cb(lv_timer_t *timer)
{
    if (s_toast_label != nullptr) {
        lv_obj_delete(s_toast_label);
        s_toast_label = nullptr;
    }
    lv_timer_del(timer);
    s_toast_timer = nullptr;
}

static void notify_ui(void *param)
{
    char *msg = (char *)param;
    if (msg == nullptr) {
        return;
    }

    if (s_toast_timer != nullptr) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = nullptr;
    }

    if (s_toast_label == nullptr) {
        const ui_palette_t *pal = ui_theme_get();
        s_toast_label = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_color(s_toast_label, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_bg_opa(s_toast_label, LV_OPA_90, 0);
        lv_obj_set_style_border_color(s_toast_label, lv_color_hex(pal->border), 0);
        lv_obj_set_style_border_width(s_toast_label, 1, 0);
        lv_obj_set_style_radius(s_toast_label, 8, 0);
        lv_obj_set_style_pad_all(s_toast_label, 8, 0);
        lv_obj_set_style_text_color(s_toast_label, lv_color_hex(pal->text), 0);
        lv_obj_set_style_text_font(s_toast_label, &lv_font_montserrat_18_latin1, 0);
        lv_obj_clear_flag(s_toast_label, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(s_toast_label, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_label_set_text(s_toast_label, msg);
    lv_obj_align(s_toast_label, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    lv_obj_move_foreground(s_toast_label);

    s_toast_timer = lv_timer_create(toast_hide_cb, 2500, nullptr);

    lv_free(msg);
}

struct shot_job_t {
    uint16_t *buf;
    int w;
    int h;
};

static void writer_task(void *arg)
{
    shot_job_t *job = (shot_job_t *)arg;
    uint16_t *buf = job->buf;
    const int w = job->w;
    const int h = job->h;
    bool ok = false;
    char path[96];
    char *msg = nullptr;

    struct tm tmbuf;
    if (timezone_mgr_get_localtime(&tmbuf) != nullptr) {
        strftime(path, sizeof(path), "/sdcard/screenshots/print_%Y%m%d_%H%M%S.bmp", &tmbuf);
    } else {
        snprintf(path, sizeof(path), "/sdcard/screenshots/print_sem_data.bmp");
    }

    mkdir("/sdcard/screenshots", 0755);

    FILE *f = fopen(path, "wb");
    if (f != nullptr) {
        const uint32_t data_size = (uint32_t)w * h * 3;
        uint8_t hdr[BMP_HEADER_SIZE] = {0};
        hdr[0] = 'B';
        hdr[1] = 'M';
        auto put32 = [&hdr](int off, uint32_t v) {
            hdr[off] = (uint8_t)(v & 0xFF);
            hdr[off + 1] = (uint8_t)((v >> 8) & 0xFF);
            hdr[off + 2] = (uint8_t)((v >> 16) & 0xFF);
            hdr[off + 3] = (uint8_t)((v >> 24) & 0xFF);
        };
        put32(2, BMP_HEADER_SIZE + data_size);
        put32(10, BMP_HEADER_SIZE);
        put32(14, 40);
        put32(18, w);
        put32(22, h);
        hdr[26] = 1;
        hdr[28] = 24;
        put32(34, data_size);
        put32(38, 2835);
        put32(42, 2835);

        uint8_t *row = (uint8_t *)heap_caps_malloc(w * 3, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        ok = row != nullptr && fwrite(hdr, 1, BMP_HEADER_SIZE, f) == BMP_HEADER_SIZE;

        if (ok) {
            for (int y = h - 1; y >= 0; y--) {
                const uint16_t *src = buf + (uint32_t)y * w;
                for (int x = 0; x < w; x++) {
                    uint16_t v = src[x];
                    uint8_t r5 = (v >> 11) & 0x1F;
                    uint8_t g6 = (v >> 5) & 0x3F;
                    uint8_t b5 = v & 0x1F;
                    row[x * 3 + 0] = (b5 << 3) | (b5 >> 2);
                    row[x * 3 + 1] = (g6 << 2) | (g6 >> 4);
                    row[x * 3 + 2] = (r5 << 3) | (r5 >> 2);
                }
                if (fwrite(row, 3, w, f) != (size_t)w) {
                    ok = false;
                    break;
                }
            }
        }

        heap_caps_free(row);
        fclose(f);
    } else {
        ESP_LOGE(TAG, "Falha ao abrir %s", path);
    }

    heap_caps_free(buf);
    heap_caps_free(job);
    s_shot_buf = nullptr;
    s_busy = false;

    if (ok) {
        ESP_LOGI(TAG, "Print salvo em %s", path);
        const char *base = strrchr(path, '/');
        msg = (char *)lv_malloc(128);
        if (msg != nullptr) {
            snprintf(msg, 128, "Print salvo: %s", base != nullptr ? base + 1 : path);
        }
    } else {
        ESP_LOGE(TAG, "Falha ao gravar %s", path);
        msg = (char *)lv_malloc(32);
        if (msg != nullptr) {
            snprintf(msg, 32, "Falha ao salvar print");
        }
    }

    if (msg != nullptr) {
        bsp_display_lock(0);
        lv_async_call(notify_ui, msg);
        bsp_display_unlock();
    }

    vTaskDelete(nullptr);
}

/* Mistura o layer_top (ARGB8888 com alpha) sobre a base RGB565 capturada.
 * O layer_top contem barra do sistema, teclado, modais e overlays. */
static void blend_top_layer(uint16_t *dst, const lv_draw_buf_t *top, int w, int h)
{
    const uint32_t stride_px = top->header.stride / 4;
    const uint32_t *src = (const uint32_t *)top->data;

    for (int y = 0; y < h; y++) {
        const uint32_t *srow = src + (uint32_t)y * stride_px;
        uint16_t *drow = dst + (uint32_t)y * w;
        for (int x = 0; x < w; x++) {
            const uint32_t p = srow[x];
            const uint32_t a = p >> 24;
            if (a == 0) {
                continue;
            }

            const uint8_t tr = (uint8_t)(p >> 16);
            const uint8_t tg = (uint8_t)(p >> 8);
            const uint8_t tb = (uint8_t)p;

            if (a >= 255) {
                drow[x] = (uint16_t)(((tr >> 3) << 11) | ((tg >> 2) << 5) | (tb >> 3));
                continue;
            }

            const uint16_t d = drow[x];
            const uint8_t dr = (uint8_t)(((d >> 11) & 0x1F) << 3);
            const uint8_t dg = (uint8_t)(((d >> 5) & 0x3F) << 2);
            const uint8_t db = (uint8_t)((d & 0x1F) << 3);

            const uint8_t r = (uint8_t)((tr * a + dr * (255 - a)) >> 8);
            const uint8_t g = (uint8_t)((tg * a + dg * (255 - a)) >> 8);
            const uint8_t b = (uint8_t)((tb * a + db * (255 - a)) >> 8);
            drow[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

void screenshot_take(void)
{
    if (s_busy) {
        return;
    }

    /* Captura na orientacao logica: tela ativa + layer_top (barra, teclado,
     * modais). O flash so e acionado depois, para nao aparecer no print. */
    lv_draw_buf_t *base = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (base == nullptr) {
        ESP_LOGE(TAG, "Snapshot da tela falhou");
        return;
    }

    lv_draw_buf_t *top = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888);
    if (top == nullptr) {
        ESP_LOGW(TAG, "Snapshot do layer_top falhou; salvando sem barra/overlays");
    }

    const int w = (int)base->header.w;
    const int h = (int)base->header.h;

    s_shot_buf = (uint16_t *)heap_caps_aligned_alloc(64, (size_t)w * h * 2, MALLOC_CAP_SPIRAM);
    if (s_shot_buf == nullptr) {
        ESP_LOGE(TAG, "Sem memoria PSRAM para o print");
        lv_draw_buf_destroy(base);
        if (top != nullptr) {
            lv_draw_buf_destroy(top);
        }
        return;
    }

    memcpy(s_shot_buf, base->data, (size_t)w * h * 2);
    lv_draw_buf_destroy(base);

    if (top != nullptr) {
        if ((int)top->header.w == w && (int)top->header.h == h) {
            blend_top_layer(s_shot_buf, top, w, h);
        } else {
            ESP_LOGW(TAG, "layer_top %dx%d difere da tela %dx%d; sem blend", (int)top->header.w, (int)top->header.h, w,
                     h);
        }
        lv_draw_buf_destroy(top);
    }

    shot_job_t *job = (shot_job_t *)heap_caps_malloc(sizeof(shot_job_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (job == nullptr) {
        ESP_LOGE(TAG, "Sem memoria para a gravacao");
        heap_caps_free(s_shot_buf);
        s_shot_buf = nullptr;
        return;
    }
    job->buf = s_shot_buf;
    job->w = w;
    job->h = h;

    s_busy = true;
    show_flash();

    if (xTaskCreate(writer_task, "shot_wr", 6144, job, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar task de gravacao");
        heap_caps_free(job);
        heap_caps_free(s_shot_buf);
        s_shot_buf = nullptr;
        s_busy = false;
    }
}
