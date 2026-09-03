/**
 * @file tab5_sdk.h
 * @brief Tab5 OS Application SDK & Native Host ABI Definitions
 *
 * Header público para desenvolvimento de aplicações isoladas e desacopladas
 * no Tab5 OS (compiladas para WebAssembly/C).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Export macro for Wasm/Native modules */
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
#define TAB5_APP_EXPORT __attribute__((visibility("default")))
#else
#define TAB5_APP_EXPORT
#endif

/* ========================================================================= */
/* Códigos de Retorno / Erros                                                */
/* ========================================================================= */
typedef enum {
    TAB5_OK = 0,
    TAB5_ERR_FAIL = -1,
    TAB5_ERR_INVALID_ARG = -2,
    TAB5_ERR_NO_MEM = -3,
    TAB5_ERR_NOT_FOUND = -4,
    TAB5_ERR_ACCESS_DENIED = -5,
    TAB5_ERR_NOT_SUPPORTED = -6,
    TAB5_ERR_INVALID_STATE = -7,
    TAB5_ERR_TIMEOUT = -8,
    TAB5_ERR_BUFFER_OVERFLOW = -9
} tab5_err_t;

/* ========================================================================= */
/* Tipos Opostos e Handles                                                   */
/* ========================================================================= */
typedef void *tab5_ui_obj_t;

/* ========================================================================= */
/* Ciclo de Vida da Aplicação                                                */
/* ========================================================================= */
typedef enum {
    TAB5_APP_STATE_UNINITIALIZED = 0,
    TAB5_APP_STATE_INITIALIZED = 1,
    TAB5_APP_STATE_RESUMED = 2,
    TAB5_APP_STATE_PAUSED = 3,
    TAB5_APP_STATE_DESTROYED = 4
} tab5_app_state_t;

typedef void (*tab5_app_init_cb_t)(void);
typedef void (*tab5_app_resume_cb_t)(void);
typedef void (*tab5_app_pause_cb_t)(void);
typedef void (*tab5_app_destroy_cb_t)(void);
typedef void (*tab5_app_open_file_cb_t)(const char *filepath);

typedef struct {
    tab5_app_init_cb_t on_init;
    tab5_app_resume_cb_t on_resume;
    tab5_app_pause_cb_t on_pause;
    tab5_app_destroy_cb_t on_destroy;
    tab5_app_open_file_cb_t on_open_file;
} tab5_lifecycle_callbacks_t;

/**
 * @brief Registra os callbacks de ciclo de vida da aplicação ativa.
 */
tab5_err_t tab5_lifecycle_register(const tab5_lifecycle_callbacks_t *cbs);

/* ========================================================================= */
/* UI / LVGL Host Bindings                                                   */
/* ========================================================================= */

/**
 * @brief Retorna o contêiner raiz da tela da aplicação (lv_obj_t*).
 */
tab5_ui_obj_t tab5_ui_get_screen(void);

/**
 * @brief Define o título da aplicação na barra superior padrão.
 */
tab5_err_t tab5_ui_app_bar_set_title(const char *title);

/**
 * @brief Adiciona um botão de ação na barra superior da aplicação.
 * @param symbol_or_text Símbolo LVGL ou texto curto (ex: LV_SYMBOL_SAVE).
 * @param on_click Callback executado ao clicar.
 * @param user_data Ponteiro de dados de usuário.
 * @return Handle para o botão criado (lv_obj_t*).
 */
tab5_ui_obj_t tab5_ui_app_bar_add_action_button(const char *symbol_or_text, void (*on_click)(void *user_data),
                                                void *user_data);

/**
 * @brief Retorna o widget de textarea principal/editor da aplicação.
 */
tab5_ui_obj_t tab5_ui_get_main_textarea(void);

/**
 * @brief Define o texto de um widget textarea.
 */
tab5_err_t tab5_ui_textarea_set_text(tab5_ui_obj_t ta, const char *text);

/**
 * @brief Obtém o texto atual de um widget textarea.
 */
const char *tab5_ui_textarea_get_text(tab5_ui_obj_t ta);

/**
 * @brief Define o texto de placeholder do textarea.
 */
tab5_err_t tab5_ui_textarea_set_placeholder(tab5_ui_obj_t ta, const char *placeholder);

/**
 * @brief Exibe o teclado virtual na tela associado a um textarea.
 */
tab5_err_t tab5_ui_keyboard_show(tab5_ui_obj_t target_textarea);

