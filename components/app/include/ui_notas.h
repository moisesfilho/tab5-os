#pragma once

#include "lvgl.h"

void ui_notas_register(void);
lv_obj_t *ui_notas_create(void);
void ui_notas_refresh_theme(void);
void ui_notas_apply_layout(void);
void ui_notas_open_file(const char *filepath);
void ui_notas_new_file(void);
bool ui_notas_save_current(void);
void ui_notas_on_open(void);
