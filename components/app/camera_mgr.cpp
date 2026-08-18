#include "camera_mgr.h"
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_video_ioctl.h"
#include "linux/videodev2.h"
#include "sys/mman.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cinttypes>
#include <cerrno>
#include <cmath>

static const char *TAG = "tab5_camera_mgr";

extern "C" {
esp_cam_sensor_device_t *sc202cs_detect(void *config);
esp_cam_sensor_device_t *sc2336_detect(void *config);
}

/* Referencia explicita para evitar descarte do linker */
static void *s_sensor_detect_refs[] = {(void *)sc202cs_detect, (void *)sc2336_detect};

#define CAM_PREVIEW_WIDTH 640
#define CAM_PREVIEW_HEIGHT 480
#define CAM_BUFFER_COUNT 2

namespace {

camera_state_t s_state = CAMERA_STATE_UNINITIALIZED;
int s_video_fd = -1;
TaskHandle_t s_stream_task_handle = nullptr;
TaskHandle_t s_save_task_handle = nullptr;
QueueHandle_t s_save_queue = nullptr;

bool s_streaming = false;
camera_frame_cb_t s_frame_cb = nullptr;
void *s_user_data = nullptr;

SemaphoreHandle_t s_lock = nullptr;
uint8_t *s_latest_frame = nullptr;
size_t s_latest_frame_size = 0;
bool s_hw_camera_available = false;
void *s_mapped_buffers[CAM_BUFFER_COUNT] = {nullptr, nullptr};
size_t s_mapped_lengths[CAM_BUFFER_COUNT] = {0, 0};

uint32_t s_cam_pixelformat = V4L2_PIX_FMT_RGB565;
uint32_t s_cam_width = CAM_PREVIEW_WIDTH;
uint32_t s_cam_height = CAM_PREVIEW_HEIGHT;

struct SaveRequest {
    uint8_t *frame_copy;
    int width;
    int height;
    char filepath[128];
    camera_capture_done_cb_t done_cb;
    void *user_data;
};

/* =========================================================================
 * Codificador JPEG Baseline AAN com Aritmetica de Ponto Fixo (Ultra Rapido)
 * ========================================================================= */

static const uint8_t s_std_lum_qt[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                         14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                         18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                         49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};

static const uint8_t s_std_chrom_qt[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                           24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                           99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                           99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

static const uint8_t s_zigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                     12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                     35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                     58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

struct BitWriter {
    FILE *fp;
    uint32_t bit_buf;
    int bit_cnt;

    inline void put_bit(int b)
    {
        bit_buf = (bit_buf << 1) | (b & 1);
        bit_cnt++;
        if (bit_cnt == 8) {
            uint8_t byte = (uint8_t)bit_buf;
            fputc(byte, fp);
            if (byte == 0xFF) {
                fputc(0x00, fp);
            }
            bit_buf = 0;
            bit_cnt = 0;
        }
    }

    inline void write_bits(uint32_t bits, int num_bits)
    {
        for (int i = num_bits - 1; i >= 0; i--) {
            put_bit((bits >> i) & 1);
        }
    }

    inline void flush()
    {
        if (bit_cnt > 0) {
            bit_buf <<= (8 - bit_cnt);
            uint8_t byte = (uint8_t)bit_buf;
            fputc(byte, fp);
            if (byte == 0xFF) {
                fputc(0x00, fp);
            }
            bit_buf = 0;
            bit_cnt = 0;
        }
    }
};

static const uint8_t s_dc_lum_bits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t s_dc_lum_val[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t s_dc_chrom_bits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t s_dc_chrom_val[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const uint8_t s_ac_lum_bits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const uint8_t s_ac_lum_val[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

static const uint8_t s_ac_chrom_bits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const uint8_t s_ac_chrom_val[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

struct HuffCode {
    uint16_t code;
    uint8_t len;
};

static HuffCode s_huff_dc_lum[256];
static HuffCode s_huff_dc_chrom[256];
static HuffCode s_huff_ac_lum[256];
static HuffCode s_huff_ac_chrom[256];
static bool s_huff_tables_built = false;

static void build_huff_table(const uint8_t *bits, const uint8_t *val, int num_val, HuffCode *out_table)
{
    memset(out_table, 0, 256 * sizeof(HuffCode));
    uint16_t code = 0;
    int k = 0;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < bits[i]; j++) {
            if (k < num_val) {
                out_table[val[k]].code = code;
                out_table[val[k]].len = i + 1;
                k++;
                code++;
            }
        }
        code <<= 1;
    }
}

static void init_huff_tables_if_needed()
{
    if (!s_huff_tables_built) {
        build_huff_table(s_dc_lum_bits, s_dc_lum_val, 12, s_huff_dc_lum);
        build_huff_table(s_dc_chrom_bits, s_dc_chrom_val, 12, s_huff_dc_chrom);
        build_huff_table(s_ac_lum_bits, s_ac_lum_val, 162, s_huff_ac_lum);
        build_huff_table(s_ac_chrom_bits, s_ac_chrom_val, 162, s_huff_ac_chrom);
        s_huff_tables_built = true;
    }
}

static const float s_C[8][8] = {
    {0.3535534f, 0.3535534f, 0.3535534f, 0.3535534f, 0.3535534f, 0.3535534f, 0.3535534f, 0.3535534f},
    {0.4903926f, 0.4157348f, 0.2777851f, 0.0975452f, -0.0975452f, -0.2777851f, -0.4157348f, -0.4903926f},
    {0.4619398f, 0.1913417f, -0.1913417f, -0.4619398f, -0.4619398f, -0.1913417f, 0.1913417f, 0.4619398f},
    {0.4157348f, -0.0975452f, -0.4903926f, -0.2777851f, 0.2777851f, 0.4903926f, 0.0975452f, -0.4157348f},
    {0.3535534f, -0.3535534f, -0.3535534f, 0.3535534f, 0.3535534f, -0.3535534f, -0.3535534f, 0.3535534f},
    {0.2777851f, -0.4903926f, 0.0975452f, 0.4157348f, -0.4157348f, -0.0975452f, 0.4903926f, -0.2777851f},
    {0.1913417f, -0.4619398f, 0.4619398f, -0.1913417f, -0.1913417f, 0.4619398f, -0.4619398f, 0.1913417f},
    {0.0975452f, -0.2777851f, 0.4157348f, -0.4903926f, 0.4903926f, -0.4157348f, 0.2777851f, -0.0975452f}};

static void exact_fdct8x8(const int16_t *in, float *out)
{
    float tmp[64];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 8; k++) {
                sum += s_C[i][k] * (float)in[k * 8 + j];
            }
            tmp[i * 8 + j] = sum;
        }
    }
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 8; k++) {
                sum += tmp[i * 8 + k] * s_C[j][k];
            }
            out[i * 8 + j] = sum;
        }
    }
}

