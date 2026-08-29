/**
 * @file ui_storage_view.cpp
 * @brief Implementação da Tela de Gerenciamento de Armazenamento e Memória
 */

#include "ui_storage_view.h"
#include "storage_mgr.h"
#include "tab5_package_mgr.h"
#include "ui_installer.h"
#include "ui_app_bar.h"
#include "ui_theme.h"
#include "ui_font.h"
#include "app_registry.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

#if LVGL_VERSION_MAJOR >= 9
#define HAVE_LVGL 1
#else
#define HAVE_LVGL 0
#endif

namespace {

static lv_obj_t *s_storage_scr = nullptr;
static ui_app_bar_t s_app_bar = {};

#if HAVE_LVGL
static void show_storage_toast(const char *msg)
{
    lv_obj_t *top = lv_layer_top();
    lv_obj_t *toast = lv_label_create(top);
    lv_label_set_text(toast, msg);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_pad_hor(toast, 16, 0);
    lv_obj_set_style_pad_ver(toast, 8, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x181825), 0);
    lv_obj_set_style_text_color(toast, lv_color_white(), 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(0x89B4FA), 0);

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

static void format_size(uint64_t bytes, char *buf, size_t len)
{
    if (bytes < 1024ULL) {
        snprintf(buf, len, "%llu B", (unsigned long long)bytes);
    } else if (bytes < 1024ULL * 1024ULL) {
        snprintf(buf, len, "%.1f KB", (double)bytes / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        snprintf(buf, len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, len, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

static void create_stat_bar(lv_obj_t *parent, const char *title, uint64_t used, uint64_t total, int32_t val_pct,
                            lv_color_t bar_color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), 85);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_bg_color(card, ui_theme_is_dark() ? lv_color_hex(0x222222) : lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *header_row = lv_obj_create(card);
    lv_obj_set_size(header_row, LV_PCT(100), 24);
    lv_obj_set_style_bg_opa(header_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_row, 0, 0);
    lv_obj_set_style_pad_all(header_row, 0, 0);
    lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_title = lv_label_create(header_row);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, ui_theme_is_dark() ? lv_color_hex(0x00E5FF) : lv_color_hex(0x007ACC), 0);

    char used_str[32], total_str[32], val_str[128];
    format_size(used, used_str, sizeof(used_str));
    format_size(total, total_str, sizeof(total_str));
    snprintf(val_str, sizeof(val_str), "%s / %s (%d%%)", used_str, total_str, (int)val_pct);

    lv_obj_t *lbl_val = lv_label_create(header_row);
    lv_label_set_text(lbl_val, val_str);
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0x888888), 0);

    lv_obj_t *bar = lv_bar_create(card);
    lv_obj_set_size(bar, LV_PCT(100), 12);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, val_pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, bar_color, LV_PART_INDICATOR);
}

