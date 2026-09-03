/**
 * @file ui_installer.h
 * @brief Interface Gráfica do Instalador de Aplicações Tab5 (.tab5pkg)
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa o instalador e registra a associação do tipo de arquivo .tab5pkg.
 */
void ui_installer_init(void);

/**
 * @brief Abre a tela/modal de confirmação de instalação para um pacote.
 * @param pkg_path Caminho completo do arquivo .tab5pkg ou diretório da app.
 */
void ui_installer_open(const char *pkg_path);

/**
 * @brief Fecha a tela/modal de instalação.
 */
void ui_installer_close(void);

#ifdef __cplusplus
}
#endif
