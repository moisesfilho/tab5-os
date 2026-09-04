#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_chat_view_s ui_chat_view_t;

ui_chat_view_t *ui_chat_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);
void ui_chat_view_refresh_theme(ui_chat_view_t *view);
void ui_chat_view_apply_layout(ui_chat_view_t *view);
void ui_chat_view_destroy(ui_chat_view_t *view);

/* Legados */
lv_obj_t *ui_chat_create(void);
void ui_chat_on_open(void);
void ui_chat_on_close(void);
void ui_chat_refresh_theme(void);
void ui_chat_apply_layout(void);

#ifdef __cplusplus
}
#endif
