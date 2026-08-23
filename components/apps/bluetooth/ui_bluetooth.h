#pragma once

#include "lvgl.h"

void ui_bluetooth_register(void);
lv_obj_t *ui_bluetooth_create(void);
void ui_bluetooth_refresh_theme(void);
void ui_bluetooth_apply_layout(void);
