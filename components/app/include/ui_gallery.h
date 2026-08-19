#pragma once

#include "lvgl.h"

void ui_gallery_register(void);
lv_obj_t *ui_gallery_create(void);
void ui_gallery_refresh_theme(void);
void ui_gallery_apply_layout(void);
void ui_gallery_open_file(const char *filepath);
void ui_gallery_on_open(void);
void ui_gallery_on_close(void);
