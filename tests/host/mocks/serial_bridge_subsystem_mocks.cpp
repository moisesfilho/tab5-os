// Host fakes (linker, linkage C) para as entradas de subsistema que o
// serial_bridge (PLANO boneco-de-lata.md, Passo 1/3) precisa invocar no
// build host — onde os módulos de produção dependem de ESP-IDF/LVGL e não
// são compilados aqui (wifi_mgr, battery_reader, screenshot, http_file_server
// são trazidos apenas como .cpp de produção quando possível).
//
// As definições usam __attribute__((weak)): se o .cpp de produção do
// subsistema for adicionado ao build host depois, a definição forte vence e
// este arquivo vira apenas um fallback silencioso.
//
// Onde o header real existe e compila em host (http_file_server.h,
// battery_reader.h, screenshot.h), a assinatura reproduz o header. Para o
// Wi-Fi, o tipo wifi_status_t vive em wifi_mgr.h que depende de
// esp_wifi_types.h (sem stub em host): usa-se typedef local enquanto o stub
// não existir.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esp_err.h"
#include "http_file_server.h"
#include "battery_reader.h"
#include "screenshot.h"

extern "C" {

/* ------------------------------------------------------------------ */
/* Captura de tela: screenshot_take já existe; screenshot_get_last_path */
/* é o getter novo previsto no PLANO (Passo 3).                       */
/* ------------------------------------------------------------------ */

__attribute__((weak)) screenshot_result_t screenshot_take(void)
{
    return SCREENSHOT_RESULT_OK;
}

__attribute__((weak)) screenshot_result_t screenshot_wait_for_completion(unsigned int timeout_ms)
{
    (void)timeout_ms;
    return SCREENSHOT_RESULT_OK;
}

__attribute__((weak)) const char *screenshot_get_last_path(void)
{
    return "/sdcard/screenshots/print_latest.bmp";
}

/* ------------------------------------------------------------------ */
/* Servidor de arquivos HTTP                                          */
/* ------------------------------------------------------------------ */

namespace {
bool s_http_running = false;
}

__attribute__((weak)) esp_err_t http_file_server_start(void)
{
    s_http_running = true;
    return ESP_OK;
}

__attribute__((weak)) esp_err_t http_file_server_stop(void)
{
    s_http_running = false;
    return ESP_OK;
}

__attribute__((weak)) bool http_file_server_is_running(void)
{
    return s_http_running;
}

__attribute__((weak)) uint16_t http_file_server_get_port(void)
{
    return 80;
}

/* ------------------------------------------------------------------ */
/* Wi-Fi: status fake (offline) — tipo local até existir stub de       */
/* esp_wifi_types.h no build host.                                     */
/* ------------------------------------------------------------------ */

#if __has_include("esp_wifi_types.h")
#include "wifi_mgr.h"
#else
typedef struct {
    bool connected;
    char ssid[33];
    char ip[16];
} wifi_status_t;
#endif

__attribute__((weak)) esp_err_t wifi_mgr_get_status(wifi_status_t *status)
{
    if (status == nullptr) {
        return ESP_FAIL;
    }
    std::memset(status, 0, sizeof(*status));
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Bateria (battery_reader_get_status já existe na forma do header)   */
/* ------------------------------------------------------------------ */

__attribute__((weak)) bool battery_reader_get_status(battery_status_t *out)
{
    if (out == nullptr) {
        return false;
    }
    out->available = true;
    out->percent = 92;
    out->source = BATTERY_SOURCE_CHARGING;
    out->voltage_mv = 4120;
    out->current_ma = -120;
    out->protect_active = false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Heap (não há stub de esp_heap_caps.h; assinaturas do ESP-IDF)      */
/* ------------------------------------------------------------------ */

__attribute__((weak)) size_t esp_get_free_heap_size(void)
{
    return 350210u;
}

__attribute__((weak)) size_t heap_caps_get_free_size(int caps)
{
    (void)caps;
    return 14201080u;
}

} /* extern "C" */
