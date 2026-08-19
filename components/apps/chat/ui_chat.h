#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cria a tela do aplicativo Chat IA.
 * @return Ponteiro para a tela LVGL criada.
 */
void ui_chat_register(void);
lv_obj_t *ui_chat_create(void);

/**
 * @brief Notifica abertura da tela de Chat.
 */
void ui_chat_on_open(void);

/**
 * @brief Notifica fechamento da tela de Chat.
 */
void ui_chat_on_close(void);

/**
 * @brief Atualiza as cores e fontes de acordo com o tema ativo.
 */
void ui_chat_refresh_theme(void);

/**
 * @brief Reorganiza os componentes e alturas de acordo com rotacao/resolucao e presenca do teclado.
 */
void ui_chat_apply_layout(void);

#ifdef __cplusplus
}
#endif
