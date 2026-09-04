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
/* Handles de Objetos de UI                                                  */
/* ========================================================================= */
typedef uint32_t tab5_ui_obj_t;
#define TAB5_UI_INVALID_OBJ 0

/* Helpers para dimensões especiais */
#define TAB5_UI_SIZE_CONTENT (-1)
#define TAB5_UI_PCT(percent) (-1000 - (percent))

/* ========================================================================= */
/* Símbolos e Ícones (Font Awesome UTF-8)                                   */
/* ========================================================================= */
#ifndef LV_SYMBOL_AUDIO
#define LV_SYMBOL_AUDIO "\xef\x80\xa8" /* 0xF028 */
#endif
#ifndef LV_SYMBOL_PLAY
#define LV_SYMBOL_PLAY "\xef\x81\x8b" /* 0xF04B */
#endif
#ifndef LV_SYMBOL_PAUSE
#define LV_SYMBOL_PAUSE "\xef\x81\x8c" /* 0xF04C */
#endif
#ifndef LV_SYMBOL_STOP
#define LV_SYMBOL_STOP "\xef\x81\x8d" /* 0xF04D */
#endif
#ifndef LV_SYMBOL_POWER
#define LV_SYMBOL_POWER "\xef\x80\x91" /* 0xF011 */
#endif
#ifndef LV_SYMBOL_OK
#define LV_SYMBOL_OK "\xef\x80\x8c" /* 0xF00C */
#endif
#ifndef LV_SYMBOL_CLOSE
#define LV_SYMBOL_CLOSE "\xef\x80\x8d" /* 0xF00D */
#endif
#ifndef LV_SYMBOL_REFRESH
#define LV_SYMBOL_REFRESH "\xef\x80\xa1" /* 0xF021 */
#endif
#ifndef LV_SYMBOL_TRASH
#define LV_SYMBOL_TRASH "\xef\x80\x94" /* 0xF014 */
#endif
#ifndef LV_SYMBOL_PLUS
#define LV_SYMBOL_PLUS "\xef\x81\xa7" /* 0xF067 */
#endif
#ifndef LV_SYMBOL_MINUS
#define LV_SYMBOL_MINUS "\xef\x81\xa8" /* 0xF068 */
#endif
#ifndef LV_SYMBOL_SAVE
#define LV_SYMBOL_SAVE "\xef\x83\x87" /* 0xF0C7 */
#endif
#ifndef LV_SYMBOL_SETTINGS
#define LV_SYMBOL_SETTINGS "\xef\x80\x93" /* 0xF013 */
#endif
#ifndef LV_SYMBOL_WIFI
#define LV_SYMBOL_WIFI "\xef\x87\xab" /* 0xF1EB */
#endif
#ifndef LV_SYMBOL_BLUETOOTH
#define LV_SYMBOL_BLUETOOTH "\xef\x8a\x93" /* 0xF293 */
#endif
#ifndef LV_SYMBOL_FILE
#define LV_SYMBOL_FILE "\xef\x85\x9b" /* 0xF15B */
#endif
#ifndef LV_SYMBOL_IMAGE
#define LV_SYMBOL_IMAGE "\xef\x80\xbe" /* 0xF03E */
#endif
#ifndef LV_SYMBOL_LEFT
#define LV_SYMBOL_LEFT "\xef\x81\x93" /* 0xF053 */
#endif
#ifndef LV_SYMBOL_RIGHT
#define LV_SYMBOL_RIGHT "\xef\x81\x94" /* 0xF054 */
#endif
#ifndef LV_SYMBOL_DIRECTORY
#define LV_SYMBOL_DIRECTORY "\xef\x81\xbb" /* 0xF07B */
#endif
#ifndef LV_SYMBOL_BULLET
#define LV_SYMBOL_BULLET "\xef\x84\x91" /* 0xF111 */
#endif
#ifndef LV_SYMBOL_KEYBOARD
#define LV_SYMBOL_KEYBOARD "\xef\x84\x9c" /* 0xF11C */
#endif
#ifndef LV_SYMBOL_EYE_OPEN
#define LV_SYMBOL_EYE_OPEN "\xef\x81\xae" /* 0xF06E */
#endif
#ifndef LV_SYMBOL_EYE_CLOSE
#define LV_SYMBOL_EYE_CLOSE "\xef\x81\xb0" /* 0xF070 */
#endif
#ifndef LV_SYMBOL_SHUFFLE
#define LV_SYMBOL_SHUFFLE "\xef\x81\xb4" /* 0xF074 */
#endif
#ifndef LV_SYMBOL_LOOP
#define LV_SYMBOL_LOOP "\xef\x80\x9e" /* 0xF01E */
#endif
#ifndef LV_SYMBOL_PREV
#define LV_SYMBOL_PREV "\xef\x81\x88" /* 0xF048 */
#endif
#ifndef LV_SYMBOL_NEXT
#define LV_SYMBOL_NEXT "\xef\x81\x91" /* 0xF051 */
#endif
#ifndef LV_SYMBOL_VOLUME_MAX
#define LV_SYMBOL_VOLUME_MAX "\xef\x80\xa8" /* 0xF028 */
#endif
#ifndef LV_SYMBOL_VOLUME_MID
#define LV_SYMBOL_VOLUME_MID "\xef\x80\xa7" /* 0xF027 */
#endif
#ifndef LV_SYMBOL_MUTE
#define LV_SYMBOL_MUTE "\xef\x80\xa6" /* 0xF026 */
#endif

