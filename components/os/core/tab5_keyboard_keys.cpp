#include "tab5_keyboard_keys.h"
#include <string.h>

/* Tabela de mapeamento: strings do modo Character -> acoes LVGL.
 * Baseada no firmware M5Tab5-Keyboard-Internal-FW e UserDemo.
 * Layout US ANSI, com processamento de Shift/Sym ja feito pelo firmware. */

/* clang-format off */
static const tab5_key_entry_t s_keymap[] = {
    /* Navegacao e controle (bytes de controle ASCII do modo Character) */
    {"\t",   0,                   '\t',  TAB5_KEY_CHAR},
    {"\n",   LV_KEY_ENTER,        0,     TAB5_KEY_SPECIAL},  /* Enter */
    {"\b",   LV_KEY_BACKSPACE,    0,     TAB5_KEY_SPECIAL},  /* Backspace */
    {"\x7F", LV_KEY_DEL,          0,     TAB5_KEY_SPECIAL},  /* Del (fwd) */
    {"\x1B", LV_KEY_ESC,          0,     TAB5_KEY_SPECIAL},  /* Esc */

    /* Setas (firmware usa codigos de controle ASCII) */
    {"\x11", LV_KEY_UP,           0,     TAB5_KEY_SPECIAL},  /* Up */
    {"\x12", LV_KEY_DOWN,         0,     TAB5_KEY_SPECIAL},  /* Down */
    {"\x13", LV_KEY_RIGHT,        0,     TAB5_KEY_SPECIAL},  /* Right */
    {"\x14", LV_KEY_LEFT,         0,     TAB5_KEY_SPECIAL},  /* Left */

    /* Variante do firmware que emite nomes em vez de bytes de controle. */
    {"esc",       LV_KEY_ESC,      0, TAB5_KEY_SPECIAL},
    {"enter",     LV_KEY_ENTER,    0, TAB5_KEY_SPECIAL},
    {"backspace", LV_KEY_BACKSPACE, 0, TAB5_KEY_SPECIAL},
    {"del",       LV_KEY_DEL,      0, TAB5_KEY_SPECIAL},
    {"up",        LV_KEY_UP,       0, TAB5_KEY_SPECIAL},
    {"down",      LV_KEY_DOWN,     0, TAB5_KEY_SPECIAL},
    {"left",      LV_KEY_LEFT,     0, TAB5_KEY_SPECIAL},
    {"right",     LV_KEY_RIGHT,    0, TAB5_KEY_SPECIAL},
    {"space",     0,               ' ', TAB5_KEY_CHAR},

    /* Espaco */
    {" ",    0,                   ' ',  TAB5_KEY_CHAR},

    /* Letras minusculas (a-z) e maiusculas (A-Z) */
    {"a", 0, 'a', TAB5_KEY_CHAR}, {"A", 0, 'A', TAB5_KEY_CHAR},
    {"b", 0, 'b', TAB5_KEY_CHAR}, {"B", 0, 'B', TAB5_KEY_CHAR},
    {"c", 0, 'c', TAB5_KEY_CHAR}, {"C", 0, 'C', TAB5_KEY_CHAR},
    {"d", 0, 'd', TAB5_KEY_CHAR}, {"D", 0, 'D', TAB5_KEY_CHAR},
    {"e", 0, 'e', TAB5_KEY_CHAR}, {"E", 0, 'E', TAB5_KEY_CHAR},
    {"f", 0, 'f', TAB5_KEY_CHAR}, {"F", 0, 'F', TAB5_KEY_CHAR},
    {"g", 0, 'g', TAB5_KEY_CHAR}, {"G", 0, 'G', TAB5_KEY_CHAR},
    {"h", 0, 'h', TAB5_KEY_CHAR}, {"H", 0, 'H', TAB5_KEY_CHAR},
    {"i", 0, 'i', TAB5_KEY_CHAR}, {"I", 0, 'I', TAB5_KEY_CHAR},
    {"j", 0, 'j', TAB5_KEY_CHAR}, {"J", 0, 'J', TAB5_KEY_CHAR},
    {"k", 0, 'k', TAB5_KEY_CHAR}, {"K", 0, 'K', TAB5_KEY_CHAR},
    {"l", 0, 'l', TAB5_KEY_CHAR}, {"L", 0, 'L', TAB5_KEY_CHAR},
    {"m", 0, 'm', TAB5_KEY_CHAR}, {"M", 0, 'M', TAB5_KEY_CHAR},
    {"n", 0, 'n', TAB5_KEY_CHAR}, {"N", 0, 'N', TAB5_KEY_CHAR},
    {"o", 0, 'o', TAB5_KEY_CHAR}, {"O", 0, 'O', TAB5_KEY_CHAR},
    {"p", 0, 'p', TAB5_KEY_CHAR}, {"P", 0, 'P', TAB5_KEY_CHAR},
    {"q", 0, 'q', TAB5_KEY_CHAR}, {"Q", 0, 'Q', TAB5_KEY_CHAR},
    {"r", 0, 'r', TAB5_KEY_CHAR}, {"R", 0, 'R', TAB5_KEY_CHAR},
    {"s", 0, 's', TAB5_KEY_CHAR}, {"S", 0, 'S', TAB5_KEY_CHAR},
    {"t", 0, 't', TAB5_KEY_CHAR}, {"T", 0, 'T', TAB5_KEY_CHAR},
    {"u", 0, 'u', TAB5_KEY_CHAR}, {"U", 0, 'U', TAB5_KEY_CHAR},
    {"v", 0, 'v', TAB5_KEY_CHAR}, {"V", 0, 'V', TAB5_KEY_CHAR},
    {"w", 0, 'w', TAB5_KEY_CHAR}, {"W", 0, 'W', TAB5_KEY_CHAR},
    {"x", 0, 'x', TAB5_KEY_CHAR}, {"X", 0, 'X', TAB5_KEY_CHAR},
    {"y", 0, 'y', TAB5_KEY_CHAR}, {"Y", 0, 'Y', TAB5_KEY_CHAR},
    {"z", 0, 'z', TAB5_KEY_CHAR}, {"Z", 0, 'Z', TAB5_KEY_CHAR},

    /* Numeros */
    {"1", 0, '1', TAB5_KEY_CHAR}, {"2", 0, '2', TAB5_KEY_CHAR},
    {"3", 0, '3', TAB5_KEY_CHAR}, {"4", 0, '4', TAB5_KEY_CHAR},
    {"5", 0, '5', TAB5_KEY_CHAR}, {"6", 0, '6', TAB5_KEY_CHAR},
    {"7", 0, '7', TAB5_KEY_CHAR}, {"8", 0, '8', TAB5_KEY_CHAR},
    {"9", 0, '9', TAB5_KEY_CHAR}, {"0", 0, '0', TAB5_KEY_CHAR},

    /* Simbolos (nivel normal) */
    {"`", 0, '`', TAB5_KEY_CHAR}, {"~", 0, '~', TAB5_KEY_CHAR},
    {"!", 0, '!', TAB5_KEY_CHAR}, {"@", 0, '@', TAB5_KEY_CHAR},
    {"#", 0, '#', TAB5_KEY_CHAR}, {"$", 0, '$', TAB5_KEY_CHAR},
    {"%", 0, '%', TAB5_KEY_CHAR}, {"^", 0, '^', TAB5_KEY_CHAR},
    {"&", 0, '&', TAB5_KEY_CHAR}, {"*", 0, '*', TAB5_KEY_CHAR},
    {"(", 0, '(', TAB5_KEY_CHAR}, {")", 0, ')', TAB5_KEY_CHAR},
    {"[", 0, '[', TAB5_KEY_CHAR}, {"]", 0, ']', TAB5_KEY_CHAR},
    {"{", 0, '{', TAB5_KEY_CHAR}, {"}", 0, '}', TAB5_KEY_CHAR},
    {"\\", 0, '\\', TAB5_KEY_CHAR}, {"|", 0, '|', TAB5_KEY_CHAR},
    {";", 0, ';', TAB5_KEY_CHAR}, {":", 0, ':', TAB5_KEY_CHAR},
    {"'", 0, '\'', TAB5_KEY_CHAR}, {"\"", 0, '"', TAB5_KEY_CHAR},
    {",", 0, ',', TAB5_KEY_CHAR}, {".", 0, '.', TAB5_KEY_CHAR},
    {"<", 0, '<', TAB5_KEY_CHAR}, {">", 0, '>', TAB5_KEY_CHAR},
    {"/", 0, '/', TAB5_KEY_CHAR}, {"?", 0, '?', TAB5_KEY_CHAR},
    {"-", 0, '-', TAB5_KEY_CHAR}, {"_", 0, '_', TAB5_KEY_CHAR},
    {"+", 0, '+', TAB5_KEY_CHAR}, {"=", 0, '=', TAB5_KEY_CHAR},

    {nullptr, 0, 0, TAB5_KEY_IGNORE}
};
/* clang-format on */

const tab5_key_entry_t *tab5_keymap_lookup(const char *str)
{
    if (str == nullptr || str[0] == '\0') {
        return nullptr;
    }

    for (const tab5_key_entry_t *e = s_keymap; e->str != nullptr; e++) {
        if (strcmp(e->str, str) == 0) {
            return e;
        }
    }

    return nullptr;
}
