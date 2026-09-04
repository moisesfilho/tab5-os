/**
 * @file tab5_storage_sandbox.h
 * @brief Sistema de Sandbox de Armazenamento e Sanitização de Caminhos
 */

#pragma once

#include "include/tab5_sdk.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Valida e normaliza um caminho de arquivo respeitando as regras de sandbox.
 *
 * @param in_path Caminho relativo ou absoluto solicitado pela aplicação.
 * @param out_path Buffer que receberá o caminho absoluto canônico seguro.
 * @param out_size Tamanho do buffer de saída.
 * @param app_id Identificador da aplicação (ex: "com.tab5.notas").
 * @param permissions Máscara de permissões da aplicação.
 * @param write_access True se for operação de escrita; False para leitura.
 * @return TAB5_OK se válido; TAB5_ERR_ACCESS_DENIED ou TAB5_ERR_INVALID_ARG caso contrário.
 */
tab5_err_t tab5_storage_sandbox_resolve_path(const char *in_path, char *out_path, size_t out_size, const char *app_id,
                                             uint32_t permissions, bool write_access);

/**
 * @brief Cria um diretório de forma segura dentro da sandbox da aplicação.
 */
tab5_err_t tab5_storage_sandbox_mkdir(const char *rel_or_abs_path, const char *app_id, uint32_t permissions);

/**
 * @brief Remove um arquivo de forma segura dentro da sandbox da aplicação.
 */
tab5_err_t tab5_storage_sandbox_remove(const char *rel_or_abs_path, const char *app_id, uint32_t permissions);

/**
 * @brief Helper para obter o diretório raiz de dados da aplicação ("/sdcard/data/<app_id>").
 */
tab5_err_t tab5_storage_sandbox_get_app_dir(const char *app_id, char *out_buf, size_t buf_size);

/**
 * @brief Lista os arquivos e subdiretórios de forma segura respeitando a sandbox.
 */
tab5_err_t tab5_storage_sandbox_scandir(const char *rel_or_abs_path, tab5_dir_entry_t *entries, uint32_t max_entries,
                                        uint32_t *out_count, const char *app_id, uint32_t permissions);

#ifdef __cplusplus
}
#endif
