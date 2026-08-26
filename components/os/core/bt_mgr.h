#pragma once

#include "esp_err.h"
#include "bt_storage.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_SCAN_MAX_DEVICES 20

typedef struct {
    char mac[18];
    char name[64];
    int8_t rssi;
    bt_dev_type_t type;
    uint8_t addr_type;
    bool connected;
    bool paired;
} bt_device_info_t;

typedef void (*bt_scan_cb_t)(const bt_device_info_t *devices, int count, void *ctx);

/* Ciclo de vida de uma tentativa de conexao (reportado via bt_conn_cb_t) */
typedef enum {
    BT_CONN_STARTED = 0,  /* procedimento GAP iniciado */
    BT_CONN_CONNECTED,    /* link fisico estabelecido (pairing em curso) */
    BT_CONN_READY,        /* HID pronto: descoberta GATT concluida e CCCDs ativos */
    BT_CONN_FAILED,       /* falhou ao conectar; reason = status NimBLE/controller */
    BT_CONN_DISCONNECTED, /* conexao encerrada; reason = motivo da pilha */
} bt_conn_event_t;

typedef void (*bt_conn_cb_t)(const char *mac, bt_conn_event_t event, int reason, void *ctx);

typedef struct {
    bool any_connected;
    bool keyboard_connected;
    bool mouse_connected;
    bool audio_connected;
    bool scanning;
    int connected_count;
    char last_connected_mac[18];
    char last_connected_name[64];
} bt_status_t;

/* Inicia o subsistema Bluetooth (GAP, HCI/HOGP) */
esp_err_t bt_mgr_start(void);

/* Inicia scan de dispositivos proximos; chama cb ao concluir */
esp_err_t bt_mgr_scan(bt_scan_cb_t cb, void *ctx);

/* Conecta e pareia com o dispositivo especificado pelo MAC.
 * Retorna ESP_OK apenas se o procedimento GAP foi iniciado de fato; erros
 * reais (ocupado, falha do controlador) sao propagados. O resultado final
 * chega pelo callback registrado em bt_mgr_set_conn_callback. */
esp_err_t bt_mgr_connect(const char *mac, const char *name, bt_dev_type_t type);

/* Registra callback de resultado de conexao (chamado na task do NimBLE). */
void bt_mgr_set_conn_callback(bt_conn_cb_t cb, void *ctx);

/* Desconecta um dispositivo ativo */
esp_err_t bt_mgr_disconnect(const char *mac);

/* Esquece e despareia um dispositivo */
esp_err_t bt_mgr_forget(const char *mac);

/* Obtem o status atual do Bluetooth */
esp_err_t bt_mgr_get_status(bt_status_t *status);

/* Helpers rapidos para checagem de estado de perifericos */
bool bt_mgr_is_keyboard_connected(void);
bool bt_mgr_is_mouse_connected(void);
bool bt_mgr_is_audio_connected(void);

/* Habilitar / Desabilitar subsistema Bluetooth */
esp_err_t bt_mgr_set_enabled(bool enabled);
bool bt_mgr_is_enabled(void);

#ifdef __cplusplus
}
#endif
