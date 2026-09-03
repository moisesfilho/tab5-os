#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_FILES_VIEW_ICONS,
    UI_FILES_VIEW_LIST,
} ui_files_view_mode_t;

typedef struct ui_files_view_s ui_files_view_t;

/**
 * @brief Cria a visualização reutilizável do gerenciador de arquivos.
 * @param parent Tela pai LVGL onde o container de arquivos será inserido.
 * @param app_bar Barra de título do aplicativo (para botões de ação e título).
 * @return Ponteiro para a instância de ui_files_view_t criada.
 */
ui_files_view_t *ui_files_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);

/**
 * @brief Abre e carrega a listagem de um caminho específico (ex: /sdcard).
 * @param view Instância da visualização de arquivos.
 * @param path Caminho do diretório a ser listado.
 */
void ui_files_view_open_path(ui_files_view_t *view, const char *path);

/**
 * @brief Atualiza as cores do tema ativo (claro / escuro).
 * @param view Instância da visualização de arquivos.
 */
void ui_files_view_refresh_theme(ui_files_view_t *view);

/**
 * @brief Ajusta o layout (redimensionamento / rotação de tela).
 * @param view Instância da visualização de arquivos.
 */
void ui_files_view_apply_layout(ui_files_view_t *view);

/**
 * @brief Destrói os recursos alocados para a visualização.
 * @param view Instância da visualização de arquivos a ser destruída.
 */
void ui_files_view_destroy(ui_files_view_t *view);

#ifdef __cplusplus
}
#endif
