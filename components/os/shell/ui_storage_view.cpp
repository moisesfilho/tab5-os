/**
 * @file ui_storage_view.cpp
 * @brief Implementação da Tela de Gerenciamento de Armazenamento e Memória
 */

#include "ui_storage_view.h"
#include "storage_mgr.h"
#include "tab5_package_mgr.h"
#include "ui_installer.h"
#include "ui_app_bar.h"
#include "ui_bar.h"
#include "ui_theme.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "app_registry.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

#if LVGL_VERSION_MAJOR >= 9
#define HAVE_LVGL 1
#else
#define HAVE_LVGL 0
#endif

namespace {

static lv_obj_t *s_storage_scr = nullptr;
static ui_app_bar_t s_app_bar = {};
static lv_obj_t *s_tabview = nullptr;
static lv_obj_t *s_tab_disks = nullptr;
static lv_obj_t *s_tab_apps = nullptr;
static lv_obj_t *s_tab_pending = nullptr;

#if HAVE_LVGL
static void show_storage_toast(const char *msg)
{
    lv_obj_t *top = lv_layer_top();
    lv_obj_t *toast = lv_label_create(top);
    lv_label_set_text(toast, msg);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT * 2 + 10);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_pad_hor(toast, 16, 0);
    lv_obj_set_style_pad_ver(toast, 8, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(toast, lv_color_hex(ui_theme_get()->surface_alt), 0);
    lv_obj_set_style_text_color(toast, lv_color_hex(ui_theme_get()->text), 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(ui_theme_get()->accent), 0);

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
    const ui_palette_t *pal = ui_theme_get();

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, lv_pct(100), 82);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *header_row = lv_obj_create(card);
    lv_obj_set_size(header_row, lv_pct(100), 24);
    lv_obj_set_style_bg_opa(header_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_row, 0, 0);
    lv_obj_set_style_pad_all(header_row, 0, 0);
    lv_obj_clear_flag(header_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_title = lv_label_create(header_row);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18_latin1, 0);

    char used_str[32], total_str[32], val_str[128];
    format_size(used, used_str, sizeof(used_str));
    format_size(total, total_str, sizeof(total_str));
    snprintf(val_str, sizeof(val_str), "%s / %s (%d%%)", used_str, total_str, (int)val_pct);

    lv_obj_t *lbl_val = lv_label_create(header_row);
    lv_label_set_text(lbl_val, val_str);
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(pal->text_muted), 0);

    lv_obj_t *bar = lv_bar_create(card);
    lv_obj_set_size(bar, lv_pct(100), 12);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, val_pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(pal->surface_alt), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, bar_color, LV_PART_INDICATOR);
}

static void render_disks_tab(void)
{
    lv_obj_clean(s_tab_disks);

    tab5_storage_stats_t flash_stats = {}, sd_stats = {};
    tab5_storage_mgr_get_flash_apps_stats(&flash_stats);
    tab5_storage_mgr_get_sd_stats(&sd_stats);

    tab5_ram_stats_t ram_stats = {};
    tab5_storage_mgr_get_ram_stats(&ram_stats);

    create_stat_bar(s_tab_disks, "Particao de Apps Embutidas (Flash 4MB)", flash_stats.used_bytes,
                    flash_stats.total_bytes, (int32_t)flash_stats.usage_percent, lv_color_hex(0x3B82F6));
    create_stat_bar(s_tab_disks, "Cartao SD (/sdcard)", sd_stats.used_bytes, sd_stats.total_bytes,
                    (int32_t)sd_stats.usage_percent, lv_color_hex(0x10B981));
    create_stat_bar(s_tab_disks, "Memoria RAM & PSRAM Livre", ram_stats.total_free_bytes, 32ULL * 1024ULL * 1024ULL,
                    (int32_t)(ram_stats.total_free_bytes * 100 / (32 * 1024 * 1024)), lv_color_hex(0xF59E0B));
}

