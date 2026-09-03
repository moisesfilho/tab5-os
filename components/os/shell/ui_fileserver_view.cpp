#include "ui_fileserver_view.h"
#include "app_registry.h"
#include "http_file_server.h"
#include "ui_bar.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "wifi_mgr.h"
#include "esp_log.h"
#include "esp_netif.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>

static const char *TAG = "tab5_ui_fileserver";

namespace {

lv_obj_t *fileserver_scr = nullptr;
ui_app_bar_t fileserver_app_bar = {};

lv_obj_t *content_container = nullptr;

/* Card Status */
lv_obj_t *card_status = nullptr;
lv_obj_t *status_title = nullptr;
lv_obj_t *status_badge = nullptr;
lv_obj_t *status_badge_label = nullptr;
lv_obj_t *toggle_btn = nullptr;
lv_obj_t *toggle_btn_label = nullptr;

/* Card Rede & URL */
lv_obj_t *card_network = nullptr;
lv_obj_t *net_title = nullptr;
lv_obj_t *url_box = nullptr;
lv_obj_t *url_label = nullptr;
lv_obj_t *net_info_label = nullptr;
lv_obj_t *net_hint_label = nullptr;

/* Card Estatísticas */
lv_obj_t *card_stats = nullptr;
lv_obj_t *stats_title = nullptr;
lv_obj_t *stats_info_label = nullptr;

lv_timer_t *refresh_timer = nullptr;

void update_ui_state(void)
{
    bool running = http_file_server_is_running();
    const ui_palette_t *pal = ui_theme_get();

    /* Atualiza Badge de Status */
    if (status_badge != nullptr && status_badge_label != nullptr) {
        if (running) {
            lv_obj_set_style_bg_color(status_badge, lv_color_hex(0x2ecc71), 0);
            lv_label_set_text(status_badge_label, LV_SYMBOL_OK " ATIVO");
        } else {
            lv_obj_set_style_bg_color(status_badge, lv_color_hex(0xe74c3c), 0);
            lv_label_set_text(status_badge_label, LV_SYMBOL_CLOSE " INATIVO");
        }
    }

    /* Atualiza Botao de Controle */
    if (toggle_btn != nullptr && toggle_btn_label != nullptr) {
        if (running) {
            lv_obj_set_style_bg_color(toggle_btn, lv_color_hex(0xe74c3c), 0);
            lv_label_set_text(toggle_btn_label, LV_SYMBOL_POWER " Desligar Servidor");
        } else {
            lv_obj_set_style_bg_color(toggle_btn, lv_color_hex(pal->accent), 0);
            lv_label_set_text(toggle_btn_label, LV_SYMBOL_PLAY " Iniciar Servidor");
        }
    }

    /* Obtem IP local */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    bool has_ip = false;
    char ip_str[32] = "Desconectado";
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        has_ip = true;
    }

    /* Atualiza URL e Informações de Rede */
    if (url_label != nullptr) {
        if (running && has_ip) {
            char url_buf[64];
            snprintf(url_buf, sizeof(url_buf), "http://%s:%u/", ip_str, (unsigned)http_file_server_get_port());
            lv_label_set_text(url_label, url_buf);
            lv_obj_set_style_text_color(url_label, lv_color_hex(0x89b4fa), 0);
        } else if (running) {
            lv_label_set_text(url_label, "Aguardando IP Wi-Fi...");
            lv_obj_set_style_text_color(url_label, lv_color_hex(pal->text_muted), 0);
        } else {
            lv_label_set_text(url_label, "Servidor Desativado");
            lv_obj_set_style_text_color(url_label, lv_color_hex(pal->text_muted), 0);
        }
    }

    if (net_info_label != nullptr) {
        char net_buf[160];
        snprintf(net_buf, sizeof(net_buf), "IP Local: %s   |   Porta: %u   |   Wi-Fi: %s", ip_str,
                 (unsigned)http_file_server_get_port(), has_ip ? "Conectado" : "Desconectado");
        lv_label_set_text(net_info_label, net_buf);
    }

