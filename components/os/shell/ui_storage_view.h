/**
 * @file ui_storage_view.h
 * @brief Interface Gráfica de Gerenciamento de Armazenamento e Memória
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registra a aplicação de Gerenciamento de Armazenamento no registry do sistema.
 */
void ui_storage_view_register(void);

/**
 * @brief Cria a tela de gerenciamento de armazenamento e memória.
 */
lv_obj_t *ui_storage_view_create(void);

/**
 * @brief Executado quando a tela é aberta no shell (atualiza dados de disco e memória).
 */
void ui_storage_view_on_open(void);

/**
 * @brief Reaplica o tema visual (Dark/Light).
 */
void ui_storage_view_refresh_theme(void);

/**
 * @brief Atualiza posicionamento e dimensões da tela de acordo com a resolução.
 */
void ui_storage_view_apply_layout(void);

#ifdef __cplusplus
}
#endif
