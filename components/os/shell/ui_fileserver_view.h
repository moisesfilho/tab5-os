#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_fileserver_view_s ui_fileserver_view_t;

ui_fileserver_view_t *ui_fileserver_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);
void ui_fileserver_view_refresh_theme(ui_fileserver_view_t *view);
void ui_fileserver_view_apply_layout(ui_fileserver_view_t *view);
void ui_fileserver_view_destroy(ui_fileserver_view_t *view);

/* Legados */
lv_obj_t *ui_fileserver_create(void);
void ui_fileserver_on_open(void);
void ui_fileserver_on_close(void);
void ui_fileserver_refresh_theme(void);
void ui_fileserver_apply_layout(void);

#ifdef __cplusplus
}
#endif
