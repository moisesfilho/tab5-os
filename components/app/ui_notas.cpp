#include "ui_notas.h"
#include "ui_shell.h"
#include "ui_keyboard.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_font.h"
#include "wifi_storage.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include "esp_log.h"

static const char *TAG = "tab5_notas";

namespace {

lv_obj_t *notas_scr = nullptr;
lv_obj_t *notas_bar = nullptr;
lv_obj_t *notas_title = nullptr;
lv_obj_t *notas_new_btn = nullptr;
lv_obj_t *notas_new_label = nullptr;
lv_obj_t *notas_save_btn = nullptr;
lv_obj_t *notas_save_label = nullptr;
lv_obj_t *notas_close = nullptr;
lv_obj_t *notas_close_label = nullptr;
lv_obj_t *notas_ta = nullptr;
lv_group_t *notas_group = nullptr;
lv_timer_t *notas_cursor_timer = nullptr;
lv_obj_t *save_modal = nullptr;
lv_obj_t *save_modal_card = nullptr;
lv_obj_t *save_modal_title = nullptr;
lv_obj_t *save_modal_ta = nullptr;
lv_obj_t *save_modal_btn_row = nullptr;
lv_obj_t *save_modal_cancel_btn = nullptr;
lv_obj_t *save_modal_cancel_lbl = nullptr;
lv_obj_t *save_modal_confirm_btn = nullptr;
lv_obj_t *save_modal_confirm_lbl = nullptr;
lv_group_t *save_modal_group = nullptr;

std::string current_note_path = "";

void apply_notas_layout(void);

void ensure_notas_dir(void)
{
    if (wifi_storage_mount() == ESP_OK) {
        mkdir("/sdcard/notas", 0777);
    }
}

void update_title(void)
{
    if (notas_title == nullptr) {
        return;
    }
    if (current_note_path.empty()) {
        lv_label_set_text(notas_title, "Notas (Sem título)");
    } else {
        size_t last_slash = current_note_path.find_last_of('/');
        std::string filename =
            (last_slash != std::string::npos) ? current_note_path.substr(last_slash + 1) : current_note_path;
        std::string title = "Notas (" + filename + ")";
        lv_label_set_text(notas_title, title.c_str());
    }
}

void close_cb(lv_event_t *event)
{
    (void)event;
    lv_timer_pause(notas_cursor_timer);
    lv_obj_set_style_bg_opa(notas_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    ui_shell_close_notas();
}

void new_note_cb(lv_event_t *event)
{
    (void)event;
    ui_notas_new_file();
}

void save_modal_close(void)
{
    if (save_modal != nullptr) {
        ui_keyboard_hide();
        lv_obj_delete(save_modal);
        save_modal = nullptr;
        save_modal_card = nullptr;
        save_modal_title = nullptr;
        save_modal_ta = nullptr;
        save_modal_btn_row = nullptr;
        save_modal_cancel_btn = nullptr;
        save_modal_cancel_lbl = nullptr;
        save_modal_confirm_btn = nullptr;
        save_modal_confirm_lbl = nullptr;
        if (save_modal_group != nullptr) {
            lv_group_delete(save_modal_group);
            save_modal_group = nullptr;
        }
    }
}

void save_modal_cancel_cb(lv_event_t *event)
{
    (void)event;
    save_modal_close();
}

bool perform_save_to_file(const std::string &path)
{
    if (notas_ta == nullptr) {
        return false;
    }

    ensure_notas_dir();

    FILE *f = fopen(path.c_str(), "w");
    if (f == nullptr) {
        ESP_LOGE(TAG, "falha ao abrir arquivo para gravacao: %s", path.c_str());
        return false;
    }

    const char *text = lv_textarea_get_text(notas_ta);
    if (text != nullptr) {
        fputs(text, f);
    }
    fclose(f);

    current_note_path = path;
    update_title();
    ESP_LOGI(TAG, "nota salva com sucesso em: %s", current_note_path.c_str());
    return true;
}

void save_modal_confirm_cb(lv_event_t *event)
{
    (void)event;
    if (save_modal_ta == nullptr) {
        return;
    }

    const char *entered_name = lv_textarea_get_text(save_modal_ta);
    std::string filename = (entered_name != nullptr) ? entered_name : "";

    /* Remove espacos em branco nas pontas */
    size_t first = filename.find_first_not_of(" \t\r\n");
    if (first != std::string::npos) {
        size_t last = filename.find_last_not_of(" \t\r\n");
        filename = filename.substr(first, (last - first + 1));
    } else {
        filename = "";
    }

    if (filename.empty()) {
        filename = "nota.txt";
    }

    /* Se o usuario nao incluiu nenhuma extensao, anexa .txt como padrao */
    if (filename.find('.') == std::string::npos) {
        filename += ".txt";
    }

    std::string full_path = std::string("/sdcard/notas/") + filename;
    perform_save_to_file(full_path);
    save_modal_close();
}

void save_modal_ta_click_cb(lv_event_t *event)
{
    (void)event;
    if (save_modal_ta != nullptr) {
        if (save_modal_group != nullptr) {
            lv_group_focus_obj(save_modal_ta);
        }
        ui_keyboard_attach(save_modal_ta);
    }
}

void show_save_modal(void)
{
    if (save_modal != nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    /* Overlay escurecido cobrindo a tela */
    save_modal = lv_obj_create(notas_scr);
    lv_obj_set_size(save_modal, lv_pct(100), lv_pct(100));
    lv_obj_align(save_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(save_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(save_modal, LV_OPA_60, 0);
    lv_obj_set_style_border_width(save_modal, 0, 0);
    lv_obj_clear_flag(save_modal, LV_OBJ_FLAG_SCROLLABLE);

    /* Card central */
    save_modal_card = lv_obj_create(save_modal);
    lv_obj_set_size(save_modal_card, 380, 180);
    lv_obj_set_style_bg_color(save_modal_card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_width(save_modal_card, 1, 0);
    lv_obj_set_style_border_color(save_modal_card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_radius(save_modal_card, 12, 0);
    lv_obj_set_style_pad_all(save_modal_card, 14, 0);
    lv_obj_clear_flag(save_modal_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(save_modal_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(save_modal_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Titulo do modal */
    save_modal_title = lv_label_create(save_modal_card);
    lv_label_set_text(save_modal_title, "Salvar Nota");
    lv_obj_set_style_text_font(save_modal_title, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(save_modal_title, lv_color_hex(pal->text), 0);

    /* Campo de texto com o nome sugerido */
    save_modal_ta = lv_textarea_create(save_modal_card);
    lv_obj_set_width(save_modal_ta, 340);
    lv_obj_set_height(save_modal_ta, 38);
    lv_textarea_set_one_line(save_modal_ta, true);
    lv_obj_set_style_text_font(save_modal_ta, &lv_font_montserrat_14_latin1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(save_modal_ta, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_border_width(save_modal_ta, 1, 0);
    lv_obj_set_style_border_color(save_modal_ta, lv_color_hex(pal->border), 0);
    lv_obj_set_style_text_color(save_modal_ta, lv_color_hex(pal->text), 0);
    lv_obj_set_style_radius(save_modal_ta, 8, 0);
    lv_obj_set_style_anim_duration(save_modal_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_bg_color(save_modal_ta, lv_color_hex(pal->accent), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(save_modal_ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_add_event_cb(save_modal_ta, save_modal_ta_click_cb, LV_EVENT_CLICKED, nullptr);

    /* Sugestao de nome */
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char default_filename[64];
    if (timeinfo.tm_year > 100) {
        std::strftime(default_filename, sizeof(default_filename), "nota_%Y%m%d_%H%M%S.txt", &timeinfo);
    } else {
        std::snprintf(default_filename, sizeof(default_filename), "nota_%lu.txt", (unsigned long)now);
    }
    lv_textarea_set_text(save_modal_ta, default_filename);

    /* Linha de botoes: Cancelar e Salvar */
    save_modal_btn_row = lv_obj_create(save_modal_card);
    lv_obj_set_size(save_modal_btn_row, 340, 42);
    lv_obj_set_style_bg_opa(save_modal_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(save_modal_btn_row, 0, 0);
    lv_obj_set_style_pad_all(save_modal_btn_row, 0, 0);
    lv_obj_clear_flag(save_modal_btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(save_modal_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(save_modal_btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Botao Cancelar */
    save_modal_cancel_btn = lv_obj_create(save_modal_btn_row);
    lv_obj_set_size(save_modal_cancel_btn, 160, 38);
    lv_obj_set_style_radius(save_modal_cancel_btn, 8, 0);
    lv_obj_set_style_bg_color(save_modal_cancel_btn, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_bg_color(save_modal_cancel_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(save_modal_cancel_btn, 1, 0);
    lv_obj_set_style_border_color(save_modal_cancel_btn, lv_color_hex(pal->border), 0);
    lv_obj_clear_flag(save_modal_cancel_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(save_modal_cancel_btn, save_modal_cancel_cb, LV_EVENT_CLICKED, nullptr);

    save_modal_cancel_lbl = lv_label_create(save_modal_cancel_btn);
    lv_label_set_text(save_modal_cancel_lbl, "Cancelar");
    lv_obj_set_style_text_font(save_modal_cancel_lbl, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(save_modal_cancel_lbl, lv_color_hex(pal->text), 0);
    lv_obj_center(save_modal_cancel_lbl);

    /* Botao Confirmar / Salvar */
    save_modal_confirm_btn = lv_obj_create(save_modal_btn_row);
    lv_obj_set_size(save_modal_confirm_btn, 160, 38);
    lv_obj_set_style_radius(save_modal_confirm_btn, 8, 0);
    lv_obj_set_style_bg_color(save_modal_confirm_btn, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_bg_color(save_modal_confirm_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(save_modal_confirm_btn, 0, 0);
    lv_obj_clear_flag(save_modal_confirm_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(save_modal_confirm_btn, save_modal_confirm_cb, LV_EVENT_CLICKED, nullptr);

    save_modal_confirm_lbl = lv_label_create(save_modal_confirm_btn);
    lv_label_set_text(save_modal_confirm_lbl, "Salvar");
    lv_obj_set_style_text_font(save_modal_confirm_lbl, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(save_modal_confirm_lbl, lv_color_white(), 0);
    lv_obj_center(save_modal_confirm_lbl);

    save_modal_group = lv_group_create();
    lv_group_add_obj(save_modal_group, save_modal_ta);
    lv_group_focus_obj(save_modal_ta);
    ui_keyboard_attach(save_modal_ta);

    apply_notas_layout();
}

void save_note_cb(lv_event_t *event)
{
    (void)event;
    ui_notas_save_current();
}

/* Blink determinístico do cursor: alterna a opacidade do LV_PART_CURSOR */
void cursor_blink_cb(lv_timer_t *timer)
{
    lv_obj_t *ta = (lv_obj_t *)lv_timer_get_user_data(timer);
    lv_opa_t opa = lv_obj_get_style_bg_opa(ta, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, opa == LV_OPA_TRANSP ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_CURSOR);
}

/* Mostra o teclado, garante o cursor visivel e da FOCO REAL ao textarea. */
void ta_click_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_set_style_bg_opa(notas_ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_timer_resume(notas_cursor_timer);
    lv_group_focus_obj(notas_ta);
    ui_keyboard_attach(notas_ta);
}

/* O textarea encosta na barra do app (janela maximizada), reduzindo
 * seu tamanho quando o teclado virtual estiver aberto. */
void apply_notas_layout(void)
{
    if (notas_ta == nullptr) {
        return;
    }

    int32_t w = lv_display_get_horizontal_resolution(NULL);
    int32_t h = lv_display_get_vertical_resolution(NULL);
    int32_t kb_h = ui_keyboard_is_visible() ? ui_keyboard_get_height() : 0;

    if (notas_bar != nullptr) {
        lv_obj_set_width(notas_bar, w);
    }

    lv_obj_set_width(notas_ta, w);
    lv_obj_set_height(notas_ta, h - 2 * UI_BAR_HEIGHT - kb_h - 12);
    if (ui_keyboard_is_visible()) {
        lv_obj_scroll_to_view(notas_ta, LV_ANIM_OFF);
    }

    /* Reposiciona o card do modal de salvar para nao sobrepor o teclado */
    if (save_modal_card != nullptr) {
        if (ui_keyboard_is_visible()) {
            /* Desloca para cima centralizado no espaco util restante */
            int32_t visible_h = h - kb_h - UI_BAR_HEIGHT;
            int32_t card_y = UI_BAR_HEIGHT + (visible_h - 180) / 2;
            if (card_y < UI_BAR_HEIGHT + 4) {
                card_y = UI_BAR_HEIGHT + 4;
            }
            lv_obj_align(save_modal_card, LV_ALIGN_TOP_MID, 0, card_y);
        } else {
            lv_obj_align(save_modal_card, LV_ALIGN_CENTER, 0, 0);
        }
    }
}

void notas_resolution_cb(lv_event_t *event)
{
    (void)event;
    apply_notas_layout();
}

/* Reaplica a paleta ativa no app. */
void apply_notas_theme(void)
{
    if (notas_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(notas_scr, lv_color_hex(pal->background), 0);

    /* Barra do app (surface_alt) */
    lv_obj_set_style_bg_color(notas_bar, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_border_color(notas_bar, lv_color_hex(pal->border), 0);
    lv_obj_set_style_text_color(notas_title, lv_color_hex(pal->text), 0);

    /* Botao Novo */
    if (notas_new_btn != nullptr) {
        lv_obj_set_style_bg_opa(notas_new_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(notas_new_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(notas_new_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (notas_new_label != nullptr) {
        lv_obj_set_style_text_color(notas_new_label, lv_color_hex(pal->text), 0);
    }

    /* Botao Salvar */
    if (notas_save_btn != nullptr) {
        lv_obj_set_style_bg_opa(notas_save_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(notas_save_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(notas_save_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (notas_save_label != nullptr) {
        lv_obj_set_style_text_color(notas_save_label, lv_color_hex(pal->text), 0);
    }

    /* Botao Fechar */
    if (notas_close != nullptr) {
        lv_obj_set_style_bg_opa(notas_close, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(notas_close, 1, 0);
        lv_obj_set_style_border_color(notas_close, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_opa(notas_close, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(notas_close, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (notas_close_label != nullptr) {
        lv_obj_set_style_text_color(notas_close_label, lv_color_hex(pal->text), 0);
    }

    lv_obj_set_style_bg_color(notas_ta, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_text_color(notas_ta, lv_color_hex(pal->text), 0);
    lv_obj_set_style_border_width(notas_ta, 0, 0);
    lv_obj_set_style_radius(notas_ta, 0, 0);
    lv_obj_set_style_pad_all(notas_ta, 14, 0);
    lv_obj_set_style_text_color(notas_ta, lv_color_hex(pal->text_muted), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(notas_ta, lv_color_hex(pal->accent), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(notas_ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_pad_right(notas_ta, -2, LV_PART_CURSOR);
}

} // namespace

lv_obj_t *ui_notas_create(void)
{
    notas_scr = lv_obj_create(NULL);

    /* Barra propria do app com layout flex row */
    notas_bar = lv_obj_create(notas_scr);
    lv_obj_set_size(notas_bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_align(notas_bar, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(notas_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notas_bar, 1, 0);
    lv_obj_set_style_border_side(notas_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(notas_bar, 0, 0);
    lv_obj_set_style_shadow_width(notas_bar, 0, 0);
    lv_obj_set_style_pad_left(notas_bar, 12, 0);
    lv_obj_set_style_pad_right(notas_bar, 8, 0);
    lv_obj_set_style_pad_top(notas_bar, 0, 0);
    lv_obj_set_style_pad_bottom(notas_bar, 0, 0);
    lv_obj_clear_flag(notas_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(notas_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(notas_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    notas_title = lv_label_create(notas_bar);
    lv_label_set_text(notas_title, "Notas");
    lv_label_set_long_mode(notas_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(notas_title, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_flex_grow(notas_title, 1);

    /* Botao Novo */
    notas_new_btn = lv_obj_create(notas_bar);
    lv_obj_set_size(notas_new_btn, 36, 36);
    lv_obj_set_style_radius(notas_new_btn, 8, 0);
    lv_obj_set_style_border_width(notas_new_btn, 1, 0);
    lv_obj_set_style_margin_right(notas_new_btn, 6, 0);
    lv_obj_clear_flag(notas_new_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(notas_new_btn, new_note_cb, LV_EVENT_CLICKED, nullptr);
    notas_new_label = lv_label_create(notas_new_btn);
    lv_label_set_text(notas_new_label, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(notas_new_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(notas_new_label);

    /* Botao Salvar */
    notas_save_btn = lv_obj_create(notas_bar);
    lv_obj_set_size(notas_save_btn, 36, 36);
    lv_obj_set_style_radius(notas_save_btn, 8, 0);
    lv_obj_set_style_border_width(notas_save_btn, 1, 0);
    lv_obj_set_style_margin_right(notas_save_btn, 6, 0);
    lv_obj_clear_flag(notas_save_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(notas_save_btn, save_note_cb, LV_EVENT_CLICKED, nullptr);
    notas_save_label = lv_label_create(notas_save_btn);
    lv_label_set_text(notas_save_label, LV_SYMBOL_SAVE);
    lv_obj_set_style_text_font(notas_save_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(notas_save_label);

    /* Botao Fechar */
    notas_close = lv_obj_create(notas_bar);
    lv_obj_set_size(notas_close, 36, 36);
    lv_obj_set_style_radius(notas_close, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(notas_close, 1, 0);
    lv_obj_clear_flag(notas_close, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(notas_close, close_cb, LV_EVENT_CLICKED, nullptr);

    notas_close_label = lv_label_create(notas_close);
    lv_label_set_text(notas_close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(notas_close_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(notas_close_label);

    /* Area de digitacao maximizada */
    notas_ta = lv_textarea_create(notas_scr);
    lv_obj_set_width(notas_ta, lv_pct(100));
    lv_obj_align(notas_ta, LV_ALIGN_TOP_MID, 0, 2 * UI_BAR_HEIGHT);
    lv_textarea_set_placeholder_text(notas_ta, "Escreva sua nota...");
    lv_textarea_set_cursor_click_pos(notas_ta, true);
    lv_obj_set_style_anim_duration(notas_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_text_font(notas_ta, &lv_font_montserrat_14_latin1, LV_PART_MAIN);
    lv_obj_add_event_cb(notas_ta, ta_click_cb, LV_EVENT_CLICKED, nullptr);

    notas_group = lv_group_create();
    lv_group_add_obj(notas_group, notas_ta);

    apply_notas_layout();
    lv_display_add_event_cb(lv_display_get_default(), notas_resolution_cb, LV_EVENT_RESOLUTION_CHANGED, nullptr);

    apply_notas_theme();
    update_title();

    lv_obj_set_style_bg_opa(notas_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    notas_cursor_timer = lv_timer_create(cursor_blink_cb, 500, notas_ta);
    lv_timer_pause(notas_cursor_timer);

    return notas_scr;
}

void ui_notas_refresh_theme(void)
{
    apply_notas_theme();
}

void ui_notas_apply_layout(void)
{
    apply_notas_layout();
}

void ui_notas_new_file(void)
{
    current_note_path = "";
    if (notas_ta != nullptr) {
        lv_textarea_set_text(notas_ta, "");
    }
    update_title();
}

void ui_notas_open_file(const char *filepath)
{
    if (filepath == nullptr || notas_ta == nullptr) {
        return;
    }

    ensure_notas_dir();

    FILE *f = fopen(filepath, "r");
    if (f == nullptr) {
        ESP_LOGE(TAG, "falha ao abrir arquivo para leitura: %s", filepath);
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0) {
        fclose(f);
        return;
    }

    /* Limit razoavel de 64KB para textarea */
    size_t read_len = (sz > 65535) ? 65535 : static_cast<size_t>(sz);
    std::string content(read_len, '\0');
    size_t bytes_read = fread(&content[0], 1, read_len, f);
    fclose(f);
    content.resize(bytes_read);

    current_note_path = filepath;
    lv_textarea_set_text(notas_ta, content.c_str());
    update_title();
    ESP_LOGI(TAG, "nota carregada de %s (%zu bytes)", filepath, bytes_read);
}

bool ui_notas_save_current(void)
{
    if (notas_ta == nullptr) {
        return false;
    }

    if (current_note_path.empty()) {
        show_save_modal();
        return true;
    }

    return perform_save_to_file(current_note_path);
}
