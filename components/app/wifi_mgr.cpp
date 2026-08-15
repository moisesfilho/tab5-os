#include "wifi_mgr.h"
#include "wifi_storage.h"
#include <stdlib.h>
#include <string.h>
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
#define CONNECT_RETRY_BASE_MS 2000
#define CONNECT_RETRY_MAX_MS 30000

static TimerHandle_t s_scan_timer = NULL;
static TimerHandle_t s_retry_timer = NULL;
static wifi_cfg_t s_cfg;
static bool s_has_cfg = false;
static bool s_connected = false;
static int s_retry_delay_ms = CONNECT_RETRY_BASE_MS;
static wifi_scan_cb_t s_scan_cb = NULL;
static void *s_scan_cb_ctx = NULL;
static char s_connected_ssid[33] = "";

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
    if (ap_num > WIFI_SCAN_MAX_APS) {
        ap_num = WIFI_SCAN_MAX_APS;
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
        if (s_scan_cb != NULL) {
            wifi_scan_cb_t cb = s_scan_cb;
            void *ctx = s_scan_cb_ctx;
            s_scan_cb = NULL;
            s_scan_cb_ctx = NULL;
            cb(aps, ap_num, ctx);
        }
    }
    free(aps);
}

static void try_connect(void)
{
    if (!s_has_cfg || s_cfg.ssid[0] == '\0') {
        return;
    }
    wifi_config_t wcfg = {0};
    strlcpy((char *)wcfg.sta.ssid, s_cfg.ssid, sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, s_cfg.password, sizeof(wcfg.sta.password));
    if (esp_wifi_set_config(WIFI_IF_STA, &wcfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config falhou");
        return;
    }
    ESP_LOGI(TAG, "conectando a \"%s\"", s_cfg.ssid);
    esp_wifi_connect();
}

static void schedule_retry(void)
{
    if (!s_has_cfg) {
        return;
    }
    xTimerChangePeriod(s_retry_timer, pdMS_TO_TICKS(s_retry_delay_ms), 0);
    xTimerStart(s_retry_timer, 0);
    s_retry_delay_ms *= 2;
    if (s_retry_delay_ms > CONNECT_RETRY_MAX_MS) {
        s_retry_delay_ms = CONNECT_RETRY_MAX_MS;
    }
}

static void retry_timer_cb(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "reconectando (backoff %d ms)", s_retry_delay_ms);
    esp_wifi_connect();
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP obtido: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "STA iniciado");
        if (s_has_cfg) {
            try_connect();
        } else {
            esp_wifi_scan_start(NULL, false);
        }
        break;
    case WIFI_EVENT_SCAN_DONE:
        ESP_LOGI(TAG, "scan concluido");
        log_scan_results();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "conectado ao AP");
        s_connected = true;
        s_retry_delay_ms = CONNECT_RETRY_BASE_MS;
        xTimerStop(s_retry_timer, 0);
        if (event_data != NULL) {
            const wifi_event_sta_connected_t *ev = (const wifi_event_sta_connected_t *)event_data;
            snprintf(s_connected_ssid, sizeof(s_connected_ssid), "%s", ev->ssid);
        }
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        s_connected = false;
        s_connected_ssid[0] = '\0';
        ESP_LOGW(TAG, "desconectado do AP");
        schedule_retry();
        break;
    default:
        break;
    }
}

static void scan_timer_cb(TimerHandle_t timer)
{
    if (!s_has_cfg && !s_connected) {
        esp_wifi_scan_start(NULL, false);
    }
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_cfg.ssid, sizeof(s_cfg.ssid), "%s", ssid);
    snprintf(s_cfg.password, sizeof(s_cfg.password), "%s", password != NULL ? password : "");
    s_has_cfg = true;

    if (wifi_storage_mount() == ESP_OK) {
        wifi_storage_save(&s_cfg);
        ESP_LOGI(TAG, "config salva no SD");
    }

    xTimerStop(s_retry_timer, 0);
    try_connect();
    return ESP_OK;
}

esp_err_t wifi_mgr_scan(wifi_scan_cb_t cb, void *ctx)
{
    s_scan_cb = cb;
    s_scan_cb_ctx = ctx;
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        s_scan_cb = NULL;
        s_scan_cb_ctx = NULL;
    }
    return err;
}

esp_err_t wifi_mgr_get_status(wifi_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    status->connected = s_connected;
    snprintf(status->ssid, sizeof(status->ssid), "%s", s_connected_ssid);
    if (s_connected) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
                snprintf(status->ip, sizeof(status->ip), IPSTR, IP2STR(&ip.ip));
            }
        }
    }
    if (status->ip[0] == '\0') {
        snprintf(status->ip, sizeof(status->ip), "-");
    }
    return ESP_OK;
}

esp_err_t wifi_mgr_start(void)
{
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_WIFI, true), TAG, "falha ao ligar radio WiFi");

    /* Carrega a config salva no SD (se existir) para conexao automatica */
    if (wifi_storage_mount() == ESP_OK && wifi_storage_load(&s_cfg) == ESP_OK) {
        s_has_cfg = s_cfg.ssid[0] != '\0';
        if (s_has_cfg) {
            ESP_LOGI(TAG, "config carregada: ssid=\"%s\"", s_cfg.ssid);
        }
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop create failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG,
                        "registro WIFI_EVENT falhou");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL), TAG,
                        "registro IP_EVENT falhou");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    s_scan_timer = xTimerCreate("wifi_scan", pdMS_TO_TICKS(SCAN_PERIOD_MS), pdTRUE, NULL, scan_timer_cb);
    if (s_scan_timer != NULL) {
        xTimerStart(s_scan_timer, 0);
    }
    s_retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(CONNECT_RETRY_BASE_MS), pdFALSE, NULL, retry_timer_cb);

    ESP_LOGI(TAG, "WiFi iniciado (radio C6 via SDIO)");
    return ESP_OK;
}
