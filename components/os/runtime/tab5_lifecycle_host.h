/**
 * @file tab5_lifecycle_host.h
 * @brief Gerenciador de Ciclo de Vida de Aplicações Isoladas no Host
 */

#pragma once

#include "include/tab5_sdk.h"
#include "tab5_host_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa e dispara o evento on_init da aplicação.
 */
tab5_err_t tab5_lifecycle_host_init_app(tab5_app_context_t *ctx);

/**
 * @brief Transiciona a aplicação para o estado ativo/visível (on_resume).
 */
tab5_err_t tab5_lifecycle_host_resume_app(tab5_app_context_t *ctx);

/**
 * @brief Pausa a execução da aplicação (on_pause).
 */
tab5_err_t tab5_lifecycle_host_pause_app(tab5_app_context_t *ctx);

/**
 * @brief Notifica abertura de arquivo compatível (on_open_file).
 */
tab5_err_t tab5_lifecycle_host_open_file(tab5_app_context_t *ctx, const char *filepath);

/**
 * @brief Encerra a aplicação, executa on_destroy e libera recursos visuais.
 */
tab5_err_t tab5_lifecycle_host_destroy_app(tab5_app_context_t *ctx);

#ifdef __cplusplus
}
#endif
