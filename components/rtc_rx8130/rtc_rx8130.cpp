/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "rtc_rx8130.h"

#include <string.h>
#include <sys/time.h>

#include "esp_log.h"

#include "bsp/m5stack_tab5.h"

#include "rx8130.h"

static const char *TAG = "rtc_rx8130";

// Endereço I2C fixo do RX8130CE no M5Stack Tab5
#define RTC_RX8130_I2C_ADDR 0x32

static RX8130_Class s_rtc;
static bool s_initialized = false;

// Lê a hora do RTC normalizando para a convenção do C (struct tm):
// o driver devolve tm_mon 1-based (registro BCD do RX8130, 1-12) e
// tm_year já em "anos desde 1900" (+100). Após a normalização o valor
// está pronto para o mktime.
static bool rtc_rx8130_read(struct tm *out)
{
    if (out == NULL) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    s_rtc.getTime(out);
    out->tm_mon -= 1;  // 1-based -> 0-based

    return true;
}

esp_err_t rtc_rx8130_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "RTC já inicializado — ignorando chamada duplicada");
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        ESP_LOGE(TAG, "barramento I2C do BSP não disponível");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_rtc.begin(bus, RTC_RX8130_I2C_ADDR)) {
        ESP_LOGE(TAG, "falha ao adicionar o dispositivo RX8130CE (0x%02X) ao barramento I2C",
                 RTC_RX8130_I2C_ADDR);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "RX8130CE detectado em 0x%02X", RTC_RX8130_I2C_ADDR);
    s_rtc.initBat();

    struct tm now;
    if (!rtc_rx8130_read(&now)) {
        return ESP_FAIL;
    }

    if (s_rtc.getLowVoltageFlag()) {
        ESP_LOGW(TAG, "VLF ativo (bateria do RTC fraca/desconectada) — a hora lida pode ser a data de fábrica");
    }

    ESP_LOGI(TAG, "hora lida do RTC: %04d-%02d-%02d %02d:%02d:%02d (wday=%d)", now.tm_year + 1900,
             now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, now.tm_wday);

    struct timeval tv = {
        .tv_sec  = mktime(&now),
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "settimeofday falhou");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "relógio do sistema semeado: epoch %lld", (long long)tv.tv_sec);
    s_initialized = true;
    return ESP_OK;
}

bool rtc_rx8130_get_time(struct tm *out)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "RTC não inicializado — chame rtc_rx8130_init() primeiro");
        return false;
    }
    return rtc_rx8130_read(out);
}