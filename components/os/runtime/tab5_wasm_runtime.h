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
    uint32_t call_depth;        /**< Chamadas WAMR ativas nesta instância. */
    bool unload_pending;        /**< Teardown solicitado durante uma chamada. */
    uint32_t generation;        /**< Época; invalida jobs postados antes do unload. */
    void *state_mutex;          /**< mutex privado; protege lifecycle e call_depth */
    void *state_cond;           /**< condvar privado; acorda o teardown quando callers saem */
    uint32_t active_admissions; /**< threads que possuem uma admissão real */
} tab5_wasm_app_instance_t;

/**
 * @brief Seleciona o primeiro entrypoint exportado suportado pela app.
 *
 * A consulta usa apenas a tabela de exports do módulo: não cria pthread nem
 * executa bytecode. A ordem é app_main, depois main; _start nunca é aceito.
 * O ponteiro retornado aponta para uma string estática do runtime.
 */
tab5_err_t tab5_wasm_select_entrypoint(tab5_wasm_app_instance_t *inst, const char **out_func_name);

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
 * @brief Chama uma função Wasm que recebe uma string UTF-8 como argumento.
 *
 * A string é copiada para a memória do módulo e liberada antes do retorno,
 * inclusive quando a chamada falha.
 */
tab5_err_t tab5_wasm_call_string_function(tab5_wasm_app_instance_t *inst, const char *func_name, const char *value);

/**
 * @brief Descarrega a aplicação Wasm e libera toda a memória PSRAM associada.
 */
tab5_err_t tab5_wasm_unload(tab5_wasm_app_instance_t *inst);

/**
 * @brief Encerra o subsistema WAMR.
 */
void tab5_wasm_runtime_destroy(void);

/* Lifecycle queries are the only supported way for other subsystems to inspect
 * an instance.  They take the instance lock and therefore are safe while a
 * dispatcher worker is racing with unload. */
bool tab5_wasm_instance_snapshot(const tab5_wasm_app_instance_t *inst, bool *out_running, uint32_t *out_generation);
bool tab5_wasm_instance_is_running(const tab5_wasm_app_instance_t *inst);
uint32_t tab5_wasm_instance_call_depth(const tab5_wasm_app_instance_t *inst);
bool tab5_wasm_instance_unload_pending(const tab5_wasm_app_instance_t *inst);

#ifdef __cplusplus
}
#endif
