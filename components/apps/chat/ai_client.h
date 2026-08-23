#pragma once

#include "esp_err.h"
#include "ai_storage.h"
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_STATE_IDLE = 0,
    AI_STATE_CONNECTING,
    AI_STATE_SENDING,
    AI_STATE_RECEIVING,
    AI_STATE_COMPLETED,
    AI_STATE_ERROR,
    AI_STATE_CANCELLED
} ai_state_t;

#ifdef __cplusplus
}
#endif

struct ai_msg_t {
    std::string role; // "system", "user", "assistant"
    std::string content;
};

typedef void (*ai_response_cb_t)(const char *response_text, void *user_data);
typedef void (*ai_state_cb_t)(ai_state_t state, const char *status_msg, void *user_data);

/**
 * @brief Inicializa os mutexes e recursos do cliente de IA.
 */
void ai_client_init(void);

/**
 * @brief Envia uma requisicao de chat completions em background (FreeRTOS task).
 * @param cfg Configuracao com URL, token, modelo e timeout.
 * @param messages Historico de mensagens da conversa.
 * @param on_response Callback invocado quando a resposta da IA e recebida.
 * @param on_state Callback invocado a cada mudanca de estado/erro.
 * @param user_data Ponteiro opcional de contexto.
 * @return ESP_OK se a task foi disparada, erro caso ja esteja ocupado ou falha de memoria.
 */
esp_err_t ai_client_send(const ai_cfg_t *cfg, const std::vector<ai_msg_t> &messages, ai_response_cb_t on_response,
                         ai_state_cb_t on_state, void *user_data);

/**
 * @brief Cancela uma requisicao em andamento.
 */
void ai_client_cancel(void);

/**
 * @brief Retorna o estado atual do cliente.
 */
ai_state_t ai_client_get_state(void);

/**
 * @brief Verifica se ha uma requisicao sendo processada no momento.
 */
bool ai_client_is_busy(void);