typedef enum {
    TAB5_UI_ALIGN_DEFAULT = 0,
    TAB5_UI_ALIGN_TOP_LEFT,
    TAB5_UI_ALIGN_TOP_MID,
    TAB5_UI_ALIGN_TOP_RIGHT,
    TAB5_UI_ALIGN_BOTTOM_LEFT,
    TAB5_UI_ALIGN_BOTTOM_MID,
    TAB5_UI_ALIGN_BOTTOM_RIGHT,
    TAB5_UI_ALIGN_LEFT_MID,
    TAB5_UI_ALIGN_RIGHT_MID,
    TAB5_UI_ALIGN_CENTER
} tab5_ui_align_t;

typedef enum {
    TAB5_UI_FLEX_FLOW_ROW = 0,
    TAB5_UI_FLEX_FLOW_COLUMN,
    TAB5_UI_FLEX_FLOW_ROW_WRAP,
    TAB5_UI_FLEX_FLOW_COLUMN_WRAP
} tab5_ui_flex_flow_t;

typedef enum {
    TAB5_UI_EVENT_CLICKED = 1,
    TAB5_UI_EVENT_VALUE_CHANGED = 2,
    TAB5_UI_EVENT_LONG_PRESSED = 3,
    TAB5_UI_EVENT_FOCUSED = 4,
    TAB5_UI_EVENT_DEFOCUSED = 5
} tab5_ui_event_type_t;

typedef void (*tab5_ui_event_cb_t)(tab5_ui_obj_t obj, uint32_t event_type, int32_t event_val);

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
typedef void (*tab5_app_open_file_cb_t)(const char *path);

typedef struct {
    tab5_app_init_cb_t on_init;
    tab5_app_resume_cb_t on_resume;
    tab5_app_pause_cb_t on_pause;
    tab5_app_destroy_cb_t on_destroy;
    tab5_app_open_file_cb_t on_open_file;
    tab5_ui_event_cb_t on_ui_event;
} tab5_lifecycle_callbacks_t;

/**
 * @brief Registra os callbacks de ciclo de vida da aplicação ativa.
 */
tab5_err_t tab5_lifecycle_register(const tab5_lifecycle_callbacks_t *cbs);

/* ========================================================================= */
/* UI / LVGL Host Bindings - Janela e Barra Superior                         */
/* ========================================================================= */

/**
 * @brief Retorna o contêiner raiz da tela da aplicação (handle).
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
 * @return Handle para o botão criado.
 */
tab5_ui_obj_t tab5_ui_app_bar_add_action_button(const char *symbol_or_text, void (*on_click)(void *user_data),
                                                void *user_data);

/**
 * @brief Retorna o widget de textarea principal/editor da aplicação (para apps de texto).
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

#define TAB5_UI_CURSOR_LAST 32767

/**
 * @brief Define a posição do cursor no textarea (use TAB5_UI_CURSOR_LAST para o fim).
 */
tab5_err_t tab5_ui_textarea_set_cursor_pos(tab5_ui_obj_t ta, int32_t pos);

/**
 * @brief Obtém a posição atual do cursor no textarea.
 */
int32_t tab5_ui_textarea_get_cursor_pos(tab5_ui_obj_t ta);

/**
 * @brief Ativa ou desativa o modo de ocultação de senha em um campo de texto.
 */
tab5_err_t tab5_ui_textarea_set_password_mode(tab5_ui_obj_t ta, bool password_mode);

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
/* UI / LVGL Host Bindings - Widgets e Layouts Genéricos                     */
/* ========================================================================= */

/**
 * @brief Cria um contêiner genérico para agrupamento de widgets.
 */
tab5_ui_obj_t tab5_ui_container_create(tab5_ui_obj_t parent);

