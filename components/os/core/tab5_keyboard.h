#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Driver para o teclado fisico M5Stack Tab5 Keyboard (SKU A164).
 * Conecta via I2C na Ext.Port1 (SDA=GPIO0, SCL=GPIO1, INT=GPIO50).
 * Modo Character: retorna strings UTF-8 + modificadores (Ctrl/Alt).
 * Deteccao automatica via probe periodico (i2c_master_probe 0x6D). */

/* Inicia o driver: cria barramento I2C bus 0, probe o teclado,
 * inicia task de leitura e timer de deteccao periodica.
 * Seguro chamar multiplas vezes (idempotente). */
esp_err_t tab5_keyboard_init(void);

/* Para o driver: para a task, remove o device I2C, libera recursos. */
esp_err_t tab5_keyboard_deinit(void);

/* Retorna true se o teclado fisico foi detectado na ultima verificacao. */
bool tab5_keyboard_is_connected(void);

#ifdef __cplusplus
}
#endif
