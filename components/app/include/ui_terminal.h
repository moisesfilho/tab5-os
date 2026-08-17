#pragma once

#include "lvgl.h"

lv_obj_t *ui_terminal_create(void);
void ui_terminal_refresh_theme(void);
void ui_terminal_apply_layout(void);
void ui_terminal_on_close(void);
