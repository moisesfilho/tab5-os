#pragma once

#include "lvgl.h"

/* Altura da barra superior; usada tambem para posicionar o textarea. */
#define UI_BAR_HEIGHT 40

void ui_bar_init(lv_obj_t *parent);
void ui_bar_refresh_theme(void);
void ui_bar_set_visible(bool visible);
