#pragma once

#include "lvgl.h"
#include "ui_app_bar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_music_view_s ui_music_view_t;

ui_music_view_t *ui_music_view_create(lv_obj_t *parent, ui_app_bar_t app_bar);
void ui_music_view_open_file(ui_music_view_t *view, const char *filepath);
void ui_music_view_refresh_theme(ui_music_view_t *view);
void ui_music_view_apply_layout(ui_music_view_t *view);
void ui_music_view_destroy(ui_music_view_t *view);

/* Legados */
lv_obj_t *ui_music_create(void);
void ui_music_on_open(void);
void ui_music_on_close(void);
void ui_music_open_file(const char *filepath);
void ui_music_refresh_theme(void);
void ui_music_apply_layout(void);

#ifdef __cplusplus
}
#endif
