#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_recorder_view_s ui_recorder_view_t;

ui_recorder_view_t *ui_recorder_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);
void ui_recorder_view_open_file(ui_recorder_view_t *view, const char *filepath);
void ui_recorder_view_refresh_theme(ui_recorder_view_t *view);
void ui_recorder_view_apply_layout(ui_recorder_view_t *view);
void ui_recorder_view_destroy(ui_recorder_view_t *view);

/* Legados */
void ui_recorder_register(void);
lv_obj_t *ui_recorder_create(void);
void ui_recorder_on_open(void);
void ui_recorder_on_close(void);
void ui_recorder_open_file(const char *filepath);
void ui_recorder_refresh_theme(void);
void ui_recorder_apply_layout(void);

#ifdef __cplusplus
}
#endif
