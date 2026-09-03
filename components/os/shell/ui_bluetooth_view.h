#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_bluetooth_view_s ui_bluetooth_view_t;

ui_bluetooth_view_t *ui_bluetooth_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);
void ui_bluetooth_view_refresh_theme(ui_bluetooth_view_t *view);
void ui_bluetooth_view_apply_layout(ui_bluetooth_view_t *view);
void ui_bluetooth_view_destroy(ui_bluetooth_view_t *view);

/* Legados */
void ui_bluetooth_register(void);
lv_obj_t *ui_bluetooth_create(void);
void ui_bluetooth_refresh_theme(void);
void ui_bluetooth_apply_layout(void);

#ifdef __cplusplus
}
#endif
