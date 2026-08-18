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
#include "wifi_mgr.h"
#include "wifi_storage.h"
#include "bt_mgr.h"
#include "ui_mouse.h"
#include "rtc_rx8130.h"
#include "display_storage.h"
#include "camera_mgr.h"
#include "http_file_server.h"

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

    /* Inicia display + touch + task LVGL (BSP oficial m5stack_tab5) */
    lv_display_t *disp = bsp_display_start();

    /* Semeia o relogio do sistema a partir do RTC RX8130CE (I2C do BSP) */
    rtc_rx8130_init();

    /* Monta o SD para restaurar configs persistidas (display e wifi) */
    if (wifi_storage_mount() == ESP_OK) {
        wifi_cfg_t cfg;
        if (wifi_storage_load(&cfg) == ESP_OK) {
            ESP_LOGI(TAG, "wifi.cfg: ssid=\"%s\" senha=%zu chars", cfg.ssid, strlen(cfg.password));
        } else {
            memset(&cfg, 0, sizeof(cfg));
            wifi_storage_save(&cfg);
            ESP_LOGW(TAG, "wifi.cfg nao existia - criado vazio");
        }
    }

    imu_reader_start(disp);

    /* Liga o radio WiFi (ESP32-C6 companion via SDIO) e inicia o STA + scan */
    wifi_mgr_start();

    /* Inicia o subsistema Bluetooth */
    bt_mgr_start();

    /* Inicializa o subsistema de Camera (SC202CS / esp_video) */
    camera_mgr_init();

    bsp_display_lock(0);

    ui_shell_init();
    ui_mouse_init();

    bsp_display_unlock();

    /* Aplica o brilho configurado (ou 80% como padrao seguro) */
    int brightness = DISPLAY_DEFAULT_BRIGHTNESS;
    display_storage_load_brightness(&brightness);
    bsp_display_brightness_set(brightness);

    ESP_LOGI(TAG, "UI iniciada");
}
