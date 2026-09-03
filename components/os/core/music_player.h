#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MUSIC_PLAYER_STATE_IDLE = 0,
    MUSIC_PLAYER_STATE_PLAYING,
    MUSIC_PLAYER_STATE_PAUSED,
} music_player_state_t;

typedef struct {
    music_player_state_t state;
    uint32_t current_time_sec;
    uint32_t total_time_sec;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    char current_filepath[256];
} music_player_status_t;

/**
 * @brief Inicializa o subsistema de reproducao de musica.
 */
esp_err_t music_player_init(void);

/**
 * @brief Inicia a reproducao de um arquivo de musica (MP3 ou WAV) do microSD.
 * @param filepath Caminho completo do arquivo.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t music_player_start(const char *filepath);

/**
 * @brief Pausa a reproducao ativa.
 */
esp_err_t music_player_pause(void);

/**
 * @brief Retoma a reproducao pausada.
 */
esp_err_t music_player_resume(void);

/**
 * @brief Para a reproducao de musica ativa.
 */
esp_err_t music_player_stop(void);

/**
 * @brief Obtem o status atual do player de musica.
 * @param status Ponteiro para preenchimento dos dados de estado.
 */
void music_player_get_status(music_player_status_t *status);

/**
 * @brief Retorna se uma reproducao esta ativa ou pausada.
 */
bool music_player_is_playing(void);

/**
 * @brief Ajusta o volume do alto-falante (0 a 100).
 */
esp_err_t music_player_set_volume(int volume);

/**
 * @brief Retorna o volume atual (0 a 100).
 */
int music_player_get_volume(void);

#ifdef __cplusplus
}
#endif