static void encode_block(BitWriter &bw, const int16_t *block, int16_t &last_dc, const HuffCode *dc_table,
                         const HuffCode *ac_table)
{
    int16_t dc_val = block[0];
    int16_t diff = dc_val - last_dc;
    last_dc = dc_val;

    int16_t abs_diff = diff < 0 ? -diff : diff;
    int cat = 0;
    while (abs_diff > 0) {
        cat++;
        abs_diff >>= 1;
    }

    bw.write_bits(dc_table[cat].code, dc_table[cat].len);
    if (cat > 0) {
        uint16_t val_bits = diff < 0 ? (diff - 1 + (1 << cat)) : diff;
        bw.write_bits(val_bits, cat);
    }

    int r = 0;
    for (int k = 1; k < 64; k++) {
        int16_t ac = block[s_zigzag[k]];
        if (ac == 0) {
            r++;
        } else {
            while (r > 15) {
                bw.write_bits(ac_table[0xF0].code, ac_table[0xF0].len);
                r -= 16;
            }
            int16_t abs_ac = ac < 0 ? -ac : ac;
            int ac_cat = 0;
            while (abs_ac > 0) {
                ac_cat++;
                abs_ac >>= 1;
            }
            uint8_t symbol = (uint8_t)((r << 4) | ac_cat);
            bw.write_bits(ac_table[symbol].code, ac_table[symbol].len);
            uint16_t val_bits = ac < 0 ? (ac - 1 + (1 << ac_cat)) : ac;
            bw.write_bits(val_bits, ac_cat);
            r = 0;
        }
    }
    if (r > 0) {
        bw.write_bits(ac_table[0x00].code, ac_table[0x00].len);
    }
}

