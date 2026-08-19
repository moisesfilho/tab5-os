#include "ui_mouse.h"
#include "ui_screensaver.h"
#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "tab5_mouse";

namespace {

lv_indev_t *s_mouse_indev = nullptr;
lv_obj_t *s_cursor_obj = nullptr;
bool s_mouse_connected = false;

portMUX_TYPE s_mouse_mux = portMUX_INITIALIZER_UNLOCKED;
int32_t s_acc_dx = 0;
int32_t s_acc_dy = 0;
bool s_btn_pressed = false;

int32_t s_cursor_x = 360;
int32_t s_cursor_y = 640;

/* Bitmap do cursor tipo seta clássica de alta visibilidade (14x19 ARGB8888) */
#define CURSOR_W 14
#define CURSOR_H 19

/* clang-format off */
const char *CURSOR_MAP[CURSOR_H] = {
    "X             ",
    "XX            ",
    "X.X           ",
    "X..X          ",
    "X...X         ",
    "X....X        ",
    "X.....X       ",
    "X......X      ",
    "X.......X     ",
    "X........X    ",
    "X.....XXXX    ",
    "X..X..X       ",
    "X.X X..X      ",
    "XX   X..X     ",
    "X     X..X    ",
    "      X..X    ",
    "       XX     ",
    "              ",
    "              ",
};
/* clang-format on */

uint32_t s_cursor_pixels[CURSOR_W * CURSOR_H];

const lv_image_dsc_t s_cursor_img_dsc = {
    .header =
        {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_ARGB8888,
            .flags = 0,
            .w = CURSOR_W,
            .h = CURSOR_H,
            .stride = CURSOR_W * sizeof(uint32_t),
            .reserved_2 = 0,
        },
    .data_size = sizeof(s_cursor_pixels),
    .data = (const uint8_t *)s_cursor_pixels,
    .reserved = nullptr,
    .reserved_2 = nullptr,
};

void init_cursor_bitmap(void)
{
    const uint32_t C_TRANS = 0x00000000;
    const uint32_t C_BLACK = 0xFF000000;
    const uint32_t C_WHITE = 0xFFFFFFFF;

    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {
            char c = CURSOR_MAP[y][x];
            uint32_t color = C_TRANS;
            if (c == 'X') {
                color = C_BLACK;
            } else if (c == '.') {
                color = C_WHITE;
            }
            s_cursor_pixels[y * CURSOR_W + x] = color;
        }
    }
}

bool s_click_pending = false;
uint8_t s_click_step = 0;

void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    int32_t dx = 0;
    int32_t dy = 0;
    bool btn = false;
    bool click_now = false;

    portENTER_CRITICAL(&s_mouse_mux);
    dx = s_acc_dx;
    dy = s_acc_dy;
    s_acc_dx = 0;
    s_acc_dy = 0;
    btn = s_btn_pressed;
    if (s_click_pending) {
        click_now = true;
        s_click_pending = false;
    }
    portEXIT_CRITICAL(&s_mouse_mux);

    if (click_now && s_click_step == 0) {
        s_click_step = 1;
    }

    if (dx != 0 || dy != 0 || btn || click_now) {
        lv_display_trigger_activity(NULL);
        if (ui_screensaver_is_active()) {
            ui_screensaver_wake_up();
        }
    }

    lv_display_t *disp = lv_display_get_default();
    if (disp == nullptr) {
        return;
    }

    int32_t width = lv_display_get_horizontal_resolution(disp);
    int32_t height = lv_display_get_vertical_resolution(disp);
    lv_disp_rotation_t rot = lv_display_get_rotation(disp);

    /* Atualiza as coordenadas relativas diretamente no espaço da tela ativa */
    s_cursor_x += dx;
    s_cursor_y += dy;

    if (s_cursor_x < 0) {
        s_cursor_x = 0;
    }
    if (s_cursor_x >= width) {
        s_cursor_x = width - 1;
    }
    if (s_cursor_y < 0) {
        s_cursor_y = 0;
    }
    if (s_cursor_y >= height) {
        s_cursor_y = height - 1;
    }

    int32_t orig_w = lv_display_get_original_horizontal_resolution(disp);
    int32_t orig_h = lv_display_get_original_vertical_resolution(disp);
    if (orig_w <= 0) {
        orig_w = 720;
    }
    if (orig_h <= 0) {
        orig_h = 1280;
    }

    /* Como o LVGL 9 aplica lv_display_rotate_point() sobre data->point,
     * convertemos a coordenada virtual ativa (s_cursor_x, s_cursor_y) de volta
     * para a coordenada física do display para que a rotação do LVGL resulte
     * exatamente na posição correta em 100% da tela. */
    int32_t raw_x = 0;
    int32_t raw_y = 0;

    switch (rot) {
    case LV_DISPLAY_ROTATION_0:
        raw_x = s_cursor_x;
        raw_y = s_cursor_y;
        break;
    case LV_DISPLAY_ROTATION_90:
        raw_x = s_cursor_y;
        raw_y = orig_h - s_cursor_x - 1;
        break;
    case LV_DISPLAY_ROTATION_180:
        raw_x = orig_w - s_cursor_x - 1;
        raw_y = orig_h - s_cursor_y - 1;
        break;
    case LV_DISPLAY_ROTATION_270:
        raw_x = orig_w - s_cursor_y - 1;
        raw_y = s_cursor_x;
        break;
    }

    data->point.x = raw_x;
    data->point.y = raw_y;

    if (s_click_step == 1) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->continue_reading = true;
        s_click_step = 2;
    } else if (s_click_step == 2) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = false;
        s_click_step = 0;
    } else {
        data->state = btn ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = false;
    }
}

} // namespace

