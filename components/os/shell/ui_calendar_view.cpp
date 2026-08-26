#include "ui_calendar_view.h"
#include "calendar_logic.h"
#include "ui_theme.h"
#include "ui_font.h"
#include <cstdio>

namespace {

void nav_prev_cb(lv_event_t *e)
{
    ui_calendar_view_t *cal = static_cast<ui_calendar_view_t *>(lv_event_get_user_data(e));
    if (cal != nullptr) {
        calendar_logic_prev_month(&cal->view_year, &cal->view_month);
        cal->selected_day = 0;
        ui_calendar_view_refresh(cal);
    }
}

void nav_next_cb(lv_event_t *e)
{
    ui_calendar_view_t *cal = static_cast<ui_calendar_view_t *>(lv_event_get_user_data(e));
    if (cal != nullptr) {
        calendar_logic_next_month(&cal->view_year, &cal->view_month);
        cal->selected_day = 0;
        ui_calendar_view_refresh(cal);
    }
}

void nav_today_cb(lv_event_t *e)
{
    ui_calendar_view_t *cal = static_cast<ui_calendar_view_t *>(lv_event_get_user_data(e));
    if (cal != nullptr) {
        ui_calendar_view_update_today(cal);
        cal->view_year = cal->today_year;
        cal->view_month = cal->today_month;
        cal->selected_day = cal->today_day;
        ui_calendar_view_refresh(cal);
    }
}

void day_cell_click_cb(lv_event_t *e)
{
    ui_calendar_view_t *cal = static_cast<ui_calendar_view_t *>(lv_event_get_user_data(e));
    if (cal == nullptr) {
        return;
    }
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
    for (int i = 0; i < 42; i++) {
        if (cal->day_cells[i] == target) {
            int first_dow = calendar_logic_get_first_day_of_week(cal->view_year, cal->view_month);
            int days_in_m = calendar_logic_get_days_in_month(cal->view_year, cal->view_month);
            if (i >= first_dow && i < first_dow + days_in_m) {
                cal->selected_day = i - first_dow + 1;
                ui_calendar_view_refresh(cal);
            }
            break;
        }
    }
}

} // namespace