static bool save_rgb565_as_jpeg(const char *filepath, const uint8_t *rgb565_data, int width, int height)
{
    init_huff_tables_if_needed();

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "falha ao abrir arquivo para gravacao: %s", filepath);
        return false;
    }

    /* SOI */
    fputc(0xFF, fp);
    fputc(0xD8, fp);

    /* APP0 */
    uint8_t app0[] = {0xFF, 0xE0, 0x00, 0x10, 'J',  'F',  'I',  'F',  0x00,
                      0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00};
    fwrite(app0, 1, sizeof(app0), fp);

    /* DQT Lum (0) */
    fputc(0xFF, fp);
    fputc(0xDB, fp);
    fputc(0x00, fp);
    fputc(0x43, fp);
    fputc(0x00, fp);
    for (int i = 0; i < 64; i++) {
        fputc(s_std_lum_qt[s_zigzag[i]], fp);
    }

    /* DQT Chrom (1) */
    fputc(0xFF, fp);
    fputc(0xDB, fp);
    fputc(0x00, fp);
    fputc(0x43, fp);
    fputc(0x01, fp);
    for (int i = 0; i < 64; i++) {
        fputc(s_std_chrom_qt[s_zigzag[i]], fp);
    }

    /* SOF0 */
    uint8_t sof0[] = {0xFF,
                      0xC0,
                      0x00,
                      0x11,
                      0x08,
                      (uint8_t)(height >> 8),
                      (uint8_t)(height & 0xFF),
                      (uint8_t)(width >> 8),
                      (uint8_t)(width & 0xFF),
                      0x03,
                      0x01,
                      0x11,
                      0x00,
                      0x02,
                      0x11,
                      0x01,
                      0x03,
                      0x11,
                      0x01};
    fwrite(sof0, 1, sizeof(sof0), fp);

    /* DHT DC Lum */
    fputc(0xFF, fp);
    fputc(0xC4, fp);
    fputc(0x00, fp);
    fputc(0x1F, fp);
    fputc(0x00, fp);
    fwrite(s_dc_lum_bits, 1, 16, fp);
    fwrite(s_dc_lum_val, 1, 12, fp);

    /* DHT AC Lum */
    fputc(0xFF, fp);
    fputc(0xC4, fp);
    fputc(0x00, fp);
    fputc(0xB5, fp);
    fputc(0x10, fp);
    fwrite(s_ac_lum_bits, 1, 16, fp);
    fwrite(s_ac_lum_val, 1, 162, fp);

    /* DHT DC Chrom */
    fputc(0xFF, fp);
    fputc(0xC4, fp);
    fputc(0x00, fp);
    fputc(0x1F, fp);
    fputc(0x01, fp);
    fwrite(s_dc_chrom_bits, 1, 16, fp);
    fwrite(s_dc_chrom_val, 1, 12, fp);

    /* DHT AC Chrom */
    fputc(0xFF, fp);
    fputc(0xC4, fp);
    fputc(0x00, fp);
    fputc(0xB5, fp);
    fputc(0x11, fp);
    fwrite(s_ac_chrom_bits, 1, 16, fp);
    fwrite(s_ac_chrom_val, 1, 162, fp);

    /* SOS */
    uint8_t sos[] = {0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00};
    fwrite(sos, 1, sizeof(sos), fp);

    BitWriter bw = {fp, 0, 0};
    int16_t last_dc_y = 0, last_dc_cb = 0, last_dc_cr = 0;

    int16_t block_y_in[64], block_cb_in[64], block_cr_in[64];
    float block_y_dct[64], block_cb_dct[64], block_cr_dct[64];
    int16_t block_y_q[64], block_cb_q[64], block_cr_q[64];

    const uint16_t *pixels = (const uint16_t *)rgb565_data;

    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            for (int by = 0; by < 8; by++) {
                int py = y + by;
                if (py >= height)
                    py = height - 1;
                for (int bx = 0; bx < 8; bx++) {
                    int px = x + bx;
                    if (px >= width)
                        px = width - 1;

                    uint16_t rgb = pixels[py * width + px];
                    int r = ((rgb >> 11) & 0x1F) << 3;
                    int g = ((rgb >> 5) & 0x3F) << 2;
                    int b = (rgb & 0x1F) << 3;

                    int y_val = ((77 * r + 150 * g + 29 * b) >> 8) - 128;
                    int cb_val = ((-43 * r - 85 * g + 128 * b) >> 8);
                    int cr_val = ((128 * r - 107 * g - 21 * b) >> 8);

                    block_y_in[by * 8 + bx] = (int16_t)y_val;
                    block_cb_in[by * 8 + bx] = (int16_t)cb_val;
                    block_cr_in[by * 8 + bx] = (int16_t)cr_val;
                }
            }

            exact_fdct8x8(block_y_in, block_y_dct);
            exact_fdct8x8(block_cb_in, block_cb_dct);
            exact_fdct8x8(block_cr_in, block_cr_dct);

            for (int k = 0; k < 64; k++) {
                block_y_q[k] = (int16_t)roundf(block_y_dct[k] / (float)s_std_lum_qt[k]);
                block_cb_q[k] = (int16_t)roundf(block_cb_dct[k] / (float)s_std_chrom_qt[k]);
                block_cr_q[k] = (int16_t)roundf(block_cr_dct[k] / (float)s_std_chrom_qt[k]);
            }

            encode_block(bw, block_y_q, last_dc_y, s_huff_dc_lum, s_huff_ac_lum);
            encode_block(bw, block_cb_q, last_dc_cb, s_huff_dc_chrom, s_huff_ac_chrom);
            encode_block(bw, block_cr_q, last_dc_cr, s_huff_dc_chrom, s_huff_ac_chrom);
        }
    }

    bw.flush();

    /* EOI */
    fputc(0xFF, fp);
    fputc(0xD9, fp);
    fclose(fp);
    return true;
}

