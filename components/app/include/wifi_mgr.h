#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SCAN_MAX_APS 16

typedef void (*wifi_scan_cb_t)(const wifi_ap_record_t *aps, int count, void *ctx);

typedef struct {
    bool connected;
    char ssid[33];
    char ip[16];
} wifi_status_t;

esp_err_t wifi_mgr_start(void);

/* Salva a config no SD e conecta na rede */
esp_err_t wifi_mgr_connect(const char *ssid, const char *password);

/* Dispara um scan; chama cb com os APs encontrados (copia valida ate o retorno) */
esp_err_t wifi_mgr_scan(wifi_scan_cb_t cb, void *ctx);

/* Desconecta da rede ativa e interrompe tentativas de reconexao */
esp_err_t wifi_mgr_disconnect(void);

/* Esquece uma rede (remove do SD) e desconecta se for a rede atual */
esp_err_t wifi_mgr_forget(const char *ssid);

/* Estado atual da conexao */
esp_err_t wifi_mgr_get_status(wifi_status_t *status);

#ifdef __cplusplus
}
#endif
