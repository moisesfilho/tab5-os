/* Mocks deterministicos dos backends de hardware para o simulador.
 * Cada manager responde sempre com os mesmos valores para que as
 * capturas de tela sejam reprodutiveis entre execucoes. */

#include "battery_reader.h"
#include "bt_mgr.h"
#include "wifi_mgr.h"
#include "imu_reader.h"
#include "music_player.h"
#include "ai_client.h"
#include "ssh_client.h"
#include "audio_recorder.h"
#include "http_file_server.h"
#include "camera_mgr.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

#include "sim_capture.hpp"
#include "timezone_mgr.h"

/* ------------------------------------------------------------------ */
/* Bateria (INA226): 80%, 3.9V, descarregando levemente                */
/* ------------------------------------------------------------------ */

bool battery_reader_get_status(battery_status_t *out)
{
    if (out == nullptr) {
        return false;
    }
    out->available = true;
    out->percent = 80;
    out->voltage_mv = 3900;
    out->current_ma = 120;
    out->protect_active = false;
    return true;
}

void battery_reader_set_protection(bool enabled)
{
    (void)enabled;
}

bool battery_reader_get_protection(void)
{
    return true;
}

/* ------------------------------------------------------------------ */
/* Wi-Fi: habilitado, conectado em rede fake                           */
/* ------------------------------------------------------------------ */

esp_err_t wifi_mgr_start(void)
{
    return ESP_OK;
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *password)
{
    (void)ssid;
    (void)password;
    return ESP_OK;
}

esp_err_t wifi_mgr_scan(wifi_scan_cb_t cb, void *ctx)
{
    if (cb != nullptr) {
        cb(nullptr, 0, ctx);
    }
    return ESP_OK;
}

esp_err_t wifi_mgr_disconnect(void)
{
    return ESP_OK;
}

esp_err_t wifi_mgr_forget(const char *ssid)
{
    (void)ssid;
    return ESP_OK;
}

esp_err_t wifi_mgr_get_status(wifi_status_t *status)
{
    if (status == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    status->connected = true;
    snprintf(status->ssid, sizeof(status->ssid), "%s", "RedeFake");
    snprintf(status->ip, sizeof(status->ip), "%s", "192.168.1.50");
    return ESP_OK;
}

static bool s_wifi_enabled = true;

esp_err_t wifi_mgr_set_enabled(bool enabled)
{
    s_wifi_enabled = enabled;
    return ESP_OK;
}

bool wifi_mgr_is_enabled(void)
{
    return s_wifi_enabled;
}

/* ------------------------------------------------------------------ */
/* Bluetooth: habilitado, sem dispositivos conectados                  */
/* ------------------------------------------------------------------ */

esp_err_t bt_mgr_start(void)
{
    return ESP_OK;
}

esp_err_t bt_mgr_scan(bt_scan_cb_t cb, void *ctx)
{
    if (cb != nullptr) {
        cb(nullptr, 0, ctx);
    }
    return ESP_OK;
}

esp_err_t bt_mgr_connect(const char *mac, const char *name, bt_dev_type_t type)
{
    (void)mac;
    (void)name;
    (void)type;
    return ESP_OK;
}

esp_err_t bt_mgr_disconnect(const char *mac)
{
    (void)mac;
    return ESP_OK;
}

esp_err_t bt_mgr_forget(const char *mac)
{
    (void)mac;
    return ESP_OK;
}

esp_err_t bt_mgr_get_status(bt_status_t *status)
{
    if (status == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(status, 0, sizeof(*status));
    return ESP_OK;
}

bool bt_mgr_is_keyboard_connected(void)
{
    return false;
}

bool bt_mgr_is_mouse_connected(void)
{
    return false;
}

bool bt_mgr_is_audio_connected(void)
{
    return false;
}

static bool s_bt_enabled = true;

esp_err_t bt_mgr_set_enabled(bool enabled)
{
    s_bt_enabled = enabled;
    return ESP_OK;
}

bool bt_mgr_is_enabled(void)
{
    return s_bt_enabled;
}

/* ------------------------------------------------------------------ */
/* IMU: rotacao automatica desabilitada                                */
/* ------------------------------------------------------------------ */

void imu_reader_set_rotation_enabled(bool enabled)
{
    (void)enabled;
}

bool imu_reader_is_rotation_enabled(void)
{
    return false;
}

/* ------------------------------------------------------------------ */
/* Print da barra: mesma semantica do firmware, gravando BMP no SD     */
/* virtual (tmpdir) via capturador do simulador.                       */
/* ------------------------------------------------------------------ */

extern "C" void screenshot_take(void)
{
    struct tm tmbuf;
    char path[128];
    if (timezone_mgr_get_localtime(&tmbuf) != nullptr) {
        strftime(path, sizeof(path), "/sdcard/screenshots/print_%Y%m%d_%H%M%S.bmp", &tmbuf);
    } else {
        snprintf(path, sizeof(path), "/sdcard/screenshots/print_sem_data.bmp");
    }
    mkdir("/sdcard/screenshots", 0755);
    sim_capture_to_bmp(path);
}
