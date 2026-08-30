#include "ui_desktop.h"
#include "app_registry.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_font.h"

#include <climits>
#include <cstring>
#include <cstdlib>

namespace {

lv_obj_t *desktop_scr = nullptr;
lv_obj_t *grid_cont = nullptr;

/* Centraliza a label do icone usando a caixa real dos glifos (nao a caixa
 * do widget). Sem isso, simbolos e textos com metricas diferentes (ex.: ">_"
 * ou LV_SYMBOL_AUDIO) aparecem deslocados dentro do botao. */
static void center_icon_label_optically(lv_obj_t *icon_label)
{
    const lv_font_t *font = lv_obj_get_style_text_font(icon_label, LV_PART_MAIN);
    if (font == nullptr || font->line_height <= 0) {
        lv_obj_center(icon_label);
        return;
    }

    const char *txt = lv_label_get_text(icon_label);
    if (txt == nullptr || txt[0] == '\0') {
        lv_obj_center(icon_label);
        return;
    }

    int32_t min_top = INT32_MAX;
    int32_t max_bottom = INT32_MIN;
    for (const uint8_t *p = (const uint8_t *)txt; *p != '\0'; p++) {
        lv_font_glyph_dsc_t gdsc;
        if (!lv_font_get_glyph_dsc(font, &gdsc, *p, 0)) {
            continue;
        }
        int32_t top = font->line_height - font->base_line - gdsc.box_h - gdsc.ofs_y;
        int32_t bottom = top + gdsc.box_h;
        if (top < min_top) {
            min_top = top;
        }
        if (bottom > max_bottom) {
            max_bottom = bottom;
        }
    }

    if (min_top == INT32_MAX) {
        lv_obj_center(icon_label);
        return;
    }

    int32_t glyph_center = (min_top + max_bottom) / 2;
    int32_t y_comp = font->line_height / 2 - glyph_center;
    lv_obj_align(icon_label, LV_ALIGN_CENTER, 0, y_comp);
}

/* Converte "#RRGGBB" (ou "RRGGBB") para lv_color_t; retorna true se válido. */
static bool parse_hex_color(const char *str, lv_color_t *out)
{
    if (str == nullptr || out == nullptr) {
        return false;
    }
    const char *p = str;
    if (p[0] == '#') {
        p++;
    }
    if (strlen(p) != 6) {
        return false;
    }
    char buf[7] = {0};
    memcpy(buf, p, 6);
    char *end = nullptr;
    unsigned long val = strtoul(buf, &end, 16);
    if (end != buf + 6) {
        return false;
    }
    *out = lv_color_hex(val);
    return true;
}

static void update_grid_padding(lv_obj_t *cont)
{
    if (cont == nullptr) {
        return;
    }
    int32_t scr_w = lv_display_get_horizontal_resolution(nullptr);
    if (scr_w <= 0) {
        scr_w = 1280;
    }

    const int32_t tile_w = 108;
    const int32_t gap = 24;
    const int32_t min_pad = 20;

    int cols = (scr_w - 2 * min_pad + gap) / (tile_w + gap);
    if (cols < 1) {
        cols = 1;
    }
    if (cols > 8) {
        cols = 8;
    }

    int32_t grid_w = cols * tile_w + (cols - 1) * gap;
    int32_t pad_hor = (scr_w - grid_w) / 2;
    if (pad_hor < min_pad) {
        pad_hor = min_pad;
    }

    lv_obj_set_style_pad_left(cont, pad_hor, 0);
    lv_obj_set_style_pad_right(cont, pad_hor, 0);
}

static void app_tile_click_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    const char *app_id = static_cast<const char *>(lv_event_get_user_data(event));
    if (app_id != nullptr) {
        const app_desc_t *app = app_registry_find_by_id(app_id);
        if (app != nullptr && app->on_launch != nullptr) {
            app->on_launch();
        }
    }
}

