#pragma once

#include "lvgl.h"

/* Fonte default da app: Montserrat Medium 14px com Latin-1 completo
 * (0x20-0x7F, 0xA0-0xFF, 0x2022) + simbolos LVGL. Gerada via lv_font_conv
 * a partir do mesmo Montserrat-Medium.ttf usado pelos fonts built-in. */
extern const lv_font_t lv_font_montserrat_14_latin1;

/* Montserrat Medium 28px, mesma cobertura de caracteres; usada em titulos
 * e no splash. Gerada em paralelo (nao editar o arquivo da fonte). */
extern const lv_font_t lv_font_montserrat_28_latin1;
