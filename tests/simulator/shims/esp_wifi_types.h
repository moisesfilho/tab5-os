#pragma once

/* Shim minimo de esp_wifi_types.h: apenas o que ui_wifi consome de um
 * wifi_ap_record_t ao listar redes. */

#include <cstdint>

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WEP,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA3_PSK,
} wifi_auth_mode_t;

typedef struct {
    uint8_t *ssid;
    uint8_t primary;
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;
