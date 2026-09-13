#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "ui_shell.h"
#include "imu_reader.h"
#include "battery_reader.h"
#include "wifi_mgr.h"
#include "wifi_storage.h"
#include "bt_mgr.h"
#include "ui_mouse.h"
#include "rtc_rx8130.h"
#include "display_storage.h"
#include "camera_mgr.h"
#include "tab5_keyboard.h"
#include "http_file_server.h"
#include "ai_storage.h"
#include "timezone_mgr.h"
#include "ui_font.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "serial_bridge.h"

static const char *TAG = "tab5_poc";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Tab5 PoC - Fase 6 (shell)");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    lv_display_t *disp = bsp_display_start();
    i2s_std_config_t i2s_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = BSP_I2S_MCLK,
                .bclk = BSP_I2S_SCLK,
                .ws = BSP_I2S_LCLK,
                .dout = BSP_I2S_DOUT,
                .din = BSP_I2S_DSIN,
                .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
            },
    };
    esp_err_t audio_ret = bsp_audio_init(&i2s_cfg);
    if (audio_ret == ESP_OK) {
        ESP_LOGI(TAG, "I2S pre-inicializado, dma_free=%d", (int)heap_caps_get_free_size(MALLOC_CAP_DMA));
    } else {
        ESP_LOGW(TAG, "Falha ao pre-inicializar I2S: %s", esp_err_to_name(audio_ret));
    }

    rtc_rx8130_init();
    timezone_mgr_init();
    if (wifi_storage_mount() == ESP_OK) {
        wifi_cfg_t cfg;
        if (wifi_storage_load(&cfg) == ESP_OK) {
            ESP_LOGI(TAG, "wifi.cfg: ssid=\"%s\" senha=%zu chars", cfg.ssid, strlen(cfg.password));
        } else {
            memset(&cfg, 0, sizeof(cfg));
            wifi_storage_save(&cfg);
            ESP_LOGW(TAG, "wifi.cfg nao existia - criado vazio");
        }
        ai_cfg_t ai_cfg;
        if (ai_storage_load(&ai_cfg) == ESP_OK) {
            ESP_LOGI(TAG, "ai.cfg: model=\"%s\" base_url=\"%s\"", ai_cfg.model, ai_cfg.base_url);
        }
    }

    imu_reader_start(disp);
    battery_reader_start();
    tab5_keyboard_init();
    wifi_mgr_start();
    bt_mgr_start();
    camera_mgr_init();

    bsp_display_lock(0);
    lv_theme_t *th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                           false, &lv_font_montserrat_18_latin1);
    lv_display_set_theme(disp, th);
    ui_shell_init();
    ui_mouse_init();
    bsp_display_unlock();

    int brightness = DISPLAY_DEFAULT_BRIGHTNESS;
    display_storage_load_brightness(&brightness);
    bsp_display_brightness_set(brightness);
    ESP_LOGI(TAG, "UI iniciada");
    serial_bridge_start();
}