/* =========================================================================
 * Task Assincrona de Gravacao de Fotos em Background
 * ========================================================================= */

static volatile bool s_is_saving = false;

void save_task_func(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "task de gravacao de fotos iniciada");

    SaveRequest req;
    while (true) {
        if (xQueueReceive(s_save_queue, &req, portMAX_DELAY) == pdTRUE) {
            s_is_saving = true;
            ESP_LOGI(TAG, "iniciando gravacao assincrona em %s...", req.filepath);
            bool ok = false;
            if (req.frame_copy != nullptr) {
                char tmp_path[160];
                snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", req.filepath);
                ok = save_rgb565_as_jpeg(tmp_path, req.frame_copy, req.width, req.height);
                if (ok) {
                    rename(tmp_path, req.filepath);
                    ESP_LOGI(TAG, "foto salva e renomeada com sucesso: %s", req.filepath);
                } else {
                    unlink(tmp_path);
                    ESP_LOGE(TAG, "falha ao salvar JPEG temporario: %s", tmp_path);
                }
                free(req.frame_copy);
            }

            s_is_saving = false;
            esp_err_t res = ok ? ESP_OK : ESP_FAIL;
            if (req.done_cb != nullptr) {
                req.done_cb(res, req.filepath, req.user_data);
            }
        }
    }
}

