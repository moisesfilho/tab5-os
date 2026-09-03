#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_RECORDER_STATE_IDLE = 0,
    AUDIO_RECORDER_STATE_RECORDING,
    AUDIO_RECORDER_STATE_PLAYING,
    AUDIO_RECORDER_STATE_PAUSED,
} audio_recorder_state_t;

typedef struct {
    audio_recorder_state_t state;
    uint32_t current_time_sec;
    uint32_t total_time_sec;
    char current_filepath[256];
} audio_recorder_status_t;

/**
 * @brief Inicializa o subsistema de gravacao e reproducao de audio.
 */
esp_err_t audio_recorder_init(void);

/**
 * @brief Inicia uma nova gravacao de voz salva no microSD.
 * @param out_filepath Buffer opcional para receber o caminho completo do arquivo gerado.
 * @param out_len Tamanho do buffer out_filepath.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t audio_recorder_start_recording(char *out_filepath, size_t out_len);

/**
 * @brief Para a gravacao ativa e finaliza o arquivo WAV com cabecalho valido.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t audio_recorder_stop_recording(void);

/**
 * @brief Inicia a reproducao de um arquivo WAV do microSD.
 * @param filepath Caminho completo do arquivo WAV.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t audio_recorder_start_playback(const char *filepath);

/**
 * @brief Pausa a reproducao ativa.
 */
esp_err_t audio_recorder_pause_playback(void);

/**
 * @brief Retoma a reproducao pausada.
 */
esp_err_t audio_recorder_resume_playback(void);

/**
 * @brief Para a reproducao de audio ativa.
 */
esp_err_t audio_recorder_stop_playback(void);

/**
 * @brief Obtem o status atual do gravador e player.
 * @param status Ponteiro para preenchimento dos dados de estado.
 */
void audio_recorder_get_status(audio_recorder_status_t *status);

/**
 * @brief Retorna se uma gravacao esta ativa.
 */
bool audio_recorder_is_recording(void);

/**
 * @brief Retorna se uma reproducao esta ativa ou pausada.
 */
bool audio_recorder_is_playing(void);

/**
 * @brief Ajusta o volume do alto-falante (0 a 100).
 */
esp_err_t audio_recorder_set_volume(int volume);

#ifdef __cplusplus
}
#endif
