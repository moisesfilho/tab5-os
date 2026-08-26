#pragma once

#include "lvgl.h"

/* Altura da barra superior; usada tambem para posicionar o textarea. */
#define UI_BAR_HEIGHT 52

void ui_bar_init(lv_obj_t *parent);
void ui_bar_refresh_theme(void);
void ui_bar_set_visible(bool visible);

/* Abertura programatica dos paineis da barra (usado tambem pelo simulador
 * de regressao visual, que nao dispara cliques em coordenadas fixas). */
void ui_bar_open_power_menu(void);
void ui_bar_open_settings(void);
void ui_bar_open_calendar_popup(void);