void ui_mouse_init(void)
{
    ESP_LOGI(TAG, "Inicializando driver de mouse BLE HID para LVGL 9...");

    init_cursor_bitmap();

    /* Cria o dispositivo de entrada do tipo ponteiro */
    s_mouse_indev = lv_indev_create();
    lv_indev_set_type(s_mouse_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_mouse_indev, mouse_read_cb);

    /* Cria a imagem do cursor na camada de sistema (acima de tudo) */
    lv_obj_t *sys_layer = lv_layer_sys();
    s_cursor_obj = lv_image_create(sys_layer);
    lv_image_set_src(s_cursor_obj, &s_cursor_img_dsc);
    lv_obj_clear_flag(s_cursor_obj, LV_OBJ_FLAG_CLICKABLE);

    /* Oculta o cursor por padrão até um mouse conectar */
    lv_obj_add_flag(s_cursor_obj, LV_OBJ_FLAG_HIDDEN);

    /* Vincula o objeto de cursor ao indev para posicionamento automático */
    lv_indev_set_cursor(s_mouse_indev, s_cursor_obj);

    ESP_LOGI(TAG, "Driver de mouse e cursor visual inicializados");
}

void ui_mouse_inject_motion(int8_t dx, int8_t dy, uint8_t buttons, int8_t wheel)
{
    (void)wheel;

    portENTER_CRITICAL(&s_mouse_mux);
    s_acc_dx += dx;
    s_acc_dy += dy;
    /* Botao esquerdo = bit 0 (0x01) */
    s_btn_pressed = (buttons & 0x01) != 0;
    portEXIT_CRITICAL(&s_mouse_mux);

    if (!s_mouse_connected) {
        ui_mouse_set_connected(true);
    }
}

void ui_mouse_inject_click(void)
{
    portENTER_CRITICAL(&s_mouse_mux);
    s_click_pending = true;
    portEXIT_CRITICAL(&s_mouse_mux);

    if (!s_mouse_connected) {
        ui_mouse_set_connected(true);
    }
}

#include "bsp/esp-bsp.h"

static void set_cursor_visible_async(void *user_data)
{
    bool visible = *(bool *)user_data;
    if (s_cursor_obj != nullptr) {
        if (visible) {
            lv_obj_remove_flag(s_cursor_obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_cursor_obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static bool s_conn_true = true;
static bool s_conn_false = false;

void ui_mouse_set_connected(bool connected)
{
    if (s_mouse_connected == connected) {
        return;
    }
    s_mouse_connected = connected;
    if (!connected) {
        portENTER_CRITICAL(&s_mouse_mux);
        s_acc_dx = 0;
        s_acc_dy = 0;
        s_btn_pressed = false;
        portEXIT_CRITICAL(&s_mouse_mux);
    }
    void *arg = connected ? (void *)&s_conn_true : (void *)&s_conn_false;
    if (bsp_display_lock(pdMS_TO_TICKS(500))) {
        set_cursor_visible_async(arg);
        bsp_display_unlock();
    } else {
        lv_async_call(set_cursor_visible_async, arg);
    }
    ESP_LOGI(TAG, "Mouse %s (cursor %s)", connected ? "CONECTADO" : "DESCONECTADO", connected ? "VISIVEL" : "OCULTO");
}

void ui_mouse_set_cursor_visible(bool visible)
{
    if (s_cursor_obj == nullptr) {
        return;
    }

    if (visible && s_mouse_connected) {
        lv_obj_remove_flag(s_cursor_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_cursor_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_mouse_is_connected(void)
{
    return s_mouse_connected;
}
