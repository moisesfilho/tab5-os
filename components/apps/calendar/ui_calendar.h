#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registra o manifesto do aplicativo Calendário no sistema (app_registry).
 */
void ui_calendar_register(void);

/**
 * @brief Cria a tela do aplicativo Calendário.
 * @return lv_obj_t* Ponteiro para a tela (screen) criada.
 */
lv_obj_t *ui_calendar_create(void);

/**
 * @brief Atualiza as cores e estilos do app Calendário conforme o tema do sistema.
 */
void ui_calendar_refresh_theme(void);

/**
 * @brief Ajusta o layout do app Calendário ao alterar resolução ou rotação de tela.
 */
void ui_calendar_apply_layout(void);

/**
 * @brief Executado no ciclo de vida de abertura do app, sincronizando a data atual.
 */
void ui_calendar_on_open(void);

#ifdef __cplusplus
}
#endif
