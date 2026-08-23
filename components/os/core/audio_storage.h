#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_CFG_PATH "/sdcard/tab5_os/audio.cfg"

#define AUDIO_MIN_VOLUME 0
#define AUDIO_MAX_VOLUME 100
#define AUDIO_DEFAULT_VOLUME 80

/**
 * @brief Carrega o volume geral salvo em NVS ou SD (com fallback de 80%).
 *
 * @param percent Ponteiro para receber o valor percentual (0-100).
 * @return ESP_OK se lido com sucesso.
 */
esp_err_t audio_storage_load_volume(int *percent);

/**
 * @brief Salva o volume geral em NVS e SD.
 *
 * @param percent Volume a ser persistido (0-100).
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t audio_storage_save_volume(int percent);

#ifdef __cplusplus
}
#endif
