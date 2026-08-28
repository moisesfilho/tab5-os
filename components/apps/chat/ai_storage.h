#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AI_CFG_PATH "/sdcard/.tab5_os/ai.cfg"
#define AI_DEFAULT_BASE_URL "https://opencode.ai/zen/go/v1"
#define AI_DEFAULT_MODEL "deepseek-v4-pro"
#define AI_DEFAULT_MAX_TOKENS 2048

#define AI_DEFAULT_TIMEOUT_SEC 120

typedef struct {
    char base_url[512];
    char token[1024];
    char model[128];
    int max_tokens;
    int timeout_sec;
} ai_cfg_t;

/**
 * @brief Preenche a estrutura com valores padrao recomendados (OpenCode Go).
 * @param cfg Ponteiro para a estrutura de destino.
 */
void ai_storage_get_default(ai_cfg_t *cfg);

/**
 * @brief Carrega as configuracoes de IA a partir de /sdcard/.tab5_os/ai.cfg.
 *        Se o arquivo nao existir ou faltar campos, preenche com padroes.
 * @param cfg Ponteiro para a estrutura onde as configuracoes serao salvas.
 * @return ESP_OK em caso de sucesso, ou codigo de erro.
 */
esp_err_t ai_storage_load(ai_cfg_t *cfg);

/**
 * @brief Salva as configuracoes de IA em /sdcard/.tab5_os/ai.cfg.
 * @param cfg Ponteiro para a estrutura com os dados a persistir.
 * @return ESP_OK em caso de sucesso, ou codigo de erro.
 */
esp_err_t ai_storage_save(const ai_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
