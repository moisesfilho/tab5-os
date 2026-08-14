/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa o RTC RX8130CE e semeia o relógio do sistema
 *
 * Obtém o barramento I2C do BSP (bsp_i2c_get_handle), adiciona o
 * dispositivo RX8130CE (endereço 0x32), ativa o modo bateria
 * (initBat), lê a data/hora atual e a aplica ao sistema via
 * settimeofday().
 *
 * @return ESP_OK em sucesso, esp_err_t de erro caso contrário
 */
esp_err_t rtc_rx8130_init(void);

/**
 * @brief Lê a data/hora atual do RTC
 *
 * @param[out] out struct tm preenchido com a hora do RTC (tm_mon
 *             0-based, tm_year desde 1900 — convenção do C)
 * @return true em sucesso, false caso o RTC não esteja inicializado
 */
bool rtc_rx8130_get_time(struct tm *out);

#ifdef __cplusplus
}
#endif