/**
 * @brief Define largura e altura de um objeto UI.
 */
tab5_err_t tab5_ui_obj_set_size(tab5_ui_obj_t obj, int32_t w, int32_t h);

/**
 * @brief Define o alinhamento de um objeto em relação ao seu pai.
 */
tab5_err_t tab5_ui_obj_set_align(tab5_ui_obj_t obj, tab5_ui_align_t align, int32_t x_ofs, int32_t y_ofs);

/**
 * @brief Configura o fluxo flexível (Flexbox) do contêiner.
 */
tab5_err_t tab5_ui_obj_set_flex_flow(tab5_ui_obj_t obj, tab5_ui_flex_flow_t flow);

/**
 * @brief Define o espaçamento interno (padding) do contêiner.
 */
tab5_err_t tab5_ui_obj_set_pad(tab5_ui_obj_t obj, int32_t pad_all);

/**
 * @brief Define o espaçamento entre itens (gap/pad_row e pad_column).
 */
tab5_err_t tab5_ui_obj_set_gap(tab5_ui_obj_t obj, int32_t gap);

/**
 * @brief Cria um widget de texto (Label).
 */
tab5_ui_obj_t tab5_ui_label_create(tab5_ui_obj_t parent, const char *text);

/**
 * @brief Altera o texto de um Label existente.
 */
tab5_err_t tab5_ui_label_set_text(tab5_ui_obj_t obj, const char *text);

/**
 * @brief Cria um botão interativo padrão.
 */
tab5_ui_obj_t tab5_ui_btn_create(tab5_ui_obj_t parent, const char *label_or_symbol);

/**
 * @brief Cria um switch de alternância booleana (ligado/desligado).
 */
tab5_ui_obj_t tab5_ui_switch_create(tab5_ui_obj_t parent);

/**
 * @brief Define o estado de um switch.
 */
tab5_err_t tab5_ui_switch_set_state(tab5_ui_obj_t obj, bool checked);

/**
 * @brief Obtém o estado atual de um switch.
 */
bool tab5_ui_switch_get_state(tab5_ui_obj_t obj);

/**
 * @brief Cria um slider / controle deslizante de valor numérico.
 */
tab5_ui_obj_t tab5_ui_slider_create(tab5_ui_obj_t parent, int32_t min, int32_t max);

/**
 * @brief Define o valor numérico de um slider.
 */
tab5_err_t tab5_ui_slider_set_value(tab5_ui_obj_t obj, int32_t val);

/**
 * @brief Obtém o valor numérico atual de um slider.
 */
int32_t tab5_ui_slider_get_value(tab5_ui_obj_t obj);

/**
 * @brief Cria uma lista rolável de itens.
 */
tab5_ui_obj_t tab5_ui_list_create(tab5_ui_obj_t parent);

/**
 * @brief Adiciona um item de botão à lista.
 */
tab5_ui_obj_t tab5_ui_list_add_btn(tab5_ui_obj_t list, const char *symbol, const char *text);

/**
 * @brief Remove e desaloca todos os filhos de um objeto ou contêiner/lista.
 */
tab5_err_t tab5_ui_obj_clean(tab5_ui_obj_t obj);

/**
 * @brief Remove todos os widgets criados pelo app da tela raiz, preservando a
 *        barra de título (app bar) e a área de conteúdo padrão do host.
 * @return TAB5_OK em caso de sucesso.
 * @note Usado para reconstruir a UI (ex.: na troca de tema).
 */
tab5_err_t tab5_ui_clear_content(void);

typedef enum {
    TAB5_UI_COLOR_PRIMARY = 0,
    TAB5_UI_COLOR_ACCENT,
    TAB5_UI_COLOR_ACCENT_SOFT,
    TAB5_UI_COLOR_SURFACE,
    TAB5_UI_COLOR_SURFACE_ALT,
    TAB5_UI_COLOR_BORDER,
    TAB5_UI_COLOR_TEXT,
    TAB5_UI_COLOR_TEXT_MUTED,
    TAB5_UI_COLOR_BG,
} tab5_ui_color_id_t;

/**
 * @brief Obtém uma cor da paleta do tema ativo do sistema (formato 0xRRGGBB).
 */
uint32_t tab5_ui_theme_get_color(tab5_ui_color_id_t color_id);

/**
 * @brief Define a cor de fundo e opacidade de um objeto UI.
 * @param color_hex Cor no formato 0xRRGGBB.
 * @param opa Opacidade de 0 (transparente) a 255 (opaco).
 */
tab5_err_t tab5_ui_obj_set_style_bg(tab5_ui_obj_t obj, uint32_t color_hex, uint8_t opa);