static void render_apps_tab(void)
{
    lv_obj_clean(s_tab_apps);
    const ui_palette_t *pal = ui_theme_get();

    auto installed_list = tab5_storage_mgr_list_installed_apps();
    if (installed_list.empty()) {
        lv_obj_t *lbl_empty = lv_label_create(s_tab_apps);
        lv_label_set_text(lbl_empty, "Nenhuma aplicacao instalada.");
        lv_obj_set_style_text_color(lbl_empty, lv_color_hex(pal->text_muted), 0);
        return;
    }

    for (const auto &app : installed_list) {
        lv_obj_t *item = lv_obj_create(s_tab_apps);
        lv_obj_set_size(item, lv_pct(100), 65);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(item, lv_color_hex(pal->border), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *info_col = lv_obj_create(item);
        lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(info_col, 0, 0);
        lv_obj_set_style_pad_all(info_col, 0, 0);
        lv_obj_clear_flag(info_col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *name_lbl = lv_label_create(info_col);
        lv_label_set_text_fmt(name_lbl, "%s (v%s)", app.name, app.version);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(pal->text), 0);

        char bin_sz[32], data_sz[32];
        format_size(app.binary_size_bytes, bin_sz, sizeof(bin_sz));
        format_size(app.data_size_bytes, data_sz, sizeof(data_sz));

        lv_obj_t *sub_lbl = lv_label_create(info_col);
        lv_label_set_text_fmt(sub_lbl, "%s  •  Bin: %s  •  Dados: %s", app.is_embedded ? "[ROM Embutida]" : "[SD Card]",
                              bin_sz, data_sz);
        lv_obj_set_style_text_color(sub_lbl, lv_color_hex(pal->text_muted), 0);

        if (!app.is_embedded) {
            lv_obj_t *btn_del = lv_button_create(item);
            lv_obj_set_size(btn_del, 110, 36);
            lv_obj_set_style_bg_color(btn_del, lv_color_hex(0xDC2626), 0);
            lv_obj_set_style_radius(btn_del, 6, 0);
            lv_obj_clear_flag(btn_del, LV_OBJ_FLAG_SCROLLABLE);

            std::string *app_id_ptr = new std::string(app.id);
            lv_obj_add_event_cb(
                btn_del,
                [](lv_event_t *e) {
                    std::string *id = (std::string *)lv_event_get_user_data(e);
                    if (id != nullptr) {
                        tab5_package_mgr_uninstall(id->c_str(), true);
                        show_storage_toast("App desinstalada com sucesso.");
                        delete id;
                        render_apps_tab();
                        render_disks_tab();
                    }
                },
                LV_EVENT_CLICKED, app_id_ptr);

            lv_obj_t *lbl_d = lv_label_create(btn_del);
            lv_label_set_text(lbl_d, "Desinstalar");
            lv_obj_set_style_text_color(lbl_d, lv_color_white(), 0);
            lv_obj_center(lbl_d);
        }
    }
}

static void render_pending_tab(void)
{
    lv_obj_clean(s_tab_pending);
    const ui_palette_t *pal = ui_theme_get();

    auto pending_list = tab5_storage_mgr_list_pending_packages();
    if (pending_list.empty()) {
        lv_obj_t *lbl_empty = lv_label_create(s_tab_pending);
        lv_label_set_text(lbl_empty, "Nenhum pacote .tab5pkg pendente em /sdcard/apps/.");
        lv_obj_set_style_text_color(lbl_empty, lv_color_hex(pal->text_muted), 0);
        return;
    }

    for (const auto &pkg : pending_list) {
        lv_obj_t *item = lv_obj_create(s_tab_pending);
        lv_obj_set_size(item, lv_pct(100), 65);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(item, lv_color_hex(pal->border), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *info_col = lv_obj_create(item);
        lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(info_col, 0, 0);
        lv_obj_set_style_pad_all(info_col, 0, 0);
        lv_obj_clear_flag(info_col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *name_lbl = lv_label_create(info_col);
        lv_label_set_text_fmt(name_lbl, "%s (v%s)", pkg.name, pkg.version);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(pal->text), 0);

        char sz_str[32];
        format_size(pkg.package_size_bytes, sz_str, sizeof(sz_str));

        lv_obj_t *sub_lbl = lv_label_create(info_col);
        lv_label_set_text_fmt(sub_lbl, "Arquivo: %s  •  Tamanho: %s", pkg.filename, sz_str);
        lv_obj_set_style_text_color(sub_lbl, lv_color_hex(pal->text_muted), 0);

        lv_obj_t *btn_inst = lv_button_create(item);
        lv_obj_set_size(btn_inst, 100, 36);
        lv_obj_set_style_bg_color(btn_inst, lv_color_hex(0x00A86B), 0);
        lv_obj_set_style_radius(btn_inst, 6, 0);
        lv_obj_clear_flag(btn_inst, LV_OBJ_FLAG_SCROLLABLE);

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
        lv_obj_set_style_text_color(lbl_i, lv_color_white(), 0);
        lv_obj_center(lbl_i);
    }
}

static void render_all_tabs(void)
{
    render_disks_tab();
    render_apps_tab();
    render_pending_tab();
}

static void apply_storage_theme(void)
{
    if (s_storage_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(s_storage_scr, lv_color_hex(pal->background), 0);
    ui_app_bar_refresh_theme(&s_app_bar);

    if (s_tabview != nullptr) {
        lv_obj_t *tab_bar = lv_tabview_get_tab_bar(s_tabview);
        if (tab_bar != nullptr) {
            lv_obj_set_style_bg_color(tab_bar, lv_color_hex(pal->surface_alt), 0);
            lv_obj_set_style_border_color(tab_bar, lv_color_hex(pal->border), 0);
            lv_obj_set_style_border_width(tab_bar, 1, 0);
            lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_text_color(tab_bar, lv_color_hex(pal->text), 0);
        }

        if (s_tab_disks != nullptr) {
            lv_obj_set_style_bg_color(s_tab_disks, lv_color_hex(pal->background), 0);
        }
        if (s_tab_apps != nullptr) {
            lv_obj_set_style_bg_color(s_tab_apps, lv_color_hex(pal->background), 0);
        }
        if (s_tab_pending != nullptr) {
            lv_obj_set_style_bg_color(s_tab_pending, lv_color_hex(pal->background), 0);
        }
    }

    render_all_tabs();
}

static void apply_storage_layout_internal(void)
{
    if (s_storage_scr == nullptr || s_tabview == nullptr) {
        return;
    }

    int32_t width = lv_display_get_horizontal_resolution(nullptr);
    int32_t height = lv_display_get_vertical_resolution(nullptr);
    int32_t top = 2 * UI_BAR_HEIGHT;
    int32_t container_h = std::max<int32_t>(height - top, 100);

    lv_obj_set_size(s_tabview, width, container_h);
    lv_obj_set_pos(s_tabview, 0, top);

    if (s_app_bar.bar != nullptr) {
        lv_obj_set_width(s_app_bar.bar, width);
    }
}

static void close_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_close_storage();
}
#endif

} // namespace

void ui_storage_view_register(void)
{
    static const app_desc_t s_storage_desc = {
        .id = "storage",
        .name = "Armazenamento",
        .icon_symbol = LV_SYMBOL_DRIVE,
        .icon_builder = nullptr,
        .icon_theme_refresh = nullptr,
        .on_launch = ui_shell_open_storage,
        .file_extensions = nullptr,
        .on_open_file = nullptr,
    };
    app_registry_register(&s_storage_desc);
}

lv_obj_t *ui_storage_view_create(void)
{
#if HAVE_LVGL
    s_storage_scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(s_storage_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_storage_scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(s_storage_scr, 0, 0);
    lv_obj_set_style_border_width(s_storage_scr, 0, 0);

    s_app_bar = ui_app_bar_create(s_storage_scr, "Armazenamento", close_cb, nullptr);

    s_tabview = lv_tabview_create(s_storage_scr);
    lv_obj_clear_flag(s_tabview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_tabview, 0, 0);

    s_tab_disks = lv_tabview_add_tab(s_tabview, "Memoria & Discos");
    s_tab_apps = lv_tabview_add_tab(s_tabview, "Apps Instaladas");
    s_tab_pending = lv_tabview_add_tab(s_tabview, "Pacotes (.tab5pkg)");

    lv_obj_set_scroll_dir(s_tab_disks, LV_DIR_VER);
    lv_obj_set_scroll_dir(s_tab_apps, LV_DIR_VER);
    lv_obj_set_scroll_dir(s_tab_pending, LV_DIR_VER);

    lv_obj_set_flex_flow(s_tab_disks, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(s_tab_apps, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(s_tab_pending, LV_FLEX_FLOW_COLUMN);

    lv_obj_set_style_pad_all(s_tab_disks, 12, 0);
    lv_obj_set_style_pad_all(s_tab_apps, 12, 0);
    lv_obj_set_style_pad_all(s_tab_pending, 12, 0);

    lv_obj_set_style_pad_row(s_tab_disks, 8, 0);
    lv_obj_set_style_pad_row(s_tab_apps, 8, 0);
    lv_obj_set_style_pad_row(s_tab_pending, 8, 0);

    apply_storage_layout_internal();
    apply_storage_theme();

    return s_storage_scr;
#else
    return nullptr;
#endif
}

void ui_storage_view_on_open(void)
{
#if HAVE_LVGL
    if (s_storage_scr == nullptr) {
        ui_storage_view_create();
    }
    apply_storage_theme();
#endif
}

void ui_storage_view_refresh_theme(void)
{
#if HAVE_LVGL
    apply_storage_theme();
#endif
}

void ui_storage_view_apply_layout(void)
{
#if HAVE_LVGL
    apply_storage_layout_internal();
#endif
}
