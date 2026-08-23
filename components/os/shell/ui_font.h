#pragma once

#include "lvgl.h"

/* Fonte default da app: Montserrat Medium 14px com Latin-1 completo
 * (0x20-0x7F, 0xA0-0xFF, 0x2022) + simbolos LVGL. Gerada via lv_font_conv
 * a partir do mesmo Montserrat-Medium.ttf usado pelos fonts built-in. */
extern const lv_font_t lv_font_montserrat_14_latin1;

/* Montserrat Medium 28px, mesma cobertura de caracteres; usada em titulos
 * e no splash. Gerada em paralelo (nao editar o arquivo da fonte). */
extern const lv_font_t lv_font_montserrat_28_latin1;

/* JetBrains Mono Regular 14px, fonte MONOESPACADA (todo glifo ocupa a mesma
 * largura), subconjunto " ", "/" , ":" e numeros; usada no relogio da barra
 * para nao haver deslocamento lateral quando os valores mudam. Gerada em
 * paralelo (nao editar o arquivo da fonte). */
extern const lv_font_t lv_font_jetbrains_mono_14_clock;

/* Montserrat Medium 56px, mesma cobertura de caracteres; usada no relogio
 * do protetor de tela. Gerada em paralelo (nao editar o arquivo da fonte). */
extern const lv_font_t lv_font_montserrat_56_latin1;
