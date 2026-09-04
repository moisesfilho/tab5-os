#include "ui_chat_view.h"
#include "ui_shell.h"
#include "ui_keyboard.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#include "ai_client.h"
#include "ai_storage.h"
#include "wifi_mgr.h"
#include "esp_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const char *TAG = "tab5_ui_chat";

namespace {

lv_obj_t *chat_scr = nullptr;
ui_app_bar_t chat_app_bar = {};
lv_obj_t *model_badge = nullptr;
lv_obj_t *model_badge_lbl = nullptr;
lv_obj_t *clear_btn = nullptr;
lv_obj_t *clear_lbl = nullptr;
lv_obj_t *config_btn = nullptr;
lv_obj_t *config_lbl = nullptr;

lv_obj_t *messages_cont = nullptr;
lv_obj_t *thinking_bubble = nullptr;
lv_obj_t *thinking_lbl = nullptr;

lv_obj_t *input_bar = nullptr;
lv_obj_t *input_ta = nullptr;
lv_obj_t *send_btn = nullptr;
lv_obj_t *send_lbl = nullptr;

// Modal de Configuração
lv_obj_t *cfg_modal = nullptr;
lv_obj_t *cfg_card = nullptr;
lv_obj_t *cfg_title = nullptr;
lv_obj_t *cfg_url_lbl = nullptr;
lv_obj_t *cfg_url_ta = nullptr;
lv_obj_t *cfg_token_lbl = nullptr;
lv_obj_t *cfg_token_ta = nullptr;
lv_obj_t *cfg_model_lbl = nullptr;
lv_obj_t *cfg_model_ta = nullptr;
lv_obj_t *cfg_max_lbl = nullptr;
lv_obj_t *cfg_max_ta = nullptr;
lv_obj_t *cfg_btn_row = nullptr;
lv_obj_t *cfg_cancel_btn = nullptr;
lv_obj_t *cfg_cancel_lbl = nullptr;
lv_obj_t *cfg_save_btn = nullptr;
lv_obj_t *cfg_save_lbl = nullptr;

ai_cfg_t s_ai_cfg;
std::vector<ai_msg_t> s_history;

void scroll_to_bottom(void)
{
    if (messages_cont != nullptr) {
        lv_obj_scroll_to_y(messages_cont, LV_COORD_MAX, LV_ANIM_ON);
    }
}

void update_model_badge(void)
{
    if (model_badge_lbl != nullptr) {
        lv_label_set_text(model_badge_lbl, s_ai_cfg.model[0] != '\0' ? s_ai_cfg.model : "deepseek-v4-pro");
    }
}

void remove_thinking_bubble(void)
{
    if (thinking_bubble != nullptr) {
        lv_obj_delete(thinking_bubble);
        thinking_bubble = nullptr;
        thinking_lbl = nullptr;
    }
}

void add_message_bubble(const char *role, const char *content)
{
    if (messages_cont == nullptr || content == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    bool is_user = (strcmp(role, "user") == 0);
    bool is_system = (strcmp(role, "system") == 0);

    lv_obj_t *row = lv_obj_create(messages_cont);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_set_style_margin_ver(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

    if (is_user) {
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else if (is_system) {
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else {
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    int32_t scr_w = lv_display_get_horizontal_resolution(nullptr);
    if (scr_w <= 0) {
        scr_w = 800;
    }
    int32_t max_bubble_w = is_system ? (scr_w * 90 / 100) : (scr_w * 80 / 100);
    int32_t pad_hor = 14;
    int32_t max_text_w = max_bubble_w - (pad_hor * 2);

    lv_point_t size_res = {0, 0};
    lv_text_get_size(&size_res, content, &lv_font_montserrat_18_latin1, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(bubble, pad_hor, 0);
    lv_obj_set_style_pad_ver(bubble, 10, 0);
    lv_obj_set_style_radius(bubble, 12, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(bubble);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18_latin1, 0);
    lv_label_set_text(lbl, content);

    if (size_res.x > max_text_w || strchr(content, '\n') != nullptr) {
        lv_obj_set_width(bubble, max_bubble_w);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    } else {
        lv_obj_set_width(bubble, LV_SIZE_CONTENT);
        lv_obj_set_width(lbl, LV_SIZE_CONTENT);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    }

    if (is_user) {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(pal->accent), 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    } else if (is_system) {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(bubble, lv_color_hex(pal->accent), 0);
        lv_obj_set_style_border_width(bubble, 1, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(pal->text_muted), 0);
    } else {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(bubble, lv_color_hex(pal->border), 0);
        lv_obj_set_style_border_width(bubble, 1, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(pal->text), 0);
    }

    scroll_to_bottom();
}

void show_thinking_bubble(const char *msg)
{
    remove_thinking_bubble();
    if (messages_cont == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    thinking_bubble = lv_obj_create(messages_cont);
    lv_obj_set_width(thinking_bubble, lv_pct(100));
    lv_obj_set_height(thinking_bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(thinking_bubble, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(thinking_bubble, 0, 0);
    lv_obj_set_style_pad_all(thinking_bubble, 2, 0);
    lv_obj_set_style_margin_ver(thinking_bubble, 4, 0);
    lv_obj_clear_flag(thinking_bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(thinking_bubble, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(thinking_bubble, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *inner = lv_obj_create(thinking_bubble);
    lv_obj_set_width(inner, LV_SIZE_CONTENT);
    lv_obj_set_height(inner, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(inner, 14, 0);
    lv_obj_set_style_pad_ver(inner, 10, 0);
    lv_obj_set_style_radius(inner, 12, 0);
    lv_obj_set_style_bg_color(inner, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);

    thinking_lbl = lv_label_create(inner);
    lv_obj_set_width(thinking_lbl, LV_SIZE_CONTENT);
    lv_label_set_text(thinking_lbl, msg != nullptr ? msg : "Pensando...");
    lv_obj_set_style_text_font(thinking_lbl, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(thinking_lbl, lv_color_hex(pal->accent), 0);

    lv_display_trigger_activity(NULL);
    scroll_to_bottom();
}

void on_ai_response(const char *response_text, void *user_data)
{
    (void)user_data;
    remove_thinking_bubble();
    lv_display_trigger_activity(NULL);

    if (response_text != nullptr && strlen(response_text) > 0) {
        s_history.push_back({"assistant", response_text});
        add_message_bubble("assistant", response_text);
    }
}

void on_ai_state(ai_state_t state, const char *status_msg, void *user_data)
{
    (void)user_data;
    lv_display_trigger_activity(NULL);

    if (state == AI_STATE_CONNECTING || state == AI_STATE_SENDING || state == AI_STATE_RECEIVING) {
        show_thinking_bubble(status_msg != nullptr ? status_msg : "Processando...");
        if (send_btn != nullptr) {
            lv_obj_set_style_bg_color(send_btn, lv_color_hex(0xE53935), 0); // Vermelho cancelar
            if (send_lbl != nullptr) {
                lv_label_set_text(send_lbl, LV_SYMBOL_CLOSE);
            }
        }
    } else {
        remove_thinking_bubble();
        if (send_btn != nullptr) {
            const ui_palette_t *pal = ui_theme_get();
            lv_obj_set_style_bg_color(send_btn, lv_color_hex(pal->accent), 0);
            if (send_lbl != nullptr) {
                lv_label_set_text(send_lbl, LV_SYMBOL_RIGHT);
            }
        }

        if (state == AI_STATE_ERROR) {
            ESP_LOGE(TAG, "on_ai_state: ERRO: %s", status_msg != nullptr ? status_msg : "desconhecido");
            if (!s_history.empty() && s_history.back().role == "user") {
                s_history.pop_back();
            }
            std::string err = "Erro: ";
            err += (status_msg != nullptr) ? status_msg : "Falha desconhecida";
            add_message_bubble("system", err.c_str());
        } else if (state == AI_STATE_CANCELLED) {
            ESP_LOGI(TAG, "on_ai_state: CANCELADO");
            add_message_bubble("system", "Requisicao cancelada.");
        }
    }
}

void do_send_message(void)
{
    if (ai_client_is_busy()) {
        ESP_LOGI(TAG, "do_send_message: cliente ocupado, solicitando cancelamento");
        ai_client_cancel();
        return;
    }

    if (input_ta == nullptr) {
        return;
    }

    const char *text = lv_textarea_get_text(input_ta);
    if (text == nullptr || strlen(text) == 0) {
        ESP_LOGW(TAG, "do_send_message: texto vazio");
        return;
    }

    wifi_status_t ws = {};
    wifi_mgr_get_status(&ws);
    if (!ws.connected) {
        ESP_LOGW(TAG, "do_send_message: Wi-Fi desconectado");
        add_message_bubble("system", "Wi-Fi desconectado! Conecte-se nas configuracoes.");
        return;
    }

    std::string user_text = text;
    while (!user_text.empty() && (user_text.back() == '\n' || user_text.back() == '\r' || user_text.back() == ' ')) {
        user_text.pop_back();
    }
    while (!user_text.empty() && (user_text.front() == '\n' || user_text.front() == '\r' || user_text.front() == ' ')) {
        user_text.erase(user_text.begin());
    }

    if (user_text.empty()) {
        lv_textarea_set_text(input_ta, "");
        return;
    }

    ESP_LOGI(TAG, "do_send_message: enviando prompt '%s' (modelo=%s, url=%s)", user_text.c_str(), s_ai_cfg.model,
             s_ai_cfg.base_url);
    lv_textarea_set_text(input_ta, "");

    s_history.push_back({"user", user_text});
    add_message_bubble("user", user_text.c_str());

    show_thinking_bubble("Conectando ao modelo...");

    esp_err_t err = ai_client_send(&s_ai_cfg, s_history, on_ai_response, on_ai_state, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ai_client_send retornou erro: %s", esp_err_to_name(err));
        remove_thinking_bubble();
        add_message_bubble("system", "Falha ao iniciar comunicacao com o servidor.");
    }
}

void send_btn_cb(lv_event_t *event)
{
    (void)event;
    do_send_message();
}

void input_ta_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        ui_keyboard_attach(input_ta);
        ui_chat_apply_layout();
        scroll_to_bottom();
    } else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ENTER || key == '\n' || key == '\r') {
            do_send_message();
        }
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        const char *txt = lv_textarea_get_text(input_ta);
        if (txt != nullptr) {
            size_t len = strlen(txt);
            if (len > 0 && (txt[len - 1] == '\n' || txt[len - 1] == '\r')) {
                char clean_txt[512];
                snprintf(clean_txt, sizeof(clean_txt), "%.*s", (int)(len - 1), txt);
                lv_textarea_set_text(input_ta, clean_txt);
                do_send_message();
            }
        }
    } else if (code == LV_EVENT_READY) {
        do_send_message();
    }
}

void back_btn_cb(lv_event_t *event)
{
    (void)event;
    if (ai_client_is_busy()) {
        ai_client_cancel();
    }
    ui_keyboard_hide();
    ui_shell_close_chat();
}

void clear_btn_cb(lv_event_t *event)
{
    (void)event;
    s_history.clear();
    if (messages_cont != nullptr) {
        lv_obj_clean(messages_cont);
    }
    thinking_bubble = nullptr;
    thinking_lbl = nullptr;
    add_message_bubble("system", "Historico de conversa limpo. Inicie uma nova conversa!");
}

// ---- Config Modal Handlers ----

void cfg_close(void)
{
    if (cfg_modal != nullptr) {
        ui_keyboard_hide();
        lv_obj_delete(cfg_modal);
        cfg_modal = nullptr;
        cfg_card = nullptr;
        cfg_title = nullptr;
        cfg_url_lbl = nullptr;
        cfg_url_ta = nullptr;
        cfg_token_lbl = nullptr;
        cfg_token_ta = nullptr;
        cfg_model_lbl = nullptr;
        cfg_model_ta = nullptr;
        cfg_max_lbl = nullptr;
        cfg_max_ta = nullptr;
        cfg_btn_row = nullptr;
        cfg_cancel_btn = nullptr;
        cfg_cancel_lbl = nullptr;
        cfg_save_btn = nullptr;
        cfg_save_lbl = nullptr;
    }
    ui_chat_apply_layout();
}

void cfg_cancel_btn_cb(lv_event_t *event)
{
    (void)event;
    cfg_close();
}

void cfg_save_btn_cb(lv_event_t *event)
{
    (void)event;
    if (cfg_url_ta != nullptr) {
        const char *u = lv_textarea_get_text(cfg_url_ta);
        if (u != nullptr && strlen(u) > 0) {
            snprintf(s_ai_cfg.base_url, sizeof(s_ai_cfg.base_url), "%s", u);
        }
    }
    if (cfg_token_ta != nullptr) {
        const char *t = lv_textarea_get_text(cfg_token_ta);
        if (t != nullptr) {
            snprintf(s_ai_cfg.token, sizeof(s_ai_cfg.token), "%s", t);
        }
    }
    if (cfg_model_ta != nullptr) {
        const char *m = lv_textarea_get_text(cfg_model_ta);
        if (m != nullptr && strlen(m) > 0) {
            snprintf(s_ai_cfg.model, sizeof(s_ai_cfg.model), "%s", m);
        }
    }
    if (cfg_max_ta != nullptr) {
        const char *mx = lv_textarea_get_text(cfg_max_ta);
        if (mx != nullptr && strlen(mx) > 0) {
            int val = (int)strtol(mx, NULL, 10);
            if (val > 0) {
                s_ai_cfg.max_tokens = val;
            }
        }
    }

    ai_storage_save(&s_ai_cfg);
    update_model_badge();
    cfg_close();
    add_message_bubble("system", "Configuracoes da IA salvas com sucesso!");
}

void cfg_ta_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(event);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        ui_keyboard_attach(ta);
    }
}

void open_config_modal(void)
{
    if (cfg_modal != nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    cfg_modal = lv_obj_create(chat_scr);
    lv_obj_set_size(cfg_modal, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cfg_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(cfg_modal, LV_OPA_60, 0);
    lv_obj_set_style_border_width(cfg_modal, 0, 0);
    lv_obj_clear_flag(cfg_modal, LV_OBJ_FLAG_SCROLLABLE);

    cfg_card = lv_obj_create(cfg_modal);
    lv_obj_set_width(cfg_card, lv_pct(88));
    lv_obj_set_height(cfg_card, lv_pct(85));
    lv_obj_center(cfg_card);
    lv_obj_set_style_bg_color(cfg_card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(cfg_card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(cfg_card, 1, 0);
    lv_obj_set_style_radius(cfg_card, 16, 0);
    lv_obj_set_style_pad_all(cfg_card, 16, 0);
    lv_obj_set_flex_flow(cfg_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cfg_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    cfg_title = lv_label_create(cfg_card);
    lv_label_set_text(cfg_title, "Configurações da IA (OpenCode Go / OpenAI)");
    lv_obj_set_style_text_font(cfg_title, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(cfg_title, lv_color_hex(pal->text), 0);
    lv_obj_set_style_margin_bottom(cfg_title, 8, 0);

    // URL Base
    cfg_url_lbl = lv_label_create(cfg_card);
    lv_label_set_text(cfg_url_lbl, "Base URL da API:");
    lv_obj_set_style_text_color(cfg_url_lbl, lv_color_hex(pal->text_muted), 0);
    lv_obj_set_style_text_font(cfg_url_lbl, &lv_font_montserrat_18_latin1, 0);

    cfg_url_ta = lv_textarea_create(cfg_card);
    lv_textarea_set_one_line(cfg_url_ta, true);
    lv_obj_set_scrollbar_mode(cfg_url_ta, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_text(cfg_url_ta, s_ai_cfg.base_url);
    lv_obj_set_width(cfg_url_ta, lv_pct(100));
    lv_obj_set_style_text_font(cfg_url_ta, &lv_font_montserrat_18_latin1, 0);
    lv_obj_add_event_cb(cfg_url_ta, cfg_ta_cb, LV_EVENT_ALL, nullptr);

    // Token
    cfg_token_lbl = lv_label_create(cfg_card);
    lv_label_set_text(cfg_token_lbl, "Bearer Token:");
    lv_obj_set_style_text_color(cfg_token_lbl, lv_color_hex(pal->text_muted), 0);
    lv_obj_set_style_text_font(cfg_token_lbl, &lv_font_montserrat_18_latin1, 0);

    cfg_token_ta = lv_textarea_create(cfg_card);
    lv_textarea_set_one_line(cfg_token_ta, true);
    lv_obj_set_scrollbar_mode(cfg_token_ta, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_password_mode(cfg_token_ta, true);
    lv_textarea_set_text(cfg_token_ta, s_ai_cfg.token);
    lv_obj_set_width(cfg_token_ta, lv_pct(100));
    lv_obj_set_style_text_font(cfg_token_ta, &lv_font_montserrat_18_latin1, 0);
    lv_obj_add_event_cb(cfg_token_ta, cfg_ta_cb, LV_EVENT_ALL, nullptr);

    // Modelo
    cfg_model_lbl = lv_label_create(cfg_card);
    lv_label_set_text(cfg_model_lbl, "Modelo (ex: deepseek-v4-pro, gpt-4o-mini):");
    lv_obj_set_style_text_color(cfg_model_lbl, lv_color_hex(pal->text_muted), 0);
    lv_obj_set_style_text_font(cfg_model_lbl, &lv_font_montserrat_18_latin1, 0);

    cfg_model_ta = lv_textarea_create(cfg_card);
    lv_textarea_set_one_line(cfg_model_ta, true);
    lv_obj_set_scrollbar_mode(cfg_model_ta, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_text(cfg_model_ta, s_ai_cfg.model);
    lv_obj_set_width(cfg_model_ta, lv_pct(100));
    lv_obj_set_style_text_font(cfg_model_ta, &lv_font_montserrat_18_latin1, 0);
    lv_obj_add_event_cb(cfg_model_ta, cfg_ta_cb, LV_EVENT_ALL, nullptr);

    // Max Tokens
    cfg_max_lbl = lv_label_create(cfg_card);
    lv_label_set_text(cfg_max_lbl, "Max Tokens (ex: 512, 1024):");
    lv_obj_set_style_text_color(cfg_max_lbl, lv_color_hex(pal->text_muted), 0);
    lv_obj_set_style_text_font(cfg_max_lbl, &lv_font_montserrat_18_latin1, 0);

    cfg_max_ta = lv_textarea_create(cfg_card);
    lv_textarea_set_one_line(cfg_max_ta, true);
    lv_obj_set_scrollbar_mode(cfg_max_ta, LV_SCROLLBAR_MODE_OFF);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", s_ai_cfg.max_tokens);
    lv_textarea_set_text(cfg_max_ta, buf);
    lv_obj_set_width(cfg_max_ta, lv_pct(100));
    lv_obj_set_style_text_font(cfg_max_ta, &lv_font_montserrat_18_latin1, 0);
    lv_obj_add_event_cb(cfg_max_ta, cfg_ta_cb, LV_EVENT_ALL, nullptr);

    // Botoes
    cfg_btn_row = lv_obj_create(cfg_card);
    lv_obj_set_width(cfg_btn_row, lv_pct(100));
    lv_obj_set_height(cfg_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cfg_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cfg_btn_row, 0, 0);
    lv_obj_set_style_pad_all(cfg_btn_row, 0, 0);
    lv_obj_set_style_margin_top(cfg_btn_row, 12, 0);
    lv_obj_clear_flag(cfg_btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cfg_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cfg_btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    cfg_cancel_btn = lv_button_create(cfg_btn_row);
    lv_obj_set_height(cfg_cancel_btn, 38);
    lv_obj_set_style_bg_color(cfg_cancel_btn, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(cfg_cancel_btn, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(cfg_cancel_btn, 1, 0);
    lv_obj_set_style_radius(cfg_cancel_btn, 8, 0);
    lv_obj_add_event_cb(cfg_cancel_btn, cfg_cancel_btn_cb, LV_EVENT_CLICKED, nullptr);

    cfg_cancel_lbl = lv_label_create(cfg_cancel_btn);
    lv_label_set_text(cfg_cancel_lbl, "Cancelar");
    lv_obj_set_style_text_color(cfg_cancel_lbl, lv_color_hex(pal->text), 0);
    lv_obj_set_style_text_font(cfg_cancel_lbl, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(cfg_cancel_lbl);

    cfg_save_btn = lv_button_create(cfg_btn_row);
    lv_obj_set_height(cfg_save_btn, 38);
    lv_obj_set_style_bg_color(cfg_save_btn, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_border_width(cfg_save_btn, 0, 0);
    lv_obj_set_style_radius(cfg_save_btn, 8, 0);
    lv_obj_add_event_cb(cfg_save_btn, cfg_save_btn_cb, LV_EVENT_CLICKED, nullptr);

    cfg_save_lbl = lv_label_create(cfg_save_btn);
    lv_label_set_text(cfg_save_lbl, "Salvar");
    lv_obj_set_style_text_color(cfg_save_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(cfg_save_lbl, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(cfg_save_lbl);
}

void config_btn_cb(lv_event_t *event)
{
    (void)event;
    open_config_modal();
}

} // namespace

struct ui_chat_view_s {
    lv_obj_t *parent = nullptr;
    ui_app_bar_t app_bar = {};
};

ui_chat_view_t *ui_chat_view_create(lv_obj_t *parent, ui_app_bar_t app_bar)
{
    chat_scr = parent;
    chat_app_bar = app_bar;
    const ui_palette_t *pal = ui_theme_get();

    model_badge = lv_obj_create(chat_app_bar.actions_cont);
    lv_obj_set_height(model_badge, 24);
    lv_obj_set_width(model_badge, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(model_badge, lv_color_hex(pal->accent_soft), 0);
    lv_obj_set_style_border_width(model_badge, 0, 0);
    lv_obj_set_style_radius(model_badge, 6, 0);
    lv_obj_set_style_pad_hor(model_badge, 8, 0);
    lv_obj_set_style_pad_ver(model_badge, 2, 0);
    lv_obj_set_style_margin_right(model_badge, 8, 0);
    lv_obj_clear_flag(model_badge, LV_OBJ_FLAG_SCROLLABLE);

    model_badge_lbl = lv_label_create(model_badge);
    lv_label_set_text(model_badge_lbl, "deepseek-v4-pro");
    lv_obj_set_style_text_font(model_badge_lbl, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(model_badge_lbl, lv_color_hex(pal->accent), 0);
    lv_obj_center(model_badge_lbl);

    clear_btn = ui_app_bar_add_action_button(&chat_app_bar, LV_SYMBOL_TRASH, clear_btn_cb, nullptr, &clear_lbl);

    // Messages Container
    messages_cont = lv_obj_create(chat_scr);
    lv_obj_set_width(messages_cont, lv_pct(100));
    lv_obj_set_style_bg_opa(messages_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(messages_cont, 0, 0);
    lv_obj_set_style_pad_hor(messages_cont, 16, 0);
    lv_obj_set_style_pad_ver(messages_cont, 8, 0);
    lv_obj_set_flex_flow(messages_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(messages_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(messages_cont, LV_SCROLLBAR_MODE_AUTO);

    // Input Bar at bottom
    input_bar = lv_obj_create(chat_scr);
    lv_obj_set_width(input_bar, lv_pct(100));
    lv_obj_set_height(input_bar, 60);
    lv_obj_set_style_bg_color(input_bar, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(input_bar, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_side(input_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(input_bar, 1, 0);
    lv_obj_set_style_radius(input_bar, 0, 0);
    lv_obj_set_style_pad_hor(input_bar, 12, 0);
    lv_obj_set_style_pad_ver(input_bar, 6, 0);
    lv_obj_clear_flag(input_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(input_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Input Text Area
    input_ta = lv_textarea_create(input_bar);
    lv_obj_set_flex_grow(input_ta, 1);
    lv_obj_set_height(input_ta, 44);
    lv_textarea_set_placeholder_text(input_ta, "Digite sua mensagem...");
    lv_textarea_set_one_line(input_ta, true);
    lv_obj_set_style_radius(input_ta, 8, 0);
    lv_obj_set_style_border_width(input_ta, 1, 0);
    lv_obj_set_style_border_color(input_ta, lv_color_hex(pal->border), 0);
    lv_obj_set_style_bg_color(input_ta, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_text_color(input_ta, lv_color_hex(pal->text), 0);
    lv_obj_set_style_text_font(input_ta, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_anim_duration(input_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(input_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_add_event_cb(input_ta, input_ta_cb, LV_EVENT_ALL, nullptr);

    // Send Button
    send_btn = lv_button_create(input_bar);
    lv_obj_set_size(send_btn, 48, 44);
    lv_obj_set_style_radius(send_btn, 8, 0);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_margin_left(send_btn, 8, 0);
    lv_obj_add_event_cb(send_btn, send_btn_cb, LV_EVENT_CLICKED, nullptr);

    send_lbl = lv_label_create(send_btn);
    lv_label_set_text(send_lbl, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(send_lbl, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(send_lbl, lv_color_white(), 0);
    lv_obj_center(send_lbl);

    ui_chat_apply_layout();
    ui_chat_refresh_theme();
    ui_chat_on_open();

    ui_chat_view_t *view = new ui_chat_view_t();
    view->parent = parent;
    view->app_bar = app_bar;
    return view;
}

void ui_chat_view_refresh_theme(ui_chat_view_t *view)
{
    (void)view;
    ui_chat_refresh_theme();
}

void ui_chat_view_apply_layout(ui_chat_view_t *view)
{
    (void)view;
    ui_chat_apply_layout();
}

void ui_chat_view_destroy(ui_chat_view_t *view)
{
    ui_chat_on_close();
    chat_scr = nullptr;
    delete view;
}

lv_obj_t *ui_chat_create(void)
{
    chat_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(chat_scr, LV_OBJ_FLAG_SCROLLABLE);

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(chat_scr, lv_color_hex(pal->background), 0);

    /* Barra padronizada do Chat IA com Badge do Modelo e Ações */
    chat_app_bar = ui_app_bar_create(chat_scr, "Chat IA", back_btn_cb, nullptr);

    model_badge = lv_obj_create(chat_app_bar.actions_cont);
    lv_obj_set_height(model_badge, 24);
    lv_obj_set_width(model_badge, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(model_badge, lv_color_hex(pal->accent_soft), 0);
    lv_obj_set_style_border_width(model_badge, 0, 0);
    lv_obj_set_style_radius(model_badge, 6, 0);
    lv_obj_set_style_pad_hor(model_badge, 8, 0);
    lv_obj_set_style_pad_ver(model_badge, 2, 0);
    lv_obj_set_style_margin_right(model_badge, 8, 0);
    lv_obj_clear_flag(model_badge, LV_OBJ_FLAG_SCROLLABLE);

    model_badge_lbl = lv_label_create(model_badge);
    lv_label_set_text(model_badge_lbl, "deepseek-v4-pro");
    lv_obj_set_style_text_font(model_badge_lbl, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(model_badge_lbl, lv_color_hex(pal->accent), 0);
    lv_obj_center(model_badge_lbl);

    clear_btn = ui_app_bar_add_action_button(&chat_app_bar, LV_SYMBOL_TRASH, clear_btn_cb, nullptr, &clear_lbl);
    config_btn = ui_app_bar_add_action_button(&chat_app_bar, LV_SYMBOL_SETTINGS, config_btn_cb, nullptr, &config_lbl);

    // Messages Container
    messages_cont = lv_obj_create(chat_scr);
    lv_obj_set_width(messages_cont, lv_pct(100));
    lv_obj_set_style_bg_opa(messages_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(messages_cont, 0, 0);
    lv_obj_set_style_pad_hor(messages_cont, 16, 0);
    lv_obj_set_style_pad_ver(messages_cont, 8, 0);
    lv_obj_set_flex_flow(messages_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(messages_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(messages_cont, LV_SCROLLBAR_MODE_AUTO);

    // Input Bar at bottom
    input_bar = lv_obj_create(chat_scr);
    lv_obj_set_width(input_bar, lv_pct(100));
    lv_obj_set_height(input_bar, 60);
    lv_obj_set_style_bg_color(input_bar, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(input_bar, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_side(input_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(input_bar, 1, 0);
    lv_obj_set_style_radius(input_bar, 0, 0);
    lv_obj_set_style_pad_hor(input_bar, 12, 0);
    lv_obj_set_style_pad_ver(input_bar, 6, 0);
    lv_obj_clear_flag(input_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(input_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    input_ta = lv_textarea_create(input_bar);
    lv_textarea_set_one_line(input_ta, true);
    lv_textarea_set_placeholder_text(input_ta, "Digite sua mensagem para a IA...");
    lv_obj_set_flex_grow(input_ta, 1);
    lv_obj_set_height(input_ta, 46);
    lv_obj_set_scrollbar_mode(input_ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_hor(input_ta, 12, 0);
    lv_obj_set_style_pad_ver(input_ta, 8, 0);
    lv_obj_set_style_bg_color(input_ta, lv_color_hex(pal->background), 0);
    lv_obj_set_style_border_color(input_ta, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(input_ta, 1, 0);
    lv_obj_set_style_radius(input_ta, 8, 0);
    lv_obj_set_style_text_color(input_ta, lv_color_hex(pal->text), 0);
    lv_obj_set_style_text_font(input_ta, &lv_font_montserrat_18_latin1, 0);
    lv_obj_add_event_cb(input_ta, input_ta_cb, LV_EVENT_ALL, nullptr);

    send_btn = lv_button_create(input_bar);
    lv_obj_set_size(send_btn, 48, 46);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_border_width(send_btn, 0, 0);
    lv_obj_set_style_radius(send_btn, 8, 0);
    lv_obj_set_style_margin_left(send_btn, 8, 0);
    lv_obj_set_style_pad_all(send_btn, 0, 0);
    lv_obj_add_event_cb(send_btn, send_btn_cb, LV_EVENT_CLICKED, nullptr);

    send_lbl = lv_label_create(send_btn);
    lv_label_set_text(send_lbl, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(send_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(send_lbl, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(send_lbl);

    ai_storage_load(&s_ai_cfg);
    update_model_badge();

    if (strlen(s_ai_cfg.token) == 0) {
        add_message_bubble("system",
                           "Bem-vindo ao Chat IA! Configure o token no botao [Engrenagem] acima para comecar.");
    }

    ui_chat_apply_layout();

    return chat_scr;
}

void ui_chat_on_open(void)
{
    ai_storage_load(&s_ai_cfg);
    update_model_badge();
    ui_chat_apply_layout();
    scroll_to_bottom();
}

void ui_chat_on_close(void)
{
    if (ai_client_is_busy()) {
        ai_client_cancel();
    }
    cfg_close();
}

void ui_chat_refresh_theme(void)
{
    if (chat_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(chat_scr, lv_color_hex(pal->background), 0);

    ui_app_bar_refresh_theme(&chat_app_bar);

    if (model_badge != nullptr) {
        lv_obj_set_style_bg_color(model_badge, lv_color_hex(pal->accent_soft), 0);
    }
    if (model_badge_lbl != nullptr) {
        lv_obj_set_style_text_color(model_badge_lbl, lv_color_hex(pal->accent), 0);
    }
    if (input_bar != nullptr) {
        lv_obj_set_style_bg_color(input_bar, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(input_bar, lv_color_hex(pal->border), 0);
    }
    if (input_ta != nullptr) {
        lv_obj_set_style_bg_color(input_ta, lv_color_hex(pal->background), 0);
        lv_obj_set_style_border_color(input_ta, lv_color_hex(pal->border), 0);
        lv_obj_set_style_text_color(input_ta, lv_color_hex(pal->text), 0);
    }
    if (send_btn != nullptr && !ai_client_is_busy()) {
        lv_obj_set_style_bg_color(send_btn, lv_color_hex(pal->accent), 0);
    }
}

void ui_chat_apply_layout(void)
{
    if (chat_scr == nullptr || chat_app_bar.bar == nullptr || messages_cont == nullptr || input_bar == nullptr) {
        return;
    }

    int32_t scr_w = lv_display_get_horizontal_resolution(nullptr);
    int32_t scr_h = lv_display_get_vertical_resolution(nullptr);
    if (scr_w <= 0)
        scr_w = 800;
    if (scr_h <= 0)
        scr_h = 480;

    int32_t kb_h = ui_keyboard_is_visible() ? ui_keyboard_get_height() : 0;
    int32_t top_y = UI_BAR_HEIGHT;
    int32_t header_h = UI_BAR_HEIGHT;
    int32_t input_h = 60;

    lv_obj_set_pos(chat_app_bar.bar, 0, top_y);
    lv_obj_set_size(chat_app_bar.bar, scr_w, header_h);

    int32_t input_y = scr_h - kb_h - input_h;
    if (input_y < top_y + header_h) {
        input_y = top_y + header_h;
    }
    lv_obj_set_pos(input_bar, 0, input_y);
    lv_obj_set_size(input_bar, scr_w, input_h);

    int32_t msg_y = top_y + header_h;
    int32_t msg_h = input_y - msg_y;
    if (msg_h < 40) {
        msg_h = 40;
    }
    lv_obj_set_pos(messages_cont, 0, msg_y);
    lv_obj_set_size(messages_cont, scr_w, msg_h);
}
