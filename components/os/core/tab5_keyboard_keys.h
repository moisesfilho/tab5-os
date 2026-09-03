#pragma once

#include "lvgl.h"

/* Mapeamento das strings UTF-8 retornadas pelo modo Character do teclado
 * M5Stack Tab5 (SKU A164) para acoes LVGL.
 *
 * O firmware retorna: byte 0 = modifier (bit0=Ctrl, bit2=Alt),
 * bytes 1..N = string UTF-8 do nome da tecla.
 *
 * Esta tabela classifica a string em uma das categorias abaixo. */

typedef enum {
    TAB5_KEY_CHAR,     /* caractere imprimivel -> ui_keyboard_inject_char() */
    TAB5_KEY_SPECIAL,  /* tecla de navegacao/controle -> ui_keyboard_inject_key() */
    TAB5_KEY_MODIFIER, /* tecla modificadora (Ctrl/Alt/Aa/Sym) -> ignorada aqui */
    TAB5_KEY_IGNORE,   /* tecla sem acao relevante */
} tab5_key_type_t;

typedef struct {
    const char *str;   /* string retornada pelo firmware (NUL-terminated) */
    uint32_t lvgl_key; /* LVGL key constant (para TAB5_KEY_SPECIAL) */
    char ch;           /* caractere ASCII (para TAB5_KEY_CHAR) */
    tab5_key_type_t type;
} tab5_key_entry_t;

/* Busca a entrada de mapeamento para a string retornada pelo firmware.
 * Retorna nullptr se a string nao for reconhecida. */
const tab5_key_entry_t *tab5_keymap_lookup(const char *str);
