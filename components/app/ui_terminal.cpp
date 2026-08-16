#include "ui_terminal.h"
#include "ui_shell.h"
#include "ui_keyboard.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_font.h"
#include "terminal_cmd.h"

#include <string>
#include <algorithm>

namespace {

lv_obj_t *term_scr = nullptr;
lv_obj_t *term_bar = nullptr;
lv_obj_t *term_back_btn = nullptr;
lv_obj_t *term_back_label = nullptr;
lv_obj_t *term_title = nullptr;
lv_obj_t *term_clear_btn = nullptr;
lv_obj_t *term_clear_label = nullptr;
lv_obj_t *term_close_btn = nullptr;
lv_obj_t *term_close_label = nullptr;

lv_obj_t *term_ta = nullptr;

std::string current_cwd = "/sdcard";
std::string term_history;
size_t prompt_min_index = 0;
bool is_processing_cmd = false;

const size_t MAX_TERMINAL_BUFFER = 8192;
const size_t TRUNCATE_TARGET = 6144;

void trim_history_if_needed(void)
{
    if (term_history.size() > MAX_TERMINAL_BUFFER) {
        size_t excess = term_history.size() - TRUNCATE_TARGET;
        size_t next_line = term_history.find('\n', excess);
        if (next_line != std::string::npos) {
            term_history = term_history.substr(next_line + 1);
        } else {
            term_history = term_history.substr(excess);
        }
    }
}

void print_new_prompt(void)
{
    term_history += current_cwd + " $ ";
    prompt_min_index = term_history.size();
    if (term_ta != nullptr) {
        is_processing_cmd = true;
        lv_textarea_set_text(term_ta, term_history.c_str());
        lv_textarea_set_cursor_pos(term_ta, LV_TEXTAREA_CURSOR_LAST);
        is_processing_cmd = false;
    }
}

void reset_terminal(void)
{
    term_history.clear();
    print_new_prompt();
}

void init_terminal_banner(void)
{
    term_history = "Tab5-OS Terminal v1.0\n"
                   "Digite 'help' para listar os comandos disponiveis.\n\n";
    print_new_prompt();
}

void execute_current_command(void)
{
    if (term_ta == nullptr || is_processing_cmd) {
        return;
    }

    const char *full_text = lv_textarea_get_text(term_ta);
    if (full_text == nullptr) {
        return;
    }

    std::string text_str = full_text;
    std::string cmd;
    if (text_str.size() >= prompt_min_index) {
        cmd = text_str.substr(prompt_min_index);
    }

    // Remove eventuais newlines/quebras do comando
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) {
        cmd.pop_back();
    }

    if (cmd == "clear") {
        reset_terminal();
        return;
    }

    // Registra o comando no histórico com quebra de linha
    term_history += cmd + "\n";

    if (!cmd.empty()) {
        std::string output = terminal_exec(cmd, current_cwd);
        if (output == "\x0C") {
            reset_terminal();
            return;
        }
        if (!output.empty()) {
            term_history += output;
            if (term_history.back() != '\n') {
                term_history += "\n";
            }
        }
    }

    trim_history_if_needed();
    print_new_prompt();
}

void textarea_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        ui_keyboard_attach(term_ta);
        // Garante que o cursor não fique atrás do prompt
        uint32_t cur = lv_textarea_get_cursor_pos(term_ta);
        if (cur < prompt_min_index) {
            lv_textarea_set_cursor_pos(term_ta, LV_TEXTAREA_CURSOR_LAST);
        }
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        if (is_processing_cmd) {
            return;
        }

        const char *txt = lv_textarea_get_text(term_ta);
        if (txt == nullptr) {
            return;
        }

        std::string current_text = txt;

        // Se o usuário apagou parte do histórico/prompt protegido, restaura
        if (current_text.size() < prompt_min_index || current_text.compare(0, prompt_min_index, term_history) != 0) {
            is_processing_cmd = true;
            lv_textarea_set_text(term_ta, term_history.c_str());
            lv_textarea_set_cursor_pos(term_ta, LV_TEXTAREA_CURSOR_LAST);
            is_processing_cmd = false;
            return;
        }

        // Verifica se o usuário inseriu Enter/Nova Linha
        if (!current_text.empty() && current_text.back() == '\n') {
            execute_current_command();
        }
    } else if (code == LV_EVENT_READY) {
        execute_current_command();
    }
}

void clear_btn_click_cb(lv_event_t *event)
{
    (void)event;
    reset_terminal();
}

void close_btn_click_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_close_terminal();
}

