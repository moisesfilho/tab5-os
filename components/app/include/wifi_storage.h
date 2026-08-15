#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_CFG_PATH "/sdcard/tab5_os/wifi.cfg"

typedef struct {
    char ssid[33];
    char password[65];
} wifi_cfg_t;

/* Monta o SD (idempotente) e garante o diretorio /sdcard/tab5_os */
esp_err_t wifi_storage_mount(void);

/* Carrega a config do wifi.cfg. ESP_ERR_NOT_FOUND se o arquivo nao existir */
esp_err_t wifi_storage_load(wifi_cfg_t *cfg);

/* Grava a config no wifi.cfg (cria o arquivo se necessario) */
esp_err_t wifi_storage_save(const wifi_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
