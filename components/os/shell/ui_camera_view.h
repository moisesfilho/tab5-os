#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_camera_view_s ui_camera_view_t;

/**
 * @brief Cria a visualização reutilizável da câmera (preview ao vivo + disparo).
 * @param parent Tela pai LVGL onde a câmera será inserida.
 * @param app_bar Barra de título do aplicativo.
 * @return Ponteiro para a instância de ui_camera_view_t criada.
 */
ui_camera_view_t *ui_camera_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);

/**
 * @brief Atualiza as cores do tema ativo (claro / escuro).
 * @param view Instância da visualização da câmera.
 */
void ui_camera_view_refresh_theme(ui_camera_view_t *view);

/**
 * @brief Ajusta o layout (redimensionamento / rotação de tela).
 * @param view Instância da visualização da câmera.
 */
void ui_camera_view_apply_layout(ui_camera_view_t *view);

/**
 * @brief Inicia o preview ao vivo da câmera.
 * @param view Instância da visualização da câmera.
 */
void ui_camera_view_start(ui_camera_view_t *view);

/**
 * @brief Para o preview ao vivo da câmera.
 * @param view Instância da visualização da câmera.
 */
void ui_camera_view_stop(ui_camera_view_t *view);

/**
 * @brief Destrói os recursos alocados para a visualização.
 * @param view Instância da visualização da câmera a ser destruída.
 */
void ui_camera_view_destroy(ui_camera_view_t *view);

#ifdef __cplusplus
}
#endif
