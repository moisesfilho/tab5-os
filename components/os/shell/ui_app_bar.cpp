#include "ui_app_bar.h"
#include "ui_bar.h"
#include "ui_theme.h"
#include "ui_font.h"

ui_app_bar_t ui_app_bar_create(lv_obj_t *parent, const char *title, lv_event_cb_t on_close_cb, void *user_data)
{
    ui_app_bar_t app_bar = {};

    /* Barra própria do app, posicionada logo abaixo da barra de status do sistema */
    app_bar.bar = lv_obj_create(parent);
    lv_obj_set_size(app_bar.bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_align(app_bar.bar, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(app_bar.bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(app_bar.bar, 1, 0);
    lv_obj_set_style_border_side(app_bar.bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(app_bar.bar, 0, 0);
    lv_obj_set_style_shadow_width(app_bar.bar, 0, 0);
    lv_obj_set_style_pad_left(app_bar.bar, 12, 0);
    lv_obj_set_style_pad_right(app_bar.bar, 8, 0);
    lv_obj_set_style_pad_top(app_bar.bar, 0, 0);
    lv_obj_set_style_pad_bottom(app_bar.bar, 0, 0);
    lv_obj_clear_flag(app_bar.bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_bar.bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(app_bar.bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Título da aplicação à esquerda (flex grow) */
    app_bar.title_label = lv_label_create(app_bar.bar);
    lv_label_set_text(app_bar.title_label, title != nullptr ? title : "");
    lv_label_set_long_mode(app_bar.title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(app_bar.title_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_flex_grow(app_bar.title_label, 1);

    /* Container flexível para ações personalizadas da aplicação */
    app_bar.actions_cont = lv_obj_create(app_bar.bar);
    lv_obj_set_size(app_bar.actions_cont, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_set_style_bg_opa(app_bar.actions_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(app_bar.actions_cont, 0, 0);
    lv_obj_set_style_pad_all(app_bar.actions_cont, 0, 0);
    lv_obj_clear_flag(app_bar.actions_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_bar.actions_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(app_bar.actions_cont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Botão fechar padronizado à direita (estilo quadrado/retangular com raio 6) */
    app_bar.close_btn = lv_button_create(app_bar.bar);
    lv_obj_set_size(app_bar.close_btn, 36, 28);
    lv_obj_set_style_radius(app_bar.close_btn, 6, 0);
    lv_obj_set_style_border_width(app_bar.close_btn, 1, 0);
    lv_obj_set_style_shadow_width(app_bar.close_btn, 0, 0);
    lv_obj_set_style_pad_all(app_bar.close_btn, 0, 0);
    lv_obj_clear_flag(app_bar.close_btn, LV_OBJ_FLAG_SCROLLABLE);
    if (on_close_cb != nullptr) {
        lv_obj_add_event_cb(app_bar.close_btn, on_close_cb, LV_EVENT_CLICKED, user_data);
    }

    app_bar.close_label = lv_label_create(app_bar.close_btn);
    lv_label_set_text(app_bar.close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(app_bar.close_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(app_bar.close_label);

    ui_app_bar_refresh_theme(&app_bar);

    return app_bar;
}

lv_obj_t *ui_app_bar_add_action_button(ui_app_bar_t *app_bar, const char *symbol_or_text, lv_event_cb_t on_click_cb,
                                       void *user_data, lv_obj_t **out_label)
{
    if (app_bar == nullptr || app_bar->actions_cont == nullptr) {
        return nullptr;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_t *btn = lv_button_create(app_bar->actions_cont);
    lv_obj_set_size(btn, 36, 28);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_margin_right(btn, 6, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(pal->border), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    if (on_click_cb != nullptr) {
        lv_obj_add_event_cb(btn, on_click_cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol_or_text != nullptr ? symbol_or_text : "");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(pal->text), 0);
    lv_obj_center(label);

    if (out_label != nullptr) {
        *out_label = label;
    }

    return btn;
}

void ui_app_bar_set_title(ui_app_bar_t *app_bar, const char *title)
{
    if (app_bar == nullptr || app_bar->title_label == nullptr) {
        return;
    }
    lv_label_set_text(app_bar->title_label, title != nullptr ? title : "");
}

void ui_app_bar_refresh_theme(ui_app_bar_t *app_bar)
{
    if (app_bar == nullptr || app_bar->bar == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    /* Barra */
    lv_obj_set_style_bg_color(app_bar->bar, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_border_color(app_bar->bar, lv_color_hex(pal->border), 0);

    /* Título */
    if (app_bar->title_label != nullptr) {
        lv_obj_set_style_text_color(app_bar->title_label, lv_color_hex(pal->text), 0);
    }

    /* Botão fechar */
    if (app_bar->close_btn != nullptr) {
        lv_obj_set_style_bg_opa(app_bar->close_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(app_bar->close_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(app_bar->close_btn, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(app_bar->close_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (app_bar->close_label != nullptr) {
        lv_obj_set_style_text_color(app_bar->close_label, lv_color_hex(pal->text), 0);
    }

    /* Botões no container de ações */
    if (app_bar->actions_cont != nullptr) {
        uint32_t count = lv_obj_get_child_count(app_bar->actions_cont);
        for (uint32_t i = 0; i < count; i++) {
            lv_obj_t *child = lv_obj_get_child(app_bar->actions_cont, i);
            if (child != nullptr) {
                if (lv_obj_check_type(child, &lv_button_class)) {
                    lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
                    lv_obj_set_style_bg_color(child, lv_color_hex(pal->surface), 0);
                    lv_obj_set_style_border_color(child, lv_color_hex(pal->border), 0);
                    lv_obj_set_style_bg_color(child, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);

                    uint32_t inner_count = lv_obj_get_child_count(child);
                    for (uint32_t j = 0; j < inner_count; j++) {
                        lv_obj_t *inner = lv_obj_get_child(child, j);
                        if (inner != nullptr && lv_obj_check_type(inner, &lv_label_class)) {
                            lv_obj_set_style_text_color(inner, lv_color_hex(pal->text), 0);
                        }
                    }
                }
            }
        }
    }
}
