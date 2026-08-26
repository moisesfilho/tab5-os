#pragma once

#include "lvgl.h"

/* Fonte padrão do sistema: Montserrat Medium 18px com Latin-1 completo
 * (0x20-0x7F, 0xA0-0xFF, 0x2022) + símbolos LVGL. */
extern const lv_font_t lv_font_montserrat_18_latin1;

/* Fonte de 14px mantida para compatibilidade ou elementos compactos */
extern const lv_font_t lv_font_montserrat_14_latin1;

/* Montserrat Medium 28px, mesma cobertura de caracteres; usada em titulos
 * e no splash. Gerada em paralelo (nao editar o arquivo da fonte). */
extern const lv_font_t lv_font_montserrat_28_latin1;

/* JetBrains Mono Regular 18px, fonte MONOESPACADA, usada no relogio da barra */
extern const lv_font_t lv_font_jetbrains_mono_18_clock;

/* JetBrains Mono Regular 14px mantida para compatibilidade */
extern const lv_font_t lv_font_jetbrains_mono_14_clock;

/* Montserrat Medium 56px, mesma cobertura de caracteres; usada no relogio
 * do protetor de tela. Gerada em paralelo (nao editar o arquivo da fonte). */
extern const lv_font_t lv_font_montserrat_56_latin1;
