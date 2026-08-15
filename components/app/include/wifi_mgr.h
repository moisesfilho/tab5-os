#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_mgr_start(void);

/* Salva a config no SD e conecta na rede */
esp_err_t wifi_mgr_connect(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
