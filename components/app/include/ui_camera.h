#pragma once

#include "lvgl.h"

lv_obj_t *ui_camera_create(void);
void ui_camera_refresh_theme(void);
void ui_camera_apply_layout(void);
void ui_camera_on_open(void);
void ui_camera_on_close(void);
