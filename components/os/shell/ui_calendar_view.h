#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *container;   /* Container raiz */
    lv_obj_t *header_cont; /* Linha do cabeçalho com botões e título */
    lv_obj_t *prev_btn;    /* Botão mês anterior */
    lv_obj_t *prev_lbl;
    lv_obj_t *title_label; /* Label com "Mês AAAA" */
    lv_obj_t *next_btn;    /* Botão próximo mês */
    lv_obj_t *next_lbl;
    lv_obj_t *today_btn; /* Botão "Hoje" */
    lv_obj_t *today_lbl;
    lv_obj_t *weekdays_cont; /* Container dos dias da semana */
    lv_obj_t *weekday_labels[7];
    lv_obj_t *grid_cont;      /* Container da grade 7 colunas x 6 linhas */
    lv_obj_t *day_cells[42];  /* Células de cada dia */
    lv_obj_t *day_labels[42]; /* Labels dos dias */
    int view_year;
    int view_month;
    int today_year;
    int today_month;
    int today_day;
    int selected_day;
    bool is_popup;
} ui_calendar_view_t;

/**
 * @brief Cria a visualização de calendário como filha de parent.
 * @param parent Objeto pai LVGL.
 * @param is_popup Se true, configura layout compacto; se false, expandido para tela inteira.
 * @return Estrutura ui_calendar_view_t inicializada.
 */
ui_calendar_view_t ui_calendar_view_create(lv_obj_t *parent, bool is_popup);

/**
 * @brief Define a data de visualização do calendário (ano e mês).
 */
void ui_calendar_view_set_date(ui_calendar_view_t *cal, int year, int month);

/**
 * @brief Atualiza a data atual ('hoje') a partir do sistema.
 */
void ui_calendar_view_update_today(ui_calendar_view_t *cal);

/**
 * @brief Atualiza os dias da grade e o cabeçalho com base em view_year/view_month e data atual.
 */
void ui_calendar_view_refresh(ui_calendar_view_t *cal);

/**
 * @brief Atualiza as cores e estilos do calendário conforme o tema ativo (ui_theme_get()).
 */
void ui_calendar_view_refresh_theme(ui_calendar_view_t *cal);

/**
 * @brief Ajusta o layout (redimensionamento / rotação).
 */
void ui_calendar_view_apply_layout(ui_calendar_view_t *cal);

#ifdef __cplusplus
}
#endif
