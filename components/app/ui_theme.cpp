#include "ui_theme.h"
#include "ui_keyboard.h"
#include "ui_status.h"

namespace {

/* Ordem dos campos: background, surface, surface_alt, border, accent,
 * accent_soft, text, text_muted (posicional, compativel com C++11). */
const ui_palette_t PALETTE_DARK = {
    0x10141C, 0x1A2130, 0x202A3D, 0x2A3450,
    0x3B82F6, 0x263E68, 0xE7ECF5, 0x8491A8,
};

const ui_palette_t PALETTE_LIGHT = {
    0xF2F4F8, 0xFFFFFF, 0xE9EDF4, 0xD7DCE5,
    0x3B82F6, 0xD6E4FB, 0x1F2430, 0x6B7385,
};

bool theme_is_dark = true;

lv_obj_t *theme_button = nullptr;
lv_obj_t *theme_button_label = nullptr;

/* Fundo da tela ativa e gerenciado pelo tema (antes ficava no app_main). */
void apply_screen_background(void)
{
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    if (scr == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(scr, lv_color_hex(ui_theme_get()->background), 0);
}

void apply_theme_button(void)
{
    if (theme_button == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(theme_button, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(theme_button, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_color(theme_button, lv_color_hex(pal->accent), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(theme_button, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(theme_button, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(theme_button, lv_color_hex(pal->accent), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(theme_button, 2, LV_STATE_FOCUSED);

    if (theme_button_label != nullptr) {
        /* Rotulo mostra o modo atual; a fonte default nao cobre emoji. */
        lv_label_set_text(theme_button_label, theme_is_dark ? "Escuro" : "Claro");
        lv_obj_set_style_text_color(theme_button_label, lv_color_hex(pal->text), 0);
    }
}

void theme_button_click_cb(lv_event_t *event)
{
    (void)event;
    ui_theme_toggle();
}

} // namespace

const ui_palette_t *ui_theme_get(void)
{
    return theme_is_dark ? &PALETTE_DARK : &PALETTE_LIGHT;
}

bool ui_theme_is_dark(void)
{
    return theme_is_dark;
}

void ui_theme_toggle(void)
{
    theme_is_dark = !theme_is_dark;

    apply_screen_background();
    ui_keyboard_refresh_theme();
    ui_status_refresh_theme();
    apply_theme_button();
}

void ui_theme_init(lv_obj_t *parent)
{
    /* Pill de alternancia no canto superior esquerdo (espelho do badge). */
    theme_button = lv_obj_create(parent);
    lv_obj_set_size(theme_button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(theme_button, LV_ALIGN_TOP_LEFT);
    lv_obj_set_x(theme_button, 28);
    lv_obj_set_y(theme_button, 12);
    lv_obj_set_style_bg_opa(theme_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(theme_button, 1, 0);
    lv_obj_set_style_radius(theme_button, 10, 0);
    lv_obj_set_style_pad_left(theme_button, 12, 0);
    lv_obj_set_style_pad_right(theme_button, 12, 0);
    lv_obj_set_style_pad_top(theme_button, 5, 0);
    lv_obj_set_style_pad_bottom(theme_button, 5, 0);
    lv_obj_set_style_shadow_width(theme_button, 0, 0);
    lv_obj_add_event_cb(theme_button, theme_button_click_cb, LV_EVENT_CLICKED, nullptr);

    theme_button_label = lv_label_create(theme_button);
    lv_obj_set_style_text_font(theme_button_label, LV_FONT_DEFAULT, 0);

    apply_screen_background();
    apply_theme_button();
}