/**
 * @brief Oculta o teclado virtual da tela.
 */
tab5_err_t tab5_ui_keyboard_hide(void);

/**
 * @brief Retorna se o teclado virtual está atualmente visível.
 */
bool tab5_ui_keyboard_is_visible(void);

/**
 * @brief Exibe uma notificação rápida (toast) na interface do usuário.
 * @param message Mensagem a ser exibida.
 * @param duration_ms Duração em milissegundos.
 */
tab5_err_t tab5_ui_show_toast(const char *message, uint32_t duration_ms);

/* ========================================================================= */
/* Storage Sandbox & I/O                                                     */
/* ========================================================================= */

/**
 * @brief Retorna o caminho absoluto do diretório de dados isolado da aplicação.
 * Formato padrão: "/sdcard/data/<app_id>"
 */
tab5_err_t tab5_storage_get_app_dir(char *out_buf, size_t buf_size);

/**
 * @brief Resolve um caminho relativo ou absoluto dentro da sandbox da aplicação.
 *
 * @param in_path Caminho de entrada (relativo ao diretório da app ou absoluto).
 * @param out_path Buffer de saída que receberá o caminho absoluto validado.
 * @param out_size Tamanho do buffer de saída.
 * @param write_access True se a operação for de escrita (mais restritiva).
 * @return TAB5_OK se o caminho for seguro e permitido; TAB5_ERR_ACCESS_DENIED caso contrário.
 */
tab5_err_t tab5_storage_path_resolve(const char *in_path, char *out_path, size_t out_size, bool write_access);

/**
 * @brief Cria um diretório de forma segura dentro da sandbox.
 */
tab5_err_t tab5_storage_mkdir(const char *rel_or_abs_path);

/**
 * @brief Remove um arquivo de forma segura dentro da sandbox.
 */
tab5_err_t tab5_storage_remove(const char *rel_or_abs_path);

/* ========================================================================= */
/* Hardware, Sistema & Notificações                                          */
/* ========================================================================= */

typedef struct {
    int percent;         /**< Nível da bateria de 0 a 100 */
    bool is_charging;    /**< True se conectado ao carregador */
    bool is_present;     /**< True se a bateria foi detectada */
    uint32_t voltage_mv; /**< Tensao em millivolts */
} tab5_battery_info_t;

typedef struct {
    bool is_connected; /**< True se conectado a uma rede Wi-Fi */
    char ssid[33];     /**< Nome da rede Wi-Fi conectada */
    char ip_addr[16];  /**< Endereço IPv4 em formato texto */
    int8_t rssi;       /**< Intensidade de sinal em dBm */
} tab5_wifi_info_t;

typedef struct {
    bool is_enabled;       /**< True se o controlador Bluetooth está ativo */
    int connected_devices; /**< Quantidade de dispositivos pareados/conectados */
} tab5_bt_info_t;

/**
 * @brief Obtém o estado atual da bateria.
 */
tab5_err_t tab5_system_get_battery(tab5_battery_info_t *out_info);

/**
 * @brief Obtém o status atual do subsistema Wi-Fi.
 */
tab5_err_t tab5_system_get_wifi_status(tab5_wifi_info_t *out_info);

/**
 * @brief Obtém o status atual do subsistema Bluetooth.
 */
tab5_err_t tab5_system_get_bt_status(tab5_bt_info_t *out_info);

/**
 * @brief Obtém a data e hora do sistema (RTC / NTP).
 * @param out_epoch_ms Ponteiro opcional para receber timestamp em milissegundos.
 * @param out_time Ponteiro opcional para struct tm preenchida.
 */
tab5_err_t tab5_system_get_time(int64_t *out_epoch_ms, struct tm *out_time);

/**
 * @brief Emite um bipe sonoro pelo alto-falante integrado do Tab5.
 * @param freq_hz Frequência em Hertz (ex: 1000 Hz).
 * @param duration_ms Duração em milissegundos.
 */
tab5_err_t tab5_sound_play_beep(uint32_t freq_hz, uint32_t duration_ms);

/**
 * @brief Registra uma mensagem de log no console/monitor do sistema.
 * @param level Nível de log (0=Error, 1=Warn, 2=Info, 3=Debug).
 * @param tag Tag identificadora do módulo.
 * @param message Mensagem de texto.
 */
void tab5_system_log(int level, const char *tag, const char *message);

#ifdef __cplusplus
}
#endif
