#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_CFG_PATH "/sdcard/.tab5_os/bt.cfg"
#define BT_MAX_SAVED_DEVICES 16

typedef enum {
    BT_DEV_TYPE_GENERIC = 0,
    BT_DEV_TYPE_KEYBOARD,
    BT_DEV_TYPE_MOUSE,
    BT_DEV_TYPE_HEADPHONE,
} bt_dev_type_t;

typedef struct {
    char mac[18];       /* Formato "XX:XX:XX:XX:XX:XX" */
    char name[64];      /* Nome amigavel */
    bt_dev_type_t type; /* Tipo de dispositivo */
    uint8_t addr_type;  /* BLE_ADDR_PUBLIC (0) ou BLE_ADDR_RANDOM (1) */
    bool paired;        /* Pareado/Bonded */
    bool auto_connect;  /* Auto reconexao no boot */
} bt_saved_device_t;

typedef struct {
    bt_saved_device_t items[BT_MAX_SAVED_DEVICES];
    int count;
} bt_saved_list_t;

/* Carrega todos os dispositivos salvos do bt.cfg */
esp_err_t bt_storage_load_all(bt_saved_list_t *list);

/* Grava todos os dispositivos salvos no bt.cfg */
esp_err_t bt_storage_save_all(const bt_saved_list_t *list);

/* Adiciona ou atualiza um dispositivo no arquivo de configuracao */
esp_err_t bt_storage_add_or_update(const bt_saved_device_t *dev);

/* Remove um dispositivo do arquivo de configuracao */
esp_err_t bt_storage_remove(const char *mac);

/* Verifica se um dispositivo esta salvo e opcionalmente retorna os dados */
bool bt_storage_find(const char *mac, bt_saved_device_t *out_dev);

#ifdef __cplusplus
}
#endif