void apply_terminal_theme(void)
{
    if (term_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(term_scr, lv_color_hex(pal->background), 0);

    if (term_bar != nullptr) {
        lv_obj_set_style_bg_color(term_bar, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(term_bar, lv_color_hex(pal->border), 0);
    }
    if (term_back_btn != nullptr) {
        lv_obj_set_style_bg_opa(term_back_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(term_back_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(term_back_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (term_back_label != nullptr) {
        lv_obj_set_style_text_color(term_back_label, lv_color_hex(pal->text), 0);
    }
    if (term_title != nullptr) {
        lv_obj_set_style_text_color(term_title, lv_color_hex(pal->text), 0);
    }
    if (term_clear_btn != nullptr) {
        lv_obj_set_style_bg_opa(term_clear_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(term_clear_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(term_clear_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (term_clear_label != nullptr) {
        lv_obj_set_style_text_color(term_clear_label, lv_color_hex(pal->text), 0);
    }
    if (term_close_btn != nullptr) {
        lv_obj_set_style_bg_opa(term_close_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(term_close_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(term_close_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (term_close_label != nullptr) {
        lv_obj_set_style_text_color(term_close_label, lv_color_hex(pal->text), 0);
    }

    if (term_ta != nullptr) {
        lv_obj_set_style_bg_color(term_ta, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_text_color(term_ta, lv_color_hex(pal->text), 0);
        lv_obj_set_style_border_color(term_ta, lv_color_hex(pal->border), 0);
        lv_obj_set_style_bg_color(term_ta, lv_color_hex(pal->accent), LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(term_ta, LV_OPA_COVER, LV_PART_CURSOR);
    }
}

} // namespace

void ui_terminal_apply_layout(void)
{
    if (term_scr == nullptr || term_ta == nullptr) {
        return;
    }

    int32_t width = lv_display_get_horizontal_resolution(nullptr);
    int32_t height = lv_display_get_vertical_resolution(nullptr);

    int32_t top = 2 * UI_BAR_HEIGHT;
    int32_t kb_h = ui_keyboard_is_visible() ? ui_keyboard_get_height() : 0;

    int32_t ta_h = height - top - kb_h;
    if (ta_h < 60) {
        ta_h = 60;
    }

    lv_obj_set_size(term_ta, width, ta_h);
    lv_obj_align(term_ta, LV_ALIGN_TOP_LEFT, 0, top);

    lv_textarea_set_cursor_pos(term_ta, LV_TEXTAREA_CURSOR_LAST);
}

void ui_terminal_refresh_theme(void)
{
    apply_terminal_theme();
}

lv_obj_t *ui_terminal_create(void)
{
    term_scr = lv_obj_create(NULL);

    /* Barra do Terminal */
    term_bar = lv_obj_create(term_scr);
    lv_obj_set_size(term_bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_align(term_bar, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(term_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(term_bar, 1, 0);
    lv_obj_set_style_border_side(term_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(term_bar, 0, 0);
    lv_obj_set_style_shadow_width(term_bar, 0, 0);
    lv_obj_set_style_pad_left(term_bar, 8, 0);
    lv_obj_set_style_pad_right(term_bar, 8, 0);
    lv_obj_set_style_pad_top(term_bar, 0, 0);
    lv_obj_set_style_pad_bottom(term_bar, 0, 0);
    lv_obj_clear_flag(term_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(term_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(term_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Botão Voltar */
    term_back_btn = lv_button_create(term_bar);
    lv_obj_set_size(term_back_btn, 32, 28);
    lv_obj_set_style_radius(term_back_btn, 6, 0);
    lv_obj_set_style_border_width(term_back_btn, 1, 0);
    lv_obj_set_style_shadow_width(term_back_btn, 0, 0);
    lv_obj_set_style_pad_all(term_back_btn, 0, 0);
    lv_obj_add_event_cb(term_back_btn, close_btn_click_cb, LV_EVENT_CLICKED, nullptr);

    term_back_label = lv_label_create(term_back_btn);
    lv_label_set_text(term_back_label, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(term_back_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(term_back_label);

    /* Título */
    term_title = lv_label_create(term_bar);
    lv_label_set_text(term_title, "Terminal");
    lv_obj_set_style_text_font(term_title, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_pad_left(term_title, 8, 0);
    lv_obj_set_flex_grow(term_title, 1);

    /* Botão Limpar */
    term_clear_btn = lv_button_create(term_bar);
    lv_obj_set_size(term_clear_btn, 32, 28);
    lv_obj_set_style_radius(term_clear_btn, 6, 0);
    lv_obj_set_style_border_width(term_clear_btn, 1, 0);
    lv_obj_set_style_shadow_width(term_clear_btn, 0, 0);
    lv_obj_set_style_pad_all(term_clear_btn, 0, 0);
    lv_obj_add_event_cb(term_clear_btn, clear_btn_click_cb, LV_EVENT_CLICKED, nullptr);

    term_clear_label = lv_label_create(term_clear_btn);
    lv_label_set_text(term_clear_label, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(term_clear_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(term_clear_label);

    /* Botão Fechar */
    term_close_btn = lv_button_create(term_bar);
    lv_obj_set_size(term_close_btn, 32, 28);
    lv_obj_set_style_radius(term_close_btn, 6, 0);
    lv_obj_set_style_border_width(term_close_btn, 1, 0);
    lv_obj_set_style_shadow_width(term_close_btn, 0, 0);
    lv_obj_set_style_pad_all(term_close_btn, 0, 0);
    lv_obj_set_style_margin_left(term_close_btn, 6, 0);
    lv_obj_add_event_cb(term_close_btn, close_btn_click_cb, LV_EVENT_CLICKED, nullptr);

    term_close_label = lv_label_create(term_close_btn);
    lv_label_set_text(term_close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(term_close_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(term_close_label);

    /* Console Interativo Único (Textarea Fullscreen) */
    term_ta = lv_textarea_create(term_scr);
    lv_obj_set_style_radius(term_ta, 0, 0);
    lv_obj_set_style_border_width(term_ta, 0, 0);
    lv_obj_set_style_pad_all(term_ta, 10, 0);
    lv_obj_set_style_text_font(term_ta, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_anim_duration(term_ta, 500, LV_PART_CURSOR);
    lv_obj_add_event_cb(term_ta, textarea_event_cb, LV_EVENT_ALL, nullptr);

    init_terminal_banner();

    apply_terminal_theme();
    ui_terminal_apply_layout();

    return term_scr;
}