/**
 * @brief Define a borda de um objeto UI.
 * @param border_hex Cor da borda 0xRRGGBB.
 * @param width Largura da borda em pixels (0 para sem borda).
 */
tab5_err_t tab5_ui_obj_set_style_border(tab5_ui_obj_t obj, uint32_t border_hex, int32_t width);

/**
 * @brief Define a cor e opacidade do texto de um objeto UI (ou de seu label filho).
 * @param color_hex Cor do texto 0xRRGGBB.
 * @param opa Opacidade de 0 a 255.
 */
tab5_err_t tab5_ui_obj_set_style_text_color(tab5_ui_obj_t obj, uint32_t color_hex, uint8_t opa);

/**
 * @brief Define o raio dos cantos arredondados (border radius).
 */
tab5_err_t tab5_ui_obj_set_style_radius(tab5_ui_obj_t obj, int32_t radius);

/**
 * @brief Configura o fator de crescimento em layout Flexbox (flex_grow).
 */
tab5_err_t tab5_ui_obj_set_flex_grow(tab5_ui_obj_t obj, uint8_t grow);

/**
 * @brief Habilita ou desabilita a capacidade de receber cliques (touch/click).
 */
tab5_err_t tab5_ui_obj_set_clickable(tab5_ui_obj_t obj, bool clickable);

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

typedef struct {
    char name[64];
    uint32_t size;
    uint32_t mtime;
    uint8_t is_dir;
} tab5_dir_entry_t;

/**
 * @brief Lista os arquivos e subdiretórios dentro de um caminho permitido.
 * @param rel_or_abs_path Caminho relativo ao sandbox da app ou absoluto (/sdcard/...).
 * @param entries Buffer alocado pelo chamador para receber as entradas.
 * @param max_entries Quantidade máxima de entradas suportadas pelo buffer.
 * @param out_count Ponteiro para receber a quantidade de entradas encontradas.
 * @return TAB5_OK em caso de sucesso.
 */
tab5_err_t tab5_storage_scandir(const char *rel_or_abs_path, tab5_dir_entry_t *entries, uint32_t max_entries,
                                uint32_t *out_count);

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
 * @param message Texto da mensagem.
 */
void tab5_system_log(int level, const char *tag, const char *message);

/* ========================================================================= */
/* Serviços do Sistema (Servidor HTTP, Gravador de Áudio, Terminal)          */
/* ========================================================================= */

/**
 * @brief Inicia o servidor HTTP de arquivos local.
 */
tab5_err_t tab5_fileserver_start(void);

/**
 * @brief Para o servidor HTTP de arquivos local.
 */
tab5_err_t tab5_fileserver_stop(void);

/**
 * @brief Retorna se o servidor HTTP está ativo.
 */
bool tab5_fileserver_is_running(void);

/**
 * @brief Retorna a porta TCP configurada para o servidor HTTP.
 */
uint16_t tab5_fileserver_get_port(void);

/**
 * @brief Inicia a gravação de áudio pelo microfone.
 */
tab5_err_t tab5_recorder_start(char *out_path, size_t out_len);

/**
 * @brief Para a gravação de áudio ativa.
 */
tab5_err_t tab5_recorder_stop(void);

/**
 * @brief Inicia reprodução de arquivo de áudio WAV gravado.
 */
tab5_err_t tab5_recorder_play(const char *path);

/**
 * @brief Pausa a reprodução de áudio.
 */
tab5_err_t tab5_recorder_pause(void);

/**
 * @brief Retoma a reprodução de áudio pausada.
 */
tab5_err_t tab5_recorder_resume(void);

/**
 * @brief Para a reprodução de áudio ativa.
 */
tab5_err_t tab5_recorder_stop_play(void);

/**
 * @brief Verifica se o microfone está gravando áudio.
 */
bool tab5_recorder_is_recording(void);

/**
 * @brief Verifica se o player de áudio está reproduzindo.
 */
bool tab5_recorder_is_playing(void);

/**
 * @brief Executa um comando do terminal shell do sistema.
 * @param cmd Comando a ser executado (ex: "ls", "free", "df", "cat arquivo.txt").
 * @param out_buf Buffer para receber a saída de texto gerada.
 * @param buf_size Capacidade do buffer de saída.
 */
tab5_err_t tab5_terminal_exec(const char *cmd, char *out_buf, size_t buf_size);

/* ========================================================================= */
/* Player de Música                                                          */
/* ========================================================================= */

