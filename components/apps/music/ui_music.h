#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registra a aplicacao Musica no app_registry e file_assoc.
 */
void ui_music_register(void);

/**
 * @brief Cria a tela do aplicativo Musica.
 * @return Ponteiro para o objeto de tela LVGL criado.
 */
lv_obj_t *ui_music_create(void);

/**
 * @brief Notifica a abertura da tela de Musica.
 */
void ui_music_on_open(void);

/**
 * @brief Notifica o fechamento da tela de Musica.
 */
void ui_music_on_close(void);

/**
 * @brief Abre o app Musica e inicia a reproducao de um arquivo especifico.
 * @param filepath Caminho completo do arquivo de audio.
 */
void ui_music_open_file(const char *filepath);

/**
 * @brief Reaplica o tema visual ativo na interface do player de musica.
 */
void ui_music_refresh_theme(void);

/**
 * @brief Ajusta o layout dos componentes de acordo com a orientacao/resolucao atual da tela.
 */
void ui_music_apply_layout(void);

#ifdef __cplusplus
}
#endif