ui_calendar_view_t ui_calendar_view_create(lv_obj_t *parent, bool is_popup)
{
    ui_calendar_view_t cal = {};
    cal.is_popup = is_popup;
    cal.selected_day = 0;

    ui_calendar_view_update_today(&cal);
    cal.view_year = cal.today_year;
    cal.view_month = cal.today_month;

    const ui_palette_t *pal = ui_theme_get();

    /* Container raiz */
    cal.container = lv_obj_create(parent);
    lv_obj_set_size(cal.container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cal.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cal.container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cal.container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cal.container, 0, 0);
    lv_obj_set_style_pad_all(cal.container, is_popup ? 8 : 12, 0);
    lv_obj_set_style_pad_row(cal.container, is_popup ? 8 : 12, 0);
    lv_obj_clear_flag(cal.container, LV_OBJ_FLAG_SCROLLABLE);

    /* Cabeçalho de navegação */
    cal.header_cont = lv_obj_create(cal.container);
    lv_obj_set_size(cal.header_cont, lv_pct(100), is_popup ? 40 : 48);
    lv_obj_set_flex_flow(cal.header_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cal.header_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cal.header_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cal.header_cont, 0, 0);
    lv_obj_set_style_pad_all(cal.header_cont, 0, 0);
    lv_obj_clear_flag(cal.header_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Botão Anterior */
    cal.prev_btn = lv_btn_create(cal.header_cont);
    lv_obj_set_size(cal.prev_btn, is_popup ? 36 : 42, is_popup ? 36 : 42);
    lv_obj_set_style_radius(cal.prev_btn, 8, 0);
    lv_obj_set_style_bg_color(cal.prev_btn, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_width(cal.prev_btn, 1, 0);
    lv_obj_set_style_border_color(cal.prev_btn, lv_color_hex(pal->border), 0);
    lv_obj_set_style_shadow_width(cal.prev_btn, 0, 0);
    cal.prev_lbl = lv_label_create(cal.prev_btn);
    lv_label_set_text(cal.prev_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(cal.prev_lbl, lv_color_hex(pal->text), 0);
    lv_obj_center(cal.prev_lbl);

    /* Título (Mês e Ano) */
    cal.title_label = lv_label_create(cal.header_cont);
    lv_obj_set_style_text_font(cal.title_label,
                               is_popup ? &lv_font_montserrat_14_latin1 : &lv_font_montserrat_28_latin1, 0);
    lv_obj_set_style_text_color(cal.title_label, lv_color_hex(pal->text), 0);
    lv_obj_set_style_text_align(cal.title_label, LV_TEXT_ALIGN_CENTER, 0);

    /* Grupo da direita: Botão Próximo e Botão Hoje */
    lv_obj_t *right_actions = lv_obj_create(cal.header_cont);
    lv_obj_set_size(right_actions, LV_SIZE_CONTENT, is_popup ? 36 : 42);
    lv_obj_set_flex_flow(right_actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(right_actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_actions, 0, 0);
    lv_obj_set_style_pad_all(right_actions, 0, 0);
    lv_obj_set_style_pad_column(right_actions, 6, 0);
    lv_obj_clear_flag(right_actions, LV_OBJ_FLAG_SCROLLABLE);

    /* Botão Hoje */
    cal.today_btn = lv_btn_create(right_actions);
    lv_obj_set_size(cal.today_btn, LV_SIZE_CONTENT, is_popup ? 36 : 42);
    lv_obj_set_style_radius(cal.today_btn, 8, 0);
    lv_obj_set_style_bg_color(cal.today_btn, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_width(cal.today_btn, 1, 0);
    lv_obj_set_style_border_color(cal.today_btn, lv_color_hex(pal->border), 0);
    lv_obj_set_style_shadow_width(cal.today_btn, 0, 0);
    lv_obj_set_style_pad_hor(cal.today_btn, 10, 0);
    cal.today_lbl = lv_label_create(cal.today_btn);
    lv_label_set_text(cal.today_lbl, "Hoje");
    lv_obj_set_style_text_color(cal.today_lbl, lv_color_hex(pal->text), 0);
    lv_obj_set_style_text_font(cal.today_lbl, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(cal.today_lbl);

    /* Botão Próximo */
    cal.next_btn = lv_btn_create(right_actions);
    lv_obj_set_size(cal.next_btn, is_popup ? 36 : 42, is_popup ? 36 : 42);
    lv_obj_set_style_radius(cal.next_btn, 8, 0);
    lv_obj_set_style_bg_color(cal.next_btn, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_width(cal.next_btn, 1, 0);
    lv_obj_set_style_border_color(cal.next_btn, lv_color_hex(pal->border), 0);
    lv_obj_set_style_shadow_width(cal.next_btn, 0, 0);
    cal.next_lbl = lv_label_create(cal.next_btn);
    lv_label_set_text(cal.next_lbl, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(cal.next_lbl, lv_color_hex(pal->text), 0);
    lv_obj_center(cal.next_lbl);

    /* Linha dos dias da semana com separadores */
    cal.weekdays_cont = lv_obj_create(cal.container);
    lv_obj_set_size(cal.weekdays_cont, lv_pct(100), is_popup ? 28 : 34);
    lv_obj_set_flex_flow(cal.weekdays_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cal.weekdays_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cal.weekdays_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cal.weekdays_cont, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_border_width(cal.weekdays_cont, 1, 0);
    lv_obj_set_style_border_color(cal.weekdays_cont, lv_color_hex(pal->border), 0);
    lv_obj_set_style_radius(cal.weekdays_cont, 6, 0);
    lv_obj_set_style_pad_all(cal.weekdays_cont, 0, 0);
    lv_obj_set_style_pad_row(cal.weekdays_cont, 0, 0);
    lv_obj_set_style_pad_column(cal.weekdays_cont, 0, 0);
    lv_obj_set_style_margin_all(cal.weekdays_cont, 0, 0);
    lv_obj_clear_flag(cal.weekdays_cont, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 7; i++) {
        lv_obj_t *wday_box = lv_obj_create(cal.weekdays_cont);
        lv_obj_set_size(wday_box, 0, lv_pct(100));
        lv_obj_set_flex_grow(wday_box, 1);
        lv_obj_set_style_bg_opa(wday_box, LV_OPA_TRANSP, 0);
        if (i < 6) {
            lv_obj_set_style_border_width(wday_box, 1, 0);
            lv_obj_set_style_border_side(wday_box, LV_BORDER_SIDE_RIGHT, 0);
            lv_obj_set_style_border_color(wday_box, lv_color_hex(pal->border), 0);
        } else {
            lv_obj_set_style_border_width(wday_box, 0, 0);
        }
        lv_obj_set_style_radius(wday_box, 0, 0);
        lv_obj_set_style_pad_all(wday_box, 0, 0);
        lv_obj_set_style_margin_all(wday_box, 0, 0);
        lv_obj_clear_flag(wday_box, LV_OBJ_FLAG_SCROLLABLE);

        cal.weekday_labels[i] = lv_label_create(wday_box);
        lv_label_set_text(cal.weekday_labels[i], calendar_logic_get_weekday_name(i));
        lv_obj_set_style_text_font(cal.weekday_labels[i], &lv_font_montserrat_14_latin1, 0);
        lv_obj_set_style_text_color(cal.weekday_labels[i], lv_color_hex(pal->text_muted), 0);
        lv_obj_center(cal.weekday_labels[i]);
    }

    /* Container da grade 6 linhas x 7 colunas (estilo tabela com bordas conectadas) */
    cal.grid_cont = lv_obj_create(cal.container);
    lv_obj_set_size(cal.grid_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cal.grid_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cal.grid_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cal.grid_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cal.grid_cont, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_width(cal.grid_cont, 1, 0);
    lv_obj_set_style_border_color(cal.grid_cont, lv_color_hex(pal->border), 0);
    lv_obj_set_style_radius(cal.grid_cont, 8, 0);
    lv_obj_set_style_pad_all(cal.grid_cont, 0, 0);
    lv_obj_set_style_pad_row(cal.grid_cont, 0, 0);
    lv_obj_set_style_pad_column(cal.grid_cont, 0, 0);
    lv_obj_set_style_margin_all(cal.grid_cont, 0, 0);
    lv_obj_clear_flag(cal.grid_cont, LV_OBJ_FLAG_SCROLLABLE);

    int cell_idx = 0;
    for (int r = 0; r < 6; r++) {
        lv_obj_t *row_obj = lv_obj_create(cal.grid_cont);
        lv_obj_set_size(row_obj, lv_pct(100), is_popup ? 36 : 48);
        lv_obj_set_flex_flow(row_obj, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row_obj, LV_OPA_TRANSP, 0);
        if (r < 5) {
            lv_obj_set_style_border_width(row_obj, 1, 0);
            lv_obj_set_style_border_side(row_obj, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(row_obj, lv_color_hex(pal->border), 0);
        } else {
            lv_obj_set_style_border_width(row_obj, 0, 0);
        }
        lv_obj_set_style_radius(row_obj, 0, 0);
        lv_obj_set_style_pad_all(row_obj, 0, 0);
        lv_obj_set_style_pad_row(row_obj, 0, 0);
        lv_obj_set_style_pad_column(row_obj, 0, 0);
        lv_obj_set_style_margin_all(row_obj, 0, 0);
        lv_obj_clear_flag(row_obj, LV_OBJ_FLAG_SCROLLABLE);

        for (int c = 0; c < 7; c++) {
            lv_obj_t *cell = lv_obj_create(row_obj);
            lv_obj_set_size(cell, 0, lv_pct(100));
            lv_obj_set_flex_grow(cell, 1);
            if (c < 6) {
                lv_obj_set_style_border_width(cell, 1, 0);
                lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_RIGHT, 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(pal->border), 0);
            } else {
                lv_obj_set_style_border_width(cell, 0, 0);
            }
            lv_obj_set_style_radius(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_margin_all(cell, 0, 0);
            lv_obj_set_style_shadow_width(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *lbl = lv_label_create(cell);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14_latin1, 0);
            lv_obj_center(lbl);

            cal.day_cells[cell_idx] = cell;
            cal.day_labels[cell_idx] = lbl;
            cell_idx++;
        }
    }

    /* Passa ponteiro alocado estático na heap para os callbacks */
    ui_calendar_view_t *heap_cal = new ui_calendar_view_t(cal);
    lv_obj_set_user_data(cal.container, heap_cal);

    lv_obj_add_event_cb(cal.prev_btn, nav_prev_cb, LV_EVENT_CLICKED, heap_cal);
    lv_obj_add_event_cb(cal.next_btn, nav_next_cb, LV_EVENT_CLICKED, heap_cal);
    lv_obj_add_event_cb(cal.today_btn, nav_today_cb, LV_EVENT_CLICKED, heap_cal);

    for (int i = 0; i < 42; i++) {
        lv_obj_add_event_cb(cal.day_cells[i], day_cell_click_cb, LV_EVENT_CLICKED, heap_cal);
    }

    ui_calendar_view_refresh(heap_cal);
    return *heap_cal;
}

void ui_calendar_view_set_date(ui_calendar_view_t *cal, int year, int month)
{
    if (cal == nullptr) {
        return;
    }
    cal->view_year = year;
    cal->view_month = month;
    ui_calendar_view_refresh(cal);
}

void ui_calendar_view_update_today(ui_calendar_view_t *cal)
{
    if (cal == nullptr) {
        return;
    }
    calendar_logic_get_current_date(&cal->today_year, &cal->today_month, &cal->today_day);
}

void ui_calendar_view_refresh(ui_calendar_view_t *cal)
{
    if (cal == nullptr || cal->container == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    /* Atualiza título: "Mês AAAA" */
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "%s %d", calendar_logic_get_month_name(cal->view_month), cal->view_year);
    lv_label_set_text(cal->title_label, title_buf);

    int first_dow = calendar_logic_get_first_day_of_week(cal->view_year, cal->view_month);
    int days_in_cur = calendar_logic_get_days_in_month(cal->view_year, cal->view_month);

    int prev_y = cal->view_year;
    int prev_m = cal->view_month;
    calendar_logic_prev_month(&prev_y, &prev_m);
    int days_in_prev = calendar_logic_get_days_in_month(prev_y, prev_m);

    for (int i = 0; i < 42; i++) {
        lv_obj_t *cell = cal->day_cells[i];
        lv_obj_t *lbl = cal->day_labels[i];
        if (cell == nullptr || lbl == nullptr) {
            continue;
        }

        char day_str[16];
        if (i < first_dow) {
            /* Dias do mês anterior */
            int d = days_in_prev - first_dow + 1 + i;
            snprintf(day_str, sizeof(day_str), "%d", d);
            lv_label_set_text(lbl, day_str);
            lv_obj_set_style_bg_color(cell, lv_color_hex(pal->surface_alt), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_40, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(pal->text_muted), 0);
            lv_obj_set_style_text_opa(lbl, LV_OPA_50, 0);
        } else if (i < first_dow + days_in_cur) {
            /* Dias do mês atual */
            int d = i - first_dow + 1;
            snprintf(day_str, sizeof(day_str), "%d", d);
            lv_label_set_text(lbl, day_str);
            lv_obj_set_style_text_opa(lbl, LV_OPA_COVER, 0);

            bool is_today =
                (cal->view_year == cal->today_year && cal->view_month == cal->today_month && d == cal->today_day);
            bool is_selected = (cal->selected_day == d);

            if (is_today) {
                lv_obj_set_style_bg_color(cell, lv_color_hex(pal->accent), 0);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(pal->surface), 0);
            } else if (is_selected) {
                lv_obj_set_style_bg_color(cell, lv_color_hex(pal->accent_soft), 0);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(pal->text), 0);
            } else {
                lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(pal->text), 0);
            }
        } else {
            /* Dias do próximo mês */
            int d = i - (first_dow + days_in_cur) + 1;
            snprintf(day_str, sizeof(day_str), "%d", d);
            lv_label_set_text(lbl, day_str);
            lv_obj_set_style_bg_color(cell, lv_color_hex(pal->surface_alt), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_40, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(pal->text_muted), 0);
            lv_obj_set_style_text_opa(lbl, LV_OPA_50, 0);
        }
    }
}

void ui_calendar_view_refresh_theme(ui_calendar_view_t *cal)
{
    if (cal == nullptr || cal->container == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    /* Botões do cabeçalho */
    if (cal->prev_btn != nullptr) {
        lv_obj_set_style_bg_color(cal->prev_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(cal->prev_btn, lv_color_hex(pal->border), 0);
    }
    if (cal->prev_lbl != nullptr) {
        lv_obj_set_style_text_color(cal->prev_lbl, lv_color_hex(pal->text), 0);
    }
    if (cal->next_btn != nullptr) {
        lv_obj_set_style_bg_color(cal->next_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(cal->next_btn, lv_color_hex(pal->border), 0);
    }
    if (cal->next_lbl != nullptr) {
        lv_obj_set_style_text_color(cal->next_lbl, lv_color_hex(pal->text), 0);
    }
    if (cal->today_btn != nullptr) {
        lv_obj_set_style_bg_color(cal->today_btn, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(cal->today_btn, lv_color_hex(pal->border), 0);
    }
    if (cal->today_lbl != nullptr) {
        lv_obj_set_style_text_color(cal->today_lbl, lv_color_hex(pal->text), 0);
    }
    if (cal->title_label != nullptr) {
        lv_obj_set_style_text_color(cal->title_label, lv_color_hex(pal->text), 0);
    }

    /* Linha dos dias da semana */
    if (cal->weekdays_cont != nullptr) {
        lv_obj_set_style_bg_color(cal->weekdays_cont, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(cal->weekdays_cont, lv_color_hex(pal->border), 0);
        uint32_t cnt = lv_obj_get_child_count(cal->weekdays_cont);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t *wbox = lv_obj_get_child(cal->weekdays_cont, i);
            if (wbox != nullptr && i < 6) {
                lv_obj_set_style_border_color(wbox, lv_color_hex(pal->border), 0);
            }
        }
    }

    /* Rótulos dos dias da semana */
    for (int i = 0; i < 7; i++) {
        if (cal->weekday_labels[i] != nullptr) {
            lv_obj_set_style_text_color(cal->weekday_labels[i], lv_color_hex(pal->text_muted), 0);
        }
    }

    /* Grade de dias */
    if (cal->grid_cont != nullptr) {
        lv_obj_set_style_bg_color(cal->grid_cont, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(cal->grid_cont, lv_color_hex(pal->border), 0);

        uint32_t row_count = lv_obj_get_child_count(cal->grid_cont);
        for (uint32_t r = 0; r < row_count; r++) {
            lv_obj_t *row_obj = lv_obj_get_child(cal->grid_cont, r);
            if (row_obj == nullptr) {
                continue;
            }
            if (r < 5) {
                lv_obj_set_style_border_color(row_obj, lv_color_hex(pal->border), 0);
            }
            uint32_t cell_count = lv_obj_get_child_count(row_obj);
            for (uint32_t c = 0; c < cell_count; c++) {
                lv_obj_t *cell = lv_obj_get_child(row_obj, c);
                if (cell != nullptr && c < 6) {
                    lv_obj_set_style_border_color(cell, lv_color_hex(pal->border), 0);
                }
            }
        }
    }

    ui_calendar_view_refresh(cal);
}

void ui_calendar_view_apply_layout(ui_calendar_view_t *cal)
{
    if (cal == nullptr) {
        return;
    }
    ui_calendar_view_refresh(cal);
}
