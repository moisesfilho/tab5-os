#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_fileserver_register(void);
lv_obj_t *ui_fileserver_create(void);
void ui_fileserver_on_open(void);
void ui_fileserver_on_close(void);
void ui_fileserver_refresh_theme(void);
void ui_fileserver_apply_layout(void);

#ifdef __cplusplus
}
#endif
