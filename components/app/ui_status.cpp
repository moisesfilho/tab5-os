#include "ui_status.h"
#include "ui_theme.h"
#include "ui_font.h"

namespace {

lv_obj_t *status_label = nullptr;

/* Badge compacto, integrado a barra superior: texto simples sem fundo,
 * apenas com respiro a direita antes do relogio. */
void apply_status_theme(void)
{
    if (status_label == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_text_color(status_label, lv_color_hex(pal->text), 0);
}

} // namespace

void ui_status_init(lv_obj_t *parent)
{
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "0°");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_margin_right(status_label, 10, 0);

    apply_status_theme();
}

void ui_status_refresh_theme(void)
{
    apply_status_theme();
}

void ui_status_set_rotation(lv_disp_rotation_t rot)
{
    if (status_label == nullptr) {
        return;
    }

    switch (rot) {
    case LV_DISPLAY_ROTATION_90:
        lv_label_set_text(status_label, "90°");
        break;
    case LV_DISPLAY_ROTATION_180:
        lv_label_set_text(status_label, "180°");
        break;
    case LV_DISPLAY_ROTATION_270:
        lv_label_set_text(status_label, "270°");
        break;
    case LV_DISPLAY_ROTATION_0:
    default:
        lv_label_set_text(status_label, "0°");
        break;
    }
}
