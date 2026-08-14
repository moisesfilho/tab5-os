#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "ui_keyboard.h"
#include "ui_status.h"
#include "imu_reader.h"

static const char *TAG = "tab5_poc";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Tab5 PoC - Fase 4 (rotacao)");

    /* Inicia display + touch + task LVGL (BSP oficial m5stack_tab5) */
    lv_display_t *disp = bsp_display_start();

    imu_reader_start(disp);

    bsp_display_lock(0);

    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x12151c), 0);

    ui_keyboard_create(scr);
    ui_status_init(scr);

    bsp_display_unlock();

    /* 60% p/ reduzir carga na alimentacao USB (backlight 100% causava brownout) */
    bsp_display_brightness_set(60);

    ESP_LOGI(TAG, "UI iniciada");
}