/* =========================================================================
 * Decodificacao e Conversao Bayer / RAW8 / RGB565 Otimizada
 * ========================================================================= */

static int s_awb_gain_r = 380;
static int s_awb_gain_b = 400;

static void convert_raw8_bayer_to_rgb565(const uint8_t *src, int src_w, int src_h, uint16_t *dst, int dst_w, int dst_h)
{
    /* Mapeamento direto e natural do sensor SC202CS (Bayer BGGR) sem filtros artificiais */
    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (dy * (src_h - 2)) / (dst_h - 1);
        sy = sy & ~1; // alinha em linhas pares

        const uint8_t *row0 = src + sy * src_w;
        const uint8_t *row1 = src + (sy + 1) * src_w;

        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (dx * (src_w - 2)) / (dst_w - 1);
            sx = sx & ~1; // alinha em colunas pares

            /* Sensor SC202CS (Bayer BGGR):
             * row0: B,  G1
             * row1: G2, R
             */
            int b_val = row0[sx];
            int g_val = (row0[sx + 1] + row1[sx]) >> 1;
            int r_val = row1[sx + 1];

            int r5 = (r_val * 31) / 255;
            int g6 = (g_val * 63) / 255;
            int b5 = (b_val * 31) / 255;

            dst[dy * dst_w + dx] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }
}

/* =========================================================================
 * Task de Streaming e Atualizacao de Preview
 * ========================================================================= */

