#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef struct {
    uint32_t background;  /* fundo da tela e do teclado */
    uint32_t surface;     /* superficies (textarea, teclas, badges) */
    uint32_t surface_alt; /* tecla com foco */
    uint32_t border;      /* bordas padrao */
    uint32_t accent;      /* destaque (foco, cursor, pressionado) */
    uint32_t accent_soft; /* teclas de acao */
    uint32_t text;        /* texto principal */
    uint32_t text_muted;  /* placeholder e texto secundario */
} ui_palette_t;

const ui_palette_t *ui_theme_get(void);
bool ui_theme_is_dark(void);
void ui_theme_toggle(void);
void ui_theme_init(lv_obj_t *parent);
