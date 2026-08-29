/**
 * @file ui_installer.cpp
 * @brief Implementação da Interface do Instalador de Aplicações
 */

#include "ui_installer.h"
#include "tab5_package_mgr.h"
#include "tab5_manifest.h"
#include "storage_mgr.h"
#include "file_assoc.h"
#include "ui_theme.h"
#include "ui_font.h"
#include "ui_bar.h"
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#if LVGL_VERSION_MAJOR >= 9
#define HAVE_LVGL 1
#else
#define HAVE_LVGL 0
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "tab5_ui_installer";
#define LOG_I(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#else
#define LOG_I(fmt, ...)
#define LOG_W(fmt, ...)
#define LOG_E(fmt, ...)
#endif

namespace {

static lv_obj_t *s_modal_mask = nullptr;
static std::string s_pending_pkg_path;
static tab5_manifest_t s_pending_manifest;

#if HAVE_LVGL
static void show_installer_toast(const char *msg)
{
    const ui_palette_t *pal = ui_theme_get();
    lv_obj_t *top = lv_layer_top();
    lv_obj_t *toast = lv_label_create(top);
    lv_label_set_text(toast, msg);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 10);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_pad_hor(toast, 16, 0);
    lv_obj_set_style_pad_ver(toast, 8, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(toast, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_text_color(toast, lv_color_hex(pal->text), 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(pal->accent), 0);

    lv_timer_t *timer = lv_timer_create(
        [](lv_timer_t *t) {
            lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(t);
            if (obj != nullptr) {
                lv_obj_delete(obj);
            }
            lv_timer_delete(t);
        },
        3000, toast);
    (void)timer;
}

static void btn_cancel_event_cb(lv_event_t *e)
{
    (void)e;
    ui_installer_close();
}

static void btn_install_event_cb(lv_event_t *e)
{
    (void)e;
    char installed_id[64] = {0};
    tab5_err_t err = tab5_package_mgr_install(s_pending_pkg_path.c_str(), installed_id, sizeof(installed_id));
    if (err == TAB5_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s v%s instalada com sucesso!", s_pending_manifest.name,
                 s_pending_manifest.version);
        show_installer_toast(msg);
        LOG_I("Aplicacao %s instalada via UI", installed_id);
    } else {
        show_installer_toast("Falha ao instalar o aplicativo.");
        LOG_E("Falha ao instalar pacote %s (err=%d)", s_pending_pkg_path.c_str(), (int)err);
    }
    ui_installer_close();
}
#endif

} // namespace

void ui_installer_init(void)
{
    file_assoc_register(".tab5pkg", [](const char *filepath) { ui_installer_open(filepath); });
}

void ui_installer_close(void)
{
#if HAVE_LVGL
    if (s_modal_mask != nullptr) {
        lv_obj_delete(s_modal_mask);
        s_modal_mask = nullptr;
    }
#endif
    s_pending_pkg_path.clear();
}

