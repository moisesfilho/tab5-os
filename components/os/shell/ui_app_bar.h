#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *bar;          /* Container principal da barra */
    lv_obj_t *title_label;  /* Label do título (alinhado à esquerda) */
    lv_obj_t *actions_cont; /* Container flex para botões de ação personalizados */
    lv_obj_t *close_btn;    /* Botão fechar padronizado à direita */
    lv_obj_t *close_label;  /* Ícone LV_SYMBOL_CLOSE */
} ui_app_bar_t;

/**
 * @brief Cria a barra de título padronizada para uma aplicação.
 *
 * @param parent Tela da aplicação (screen) onde a barra será inserida.
 * @param title Texto do título da aplicação.
 * @param on_close_cb Callback executado ao clicar no botão fechar.
 * @param user_data Dados opcionais repassados ao callback de fechar.
 * @return ui_app_bar_t Estrutura com os objetos criados.
 */
ui_app_bar_t ui_app_bar_create(lv_obj_t *parent, const char *title, lv_event_cb_t on_close_cb, void *user_data);

/**
 * @brief Adiciona um botão de ação personalizado na barra de título (à esquerda do fechar).
 *
 * @param app_bar Ponteiro para a barra de título.
 * @param symbol_or_text Texto ou símbolo LVGL (ex: LV_SYMBOL_SAVE, LV_SYMBOL_PLUS).
 * @param on_click_cb Callback executado ao clicar no botão.
 * @param user_data Dados opcionais repassados ao callback.
 * @param out_label Ponteiro opcional para receber o objeto lv_label criado dentro do botão.
 * @return lv_obj_t* Ponteiro para o botão criado.
 */
lv_obj_t *ui_app_bar_add_action_button(ui_app_bar_t *app_bar, const char *symbol_or_text, lv_event_cb_t on_click_cb,
                                       void *user_data, lv_obj_t **out_label);

/**
 * @brief Atualiza o título exibido na barra.
 *
 * @param app_bar Ponteiro para a barra de título.
 * @param title Novo texto do título.
 */
void ui_app_bar_set_title(ui_app_bar_t *app_bar, const char *title);

/**
 * @brief Reaplica as cores do tema ativo na barra, título, botões de ação e botão fechar.
 *
 * @param app_bar Ponteiro para a barra de título.
 */
void ui_app_bar_refresh_theme(ui_app_bar_t *app_bar);

#ifdef __cplusplus
}
#endif