void stream_task_func(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "task de streaming da camera iniciada (fd=%d, fmt=0x%08" PRIx32 ", dim=%" PRIu32 "x%" PRIu32 ")",
             s_video_fd, s_cam_pixelformat, s_cam_width, s_cam_height);

    bool stream_on_active = false;
    uint32_t frames_received = 0;

    while (true) {
        if (!s_streaming) {
            if (stream_on_active && s_hw_camera_available && s_video_fd >= 0) {
                int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                ioctl(s_video_fd, VIDIOC_STREAMOFF, &type);
                stream_on_active = false;
                ESP_LOGI(TAG, "streaming de camera pausado (STREAMOFF)");
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!stream_on_active && s_hw_camera_available && s_video_fd >= 0) {
            for (int i = 0; i < CAM_BUFFER_COUNT; i++) {
                struct v4l2_buffer buf;
                memset(&buf, 0, sizeof(buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;
                ioctl(s_video_fd, VIDIOC_QBUF, &buf);
            }

            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(s_video_fd, VIDIOC_STREAMON, &type) == 0) {
                stream_on_active = true;
                ESP_LOGI(TAG, "streaming de camera iniciado/retomado (STREAMON)");
            } else {
                ESP_LOGE(TAG, "falha ao iniciar VIDIOC_STREAMON (errno=%d)", errno);
            }
        }

        bool got_hw_frame = false;
        if (s_hw_camera_available && s_video_fd >= 0 && stream_on_active) {
            struct v4l2_buffer vbuf;
            memset(&vbuf, 0, sizeof(vbuf));
            vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            vbuf.memory = V4L2_MEMORY_MMAP;

            int dq_res = ioctl(s_video_fd, VIDIOC_DQBUF, &vbuf);
            if (dq_res == 0) {
                frames_received++;
                if (frames_received % 30 == 1) {
                    ESP_LOGI(TAG, "camera frame #%lu bytesused=%lu idx=%lu", (unsigned long)frames_received,
                             (unsigned long)vbuf.bytesused, (unsigned long)vbuf.index);
                }

                if (vbuf.index < CAM_BUFFER_COUNT && s_mapped_buffers[vbuf.index] != nullptr) {
                    void *ptr = s_mapped_buffers[vbuf.index];

                    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
                        if (s_cam_pixelformat == V4L2_PIX_FMT_RGB565 && s_cam_width == CAM_PREVIEW_WIDTH &&
                            s_cam_height == CAM_PREVIEW_HEIGHT) {
                            memcpy(s_latest_frame, ptr, CAM_PREVIEW_WIDTH * CAM_PREVIEW_HEIGHT * 2);
                        } else if (s_cam_pixelformat == V4L2_PIX_FMT_RGB565) {
                            const uint16_t *src = (const uint16_t *)ptr;
                            uint16_t *dst = (uint16_t *)s_latest_frame;
                            for (int y = 0; y < CAM_PREVIEW_HEIGHT; y++) {
                                int sy = (y * (int)s_cam_height) / CAM_PREVIEW_HEIGHT;
                                for (int x = 0; x < CAM_PREVIEW_WIDTH; x++) {
                                    int sx = (x * (int)s_cam_width) / CAM_PREVIEW_WIDTH;
                                    dst[y * CAM_PREVIEW_WIDTH + x] = src[sy * s_cam_width + sx];
                                }
                            }
                        } else {
                            /* Decodificacao RAW8 / Bayer com mapeamento direto */
                            convert_raw8_bayer_to_rgb565((const uint8_t *)ptr, (int)s_cam_width, (int)s_cam_height,
                                                         (uint16_t *)s_latest_frame, CAM_PREVIEW_WIDTH,
                                                         CAM_PREVIEW_HEIGHT);
                        }
                        xSemaphoreGive(s_lock);
                    }

                    if (s_streaming && s_frame_cb != nullptr) {
                        s_frame_cb(s_latest_frame, CAM_PREVIEW_WIDTH, CAM_PREVIEW_HEIGHT, s_user_data);
                    }
                    got_hw_frame = true;
                }
                ioctl(s_video_fd, VIDIOC_QBUF, &vbuf);
            } else {
                static int s_err_count = 0;
                if (++s_err_count % 60 == 1) {
                    ESP_LOGW(TAG, "VIDIOC_DQBUF retorno=%d errno=%d", dq_res, errno);
                }
            }
        }

        if (!got_hw_frame && s_streaming) {
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}

static esp_err_t open_and_setup_v4l2_device(void)
{
    if (s_video_fd >= 0) {
        return ESP_OK;
    }

    s_video_fd = open(BSP_CAMERA_DEVICE, O_RDWR);
    if (s_video_fd < 0) {
        ESP_LOGE(TAG, "falha ao abrir %s (errno=%d)", BSP_CAMERA_DEVICE, errno);
        return ESP_FAIL;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(s_video_fd, VIDIOC_QUERYCAP, &cap) != 0) {
        ESP_LOGE(TAG, "falha no VIDIOC_QUERYCAP em %s", BSP_CAMERA_DEVICE);
        close(s_video_fd);
        s_video_fd = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "dispositivo %s aberto com sucesso (driver=%s card=%s caps=0x%08" PRIx32 ")", BSP_CAMERA_DEVICE,
             cap.driver, cap.card, cap.capabilities);
    s_hw_camera_available = true;

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAM_PREVIEW_WIDTH;
    fmt.fmt.pix.height = CAM_PREVIEW_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(s_video_fd, VIDIOC_S_FMT, &fmt) != 0) {
        ESP_LOGW(TAG, "VIDIOC_S_FMT RGB565 nao aceito diretamente; consultando formato padrao");
        ioctl(s_video_fd, VIDIOC_G_FMT, &fmt);
    }

    s_cam_pixelformat = fmt.fmt.pix.pixelformat;
    s_cam_width = fmt.fmt.pix.width;
    s_cam_height = fmt.fmt.pix.height;
    ESP_LOGI(TAG, "formato de video configurado: %" PRIu32 "x%" PRIu32 ", fourcc=0x%08" PRIx32, s_cam_width,
             s_cam_height, s_cam_pixelformat);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = CAM_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_video_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "falha no VIDIOC_REQBUFS");
    }

    for (int i = 0; i < CAM_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(s_video_fd, VIDIOC_QUERYBUF, &buf) == 0) {
            s_mapped_buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_video_fd, buf.m.offset);
            s_mapped_lengths[i] = buf.length;
            ioctl(s_video_fd, VIDIOC_QBUF, &buf);
            ESP_LOGI(TAG, "buffer #%d mapeado: len=%lu", i, (unsigned long)buf.length);
        }
    }

    if (s_stream_task_handle == nullptr) {
        xTaskCreatePinnedToCore(stream_task_func, "cam_stream", 4096, nullptr, 5, &s_stream_task_handle, 1);
    }

    return ESP_OK;
}

} // namespace

