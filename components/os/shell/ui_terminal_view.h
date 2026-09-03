#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_terminal_view_s ui_terminal_view_t;

ui_terminal_view_t *ui_terminal_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);
void ui_terminal_view_refresh_theme(ui_terminal_view_t *view);
void ui_terminal_view_apply_layout(ui_terminal_view_t *view);
void ui_terminal_view_destroy(ui_terminal_view_t *view);

/* Legados */
void ui_terminal_register(void);
lv_obj_t *ui_terminal_create(void);
void ui_terminal_refresh_theme(void);
void ui_terminal_apply_layout(void);
void ui_terminal_on_close(void);

#ifdef __cplusplus
}
#endif
