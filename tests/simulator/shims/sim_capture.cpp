#include "sim_capture.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lvgl.h"

#define BMP_HEADER_SIZE 54

extern "C" bool bsp_display_lock(uint32_t timeout_ms);
extern "C" void bsp_display_unlock(void);

/* Mistura o layer_top (ARGB8888) sobre a base RGB565 — mesma logica do
 * screenshot.cpp do firmware, para que as capturas do simulador tenham
 * exatamente o mesmo formato do device. */
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

bool sim_capture_to_bmp(const char *path)
{
    lv_display_t *disp = lv_display_get_default();
    if (disp == nullptr || path == nullptr) {
        return false;
    }

    /* Garante renderizacao completa antes do snapshot. */
    lv_refr_now(disp);

    lv_draw_buf_t *base = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (base == nullptr) {
        fprintf(stderr, "sim: snapshot da tela falhou\n");
        return false;
    }

    lv_draw_buf_t *top = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888);

    const int w = (int)base->header.w;
    const int h = (int)base->header.h;

    uint16_t *buf = (uint16_t *)malloc((size_t)w * h * 2);
    if (buf == nullptr) {
        lv_draw_buf_destroy(base);
        if (top != nullptr) {
            lv_draw_buf_destroy(top);
        }
        return false;
    }

    memcpy(buf, base->data, (size_t)w * h * 2);
    lv_draw_buf_destroy(base);

    if (top != nullptr) {
        if ((int)top->header.w == w && (int)top->header.h == h) {
            blend_top_layer(buf, top, w, h);
        } else {
            fprintf(stderr, "sim: layer_top %dx%d difere da tela %dx%d; sem blend\n", (int)top->header.w,
                    (int)top->header.h, w, h);
        }
        lv_draw_buf_destroy(top);
    }

    bool ok = false;
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

        ok = fwrite(hdr, 1, BMP_HEADER_SIZE, f) == BMP_HEADER_SIZE;

        uint8_t *row = (uint8_t *)malloc(w * 3);
        for (int y = h - 1; ok && y >= 0; y--) {
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
            }
        }
        free(row);
        fclose(f);
    } else {
        fprintf(stderr, "sim: falha ao abrir %s para gravacao\n", path);
    }

    free(buf);
    printf("sim: captura %s (%s)\n", path, ok ? "ok" : "FALHOU");
    return ok;
}
