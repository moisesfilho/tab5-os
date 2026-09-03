/**
 * @file tab5_wasm_runtime.h
 * @brief WebAssembly Micro Runtime (WAMR) Integration & App Sandboxing
 */

#pragma once

#include "include/tab5_sdk.h"
#include "tab5_host_abi.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_WASM_DEFAULT_STACK_SIZE (64 * 1024)
#define TAB5_WASM_DEFAULT_HEAP_SIZE (512 * 1024)

/**
 * @brief Descritor da instância de execução de um aplicativo Wasm.
 */
typedef struct {
    char app_id[64];
    tab5_app_context_t *host_ctx;
    void *module;      /**< wasm_module_t */
    void *module_inst; /**< wasm_module_inst_t */
    void *exec_env;    /**< wasm_exec_env_t */
    uint8_t *wasm_buf; /**< Buffer em PSRAM contendo o bytecode .wasm */
    size_t wasm_buf_size;
    bool is_running;
} tab5_wasm_app_instance_t;

/**
 * @brief Inicializa o motor WAMR e registra os símbolos da Host ABI.
 */
tab5_err_t tab5_wasm_runtime_init(void);

/**
 * @brief Carrega um módulo Wasm a partir de um buffer em memória.
 */
tab5_err_t tab5_wasm_load_from_bytes(const uint8_t *bytes, size_t size, uint32_t stack_size, uint32_t heap_size,
                                     tab5_app_context_t *ctx, tab5_wasm_app_instance_t *out_inst);

/**
 * @brief Carrega um aplicativo Wasm diretamente do arquivo no SD ou LittleFS.
 */
tab5_err_t tab5_wasm_load_from_file(const char *wasm_path, uint32_t stack_size, uint32_t heap_size,
                                    tab5_app_context_t *ctx, tab5_wasm_app_instance_t *out_inst);

/**
 * @brief Executa o ponto de entrada da aplicação (ex: "app_main" ou "main").
 */
tab5_err_t tab5_wasm_call_function(tab5_wasm_app_instance_t *inst, const char *func_name, uint32_t argc,
                                   uint32_t *argv);

/**
 * @brief Descarrega a aplicação Wasm e libera toda a memória PSRAM associada.
 */
tab5_err_t tab5_wasm_unload(tab5_wasm_app_instance_t *inst);

/**
 * @brief Encerra o subsistema WAMR.
 */
void tab5_wasm_runtime_destroy(void);

#ifdef __cplusplus
}
#endif
