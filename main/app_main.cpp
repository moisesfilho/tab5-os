#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "ui_keyboard.h"
#include "ui_bar.h"
#include "imu_reader.h"
#include "rtc_rx8130.h"

static const char *TAG = "tab5_poc";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Tab5 PoC - Fase 4 (rotacao)");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Inicia display + touch + task LVGL (BSP oficial m5stack_tab5) */
    lv_display_t *disp = bsp_display_start();

    /* Semeia o relogio do sistema a partir do RTC RX8130CE (I2C do BSP) */
    rtc_rx8130_init();

    imu_reader_start(disp);

    bsp_display_lock(0);

    lv_obj_t *scr = lv_disp_get_scr_act(NULL);

    ui_keyboard_create(scr);
    ui_bar_init(scr);

    bsp_display_unlock();

    /* 60% p/ reduzir carga na alimentacao USB (backlight 100% causava brownout) */
    bsp_display_brightness_set(60);

    ESP_LOGI(TAG, "UI iniciada");
}
