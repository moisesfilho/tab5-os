#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_CFG_PATH "/sdcard/.tab5_os/wifi.cfg"
#define WIFI_MAX_SAVED_NETWORKS 16

/* Diretório oculto onde o SO consolida todas as suas configurações no SD */
#define TAB5_CONFIG_DIR "/sdcard/.tab5_os"

typedef struct {
    char ssid[33];
    char password[65];
} wifi_cfg_t;

typedef struct {
    wifi_cfg_t items[WIFI_MAX_SAVED_NETWORKS];
    int count;
} wifi_saved_list_t;

/* Monta o SD (idempotente) e garante o diretorio /sdcard/.tab5_os */
esp_err_t wifi_storage_mount(void);

/* Carrega a primeira config do wifi.cfg (retrocompatibilidade) */
esp_err_t wifi_storage_load(wifi_cfg_t *cfg);

/* Grava uma config como unica/primeira (retrocompatibilidade) */
esp_err_t wifi_storage_save(const wifi_cfg_t *cfg);

/* Carrega todas as redes salvas do wifi.cfg */
esp_err_t wifi_storage_load_all(wifi_saved_list_t *list);

/* Grava todas as redes salvas no wifi.cfg */
esp_err_t wifi_storage_save_all(const wifi_saved_list_t *list);

/* Adiciona ou atualiza uma rede no arquivo de configuracao */
esp_err_t wifi_storage_add_or_update(const char *ssid, const char *password);

/* Remove (esquece) uma rede do arquivo de configuracao */
esp_err_t wifi_storage_remove(const char *ssid);

/* Verifica se uma rede esta salva e opcionalmente retorna a senha */
bool wifi_storage_find(const char *ssid, char *out_password, size_t max_len);

#ifdef __cplusplus
}
#endif
