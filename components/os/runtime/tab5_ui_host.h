/**
 * @file tab5_ui_host.h
 * @brief UI Host Bindings & Shell Integration for Sandboxed Apps
 */

#pragma once

#include "include/tab5_sdk.h"
#include "tab5_host_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cria a tela base e a barra de título para uma aplicação isolada.
 */
tab5_err_t tab5_ui_host_create_app_screen(const char *app_name, tab5_app_context_t *ctx);

/**
 * @brief Destrói a tela e libera recursos visuais da aplicação.
 */
tab5_err_t tab5_ui_host_destroy_app_screen(tab5_app_context_t *ctx);

/**
 * @brief Define o foco do teclado virtual.
 */
tab5_err_t tab5_ui_host_keyboard_show(void *target_textarea);

/**
 * @brief Oculta o teclado virtual.
 */
tab5_err_t tab5_ui_host_keyboard_hide(void);

/**
 * @brief Verifica se o teclado virtual está aberto.
 */
bool tab5_ui_host_keyboard_is_visible(void);

/**
 * @brief Exibe um toast overlay na interface.
 */
tab5_err_t tab5_ui_host_show_toast(const char *message, uint32_t duration_ms);

/**
 * @brief Define o texto de um widget textarea.
 */
tab5_err_t tab5_ui_host_textarea_set_text(void *ta, const char *text);

/**
 * @brief Obtém o texto atual de um widget textarea.
 */
const char *tab5_ui_host_textarea_get_text(void *ta);

/**
 * @brief Define o texto de placeholder do textarea.
 */
tab5_err_t tab5_ui_host_textarea_set_placeholder(void *ta, const char *placeholder);

/**
 * @brief Define a posição do cursor no textarea.
 */
tab5_err_t tab5_ui_host_textarea_set_cursor_pos(void *ta, int32_t pos);

/**
 * @brief Obtém a posição do cursor no textarea.
 */
int32_t tab5_ui_host_textarea_get_cursor_pos(void *ta);

/**
 * @brief Ativa/desativa modo de senha no textarea.
 */
tab5_err_t tab5_ui_host_textarea_set_password_mode(void *ta, bool password_mode);

/**
 * @brief Ajusta o layout do editor/conteúdo em relação ao teclado virtual.
 */
void tab5_ui_host_apply_layout(void);

/**
 * @brief Atualiza o tema da interface ativa das aplicações isoladas.
 */
void tab5_ui_host_refresh_theme(void);

/**
 * @brief Notifica a aplicação isolada de que ela foi retomada (dump de start
 * de preview / varredura de diretório).
 */
void tab5_ui_host_resume_app(tab5_app_context_t *ctx);

/**
 * @brief Notifica a aplicação isolada da abertura de um arquivo compatível.
 */
void tab5_ui_host_open_file(tab5_app_context_t *ctx, const char *path);

/* ========================================================================= */
/* Manipulação de Handles de UI e Widgets Genéricos                          */
/* ========================================================================= */

/**
 * @brief Registra um lv_obj_t no subsistema de handles e retorna um tab5_ui_obj_t.
 */
tab5_ui_obj_t tab5_ui_host_register_obj(void *lv_obj);

/**
 * @brief Obtém o ponteiro nativo lv_obj_t* a partir de um handle.
 */
void *tab5_ui_host_get_lv_obj(tab5_ui_obj_t handle);

/**
 * @brief Limpa a tabela de handles de UI da aplicação.
 */
void tab5_ui_host_clear_handles(void);

/**
 * @brief Cria contêiner genérico.
 */
tab5_ui_obj_t tab5_ui_host_container_create(tab5_ui_obj_t parent_handle);

/**
 * @brief Define tamanho de objeto de UI.
 */
tab5_err_t tab5_ui_host_obj_set_size(tab5_ui_obj_t obj_handle, int32_t w, int32_t h);

/**
 * @brief Define alinhamento de objeto de UI.
 */
tab5_err_t tab5_ui_host_obj_set_align(tab5_ui_obj_t obj_handle, tab5_ui_align_t align, int32_t x_ofs, int32_t y_ofs);

/**
 * @brief Define fluxo flexível de contêiner.
 */
tab5_err_t tab5_ui_host_obj_set_flex_flow(tab5_ui_obj_t obj_handle, tab5_ui_flex_flow_t flow);

