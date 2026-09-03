#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_gallery_view_s ui_gallery_view_t;

/**
 * @brief Cria a visualização reutilizável da galeria de fotos.
 * @param parent Tela pai LVGL onde a galeria será inserida.
 * @param app_bar Barra de título do aplicativo.
 * @return Ponteiro para a instância de ui_gallery_view_t criada.
 */
ui_gallery_view_t *ui_gallery_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);

/**
 * @brief Carrega e exibe as fotos do diretório padrão (/sdcard/imagens).
 * @param view Instância da visualização da galeria.
 */
void ui_gallery_view_start(ui_gallery_view_t *view);

/**
 * @brief Abre um arquivo de imagem específico na galeria.
 * @param view Instância da visualização da galeria.
 * @param path Caminho da imagem a ser exibida (ou nullptr para o diretório padrão).
 */
void ui_gallery_view_open_file(ui_gallery_view_t *view, const char *path);

/**
 * @brief Atualiza as cores do tema ativo (claro / escuro).
 * @param view Instância da visualização da galeria.
 */
void ui_gallery_view_refresh_theme(ui_gallery_view_t *view);

/**
 * @brief Ajusta o layout (redimensionamento / rotação de tela).
 * @param view Instância da visualização da galeria.
 */
void ui_gallery_view_apply_layout(ui_gallery_view_t *view);

/**
 * @brief Destrói os recursos alocados para a visualização.
 * @param view Instância da visualização da galeria a ser destruída.
 */
void ui_gallery_view_destroy(ui_gallery_view_t *view);

#ifdef __cplusplus
}
#endif
