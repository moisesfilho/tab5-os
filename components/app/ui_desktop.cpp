#include "ui_desktop.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_font.h"

namespace {

lv_obj_t *desktop_scr = nullptr;
lv_obj_t *app_tile = nullptr;
lv_obj_t *app_icon = nullptr;
lv_obj_t *app_icon_label = nullptr;
lv_obj_t *app_label = nullptr;

void app_tile_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_open_notas();
}

/* Reaplica a paleta ativa na area de trabalho. */
void apply_desktop_theme(void)
{
    if (desktop_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(desktop_scr, lv_color_hex(pal->background), 0);

    if (app_icon != nullptr) {
        lv_obj_set_style_bg_color(app_icon, lv_color_hex(pal->accent_soft), 0);
    }
    if (app_icon_label != nullptr) {
        lv_obj_set_style_text_color(app_icon_label, lv_color_hex(pal->accent), 0);
    }
    if (app_label != nullptr) {
        lv_obj_set_style_text_color(app_label, lv_color_hex(pal->text), 0);
    }
}

} // namespace

void ui_desktop_create(lv_obj_t *scr)
{
    desktop_scr = scr;

    /* Tile do app ancorado a esquerda, com margem da borda; coluna flex
     * para acomodar futuros icones (launcher de SO). */
    app_tile = lv_obj_create(scr);
    lv_obj_set_size(app_tile, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(app_tile, LV_ALIGN_TOP_LEFT, 16, UI_BAR_HEIGHT + 24);
    lv_obj_set_style_bg_opa(app_tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(app_tile, 0, 0);
    lv_obj_set_style_shadow_width(app_tile, 0, 0);
    lv_obj_set_style_pad_all(app_tile, 8, 0);
    lv_obj_clear_flag(app_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(app_tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(app_tile, app_tile_cb, LV_EVENT_CLICKED, nullptr);

    app_icon = lv_obj_create(app_tile);
    lv_obj_set_size(app_icon, 76, 76);
    lv_obj_set_style_radius(app_icon, 18, 0);
    lv_obj_set_style_border_width(app_icon, 0, 0);
    lv_obj_set_style_shadow_width(app_icon, 0, 0);
    lv_obj_clear_flag(app_icon, LV_OBJ_FLAG_CLICKABLE);

    app_icon_label = lv_label_create(app_icon);
    lv_label_set_text(app_icon_label, "N");
    lv_obj_set_style_text_font(app_icon_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_center(app_icon_label);

    app_label = lv_label_create(app_tile);
    lv_label_set_text(app_label, "Notas");
    lv_obj_set_style_text_font(app_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_top(app_label, 6, 0);

    apply_desktop_theme();
}

void ui_desktop_refresh_theme(void)
{
    apply_desktop_theme();
}
