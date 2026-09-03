#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_wifi_view_s ui_wifi_view_t;

ui_wifi_view_t *ui_wifi_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);
void ui_wifi_view_refresh_theme(ui_wifi_view_t *view);
void ui_wifi_view_apply_layout(ui_wifi_view_t *view);
void ui_wifi_view_destroy(ui_wifi_view_t *view);

/* Legados para shell se necessário */
void ui_wifi_register(void);
lv_obj_t *ui_wifi_create(void);
void ui_wifi_refresh_theme(void);
void ui_wifi_apply_layout(void);

#ifdef __cplusplus
}
#endif
