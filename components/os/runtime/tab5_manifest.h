/**
 * @file tab5_manifest.h
 * @brief Parser and Validator for Tab5 Manifests (manifest.json)
 */

#pragma once

#include "include/tab5_sdk.h"
#include "tab5_host_abi.h"
#include "tab5_wasm_runtime.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_MANIFEST_MAX_FILE_ASSOC 8
#define TAB5_MANIFEST_EXT_LEN 16

typedef struct {
    char id[64];            /**< Identificador único (ex: "com.tab5.notas") */
    char name[64];          /**< Nome amigável (ex: "Notas") */
    char version[32];       /**< Versão semântica (ex: "1.0.0") */
    char author[64];        /**< Nome do criador/author */
    char description[256];  /**< Descrição da aplicação */
    char entry[64];         /**< Arquivo de entrada WASM (padrão: "app.wasm") */
    char icon_symbol[32];   /**< Símbolo LVGL (ex: "LV_SYMBOL_EDIT", ">_") */
    char icon_bg_color[16]; /**< Cor de fundo em hex (ex: "#2196F3") */
    char file_associations[TAB5_MANIFEST_MAX_FILE_ASSOC][TAB5_MANIFEST_EXT_LEN]; /**< Extensões suportadas */
    int file_assoc_count; /**< Quantidade de extensões associadas */
    uint32_t permissions; /**< Máscara de bits tab5_permission_flags_t */
    uint32_t stack_size;  /**< Tamanho de stack em bytes */
    uint32_t heap_size;   /**< Tamanho de heap inicial em bytes */
} tab5_manifest_t;

/**
 * @brief Faz o parse de uma string JSON contendo o manifest.json.
 */
tab5_err_t tab5_manifest_parse_json(const char *json_str, tab5_manifest_t *out_manifest);

/**
 * @brief Carrega e faz o parse do manifest.json a partir de um arquivo em disco.
 */
tab5_err_t tab5_manifest_load_from_file(const char *filepath, tab5_manifest_t *out_manifest);

/**
 * @brief Valida se os campos obrigatórios do manifesto estão preenchidos e corretos.
 */
bool tab5_manifest_is_valid(const tab5_manifest_t *manifest);

/**
 * @brief Compara duas versões semânticas (ex: "1.2.0" vs "1.0.0").
 * @return > 0 se v1 > v2, 0 se v1 == v2, < 0 se v1 < v2.
 */
int tab5_manifest_version_compare(const char *v1, const char *v2);

#ifdef __cplusplus
}
#endif
