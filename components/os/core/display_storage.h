#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_CFG_PATH "/sdcard/.tab5_os/display.cfg"

#define DISPLAY_DEFAULT_BRIGHTNESS 80
#define DISPLAY_MIN_BRIGHTNESS 10
#define DISPLAY_MAX_BRIGHTNESS 100

/**
 * @brief Carrega a orientacao da tela salva no cartao SD.
 *
 * @param rot Ponteiro para receber a rotacao lida.
 * @return ESP_OK se lido com sucesso, ESP_ERR_NOT_FOUND se o arquivo nao existir ou erro de leitura.
 */
esp_err_t display_storage_load_rotation(lv_disp_rotation_t *rot);

/**
 * @brief Salva a orientacao da tela no cartao SD.
 *
 * @param rot Rotacao atual a ser persistida.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t display_storage_save_rotation(lv_disp_rotation_t rot);

/**
 * @brief Carrega o nivel de brilho da tela salvo em NVS ou SD (com fallback de 80%).
 *
 * @param percent Ponteiro para receber o valor percentual (10-100).
 * @return ESP_OK se lido com sucesso.
 */
esp_err_t display_storage_load_brightness(int *percent);

/**
 * @brief Salva o nivel de brilho da tela em NVS e SD.
 *
 * @param percent Nivel de brilho a ser persistido (10-100).
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t display_storage_save_brightness(int percent);

#ifdef __cplusplus
}
#endif
