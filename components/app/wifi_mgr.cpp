#include "wifi_mgr.h"
#include <stdlib.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "tab5_wifi";

#define SCAN_PERIOD_MS 30000

static TimerHandle_t s_scan_timer = NULL;

static void log_scan_results(void)
{
    uint16_t ap_num = 0;
    if (esp_wifi_scan_get_ap_num(&ap_num) != ESP_OK) {
        return;
    }
    if (ap_num == 0) {
        ESP_LOGI(TAG, "scan: nenhum AP encontrado");
        return;
    }

    wifi_ap_record_t *aps = (wifi_ap_record_t *)calloc(ap_num, sizeof(wifi_ap_record_t));
    if (aps == NULL) {
        return;
    }
    if (esp_wifi_scan_get_ap_records(&ap_num, aps) == ESP_OK) {
        for (int i = 0; i < ap_num; i++) {
            ESP_LOGI(TAG, "scan[%d] ssid=\"%s\" rssi=%d ch=%d auth=%d", i, (char *)aps[i].ssid, aps[i].rssi,
                     aps[i].primary, (int)aps[i].authmode);
        }
    }
    free(aps);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "STA iniciado");
        esp_wifi_scan_start(NULL, false);
        break;
    case WIFI_EVENT_SCAN_DONE:
        ESP_LOGI(TAG, "scan concluido");
        log_scan_results();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "conectado ao AP");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "desconectado do AP");
        break;
    default:
        break;
    }
}

static void scan_timer_cb(TimerHandle_t timer)
{
    esp_wifi_scan_start(NULL, false);
}

esp_err_t wifi_mgr_start(void)
{
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_WIFI, true), TAG, "falha ao ligar radio WiFi");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop create failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG,
                        "registro WIFI_EVENT falhou");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    s_scan_timer = xTimerCreate("wifi_scan", pdMS_TO_TICKS(SCAN_PERIOD_MS), pdTRUE, NULL, scan_timer_cb);
    if (s_scan_timer != NULL) {
        xTimerStart(s_scan_timer, 0);
    }

    ESP_LOGI(TAG, "WiFi iniciado (radio C6 via SDIO)");
    return ESP_OK;
}
