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

#ifdef __cplusplus
}
#endif
