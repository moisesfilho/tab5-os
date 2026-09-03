/**
 * @file tab5_manifest.h
 * @brief Definições de Estrutura do Manifesto de Aplicações Tab5 OS
 */

#pragma once

#include "tab5_sdk.h"
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

#ifdef __cplusplus
}
#endif
