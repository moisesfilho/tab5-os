#include "ui_theme.h"
#include "ui_keyboard.h"
#include "ui_status.h"
#include "ui_bar.h"
#include "tab5_ui_host.h"

namespace {

/* Ordem dos campos: background, surface, surface_alt, border, accent,
 * accent_soft, text, text_muted (posicional, compativel com C++11). */
/* clang-format off (tabela 2x4, posicional) */
const ui_palette_t PALETTE_DARK = {
    0x10141C, 0x1A2130, 0x202A3D, 0x2A3450, 0x3B82F6, 0x263E68, 0xE7ECF5, 0x8491A8,
};

const ui_palette_t PALETTE_LIGHT = {
    0xF2F4F8, 0xFFFFFF, 0xE9EDF4, 0xD7DCE5, 0x3B82F6, 0xD6E4FB, 0x1F2430, 0x6B7385,
};
/* clang-format on */

bool theme_is_dark = true;

/* O fundo da tela ativa e gerenciado pelo tema (antes ficava no app_main). */
void apply_screen_background(void)
{
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    if (scr == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(scr, lv_color_hex(ui_theme_get()->background), 0);
}

/* Reaplica a paleta ativa em todos os modulos da aplicacao. */
void apply_theme_all(void)
{
    apply_screen_background();
    ui_keyboard_refresh_theme();
    ui_status_refresh_theme();
    ui_bar_refresh_theme();
    tab5_ui_host_refresh_theme();
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

void ui_theme_set(bool dark)
{
    if (theme_is_dark == dark) {
        return;
    }

    theme_is_dark = dark;
    apply_theme_all();
}

void ui_theme_toggle(void)
{
    ui_theme_set(!theme_is_dark);
}
