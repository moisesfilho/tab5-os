#pragma once

#include "lvgl.h"

void ui_keyboard_create(lv_obj_t *parent);
void ui_keyboard_attach(lv_obj_t *ta);
void ui_keyboard_hide(void);
void ui_keyboard_refresh_theme(void);
bool ui_keyboard_is_visible(void);
int32_t ui_keyboard_get_height(void);