typedef struct {
    uint32_t state;             /**< 0: IDLE, 1: PLAYING, 2: PAUSED */
    uint32_t current_time_sec;  /**< Tempo atual em segundos */
    uint32_t total_time_sec;    /**< Duracao total em segundos */
    char current_filepath[256]; /**< Caminho do arquivo sendo reproduzido */
} tab5_music_status_t;

/**
 * @brief Inicia a reproducao de um arquivo de musica (MP3/WAV).
 */
tab5_err_t tab5_music_play(const char *filepath);

/**
 * @brief Pausa a reproducao musical ativa.
 */
tab5_err_t tab5_music_pause(void);

/**
 * @brief Retoma a reproducao musical pausada.
 */
tab5_err_t tab5_music_resume(void);

/**
 * @brief Para a reproducao musical ativa.
 */
tab5_err_t tab5_music_stop(void);

/**
 * @brief Verifica se ha musica sendo reproduzida.
 */
bool tab5_music_is_playing(void);

/**
 * @brief Ajusta o volume do player de musica (0 a 100).
 */
tab5_err_t tab5_music_set_volume(int32_t volume);

/**
 * @brief Obtem o volume atual do player de musica (0 a 100).
 */
int32_t tab5_music_get_volume(void);

/**
 * @brief Obtem informacoes completas de estado do player de musica.
 */
tab5_err_t tab5_music_get_status(tab5_music_status_t *out_status);

/* ========================================================================= */
/* Gerenciamento de Rede Wi-Fi                                               */
/* ========================================================================= */

typedef struct {
    char ssid[33];    /**< Nome da rede Wi-Fi */
    int8_t rssi;      /**< Intensidade do sinal em dBm */
    uint8_t authmode; /**< Modo de seguranca (0: Aberta, >0: Protegida) */
} tab5_wifi_ap_t;

/**
 * @brief Realiza uma varredura de redes Wi-Fi proximas.
 * @param out_aps Array para receber os registros dos APs encontrados.
 * @param max_aps Capacidade do array de APs.
 * @param out_count Ponteiro para receber a quantidade encontrada.
 */
tab5_err_t tab5_wifi_scan(tab5_wifi_ap_t *out_aps, uint32_t max_aps, uint32_t *out_count);

/**
 * @brief Conecta a uma rede Wi-Fi e salva as credenciais no microSD de forma segura.
 */
tab5_err_t tab5_wifi_connect(const char *ssid, const char *password);

/**
 * @brief Desconecta da rede Wi-Fi ativa.
 */
tab5_err_t tab5_wifi_disconnect(void);

/**
 * @brief Remove as credenciais salvas de uma rede Wi-Fi.
 */
tab5_err_t tab5_wifi_forget(const char *ssid);

/**
 * @brief Habilita ou desabilita o hardware de Wi-Fi.
 */
tab5_err_t tab5_wifi_set_enabled(bool enabled);

/**
 * @brief Verifica se o subsistema Wi-Fi esta ativo.
 */
bool tab5_wifi_is_enabled(void);

/* ========================================================================= */
/* Gerenciamento de Dispositivos Bluetooth BLE                               */
/* ========================================================================= */

typedef struct {
    char mac[18];      /**< Endereco MAC formatado "AA:BB:CC:DD:EE:FF" */
    char name[64];     /**< Nome amigavel do dispositivo */
    int8_t rssi;       /**< Intensidade do sinal */
    uint8_t type;      /**< Tipo: 0 Generico, 1 Teclado, 2 Mouse, 3 Fone */
    uint8_t connected; /**< 1 se conectado, 0 caso contrario */
    uint8_t paired;    /**< 1 se pareado previamente, 0 caso contrario */
} tab5_bt_dev_t;

/**
 * @brief Realiza varredura de dispositivos Bluetooth BLE proximos.
 */
tab5_err_t tab5_bt_scan(tab5_bt_dev_t *out_devs, uint32_t max_devs, uint32_t *out_count);

/**
 * @brief Conecta a um dispositivo Bluetooth BLE.
 */
tab5_err_t tab5_bt_connect(const char *mac, const char *name, uint32_t dev_type);

/**
 * @brief Desconecta de um dispositivo Bluetooth BLE.
 */
tab5_err_t tab5_bt_disconnect(const char *mac);

/**
 * @brief Remove o pareamento salvo de um dispositivo Bluetooth.
 */
tab5_err_t tab5_bt_forget(const char *mac);

/**
 * @brief Habilita ou desabilita o subsistema Bluetooth.
 */
tab5_err_t tab5_bt_set_enabled(bool enabled);

/**
 * @brief Verifica se o subsistema Bluetooth esta ativo.
 */
bool tab5_bt_is_enabled(void);

#ifdef __cplusplus
}
#endif
