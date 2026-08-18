#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAMERA_STATE_UNINITIALIZED,
    CAMERA_STATE_IDLE,
    CAMERA_STATE_STREAMING,
    CAMERA_STATE_ERROR,
} camera_state_t;

/**
 * @brief Callback invocado para cada novo frame de preview capturado.
 *
 * @param frame_buf Ponteiro para o buffer de pixels (RGB565).
 * @param width Largura do frame em pixels.
 * @param height Altura do frame em pixels.
 * @param user_data Ponteiro de contexto do chamador.
 */
typedef void (*camera_frame_cb_t)(const uint8_t *frame_buf, uint16_t width, uint16_t height, void *user_data);

/**
 * @brief Callback invocado quando uma captura assíncrona é concluída.
 *
 * @param result ESP_OK se gravado com sucesso, ou código de erro.
 * @param filepath Caminho absoluto do arquivo salvo no SD card.
 * @param user_data Ponteiro de contexto do chamador.
 */
typedef void (*camera_capture_done_cb_t)(esp_err_t result, const char *filepath, void *user_data);

/**
 * @brief Inicializa o subsistema de câmera e recursos associados.
 *
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t camera_mgr_init(void);

/**
 * @brief Obtém o estado atual do subsistema de câmera.
 */
camera_state_t camera_mgr_get_state(void);

/**
 * @brief Inicia o streaming de vídeo e a task de captura contínua de frames.
 *
 * @param frame_cb Callback invocado quando um novo frame de preview estiver pronto.
 * @param user_data Ponteiro de contexto passado ao callback.
 * @return ESP_OK se o streaming foi iniciado com sucesso.
 */
esp_err_t camera_mgr_start_preview(camera_frame_cb_t frame_cb, void *user_data);

/**
 * @brief Para o streaming de vídeo e suspende a task de captura para economizar recursos.
 *
 * @return ESP_OK se parado com sucesso.
 */
esp_err_t camera_mgr_stop_preview(void);

/**
 * @brief Captura um snapshot com base no frame atual e grava como JPEG no microSD de forma assíncrona.
 *
 * Não bloqueia a thread de UI / LVGL.
 *
 * @param out_filepath Buffer opcional para receber o caminho do arquivo que será salvo.
 * @param max_len Tamanho máximo do buffer out_filepath.
 * @param done_cb Callback opcional invocado ao concluir a gravação no SD.
 * @param user_data Ponteiro de contexto passado ao callback.
 * @return ESP_OK se o disparo foi enfileirado com sucesso.
 */
esp_err_t camera_mgr_capture_photo_async(char *out_filepath, size_t max_len, camera_capture_done_cb_t done_cb,
                                         void *user_data);

/**
 * @brief Captura um snapshot de forma síncrona.
 *
 * @param out_filepath Buffer opcional para receber o caminho absoluto do arquivo salvo.
 * @param max_len Tamanho máximo do buffer out_filepath.
 * @return ESP_OK em caso de sucesso no disparo e salvamento.
 */
esp_err_t camera_mgr_capture_photo(char *out_filepath, size_t max_len);

/**
 * @brief Verifica se ha alguma gravacao de foto em andamento.
 */
bool camera_mgr_is_saving(void);

/**
 * @brief Aguarda a conclusao de qualquer gravacao pendente.
 *
 * @param timeout_ms Tempo maximo de espera em milissegundos.
 * @return ESP_OK se nao ha gravacao pendente, ESP_ERR_TIMEOUT caso expire.
 */
esp_err_t camera_mgr_wait_save_done(uint32_t timeout_ms);

/**
 * @brief Desinicializa o subsistema de câmera e desliga o sensor.
 */
void camera_mgr_deinit(void);

#ifdef __cplusplus
}
#endif
