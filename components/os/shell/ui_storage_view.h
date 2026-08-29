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
 * @brief Inicializa e registra a app de Armazenamento no registry do sistema.
 */
void ui_storage_view_init(void);

/**
 * @brief Abre a tela de gerenciamento de armazenamento e memória.
 */
void ui_storage_view_open(void);

/**
 * @brief Fecha a tela de armazenamento.
 */
void ui_storage_view_close(void);

#ifdef __cplusplus
}
#endif