static void build_storage_view_content(lv_obj_t *parent)
{
    lv_obj_t *tabview = lv_tabview_create(parent);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));

    lv_obj_t *tab_disks = lv_tabview_add_tab(tabview, "Memoria & Discos");
    lv_obj_t *tab_apps = lv_tabview_add_tab(tabview, "Apps Instaladas");
    lv_obj_t *tab_pending = lv_tabview_add_tab(tabview, "Pacotes (.tab5pkg)");

    // 1. Tab Discos
    lv_obj_set_flex_flow(tab_disks, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab_disks, 15, 0);

    tab5_storage_stats_t flash_stats = {}, sd_stats = {};
    tab5_storage_mgr_get_flash_apps_stats(&flash_stats);
    tab5_storage_mgr_get_sd_stats(&sd_stats);

    tab5_ram_stats_t ram_stats = {};
    tab5_storage_mgr_get_ram_stats(&ram_stats);

    create_stat_bar(tab_disks, "Particao de Apps Embutidas (Flash 4MB)", flash_stats.used_bytes,
                    flash_stats.total_bytes, (int32_t)flash_stats.usage_percent, lv_color_hex(0x3B82F6));
    create_stat_bar(tab_disks, "Cartao SD (/sdcard)", sd_stats.used_bytes, sd_stats.total_bytes,
                    (int32_t)sd_stats.usage_percent, lv_color_hex(0x10B981));
    create_stat_bar(tab_disks, "Memoria RAM & PSRAM Livre", ram_stats.total_free_bytes, 32ULL * 1024ULL * 1024ULL,
                    (int32_t)(ram_stats.total_free_bytes * 100 / (32 * 1024 * 1024)), lv_color_hex(0xF59E0B));

    // 2. Tab Apps Instaladas
    lv_obj_set_flex_flow(tab_apps, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab_apps, 15, 0);

    auto installed_list = tab5_storage_mgr_list_installed_apps();
    if (installed_list.empty()) {
        lv_obj_t *lbl_empty = lv_label_create(tab_apps);
        lv_label_set_text(lbl_empty, "Nenhuma app instalada.");
        lv_obj_set_style_text_color(lbl_empty, lv_color_hex(0x888888), 0);
    } else {
        for (const auto &app : installed_list) {
            lv_obj_t *item = lv_obj_create(tab_apps);
            lv_obj_set_size(item, LV_PCT(100), 65);
            lv_obj_set_style_radius(item, 8, 0);
            lv_obj_set_style_pad_all(item, 8, 0);
            lv_obj_set_style_bg_color(item, ui_theme_is_dark() ? lv_color_hex(0x1E1E1E) : lv_color_white(), 0);
            lv_obj_set_style_border_color(item, ui_theme_is_dark() ? lv_color_hex(0x333333) : lv_color_hex(0xE0E0E0),
                                          0);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *info_col = lv_obj_create(item);
            lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(info_col, 0, 0);
            lv_obj_set_style_pad_all(info_col, 0, 0);
            lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);

            lv_obj_t *name_lbl = lv_label_create(info_col);
            lv_label_set_text_fmt(name_lbl, "%s (v%s)", app.name, app.version);
            lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18_latin1, 0);

            char bin_sz[32], data_sz[32];
            format_size(app.binary_size_bytes, bin_sz, sizeof(bin_sz));
            format_size(app.data_size_bytes, data_sz, sizeof(data_sz));

            lv_obj_t *sub_lbl = lv_label_create(info_col);
            lv_label_set_text_fmt(sub_lbl, "%s  •  Bin: %s  •  Dados: %s",
                                  app.is_embedded ? "[ROM Embutida]" : "[SD Card]", bin_sz, data_sz);
            lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x888888), 0);

            if (!app.is_embedded) {
                lv_obj_t *btn_del = lv_button_create(item);
                lv_obj_set_size(btn_del, 110, 36);
                lv_obj_set_style_bg_color(btn_del, lv_color_hex(0xDC2626), 0);
                std::string *app_id_ptr = new std::string(app.id);
                lv_obj_add_event_cb(
                    btn_del,
                    [](lv_event_t *e) {
                        std::string *id = (std::string *)lv_event_get_user_data(e);
                        if (id != nullptr) {
                            tab5_package_mgr_uninstall(id->c_str(), true);
                            show_storage_toast("App desinstalada com sucesso.");
                            delete id;
                            ui_storage_view_open(); // Recarrega tela
                        }
                    },
                    LV_EVENT_CLICKED, app_id_ptr);

                lv_obj_t *lbl_d = lv_label_create(btn_del);
                lv_label_set_text(lbl_d, "Desinstalar");
                lv_obj_center(lbl_d);
            }
        }
    }

    // 3. Tab Pacotes Pendentes
    lv_obj_set_flex_flow(tab_pending, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab_pending, 15, 0);

    auto pending_list = tab5_storage_mgr_list_pending_packages();
    if (pending_list.empty()) {
        lv_obj_t *lbl_empty = lv_label_create(tab_pending);
        lv_label_set_text(lbl_empty, "Nenhum pacote .tab5pkg pendente em /sdcard/apps/.");
        lv_obj_set_style_text_color(lbl_empty, lv_color_hex(0x888888), 0);
    } else {
        for (const auto &pkg : pending_list) {
            lv_obj_t *item = lv_obj_create(tab_pending);
            lv_obj_set_size(item, LV_PCT(100), 65);
            lv_obj_set_style_radius(item, 8, 0);
            lv_obj_set_style_pad_all(item, 8, 0);
            lv_obj_set_style_bg_color(item, ui_theme_is_dark() ? lv_color_hex(0x1E1E1E) : lv_color_white(), 0);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *info_col = lv_obj_create(item);
            lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(info_col, 0, 0);
            lv_obj_set_style_pad_all(info_col, 0, 0);
            lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);

            lv_obj_t *name_lbl = lv_label_create(info_col);
            lv_label_set_text_fmt(name_lbl, "%s (v%s)", pkg.name, pkg.version);
            lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18_latin1, 0);

            char sz_str[32];
            format_size(pkg.package_size_bytes, sz_str, sizeof(sz_str));

            lv_obj_t *sub_lbl = lv_label_create(info_col);
            lv_label_set_text_fmt(sub_lbl, "Arquivo: %s  •  Tamanho: %s", pkg.filename, sz_str);
            lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x888888), 0);

            lv_obj_t *btn_inst = lv_button_create(item);
            lv_obj_set_size(btn_inst, 100, 36);
            lv_obj_set_style_bg_color(btn_inst, lv_color_hex(0x00A86B), 0);
            std::string *pkg_path_ptr = new std::string(pkg.full_path);
            lv_obj_add_event_cb(
                btn_inst,
                [](lv_event_t *e) {
                    std::string *p = (std::string *)lv_event_get_user_data(e);
                    if (p != nullptr) {
                        ui_installer_open(p->c_str());
                        delete p;
                    }
                },
                LV_EVENT_CLICKED, pkg_path_ptr);

            lv_obj_t *lbl_i = lv_label_create(btn_inst);
            lv_label_set_text(lbl_i, "Instalar");
            lv_obj_center(lbl_i);
        }
    }
}
#endif

} // namespace

