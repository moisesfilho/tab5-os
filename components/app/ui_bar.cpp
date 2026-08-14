#include "ui_bar.h"
#include "ui_theme.h"
#include "ui_status.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "imu_reader.h"
#include <time.h>

namespace {

lv_obj_t *bar = nullptr;
lv_obj_t *gear = nullptr;
lv_obj_t *gear_label = nullptr;
lv_obj_t *clock_label = nullptr;

/* Menu Configuracao (overlay + painel) */
lv_obj_t *menu_overlay = nullptr;
lv_obj_t *menu_panel = nullptr;
lv_obj_t *menu_header_label = nullptr;
lv_obj_t *menu_row_theme = nullptr;
lv_obj_t *menu_row_theme_label = nullptr;
lv_obj_t *menu_chevron_label = nullptr;
lv_obj_t *menu_row_light = nullptr;
lv_obj_t *menu_row_light_label = nullptr;
lv_obj_t *menu_row_dark = nullptr;
lv_obj_t *menu_row_dark_label = nullptr;
lv_obj_t *menu_row_back = nullptr;
lv_obj_t *menu_row_back_label = nullptr;
lv_obj_t *menu_row_rotation_label = nullptr;
lv_obj_t *menu_row_rotation_switch = nullptr;

void close_menu(void);

void menu_overlay_cb(lv_event_t *event)
{
    (void)event;
    close_menu();
}

void close_menu(void)
{
    if (menu_overlay != nullptr) {
        lv_obj_delete(menu_overlay);
        menu_overlay = nullptr;
    }
    if (menu_panel != nullptr) {
        lv_obj_delete(menu_panel);
        menu_panel = nullptr;
    }

    menu_header_label = nullptr;
    menu_row_theme = nullptr;
    menu_row_theme_label = nullptr;
    menu_chevron_label = nullptr;
    menu_row_light = nullptr;
    menu_row_light_label = nullptr;
    menu_row_dark = nullptr;
    menu_row_dark_label = nullptr;
    menu_row_back = nullptr;
    menu_row_back_label = nullptr;
    menu_row_rotation_label = nullptr;
    menu_row_rotation_switch = nullptr;
}

void menu_header_create(const char *text)
{
    menu_header_label = lv_label_create(menu_panel);
    lv_label_set_text(menu_header_label, text);
    lv_obj_set_style_text_font(menu_header_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_left(menu_header_label, 14, 0);
    lv_obj_set_style_pad_right(menu_header_label, 14, 0);
    lv_obj_set_style_pad_top(menu_header_label, 12, 0);
    lv_obj_set_style_pad_bottom(menu_header_label, 4, 0);
}

/* Item do menu: linha clicavel com label. `chevron` adiciona uma seta
 * discreta a direita (usa LV_SYMBOL_RIGHT, coberto pela fonte default). */
void menu_row_create(const char *text, lv_event_cb_t cb,
                     lv_obj_t **out_row, lv_obj_t **out_label, bool chevron)
{
    lv_obj_t *row = lv_obj_create(menu_panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14_latin1, 0);

    if (out_row != nullptr) {
        *out_row = row;
    }
    if (out_label != nullptr) {
        *out_label = label;
    }

    if (chevron) {
        menu_chevron_label = lv_label_create(row);
        lv_label_set_text(menu_chevron_label, LV_SYMBOL_RIGHT);
        lv_obj_set_align(menu_chevron_label, LV_ALIGN_RIGHT_MID);
        lv_obj_set_x(menu_chevron_label, -14);
        lv_obj_set_style_text_font(menu_chevron_label, &lv_font_montserrat_14_latin1, 0);
    }
}

/* Reestiliza um item de tema: o ativo ganha fundo accent_soft e texto
 * accent (indicador tipo radio); o inativo fica com texto normal. */
void apply_theme_item(lv_obj_t *row, lv_obj_t *label, bool active)
{
    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_opa(row, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(pal->accent_soft), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(active ? pal->accent : pal->text), 0);
}

/* Aplica a paleta atual ao painel do menu (se estiver aberto). */
void apply_menu_theme(void)
{
    if (menu_panel == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(menu_panel, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(menu_panel, lv_color_hex(pal->border), 0);

    if (menu_header_label != nullptr) {
        lv_obj_set_style_text_color(menu_header_label, lv_color_hex(pal->text_muted), 0);
    }
    if (menu_row_theme_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_theme_label, lv_color_hex(pal->text), 0);
    }
    if (menu_chevron_label != nullptr) {
        lv_obj_set_style_text_color(menu_chevron_label, lv_color_hex(pal->text_muted), 0);
    }
    if (menu_row_back_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_back_label, lv_color_hex(pal->text_muted), 0);
    }
    if (menu_row_light_label != nullptr && menu_row_light != nullptr) {
        apply_theme_item(menu_row_light, menu_row_light_label, !ui_theme_is_dark());
    }
    if (menu_row_dark_label != nullptr && menu_row_dark != nullptr) {
        apply_theme_item(menu_row_dark, menu_row_dark_label, ui_theme_is_dark());
    }
    if (menu_row_rotation_label != nullptr) {
        lv_obj_set_style_text_color(menu_row_rotation_label, lv_color_hex(pal->text), 0);
    }
    if (menu_row_rotation_switch != nullptr) {
        lv_obj_set_style_bg_color(menu_row_rotation_switch, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(menu_row_rotation_switch, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(menu_row_rotation_switch, lv_color_hex(pal->accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(menu_row_rotation_switch, lv_color_hex(pal->text), LV_PART_KNOB);
    }
}

void open_menu(bool theme_page);

void menu_theme_cb(lv_event_t *event)
{
    (void)event;
    open_menu(true);
}

void menu_light_cb(lv_event_t *event)
{
    (void)event;
    ui_theme_set(false);
    close_menu();
}

void menu_dark_cb(lv_event_t *event)
{
    (void)event;
    ui_theme_set(true);
    close_menu();
}

void menu_back_cb(lv_event_t *event)
{
    (void)event;
    open_menu(false);
}

/* Repassa o estado do switch de rotacao para o modulo do IMU. */
void rotation_switch_cb(lv_event_t *event)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(event);
    imu_reader_set_rotation_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

/* Row "Rotação" com switch de liga/desliga no estilo SO. */
void menu_rotation_row_create(void)
{
    lv_obj_t *row = lv_obj_create(menu_panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);

    menu_row_rotation_label = lv_label_create(row);
    lv_label_set_text(menu_row_rotation_label, "Rotação");
    lv_obj_set_style_text_font(menu_row_rotation_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(menu_row_rotation_label, LV_ALIGN_LEFT_MID, 14, 0);

    menu_row_rotation_switch = lv_switch_create(row);
    lv_obj_set_size(menu_row_rotation_switch, 44, 24);
    lv_obj_align(menu_row_rotation_switch, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_radius(menu_row_rotation_switch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(menu_row_rotation_switch, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(menu_row_rotation_switch, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(menu_row_rotation_switch, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(menu_row_rotation_switch, rotation_switch_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    if (imu_reader_is_rotation_enabled()) {
        lv_obj_add_state(menu_row_rotation_switch, LV_STATE_CHECKED);
    }
}

void gear_click_cb(lv_event_t *event)
{
    (void)event;
    open_menu(false);
}

/* Popover ancorado sob a engrenagem, com overlay transparente que fecha
 * ao tocar fora. `theme_page` escolhe a pagina inicial do painel. */
void open_menu(bool theme_page)
{
    close_menu();

    lv_obj_t *layer = lv_layer_top();

    menu_overlay = lv_obj_create(layer);
    lv_obj_set_size(menu_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(menu_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_overlay, 0, 0);
    lv_obj_set_style_shadow_width(menu_overlay, 0, 0);
    lv_obj_clear_flag(menu_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(menu_overlay, menu_overlay_cb, LV_EVENT_CLICKED, nullptr);

    menu_panel = lv_obj_create(layer);
    lv_obj_set_size(menu_panel, 220, LV_SIZE_CONTENT);
    lv_obj_set_align(menu_panel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(menu_panel, 10, UI_BAR_HEIGHT + 8);
    lv_obj_set_style_bg_opa(menu_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(menu_panel, 1, 0);
    lv_obj_set_style_radius(menu_panel, 12, 0);
    lv_obj_set_style_shadow_width(menu_panel, 12, 0);
    lv_obj_set_style_shadow_opa(menu_panel, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(menu_panel, 4, 0);
    lv_obj_clear_flag(menu_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Empilha header e itens verticalmente. */
    lv_obj_set_flex_flow(menu_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (!theme_page) {
        menu_header_create("Configuração");
        menu_row_create("Tema", menu_theme_cb, &menu_row_theme, &menu_row_theme_label, true);
        menu_rotation_row_create();
    } else {
        menu_header_create("Tema");
        menu_row_create("Claro", menu_light_cb, &menu_row_light, &menu_row_light_label, false);
        menu_row_create("Escuro", menu_dark_cb, &menu_row_dark, &menu_row_dark_label, false);
        menu_row_create("Voltar", menu_back_cb, &menu_row_back, &menu_row_back_label, false);
    }

    apply_menu_theme();
}

void clock_update(void)
{
    if (clock_label == nullptr) {
        return;
    }

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char buf[32];
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &t);
    lv_label_set_text(clock_label, buf);
}

void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    clock_update();
}

} // namespace

void ui_bar_refresh_theme(void)
{
    if (bar == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(bar, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(pal->border), 0);

    if (gear_label != nullptr) {
        lv_obj_set_style_text_color(gear_label, lv_color_hex(pal->text), 0);
        lv_obj_set_style_text_color(gear_label, lv_color_hex(pal->accent), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(gear_label, lv_color_hex(pal->accent), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(gear, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }

    if (clock_label != nullptr) {
        lv_obj_set_style_text_color(clock_label, lv_color_hex(pal->text), 0);
    }

    apply_menu_theme();

    /* O tema tambem alcanca as telas do shell (desktop e apps). */
    ui_shell_refresh_theme();
}

void ui_bar_init(lv_obj_t *parent)
{
    /* A barra vive no layer top; o fundo da tela e do shell/tema. */
    bar = lv_obj_create(parent);
    lv_obj_set_size(bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_set_align(bar, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Layout flex: engrenagem a esquerda, espaco flexivel, status e
     * relogio a direita. */
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Engrenagem: abre o menu de configuracao. */
    gear = lv_obj_create(bar);
    lv_obj_set_size(gear, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(gear, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gear, 0, 0);
    lv_obj_set_style_shadow_width(gear, 0, 0);
    lv_obj_set_style_radius(gear, 8, 0);
    lv_obj_set_style_pad_left(gear, 12, 0);
    lv_obj_set_style_pad_right(gear, 12, 0);
    lv_obj_set_style_pad_top(gear, 6, 0);
    lv_obj_set_style_pad_bottom(gear, 6, 0);
    /* Sem scroll: remove os scrollbars e o arraste da engrenagem. */
    lv_obj_clear_flag(gear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(gear, gear_click_cb, LV_EVENT_CLICKED, nullptr);

    gear_label = lv_label_create(gear);
    lv_label_set_text(gear_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gear_label, &lv_font_montserrat_14_latin1, 0);

    /* Espacador flexivel empurra badge e relogio para a direita. */
    lv_obj_t *spacer = lv_obj_create(bar);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_shadow_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    /* Badge de orientacao integrado a barra (compacto). */
    ui_status_init(bar);

    /* Relogio no formato brasileiro (dd/mm/aaaa hh:mm:ss). */
    clock_label = lv_label_create(bar);
    lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_margin_right(clock_label, 12, 0);
    lv_timer_create(clock_timer_cb, 1000, nullptr);
    clock_update();

    ui_bar_refresh_theme();
}
