#include "ui_keyboard.h"
#include "ui_theme.h"
#include "ui_font.h"
#include <string.h>

/* Macros privados do LVGL, definidos apenas em lv_keyboard.c e usados nos
 * mapas custom de teclado. Espelhados aqui para compilar fora do widget.
 * lv_buttonmatrix_ctrl_t e enum em C++, entao o cast explicito e obrigatorio. */
#ifndef LV_KB_BTN
#define LV_KB_BTN(width) ((lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_POPOVER | (width)))
#endif
#ifndef LV_KB_CTRL_FLAGS_W2
#define LV_KB_CTRL_FLAGS_W2 ((lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | LV_BUTTONMATRIX_CTRL_WIDTH_2))
#endif
#ifndef LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER
#define LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER "abc"
#endif

namespace {

lv_obj_t *keyboard = nullptr;
lv_obj_t *kb_target = nullptr;

/* Pagina de acentos (modo SPECIAL, aberto pela tecla "1#"). O mecanismo
 * nativo troca de modo por texto exato, entao "abc"/LV_SYMBOL_KEYBOARD
 * voltam para o QWERTY e o texto nao-reconhecido e inserido no textarea.
 * Estrutura espelhada em default_kb_map_spec (lv_keyboard.c:156-175). */
static const char *const pt_br_map_spec[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "à", "á", "â", "ã", "ç", "é", "ê", "í", "ó", "ô", "õ", "ú", "\n",
    "À", "Á", "Â", "Ã", "Ç", "É", "Ê", "Í", "Ó", "Ô", "Õ", "Ú", "\n",
    ",", ".", ";", ":", "!", "?", "-", "_", "+", "=", "/", "@", "#", "\n",
    LV_SYMBOL_KEYBOARD, LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER, LV_SYMBOL_LEFT, " ",
    LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t pt_br_ctrl_spec[] = {
    /* Linha 1 (11): 1..0 + backspace */
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_CTRL_FLAGS_W2,
    /* Linha 2 (12): acentos minusculos */
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1),
    /* Linha 3 (12): acentos maiusculos */
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1),
    /* Linha 4 (13): pontuacao */
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    /* Linha 5 (6): teclado, abc, esquerda, espaco, direita, OK */
    LV_KB_CTRL_FLAGS_W2, LV_KB_CTRL_FLAGS_W2,
    LV_KB_CTRL_FLAGS_W2, LV_BUTTONMATRIX_CTRL_WIDTH_6,
    LV_KB_CTRL_FLAGS_W2, LV_KB_CTRL_FLAGS_W2
};

bool is_action_key(const char *text)
{
    if (text == nullptr) {
        return false;
    }

    return strstr(text, LV_SYMBOL_BACKSPACE) != nullptr ||
           strstr(text, LV_SYMBOL_OK) != nullptr ||
           strstr(text, LV_SYMBOL_LEFT) != nullptr ||
           strstr(text, LV_SYMBOL_RIGHT) != nullptr ||
           strstr(text, LV_SYMBOL_CLOSE) != nullptr ||
           strstr(text, LV_SYMBOL_UP) != nullptr ||
           strstr(text, LV_SYMBOL_NEW_LINE) != nullptr ||
           strcmp(text, "abc") == 0 || strcmp(text, "ABC") == 0 ||
           strcmp(text, "1#") == 0;
}

/* Marca as teclas de acao com LV_STATE_CHECKED para receberem o destaque
 * via estilo LV_PART_ITEMS | LV_STATE_CHECKED (mecanismo do LVGL 9). */
void accent_action_keys(lv_obj_t *kb)
{
    const char *const *map = lv_buttonmatrix_get_map(kb);
    uint32_t id = 0;
    for (uint32_t i = 0; map[i] != nullptr && map[i][0] != '\0'; i++) {
        if (map[i][0] == '\n') {
            continue;
        }
        if (is_action_key(map[i])) {
            lv_buttonmatrix_set_button_ctrl(kb, id, LV_BUTTONMATRIX_CTRL_CHECKED);
        } else {
            lv_buttonmatrix_clear_button_ctrl(kb, id, LV_BUTTONMATRIX_CTRL_CHECKED);
        }
        id++;
    }
}

/* Reaplica os estilos a partir da paleta ativa do tema. */
void apply_keyboard_theme(void)
{
    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(keyboard, lv_color_hex(pal->background), 0);
    lv_obj_set_style_pad_all(keyboard, 8, 0);
    lv_obj_set_style_pad_row(keyboard, 7, 0);
    lv_obj_set_style_pad_column(keyboard, 7, 0);
    lv_obj_set_style_radius(keyboard, 14, 0);

    /* Estado normal das teclas, pressionado, foco e acao tem contraste claro. */
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(pal->surface), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(pal->surface_alt), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(pal->accent), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(pal->accent_soft), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(keyboard, lv_color_hex(pal->accent), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(pal->text), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(pal->text), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_white(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard, lv_color_hex(pal->border), LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard, lv_color_hex(pal->accent), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(keyboard, 8, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
}

/* Ajusta a altura do teclado conforme a orientacao: no retrato as teclas
 * ficam mais compactas. Os getters do LVGL 9 ja devolvem as dimensoes
 * logicas trocadas na rotacao, entao comparar h x w identifica o retrato. */
void apply_keyboard_layout(void)
{
    int32_t h = lv_display_get_vertical_resolution(NULL);
    int32_t w = lv_display_get_horizontal_resolution(NULL);

    if (h > w) {
        lv_obj_set_height(keyboard, h * 35 / 100);
    } else {
        lv_obj_set_height(keyboard, h * 52 / 100);
    }
}

/* Chamado pelo display a cada mudanca de resolucao/rotacao (task LVGL). */
void keyboard_resolution_cb(lv_event_t *event)
{
    (void)event;
    apply_keyboard_layout();
}

/* OK e fechar escondem o teclado (comportamento de SO). */
void keyboard_ready_cb(lv_event_t *event)
{
    (void)event;
    ui_keyboard_hide();
}

void keyboard_cancel_cb(lv_event_t *event)
{
    (void)event;
    ui_keyboard_hide();
}

} // namespace

void ui_keyboard_create(lv_obj_t *parent)
{
    keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(keyboard, lv_pct(100), lv_pct(52));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14_latin1, LV_PART_ITEMS);

    /* Pagina de acentos no modo SPECIAL (aberta pela tecla "1#"). */
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, pt_br_map_spec, pt_br_ctrl_spec);

    accent_action_keys(keyboard);
    apply_keyboard_theme();

    /* Oculta por padrao; o app mostra ao focar um campo de texto. */
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, keyboard_ready_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(keyboard, keyboard_cancel_cb, LV_EVENT_CANCEL, nullptr);

    apply_keyboard_layout();
    lv_display_add_event_cb(lv_display_get_default(), keyboard_resolution_cb,
                            LV_EVENT_RESOLUTION_CHANGED, nullptr);
}

void ui_keyboard_attach(lv_obj_t *ta)
{
    kb_target = ta;
    lv_keyboard_set_textarea(keyboard, ta);
    lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    apply_keyboard_layout();
}

void ui_keyboard_hide(void)
{
    if (kb_target != nullptr) {
        lv_obj_clear_state(kb_target, LV_STATE_FOCUSED);
        kb_target = nullptr;
    }
    lv_keyboard_set_textarea(keyboard, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void ui_keyboard_refresh_theme(void)
{
    apply_keyboard_theme();
}
