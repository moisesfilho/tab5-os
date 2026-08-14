#include "ui_keyboard.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_font.h"
#include <string.h>

namespace {

lv_obj_t *kb_textarea = nullptr;
lv_obj_t *keyboard = nullptr;

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

/* Reaplica todos os estilos a partir da paleta ativa do tema. */
void apply_keyboard_theme(void)
{
    const ui_palette_t *pal = ui_theme_get();

    /* Area de digitacao */
    lv_obj_set_style_bg_color(kb_textarea, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_text_color(kb_textarea, lv_color_hex(pal->text), 0);
    lv_obj_set_style_border_width(kb_textarea, 1, 0);
    lv_obj_set_style_border_color(kb_textarea, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_color(kb_textarea, lv_color_hex(pal->accent), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(kb_textarea, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(kb_textarea, 10, 0);
    lv_obj_set_style_pad_left(kb_textarea, 18, 0);
    lv_obj_set_style_pad_right(kb_textarea, 18, 0);
    lv_obj_set_style_pad_top(kb_textarea, 12, 0);
    lv_obj_set_style_pad_bottom(kb_textarea, 12, 0);
    lv_obj_set_style_text_color(kb_textarea, lv_color_hex(pal->text_muted), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(kb_textarea, lv_color_hex(pal->accent), LV_PART_CURSOR);
    lv_obj_set_style_width(kb_textarea, 2, LV_PART_CURSOR);

    /* Teclado virtual */
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

/* Ajusta a altura do teclado e do textarea conforme a orientacao:
 * no retrato as teclas ficam mais compactas (2/3 da altura). Os getters
 * do LVGL 9 ja devolvem as dimensoes logicas trocadas na rotacao, entao
 * comparar vertical x horizontal identifica o retrato de forma segura. */
void apply_keyboard_layout(void)
{
    int32_t h = lv_display_get_vertical_resolution(NULL);
    int32_t w = lv_display_get_horizontal_resolution(NULL);

    if (h > w) {
        /* Retrato (tela logica 720x1280) */
        lv_obj_set_height(keyboard, h * 35 / 100);
        lv_obj_set_height(kb_textarea, h * 10 / 100);
    } else {
        /* Paisagem (tela logica 1280x720) */
        lv_obj_set_height(keyboard, h * 52 / 100);
        lv_obj_set_height(kb_textarea, h * 22 / 100);
    }
}

/* Chamado pelo display a cada mudanca de resolucao/rotacao (task LVGL). */
void keyboard_resolution_cb(lv_event_t *event)
{
    (void)event;
    apply_keyboard_layout();
}

} // namespace

void ui_keyboard_create(lv_obj_t *parent)
{
    /* Area de digitacao */
    kb_textarea = lv_textarea_create(parent);
    lv_obj_set_width(kb_textarea, lv_pct(90));
    lv_obj_set_height(kb_textarea, lv_pct(22));
    /* Abaixo da barra superior (altura + respiro de 14px). */
    lv_obj_align(kb_textarea, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT + 14);
    lv_textarea_set_placeholder_text(kb_textarea, "Toque para digitar...");
    lv_textarea_set_one_line(kb_textarea, true);
    lv_textarea_set_cursor_click_pos(kb_textarea, true);
    lv_obj_set_style_text_font(kb_textarea, &lv_font_montserrat_14_latin1, LV_PART_MAIN);

    /* Teclado virtual */
    keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(keyboard, lv_pct(100), lv_pct(58));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, kb_textarea);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14_latin1, LV_PART_ITEMS);

    accent_action_keys(keyboard);
    apply_keyboard_theme();

    /* Estado inicial consistente e reacao a mudancas de orientacao. */
    apply_keyboard_layout();
    lv_display_add_event_cb(lv_display_get_default(), keyboard_resolution_cb,
                            LV_EVENT_RESOLUTION_CHANGED, nullptr);
}

void ui_keyboard_refresh_theme(void)
{
    apply_keyboard_theme();
}