void ui_storage_view_init(void)
{
    static app_desc_t s_storage_desc = {.id = "storage",
                                        .name = "Armazenamento",
                                        .icon_symbol = LV_SYMBOL_DRIVE,
                                        .icon_builder = nullptr,
                                        .icon_theme_refresh = nullptr,
                                        .on_launch = ui_storage_view_open,
                                        .file_extensions = nullptr,
                                        .on_open_file = nullptr};
    app_registry_register(&s_storage_desc);
}

#if HAVE_LVGL
static void storage_close_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ui_storage_view_close();
}
#endif

void ui_storage_view_open(void)
{
#if HAVE_LVGL
    if (s_storage_scr == nullptr) {
        s_storage_scr = lv_obj_create(nullptr);
        lv_obj_set_size(s_storage_scr, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(s_storage_scr, 0, 0);
        lv_obj_set_style_bg_color(s_storage_scr, ui_theme_is_dark() ? lv_color_hex(0x121212) : lv_color_hex(0xFAFAFA),
                                  0);
        lv_obj_set_flex_flow(s_storage_scr, LV_FLEX_FLOW_COLUMN);

        s_app_bar = ui_app_bar_create(s_storage_scr, "Armazenamento", storage_close_btn_event_cb, nullptr);

        lv_obj_t *content_area = lv_obj_create(s_storage_scr);
        lv_obj_set_size(content_area, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(content_area, 0, 0);
        lv_obj_set_style_border_width(content_area, 0, 0);
        lv_obj_set_style_bg_opa(content_area, LV_OPA_TRANSP, 0);

        build_storage_view_content(content_area);
    }

    lv_screen_load(s_storage_scr);
#endif
}

void ui_storage_view_close(void)
{
#if HAVE_LVGL
    if (s_storage_scr != nullptr) {
        lv_obj_delete(s_storage_scr);
        s_storage_scr = nullptr;
    }
#endif
}
