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
void ui_shell_open_bluetooth(void);
void ui_shell_close_bluetooth(void);
void ui_shell_open_terminal(void);
void ui_shell_close_terminal(void);
void ui_shell_open_camera(void);
void ui_shell_close_camera(void);
void ui_shell_open_gallery(void);
void ui_shell_open_gallery_with_file(const char *filepath);
void ui_shell_close_gallery(void);
void ui_shell_open_fileserver(void);
void ui_shell_close_fileserver(void);
void ui_shell_open_recorder(void);
void ui_shell_open_recorder_with_file(const char *filepath);
void ui_shell_close_recorder(void);
void ui_shell_open_chat(void);
void ui_shell_close_chat(void);
void ui_shell_open_music(void);
void ui_shell_open_music_with_file(const char *filepath);
void ui_shell_close_music(void);
void ui_shell_refresh_theme(void);
void ui_shell_notify_keyboard_layout(void);
