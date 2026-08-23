#pragma once

#include "lvgl.h"

void ui_files_register(void);
lv_obj_t *ui_files_create(void);
void ui_files_refresh_theme(void);
void ui_files_apply_layout(void);
void ui_files_open_path(const char *path);