esp_err_t camera_mgr_init(void)
{
    if (s_state != CAMERA_STATE_UNINITIALIZED) {
        return ESP_OK;
    }

    if (s_lock == nullptr) {
        s_lock = xSemaphoreCreateMutex();
    }

    if (s_save_queue == nullptr) {
        s_save_queue = xQueueCreate(4, sizeof(SaveRequest));
        xTaskCreatePinnedToCore(save_task_func, "cam_save", 4096, nullptr, 2, &s_save_task_handle, 1);
    }

    size_t buf_size = CAM_PREVIEW_WIDTH * CAM_PREVIEW_HEIGHT * 2;
    if (s_latest_frame == nullptr) {
        s_latest_frame = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_latest_frame) {
            s_latest_frame = (uint8_t *)malloc(buf_size);
        }
        if (s_latest_frame) {
            memset(s_latest_frame, 0, buf_size);
            s_latest_frame_size = buf_size;
        }
    }

    (void)s_sensor_detect_refs;

    /* Inicializa hardware do sensor e driver da camera */
    ESP_LOGI(TAG, "chamando bsp_camera_start()...");
    esp_err_t err = bsp_camera_start(nullptr);
    if (err == ESP_OK) {
        s_hw_camera_available = true;
        ESP_LOGI(TAG, "sensor de camera bsp_camera_start inicializado com sucesso");
        open_and_setup_v4l2_device();
    } else {
        s_hw_camera_available = false;
        ESP_LOGW(TAG, "bsp_camera_start retornou erro (%s)", esp_err_to_name(err));
    }

    s_state = CAMERA_STATE_IDLE;
    return ESP_OK;
}

camera_state_t camera_mgr_get_state(void)
{
    return s_state;
}

esp_err_t camera_mgr_start_preview(camera_frame_cb_t frame_cb, void *user_data)
{
    if (s_state == CAMERA_STATE_UNINITIALIZED) {
        camera_mgr_init();
    }

    s_frame_cb = frame_cb;
    s_user_data = user_data;

    if (s_video_fd < 0 && s_hw_camera_available) {
        open_and_setup_v4l2_device();
    }

    s_streaming = true;
    s_state = CAMERA_STATE_STREAMING;
    ESP_LOGI(TAG, "camera_mgr_start_preview ativado");
    return ESP_OK;
}

esp_err_t camera_mgr_stop_preview(void)
{
    s_streaming = false;
    s_frame_cb = nullptr;
    s_user_data = nullptr;
    s_state = CAMERA_STATE_IDLE;
    ESP_LOGI(TAG, "camera_mgr_stop_preview pausado com sucesso");
    return ESP_OK;
}

