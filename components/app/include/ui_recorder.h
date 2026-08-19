#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cria a tela do aplicativo Gravador de Voz e Player.
 * @return Ponteiro para o objeto de tela LVGL criado.
 */
void ui_recorder_register(void);
lv_obj_t *ui_recorder_create(void);

/**
 * @brief Notifica a abertura da tela do Gravador.
 */
void ui_recorder_on_open(void);

/**
 * @brief Notifica o fechamento da tela do Gravador.
 */
void ui_recorder_on_close(void);

/**
 * @brief Abre o Gravador e inicia a reproducao de um arquivo WAV especifico.
 * @param filepath Caminho completo do arquivo de audio.
 */
void ui_recorder_open_file(const char *filepath);

/**
 * @brief Reaplica o tema visual ativo na interface do gravador.
 */
void ui_recorder_refresh_theme(void);

/**
 * @brief Ajusta o layout dos componentes de acordo com a orientacao/resolucao atual da tela.
 */
void ui_recorder_apply_layout(void);

#ifdef __cplusplus
}
#endif
