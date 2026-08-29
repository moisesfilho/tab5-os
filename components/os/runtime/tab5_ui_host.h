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
tab5_err_t tab5_ui_host_keyboard_show(tab5_ui_obj_t target_textarea);

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
tab5_err_t tab5_ui_host_textarea_set_text(tab5_ui_obj_t ta, const char *text);

/**
 * @brief Obtém o texto atual de um widget textarea.
 */
const char *tab5_ui_host_textarea_get_text(tab5_ui_obj_t ta);

/**
 * @brief Define o texto de placeholder do textarea.
 */
tab5_err_t tab5_ui_host_textarea_set_placeholder(tab5_ui_obj_t ta, const char *placeholder);

/**
 * @brief Ajusta o layout do editor/conteúdo em relação ao teclado virtual.
 */
void tab5_ui_host_apply_layout(void);

#ifdef __cplusplus
}
#endif