    /* Contagem de Fotos e Espaço no SD */
    if (stats_info_label != nullptr) {
        size_t photo_count = 0;
        size_t total_bytes = 0;
        DIR *d = opendir("/sdcard/imagens");
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != nullptr) {
                if (ent->d_name[0] == '.')
                    continue;
                std::string full = std::string("/sdcard/imagens/") + ent->d_name;
                struct stat st;
                if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                    photo_count++;
                    total_bytes += st.st_size;
                }
            }
            closedir(d);
        }

        char stats_buf[160];
        snprintf(stats_buf, sizeof(stats_buf),
                 "Fotos em /sdcard/imagens: %zu fotos\nEspaço Ocupado: %.1f MB (%zu bytes)", photo_count,
                 (float)total_bytes / (1024.0F * 1024.0F), total_bytes);
        lv_label_set_text(stats_info_label, stats_buf);
    }
}

void toggle_btn_cb(lv_event_t *e)
{
    (void)e;
    if (http_file_server_is_running()) {
        http_file_server_stop();
    } else {
        http_file_server_start();
    }
    update_ui_state();
}

void close_click_cb(lv_event_t *e)
{
    (void)e;
    ui_shell_close_fileserver();
}

void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_ui_state();
}

void apply_fileserver_theme(void)
{
    if (fileserver_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(fileserver_scr, lv_color_hex(pal->background), 0);

    /* Barra Superior */
    ui_app_bar_refresh_theme(&fileserver_app_bar);

    /* Cards */
    lv_obj_t *cards[] = {card_status, card_network, card_stats};
    for (lv_obj_t *c : cards) {
        if (c != nullptr) {
            lv_obj_set_style_bg_color(c, lv_color_hex(pal->surface), 0);
            lv_obj_set_style_border_color(c, lv_color_hex(pal->border), 0);
        }
    }

    if (status_title)
        lv_obj_set_style_text_color(status_title, lv_color_hex(pal->text), 0);
    if (net_title)
        lv_obj_set_style_text_color(net_title, lv_color_hex(pal->text), 0);
    if (stats_title)
        lv_obj_set_style_text_color(stats_title, lv_color_hex(pal->text), 0);

    if (url_box != nullptr) {
        lv_obj_set_style_bg_color(url_box, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(url_box, lv_color_hex(pal->border), 0);
    }

    if (net_info_label)
        lv_obj_set_style_text_color(net_info_label, lv_color_hex(pal->text), 0);
    if (net_hint_label)
        lv_obj_set_style_text_color(net_hint_label, lv_color_hex(pal->text_muted), 0);
    if (stats_info_label)
        lv_obj_set_style_text_color(stats_info_label, lv_color_hex(pal->text_muted), 0);

    update_ui_state();
}

} // namespace

struct ui_fileserver_view_s {
    lv_obj_t *parent = nullptr;
    ui_app_bar_t app_bar = {};
};

ui_fileserver_view_t *ui_fileserver_view_create(lv_obj_t *parent, ui_app_bar_t app_bar)
{
    (void)app_bar;
    fileserver_scr = parent;

    /* Container de Conteudo com Scroll */
    content_container = lv_obj_create(fileserver_scr);
    lv_obj_set_size(content_container, lv_pct(100), LV_PCT(100));
    lv_obj_set_y(content_container, UI_BAR_HEIGHT * 2);
    lv_obj_set_style_bg_opa(content_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_container, 0, 0);
    lv_obj_set_style_pad_all(content_container, 16, 0);
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 1. Card Status & Controle */
    card_status = lv_obj_create(content_container);
    lv_obj_set_size(card_status, lv_pct(96), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card_status, 14, 0);
    lv_obj_set_style_border_width(card_status, 1, 0);
    lv_obj_set_style_pad_all(card_status, 14, 0);
    lv_obj_clear_flag(card_status, LV_OBJ_FLAG_SCROLLABLE);

    status_title = lv_label_create(card_status);
    lv_label_set_text(status_title, "Estado do Serviço");
    lv_obj_set_style_text_font(status_title, &lv_font_montserrat_18_latin1, 0);
    lv_obj_align(status_title, LV_ALIGN_TOP_LEFT, 0, 4);

    status_badge = lv_obj_create(card_status);
    lv_obj_set_size(status_badge, 120, 36);
    lv_obj_align(status_badge, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(status_badge, 18, 0);
    lv_obj_set_style_border_width(status_badge, 0, 0);
    lv_obj_set_style_pad_all(status_badge, 0, 0);
    lv_obj_clear_flag(status_badge, LV_OBJ_FLAG_SCROLLABLE);

    status_badge_label = lv_label_create(status_badge);
    lv_label_set_text(status_badge_label, "ATIVO");
    lv_obj_set_style_text_font(status_badge_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(status_badge_label, lv_color_white(), 0);
    lv_obj_center(status_badge_label);

    toggle_btn = lv_button_create(card_status);
    lv_obj_set_size(toggle_btn, lv_pct(100), 46);
    lv_obj_align(toggle_btn, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_radius(toggle_btn, 10, 0);
    lv_obj_add_event_cb(toggle_btn, toggle_btn_cb, LV_EVENT_CLICKED, nullptr);

    toggle_btn_label = lv_label_create(toggle_btn);
    lv_label_set_text(toggle_btn_label, "Iniciar Servidor");
    lv_obj_set_style_text_font(toggle_btn_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(toggle_btn_label);

    /* 2. Card Rede & Acesso */
    card_network = lv_obj_create(content_container);
    lv_obj_set_size(card_network, lv_pct(96), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card_network, 14, 0);
    lv_obj_set_style_border_width(card_network, 1, 0);
    lv_obj_set_style_pad_all(card_network, 14, 0);
    lv_obj_clear_flag(card_network, LV_OBJ_FLAG_SCROLLABLE);

    net_title = lv_label_create(card_network);
    lv_label_set_text(net_title, "Acesso Web / WebDAV");
    lv_obj_set_style_text_font(net_title, &lv_font_montserrat_18_latin1, 0);
    lv_obj_align(net_title, LV_ALIGN_TOP_LEFT, 0, 0);

    url_box = lv_obj_create(card_network);
    lv_obj_set_size(url_box, lv_pct(100), 44);
    lv_obj_align(url_box, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_radius(url_box, 8, 0);
    lv_obj_set_style_border_width(url_box, 1, 0);
    lv_obj_set_style_pad_all(url_box, 0, 0);
    lv_obj_clear_flag(url_box, LV_OBJ_FLAG_SCROLLABLE);

    url_label = lv_label_create(url_box);
    lv_label_set_text(url_label, "http://...:80/");
    lv_obj_set_style_text_font(url_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(url_label);

    net_info_label = lv_label_create(card_network);
    lv_label_set_text(net_info_label, "Sub-rede: Wi-Fi Station");
    lv_obj_set_style_text_font(net_info_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(net_info_label, LV_ALIGN_TOP_LEFT, 0, 84);

    net_hint_label = lv_label_create(card_network);
    lv_label_set_text(net_hint_label, "Digite este endereco no navegador do seu computador/celular.");
    lv_obj_set_style_text_font(net_hint_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(net_hint_label, LV_ALIGN_TOP_LEFT, 0, 106);

    /* 3. Card Estatísticas */
    card_stats = lv_obj_create(content_container);
    lv_obj_set_size(card_stats, lv_pct(96), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card_stats, 14, 0);
    lv_obj_set_style_border_width(card_stats, 1, 0);
    lv_obj_set_style_pad_all(card_stats, 14, 0);
    lv_obj_clear_flag(card_stats, LV_OBJ_FLAG_SCROLLABLE);

    stats_title = lv_label_create(card_stats);
    lv_label_set_text(stats_title, "Estatísticas de Transferência");
    lv_obj_set_style_text_font(stats_title, &lv_font_montserrat_18_latin1, 0);
    lv_obj_align(stats_title, LV_ALIGN_TOP_LEFT, 0, 0);

    stats_info_label = lv_label_create(card_stats);
    lv_label_set_text(stats_info_label, "Upload: 0 B   |   Download: 0 B   |   Conexões: 0");
    lv_obj_set_style_text_font(stats_info_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(stats_info_label, LV_ALIGN_TOP_LEFT, 0, 32);

    ui_fileserver_refresh_theme();
    ui_fileserver_apply_layout();
    ui_fileserver_on_open();

    ui_fileserver_view_t *view = new ui_fileserver_view_t();
    view->parent = parent;
    view->app_bar = app_bar;
    return view;
}

void ui_fileserver_view_refresh_theme(ui_fileserver_view_t *view)
{
    (void)view;
    ui_fileserver_refresh_theme();
}

void ui_fileserver_view_apply_layout(ui_fileserver_view_t *view)
{
    (void)view;
    ui_fileserver_apply_layout();
}

void ui_fileserver_view_destroy(ui_fileserver_view_t *view)
{
    ui_fileserver_on_close();
    fileserver_scr = nullptr;
    delete view;
}

lv_obj_t *ui_fileserver_create(void)
{
    fileserver_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(fileserver_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Barra Superior padronizada */
    fileserver_app_bar = ui_app_bar_create(fileserver_scr, "Servidor de Arquivos", close_click_cb, nullptr);

    /* Container de Conteudo com Scroll */
    content_container = lv_obj_create(fileserver_scr);
    lv_obj_set_size(content_container, lv_pct(100), LV_PCT(100));
    lv_obj_set_y(content_container, UI_BAR_HEIGHT * 2);
    lv_obj_set_style_bg_opa(content_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_container, 0, 0);
    lv_obj_set_style_pad_all(content_container, 16, 0);
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 1. Card Status & Controle */
    card_status = lv_obj_create(content_container);
    lv_obj_set_size(card_status, lv_pct(96), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card_status, 14, 0);
    lv_obj_set_style_border_width(card_status, 1, 0);
    lv_obj_set_style_pad_all(card_status, 14, 0);
    lv_obj_clear_flag(card_status, LV_OBJ_FLAG_SCROLLABLE);

    status_title = lv_label_create(card_status);
    lv_label_set_text(status_title, "Estado do Serviço");
    lv_obj_set_style_text_font(status_title, &lv_font_montserrat_18_latin1, 0);
    lv_obj_align(status_title, LV_ALIGN_TOP_LEFT, 0, 4);

    status_badge = lv_obj_create(card_status);
    lv_obj_set_size(status_badge, 120, 36);
    lv_obj_align(status_badge, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(status_badge, 18, 0);
    lv_obj_set_style_border_width(status_badge, 0, 0);
    lv_obj_set_style_pad_all(status_badge, 0, 0);
    lv_obj_clear_flag(status_badge, LV_OBJ_FLAG_SCROLLABLE);

    status_badge_label = lv_label_create(status_badge);
    lv_label_set_text(status_badge_label, "ATIVO");
    lv_obj_set_style_text_font(status_badge_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(status_badge_label, lv_color_white(), 0);
    lv_obj_center(status_badge_label);

    toggle_btn = lv_button_create(card_status);
    lv_obj_set_size(toggle_btn, lv_pct(100), 46);
    lv_obj_align(toggle_btn, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_radius(toggle_btn, 10, 0);
    lv_obj_add_event_cb(toggle_btn, toggle_btn_cb, LV_EVENT_CLICKED, nullptr);

    toggle_btn_label = lv_label_create(toggle_btn);
    lv_label_set_text(toggle_btn_label, LV_SYMBOL_POWER " Desligar Servidor");
    lv_obj_set_style_text_font(toggle_btn_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(toggle_btn_label, lv_color_white(), 0);
    lv_obj_center(toggle_btn_label);

    /* 2. Card Conexao e URL */
    card_network = lv_obj_create(content_container);
    lv_obj_set_size(card_network, lv_pct(96), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card_network, 14, 0);
    lv_obj_set_style_border_width(card_network, 1, 0);
    lv_obj_set_style_pad_all(card_network, 14, 0);
    lv_obj_set_style_margin_top(card_network, 8, 0);
    lv_obj_clear_flag(card_network, LV_OBJ_FLAG_SCROLLABLE);

    net_title = lv_label_create(card_network);
    lv_label_set_text(net_title, "Acesso Web na Rede Local");
    lv_obj_set_style_text_font(net_title, &lv_font_montserrat_18_latin1, 0);
    lv_obj_align(net_title, LV_ALIGN_TOP_LEFT, 0, 0);

    url_box = lv_obj_create(card_network);
    lv_obj_set_size(url_box, lv_pct(100), 44);
    lv_obj_align(url_box, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_radius(url_box, 8, 0);
    lv_obj_set_style_border_width(url_box, 1, 0);
    lv_obj_set_style_pad_all(url_box, 8, 0);
    lv_obj_clear_flag(url_box, LV_OBJ_FLAG_SCROLLABLE);

    url_label = lv_label_create(url_box);
    lv_label_set_text(url_label, "http://192.168.68.107:8080/");
    lv_obj_set_style_text_font(url_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_center(url_label);

    net_info_label = lv_label_create(card_network);
    lv_label_set_text(net_info_label, "IP: 192.168.68.107 | Porta: 8080");
    lv_obj_set_style_text_font(net_info_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_align(net_info_label, LV_ALIGN_TOP_LEFT, 0, 84);

    net_hint_label = lv_label_create(card_network);
    lv_label_set_text(net_hint_label, "Dica: Digite o endereço acima no navegador do computador ou celular\nconectado "
                                      "à mesma rede Wi-Fi para visualizar e baixar as fotos.");
    lv_obj_set_style_text_font(net_hint_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_align(net_hint_label, LV_ALIGN_TOP_LEFT, 0, 114);

    /* 3. Card Estatisticas */
    card_stats = lv_obj_create(content_container);
    lv_obj_set_size(card_stats, lv_pct(96), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card_stats, 14, 0);
    lv_obj_set_style_border_width(card_stats, 1, 0);
    lv_obj_set_style_pad_all(card_stats, 14, 0);
    lv_obj_set_style_margin_top(card_stats, 8, 0);
    lv_obj_clear_flag(card_stats, LV_OBJ_FLAG_SCROLLABLE);

    stats_title = lv_label_create(card_stats);
    lv_label_set_text(stats_title, "Armazenamento Compartilhado");
    lv_obj_set_style_text_font(stats_title, &lv_font_montserrat_18_latin1, 0);
    lv_obj_align(stats_title, LV_ALIGN_TOP_LEFT, 0, 0);

    stats_info_label = lv_label_create(card_stats);
    lv_label_set_text(stats_info_label, "Fotos em /sdcard/imagens: 0 fotos\nEspaço Ocupado: 0 MB");
    lv_obj_set_style_text_font(stats_info_label, &lv_font_montserrat_18_latin1, 0);
    lv_obj_align(stats_info_label, LV_ALIGN_TOP_LEFT, 0, 30);

    apply_fileserver_theme();
    return fileserver_scr;
}

void ui_fileserver_on_open(void)
{
    ESP_LOGI(TAG, "abrindo app Servidor de Arquivos");
    update_ui_state();
    if (refresh_timer == nullptr) {
        refresh_timer = lv_timer_create(timer_cb, 2000, nullptr);
    }
}

void ui_fileserver_on_close(void)
{
    ESP_LOGI(TAG, "fechando app Servidor de Arquivos");
    /* Para o servidor HTTP ao fechar o app: libera RAM interna (tasks/buffers
     * de rede) que, se mantida ativa, esgota a heap DMA e impede o audio
     * (I2S/ES8388) e o SD de alocarem buffers (abort no bsp_audio_init). */
    http_file_server_stop();
    if (refresh_timer != nullptr) {
        lv_timer_delete(refresh_timer);
        refresh_timer = nullptr;
    }
}

void ui_fileserver_refresh_theme(void)
{
    apply_fileserver_theme();
}

void ui_fileserver_apply_layout(void)
{
    if (fileserver_scr != nullptr) {
        lv_obj_invalidate(fileserver_scr);
    }
}

void ui_fileserver_register(void)
{
    static const app_desc_t s_fileserver_desc = {
        .id = "fileserver",
        .name = "Servidor",
        .icon_symbol = LV_SYMBOL_DRIVE,
        .icon_bg_color = nullptr,
        .icon_builder = nullptr,
        .icon_theme_refresh = nullptr,
        .on_launch = ui_shell_open_fileserver,
        .file_extensions = nullptr,
        .on_open_file = nullptr,
    };
    app_registry_register(&s_fileserver_desc);
}
