#pragma once

#include "lvgl.h"

void ui_shell_init(void);
lv_obj_t *ui_shell_get_desktop_screen(void);
void ui_shell_open_storage(void);
void ui_shell_close_storage(void);
void ui_shell_refresh_theme(void);
void ui_shell_notify_keyboard_layout(void);
