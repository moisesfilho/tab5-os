#pragma once

#include "lvgl.h"

void ui_shell_init(void);
void ui_shell_open_notas(void);
void ui_shell_open_notas_with_file(const char *filepath);
void ui_shell_close_notas(void);
void ui_shell_open_wifi(void);
void ui_shell_close_wifi(void);
void ui_shell_open_files(void);
void ui_shell_close_files(void);
void ui_shell_refresh_theme(void);
void ui_shell_notify_keyboard_layout(void);