/* Reaplica a paleta ativa na area de trabalho. */
void apply_desktop_theme(void)
{
    if (desktop_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(desktop_scr, lv_color_hex(pal->background), 0);

    if (grid_cont != nullptr) {
        uint32_t tile_count = lv_obj_get_child_count(grid_cont);
        const auto &apps = app_registry_get_all();

        for (uint32_t i = 0; i < tile_count && i < apps.size(); i++) {
            lv_obj_t *tile = lv_obj_get_child(grid_cont, i);
            if (tile == nullptr) {
                continue;
            }

            const app_desc_t &app = apps[i];
            lv_obj_t *icon_box = lv_obj_get_child(tile, 0);
            lv_obj_t *app_label = lv_obj_get_child(tile, 1);

            if (icon_box != nullptr) {
                lv_color_t bg = lv_color_hex(pal->accent_soft);
                if (app.icon_bg_color != nullptr && app.icon_bg_color[0] != '\0') {
                    parse_hex_color(app.icon_bg_color, &bg);
                }
                lv_obj_set_style_bg_color(icon_box, bg, 0);

                if (app.icon_theme_refresh != nullptr) {
                    app.icon_theme_refresh(icon_box);
                } else {
                    uint32_t inner_count = lv_obj_get_child_count(icon_box);
                    for (uint32_t j = 0; j < inner_count; j++) {
                        lv_obj_t *inner = lv_obj_get_child(icon_box, j);
                        if (inner != nullptr && lv_obj_check_type(inner, &lv_label_class)) {
                            if (app.icon_bg_color != nullptr && app.icon_bg_color[0] != '\0') {
                                lv_obj_set_style_text_color(inner, lv_color_hex(0xFFFFFF), 0);
                            } else {
                                lv_obj_set_style_text_color(inner, lv_color_hex(pal->accent), 0);
                            }
                        }
                    }
                }
            }

            if (app_label != nullptr && lv_obj_check_type(app_label, &lv_label_class)) {
                lv_obj_set_style_text_color(app_label, lv_color_hex(pal->text), 0);
            }
        }

        update_grid_padding(grid_cont);
    }
}

} // namespace

void ui_desktop_create(lv_obj_t *scr)
{
    desktop_scr = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    /* Container responsivo para os tiles de aplicativos (Grid centralizado, preenchimento esquerda->direita) */
    grid_cont = lv_obj_create(scr);
    lv_obj_set_size(grid_cont, lv_pct(100), lv_pct(100));
    lv_obj_align(grid_cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(grid_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid_cont, 0, 0);
    lv_obj_set_style_shadow_width(grid_cont, 0, 0);
    lv_obj_set_style_pad_top(grid_cont, UI_BAR_HEIGHT + 16, 0);
    lv_obj_set_style_pad_bottom(grid_cont, 16, 0);
    lv_obj_set_style_pad_row(grid_cont, 20, 0);
    lv_obj_set_style_pad_column(grid_cont, 24, 0);
    lv_obj_clear_flag(grid_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(grid_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(grid_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    update_grid_padding(grid_cont);

    lv_obj_add_event_cb(
        grid_cont, [](lv_event_t *e) { update_grid_padding((lv_obj_t *)lv_event_get_target(e)); },
        LV_EVENT_SIZE_CHANGED, nullptr);

    /* Constroi dinamicamente os tiles de cada aplicacao registrada no SO */
    const auto &apps = app_registry_get_all();
    for (const auto &app : apps) {
        lv_obj_t *tile = lv_obj_create(grid_cont);
        lv_obj_set_size(tile, 108, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_shadow_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 8, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(tile, app_tile_click_cb, LV_EVENT_ALL, (void *)app.id);

        lv_obj_t *icon_box = lv_obj_create(tile);
        lv_obj_set_size(icon_box, 84, 84);
        lv_obj_set_style_radius(icon_box, 20, 0);
        lv_obj_set_style_border_width(icon_box, 0, 0);
        lv_obj_set_style_shadow_width(icon_box, 0, 0);
        lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(icon_box, LV_SCROLLBAR_MODE_OFF);

        if (app.icon_builder != nullptr) {
            app.icon_builder(icon_box);
        } else {
            lv_obj_t *icon_label = lv_label_create(icon_box);
            lv_label_set_text(icon_label, app.icon_symbol != nullptr ? app.icon_symbol : "");
            lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_28_latin1, 0);
            center_icon_label_optically(icon_label);
        }

        lv_obj_t *label = lv_label_create(tile);
        lv_label_set_text(label, app.name != nullptr ? app.name : "");
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_pad_top(label, 6, 0);
    }

    apply_desktop_theme();
}

void ui_desktop_refresh_theme(void)
{
    apply_desktop_theme();
}