void ui_installer_open(const char *pkg_path)
{
    if (pkg_path == nullptr || pkg_path[0] == '\0') {
        return;
    }

    s_pending_pkg_path = pkg_path;
    memset(&s_pending_manifest, 0, sizeof(s_pending_manifest));

    // Carrega manifesto
    std::string manifest_file = std::string(pkg_path) + "/manifest.json";
    tab5_err_t err = tab5_manifest_load_from_file(manifest_file.c_str(), &s_pending_manifest);
    if (err != TAB5_OK) {
        err = tab5_manifest_load_from_file(pkg_path, &s_pending_manifest);
        if (err != TAB5_OK) {
#if HAVE_LVGL
            show_installer_toast("Pacote invalido ou corrompido.");
#endif
            return;
        }
    }

    // Verifica espaço
    uint64_t pkg_size = tab5_storage_mgr_calculate_path_size(pkg_path);
    if (!tab5_storage_mgr_has_enough_sd_space(pkg_size)) {
#if HAVE_LVGL
        show_installer_toast("Espaco insuficiente no Cartao SD!");
#endif
        return;
    }

#if HAVE_LVGL
    if (s_modal_mask != nullptr) {
        ui_installer_close();
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_t *top_layer = lv_layer_top();
    s_modal_mask = lv_obj_create(top_layer);
    lv_obj_clear_flag(s_modal_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_modal_mask, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_modal_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_modal_mask, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_modal_mask, 0, 0);
    lv_obj_set_style_pad_all(s_modal_mask, 0, 0);
    lv_obj_set_flex_flow(s_modal_mask, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_modal_mask, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Card central
    lv_obj_t *card = lv_obj_create(s_modal_mask);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 520, 500);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Título do modal
    lv_obj_t *lbl_header = lv_label_create(card);
    lv_label_set_text(lbl_header, "Instalador de Aplicativo");
    lv_obj_set_style_text_font(lbl_header, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(lbl_header, lv_color_hex(pal->accent), 0);

    // Ícone e Nome da App
    lv_obj_t *app_row = lv_obj_create(card);
    lv_obj_clear_flag(app_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(app_row, lv_pct(100), 65);
    lv_obj_set_style_bg_opa(app_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(app_row, 0, 0);
    lv_obj_set_style_pad_all(app_row, 0, 0);
    lv_obj_set_flex_flow(app_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(app_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon_lbl = lv_label_create(app_row);
    lv_label_set_text(icon_lbl, s_pending_manifest.icon_symbol[0] ? s_pending_manifest.icon_symbol : "#");
    lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_28_latin1, 0);
    lv_obj_set_style_text_color(icon_lbl, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_margin_right(icon_lbl, 15, 0);

    lv_obj_t *info_col = lv_obj_create(app_row);
    lv_obj_clear_flag(info_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_col, 0, 0);
    lv_obj_set_style_pad_all(info_col, 0, 0);
    lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *name_lbl = lv_label_create(info_col);
    lv_label_set_text(name_lbl, s_pending_manifest.name);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18_latin1, 0);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(pal->text), 0);

    char meta_str[128];
    snprintf(meta_str, sizeof(meta_str), "v%s  •  %s", s_pending_manifest.version,
             s_pending_manifest.author[0] ? s_pending_manifest.author : "Comunidade");
    lv_obj_t *ver_lbl = lv_label_create(info_col);
    lv_label_set_text(ver_lbl, meta_str);
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(pal->text_muted), 0);

    // Descrição
    if (s_pending_manifest.description[0] != '\0') {
        lv_obj_t *desc_lbl = lv_label_create(card);
        lv_label_set_text(desc_lbl, s_pending_manifest.description);
        lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(desc_lbl, lv_pct(100));
        lv_obj_set_style_text_color(desc_lbl, lv_color_hex(pal->text_muted), 0);
    }

    // Caixa de permissões
    lv_obj_t *perm_box = lv_obj_create(card);
    lv_obj_set_size(perm_box, lv_pct(100), 150);
    lv_obj_set_scroll_dir(perm_box, LV_DIR_VER);
    lv_obj_set_style_bg_color(perm_box, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_border_color(perm_box, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(perm_box, 1, 0);
    lv_obj_set_style_radius(perm_box, 8, 0);
    lv_obj_set_style_pad_all(perm_box, 10, 0);
    lv_obj_set_flex_flow(perm_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(perm_box, 4, 0);

    lv_obj_t *perm_title = lv_label_create(perm_box);
    lv_label_set_text(perm_title, "Permissoes Requisitadas:");
    lv_obj_set_style_text_color(perm_title, lv_color_hex(pal->text_muted), 0);

    auto add_perm_item = [perm_box, pal](const char *text) {
        lv_obj_t *lbl = lv_label_create(perm_box);
        lv_label_set_text_fmt(lbl, " • %s", text);
        lv_obj_set_style_text_color(lbl, lv_color_hex(pal->text), 0);
    };

    if (s_pending_manifest.permissions & (TAB5_PERM_STORAGE_READ | TAB5_PERM_STORAGE_WRITE)) {
        add_perm_item("Armazenamento privado (leitura e escrita)");
    }
    if (s_pending_manifest.permissions & TAB5_PERM_UI_KEYBOARD) {
        add_perm_item("Teclado Virtual do Sistema");
    }
    if (s_pending_manifest.permissions & TAB5_PERM_NETWORK) {
        add_perm_item("Acesso a Rede / Internet");
    }
    if (s_pending_manifest.permissions & TAB5_PERM_BLUETOOTH) {
        add_perm_item("Acesso a Bluetooth");
    }
    if (s_pending_manifest.permissions & TAB5_PERM_AUDIO) {
        add_perm_item("Reproducao de Audio / Buzzer");
    }
    if (s_pending_manifest.permissions == 0) {
        add_perm_item("Nenhuma permissao especial requisitada");
    }

    // Botões Cancelar / Instalar
    lv_obj_t *btn_row = lv_obj_create(card);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(btn_row, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_cancel = lv_button_create(btn_row);
    lv_obj_set_size(btn_cancel, 180, 42);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_border_color(btn_cancel, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_radius(btn_cancel, 8, 0);
    lv_obj_add_event_cb(btn_cancel, btn_cancel_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, "Cancelar");
    lv_obj_set_style_text_color(lbl_c, lv_color_hex(pal->text), 0);
    lv_obj_center(lbl_c);

    lv_obj_t *btn_install = lv_button_create(btn_row);
    lv_obj_set_size(btn_install, 180, 42);
    lv_obj_set_style_bg_color(btn_install, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_radius(btn_install, 8, 0);
    lv_obj_add_event_cb(btn_install, btn_install_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_i = lv_label_create(btn_install);
    lv_label_set_text(lbl_i, "Instalar");
    lv_obj_set_style_text_color(lbl_i, lv_color_white(), 0);
    lv_obj_center(lbl_i);

#endif
}
