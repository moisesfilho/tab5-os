#include "wifi_storage.h"
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "esp_check.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "tab5_wifi_storage";

static bool s_mounted = false;

esp_err_t wifi_storage_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bsp_sdcard_mount(), TAG, "mount SD falhou");
    if (mkdir("/sdcard/tab5_os", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir tab5_os falhou (errno=%d)", errno);
    }
    s_mounted = true;
    ESP_LOGI(TAG, "SD montado em /sdcard");
    return ESP_OK;
}

esp_err_t wifi_storage_load(wifi_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(WIFI_CFG_PATH, "r");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char line[128];
    cfg->ssid[0] = '\0';
    cfg->password[0] = '\0';
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char *value = eq + 1;
        value[strcspn(value, "\r\n")] = '\0';
        if (strcmp(line, "ssid") == 0) {
            snprintf(cfg->ssid, sizeof(cfg->ssid), "%s", value);
        } else if (strcmp(line, "password") == 0) {
            snprintf(cfg->password, sizeof(cfg->password), "%s", value);
        }
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t wifi_storage_save(const wifi_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(WIFI_CFG_PATH, "w");
    if (f == NULL) {
        return ESP_FAIL;
    }
    fprintf(f, "ssid=%s\npassword=%s\n", cfg->ssid, cfg->password);
    fclose(f);
    return ESP_OK;
}
