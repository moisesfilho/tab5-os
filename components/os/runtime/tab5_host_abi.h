/**
 * @file tab5_host_abi.h
 * @brief Tab5 OS Native Host ABI & Symbols Registry
 *
 * Expõe as funções nativas do host para o motor WebAssembly (WAMR) ou chamadas
 * C/C++ de aplicações isoladas.
 */

#pragma once

#include "include/tab5_sdk.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Definição de símbolo nativo compatível com a tabela WAMR NativeSymbol.
 */
typedef struct {
    const char *symbol_name; /**< Nome da função exportada para o ambiente Wasm */
    void *func_ptr;          /**< Ponteiro de função nativa C no Host */
    const char *signature;   /**< Assinatura de tipos WAMR (ex: "($)i", "(*)i") */
    void *attachment;        /**< Ponteiro opcional de attachment (compatível com NativeSymbol) */
} tab5_native_symbol_t;

/**
 * @brief Permissões atribuídas à aplicação no momento da execução.
 */
typedef enum {
    TAB5_PERM_NONE = 0,
    TAB5_PERM_STORAGE_READ = (1 << 0),  /**< Leitura no SD fora da sandbox (/sdcard/) */
    TAB5_PERM_STORAGE_WRITE = (1 << 1), /**< Escrita no SD fora da sandbox */
    TAB5_PERM_UI_KEYBOARD = (1 << 2),   /**< Uso do teclado virtual */
    TAB5_PERM_NETWORK = (1 << 3),       /**< Acesso a Wi-Fi / Rede */
    TAB5_PERM_BLUETOOTH = (1 << 4),     /**< Acesso a Bluetooth */
    TAB5_PERM_AUDIO = (1 << 5),         /**< Reprodução e gravação de áudio */
    TAB5_PERM_ALL = 0xFFFF
} tab5_permission_flags_t;

/**
 * @brief Estrutura de contexto do processo/app em execução no Host.
 */
typedef struct {
    char app_id[64];                      /**< Identificador único (ex: "com.tab5.notas") */
    char app_name[64];                    /**< Nome da aplicação */
    uint32_t permissions;                 /**< Máscara de bits de tab5_permission_flags_t */
    tab5_app_state_t state;               /**< Estado atual do ciclo de vida */
    void *root_screen;                    /**< lv_obj_t* da tela da app */
    void *app_bar;                        /**< Handle da barra de título padrão */
    void *app_bar_handle;                 /**< Ponteiro para ui_app_bar_t (replice de tema) */
    void *content_area;                   /**< Handle do widget de edição/conteúdo principal (textarea) */
    tab5_lifecycle_callbacks_t lifecycle; /**< Callbacks registrados pela app */
    bool is_wasm;                         /**< Indica se o contexto e de app WebAssembly */
    void *wasm_instance;                  /**< Ponteiro para tab5_wasm_app_instance_t */
    void *user_data;                      /**< Ponteiro genérico do runtime/instância */
} tab5_app_context_t;

/**
 * @brief Inicializa a infraestrutura de Host ABI.
 */
tab5_err_t tab5_host_abi_init(void);

/**
 * @brief Retorna o array de símbolos nativos exportados.
 * @param out_count Ponteiro para receber a quantidade de símbolos no array.
 * @return Ponteiro constante para o array de tab5_native_symbol_t.
 */
const tab5_native_symbol_t *tab5_host_abi_get_symbols(uint32_t *out_count);

/**
 * @brief Define o contexto da aplicação atualmente ativa no Host.
 */
tab5_err_t tab5_host_set_active_app(tab5_app_context_t *ctx);

/**
 * @brief Retorna o contexto da aplicação atualmente ativa.
 */
tab5_app_context_t *tab5_host_get_active_app(void);

/**
 * @brief Limpa o contexto da aplicação ativa.
 */
void tab5_host_clear_active_app(void);

/**
 * @brief Verifica se a aplicação ativa possui uma determinada permissão.
 */
bool tab5_host_has_permission(uint32_t permission_flag);

#ifdef __cplusplus
}
#endif