/**
 * @brief Define padding interno do contêiner.
 */
tab5_err_t tab5_ui_host_obj_set_pad(tab5_ui_obj_t obj_handle, int32_t pad_all);

/**
 * @brief Define espaçamento entre itens (gap).
 */
tab5_err_t tab5_ui_host_obj_set_gap(tab5_ui_obj_t obj_handle, int32_t gap);

/**
 * @brief Cria Label de texto.
 */
tab5_ui_obj_t tab5_ui_host_label_create(tab5_ui_obj_t parent_handle, const char *text);

/**
 * @brief Altera texto do Label.
 */
tab5_err_t tab5_ui_host_label_set_text(tab5_ui_obj_t obj_handle, const char *text);

/**
 * @brief Cria Botão interativo.
 */
tab5_ui_obj_t tab5_ui_host_btn_create(tab5_ui_obj_t parent_handle, const char *label_or_symbol);

/**
 * @brief Cria Switch booleano.
 */
tab5_ui_obj_t tab5_ui_host_switch_create(tab5_ui_obj_t parent_handle);

/**
 * @brief Define estado do Switch.
 */
tab5_err_t tab5_ui_host_switch_set_state(tab5_ui_obj_t obj_handle, bool checked);

/**
 * @brief Obtém estado do Switch.
 */
bool tab5_ui_host_switch_get_state(tab5_ui_obj_t obj_handle);

/**
 * @brief Cria Slider numérico.
 */
tab5_ui_obj_t tab5_ui_host_slider_create(tab5_ui_obj_t parent_handle, int32_t min, int32_t max);

/**
 * @brief Define valor do Slider.
 */
tab5_err_t tab5_ui_host_slider_set_value(tab5_ui_obj_t obj_handle, int32_t val);

/**
 * @brief Obtém valor do Slider.
 */
int32_t tab5_ui_host_slider_get_value(tab5_ui_obj_t obj_handle);

/**
 * @brief Cria Lista rolável.
 */
tab5_ui_obj_t tab5_ui_host_list_create(tab5_ui_obj_t parent_handle);

/**
 * @brief Adiciona botão/item na Lista.
 */
tab5_ui_obj_t tab5_ui_host_list_add_btn(tab5_ui_obj_t list_handle, const char *symbol, const char *text);

/**
 * @brief Limpa e desaloca todos os filhos de um objeto UI / Lista.
 */
tab5_err_t tab5_ui_host_obj_clean(tab5_ui_obj_t obj_handle);

/**
 * @brief Remove os widgets do app da tela raiz, preservando a app bar e a
 *        área de conteúdo padrão do host (usado na reconstrução de tema).
 */
tab5_err_t tab5_ui_host_clear_app_content(tab5_app_context_t *ctx);

/**
 * @brief Obtém uma cor da paleta do tema ativo.
 */
uint32_t tab5_ui_host_theme_get_color(uint32_t color_id);

/**
 * @brief Define cor de fundo e opacidade de um objeto UI.
 */
tab5_err_t tab5_ui_host_obj_set_style_bg(tab5_ui_obj_t obj_handle, uint32_t color_hex, uint8_t opa);

/**
 * @brief Define borda de um objeto UI.
 */
tab5_err_t tab5_ui_host_obj_set_style_border(tab5_ui_obj_t obj_handle, uint32_t border_hex, int32_t width);

/**
 * @brief Define cor e opacidade do texto de um objeto UI.
 */
tab5_err_t tab5_ui_host_obj_set_style_text_color(tab5_ui_obj_t obj_handle, uint32_t color_hex, uint8_t opa);

/**
 * @brief Define raio dos cantos de um objeto UI.
 */
tab5_err_t tab5_ui_host_obj_set_style_radius(tab5_ui_obj_t obj_handle, int32_t radius);

/**
 * @brief Define fator de crescimento no Flexbox.
 */
tab5_err_t tab5_ui_host_obj_set_flex_grow(tab5_ui_obj_t obj_handle, uint8_t grow);

/**
 * @brief Habilita ou desabilita cliques em um objeto UI.
 */
tab5_err_t tab5_ui_host_obj_set_clickable(tab5_ui_obj_t obj_handle, bool clickable);

#ifdef __cplusplus
}
#endif