esp_err_t camera_mgr_capture_photo_async(char *out_filepath, size_t max_len, camera_capture_done_cb_t done_cb,
                                         void *user_data)
{
    mkdir("/sdcard/imagens", 0777);

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char filename[128];
    if (timeinfo.tm_year + 1900 < 2024) {
        static uint32_t s_photo_id = 1;
        snprintf(filename, sizeof(filename), "/sdcard/imagens/IMG_%04lu.jpg", (unsigned long)s_photo_id++);
    } else {
        snprintf(filename, sizeof(filename), "/sdcard/imagens/IMG_%04d%02d%02d_%02d%02d%02d.jpg",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                 timeinfo.tm_sec);
    }

    if (out_filepath != nullptr && max_len > 0) {
        strncpy(out_filepath, filename, max_len - 1);
        out_filepath[max_len - 1] = '\0';
    }

    size_t fsize = CAM_PREVIEW_WIDTH * CAM_PREVIEW_HEIGHT * 2;
    uint8_t *copy = (uint8_t *)heap_caps_malloc(fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) {
        copy = (uint8_t *)malloc(fsize);
    }
    if (!copy) {
        ESP_LOGE(TAG, "sem memoria para snapshot");
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_latest_frame != nullptr) {
            memcpy(copy, s_latest_frame, fsize);
        }
        xSemaphoreGive(s_lock);
    }

    SaveRequest req;
    req.frame_copy = copy;
    req.width = CAM_PREVIEW_WIDTH;
    req.height = CAM_PREVIEW_HEIGHT;
    strncpy(req.filepath, filename, sizeof(req.filepath) - 1);
    req.filepath[sizeof(req.filepath) - 1] = '\0';
    req.done_cb = done_cb;
    req.user_data = user_data;

    s_is_saving = true;
    if (s_save_queue != nullptr && xQueueSend(s_save_queue, &req, 0) == pdTRUE) {
        return ESP_OK;
    }

    s_is_saving = false;
    free(copy);
    return ESP_FAIL;
}

esp_err_t camera_mgr_capture_photo(char *out_filepath, size_t max_len)
{
    mkdir("/sdcard/imagens", 0777);

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char filename[128];
    if (timeinfo.tm_year + 1900 < 2024) {
        static uint32_t s_photo_id = 1;
        snprintf(filename, sizeof(filename), "/sdcard/imagens/IMG_%04lu.jpg", (unsigned long)s_photo_id++);
    } else {
        snprintf(filename, sizeof(filename), "/sdcard/imagens/IMG_%04d%02d%02d_%02d%02d%02d.jpg",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                 timeinfo.tm_sec);
    }

    bool success = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (s_latest_frame != nullptr) {
            success = save_rgb565_as_jpeg(filename, s_latest_frame, CAM_PREVIEW_WIDTH, CAM_PREVIEW_HEIGHT);
        }
        xSemaphoreGive(s_lock);
    }

    if (success) {
        ESP_LOGI(TAG, "foto salva com sucesso: %s", filename);
        if (out_filepath != nullptr && max_len > 0) {
            strncpy(out_filepath, filename, max_len - 1);
            out_filepath[max_len - 1] = '\0';
        }
        return ESP_OK;
    }

    ESP_LOGE(TAG, "falha ao salvar foto em %s", filename);
    return ESP_FAIL;
}

bool camera_mgr_is_saving(void)
{
    bool queue_has_items = (s_save_queue != nullptr && uxQueueMessagesWaiting(s_save_queue) > 0);
    return s_is_saving || queue_has_items;
}

esp_err_t camera_mgr_wait_save_done(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (camera_mgr_is_saving() && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(40));
        elapsed += 40;
    }
    return camera_mgr_is_saving() ? ESP_ERR_TIMEOUT : ESP_OK;
}

void camera_mgr_deinit(void)
{
    camera_mgr_stop_preview();
    if (s_latest_frame != nullptr) {
        free(s_latest_frame);
        s_latest_frame = nullptr;
    }
    if (s_lock != nullptr) {
        vSemaphoreDelete(s_lock);
        s_lock = nullptr;
    }
    s_state = CAMERA_STATE_UNINITIALIZED;
}
