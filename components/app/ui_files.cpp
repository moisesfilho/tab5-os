#include "ui_files.h"
#include "ui_bar.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "wifi_storage.h"
#include "file_assoc.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include <string>
#include <algorithm>

namespace {

enum class ViewMode : uint8_t {
    ICONS,
    LIST,
};

struct FileEntry {
    std::string name;
    bool is_dir;
    size_t size;
    time_t mtime;
};

lv_obj_t *files_scr = nullptr;
lv_obj_t *files_bar = nullptr;
lv_obj_t *files_back_btn = nullptr;
lv_obj_t *files_back_label = nullptr;
lv_obj_t *files_path_label = nullptr;
lv_obj_t *files_view_btn = nullptr;
lv_obj_t *files_view_label = nullptr;
lv_obj_t *files_close_btn = nullptr;
lv_obj_t *files_close_label = nullptr;

lv_obj_t *files_container = nullptr;
lv_obj_t *empty_label = nullptr;

std::string current_path = "/sdcard";
ViewMode current_view_mode = ViewMode::ICONS;
std::vector<FileEntry> entries;

void render_content(void);
void load_directory(const std::string &path);

void format_file_size(size_t size, bool is_dir, char *buf, size_t buf_len)
{
    if (is_dir) {
        std::snprintf(buf, buf_len, "<DIR>");
        return;
    }
    if (size < 1024) {
        std::snprintf(buf, buf_len, "%u B", (unsigned int)size);
    } else if (size < 1024 * 1024) {
        std::snprintf(buf, buf_len, "%.1f KB", (float)size / 1024.0F);
    } else {
        std::snprintf(buf, buf_len, "%.1f MB", (float)size / (1024.0F * 1024.0F));
    }
}

void format_file_date(time_t mtime, char *buf, size_t buf_len)
{
    if (mtime == 0) {
        std::snprintf(buf, buf_len, "--/--/---- --:--");
        return;
    }
    struct tm timeinfo;
    localtime_r(&mtime, &timeinfo);
    std::strftime(buf, buf_len, "%d/%m/%Y %H:%M", &timeinfo);
}

void back_click_cb(lv_event_t *event)
{
    (void)event;
    if (current_path == "/sdcard" || current_path == "/sdcard/") {
        return;
    }

    size_t last_slash = current_path.find_last_of('/');
    if (last_slash != std::string::npos && last_slash > 0) {
        std::string parent = current_path.substr(0, last_slash);
        if (parent.length() < 7) { /* menor que /sdcard */
            parent = "/sdcard";
        }
        load_directory(parent);
    } else {
        load_directory("/sdcard");
    }
}

void toggle_view_click_cb(lv_event_t *event)
{
    (void)event;
    if (current_view_mode == ViewMode::ICONS) {
        current_view_mode = ViewMode::LIST;
        if (files_view_label != nullptr) {
            lv_label_set_text(files_view_label, LV_SYMBOL_IMAGE);
        }
    } else {
        current_view_mode = ViewMode::ICONS;
        if (files_view_label != nullptr) {
            lv_label_set_text(files_view_label, LV_SYMBOL_LIST);
        }
    }
    render_content();
}

void close_click_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_close_files();
}

void item_click_cb(lv_event_t *event)
{
    const char *name = static_cast<const char *>(lv_event_get_user_data(event));
    if (name == nullptr) {
        return;
    }

    std::string next_path = current_path;
    if (next_path.back() != '/') {
        next_path += "/";
    }
    next_path += name;

    struct stat st;
    if (stat(next_path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            load_directory(next_path);
        } else {
            /* Tenta abrir o arquivo com o app associado a sua extensao */
            file_assoc_open(next_path.c_str());
        }
    }
}

void load_directory(const std::string &path)
{
    current_path = path;
    entries.clear();

    if (wifi_storage_mount() == ESP_OK) {
        DIR *dir = opendir(current_path.c_str());
        if (dir != nullptr) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                std::string full_path = current_path;
                if (full_path.back() != '/') {
                    full_path += "/";
                }
                full_path += entry->d_name;

                struct stat st = {};
                stat(full_path.c_str(), &st);

                FileEntry fe;
                fe.name = entry->d_name;
                fe.is_dir = S_ISDIR(st.st_mode);
                fe.size = st.st_size;
                fe.mtime = st.st_mtime;
                entries.push_back(fe);
            }
            closedir(dir);
        }
    }

    /* Ordena: pastas primeiro, depois arquivos alfabeticamente */
    std::sort(entries.begin(), entries.end(), [](const FileEntry &a, const FileEntry &b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir > b.is_dir;
        }
        return a.name < b.name;
    });

    if (files_path_label != nullptr) {
        lv_label_set_text(files_path_label, current_path.c_str());
    }

    render_content();
}

void apply_files_theme(void)
{
    if (files_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(files_scr, lv_color_hex(pal->background), 0);

    if (files_bar != nullptr) {
        lv_obj_set_style_bg_color(files_bar, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_border_color(files_bar, lv_color_hex(pal->border), 0);
    }
    if (files_back_btn != nullptr) {
        lv_obj_set_style_bg_opa(files_back_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(files_back_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(files_back_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (files_back_label != nullptr) {
        lv_obj_set_style_text_color(files_back_label, lv_color_hex(pal->text), 0);
    }
    if (files_path_label != nullptr) {
        lv_obj_set_style_text_color(files_path_label, lv_color_hex(pal->text), 0);
    }
    if (files_view_btn != nullptr) {
        lv_obj_set_style_bg_opa(files_view_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(files_view_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(files_view_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (files_view_label != nullptr) {
        lv_obj_set_style_text_color(files_view_label, lv_color_hex(pal->text), 0);
    }
    if (files_close_btn != nullptr) {
        lv_obj_set_style_bg_opa(files_close_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(files_close_btn, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(files_close_btn, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    }
    if (files_close_label != nullptr) {
        lv_obj_set_style_text_color(files_close_label, lv_color_hex(pal->text), 0);
    }
    if (files_container != nullptr) {
        lv_obj_set_style_bg_color(files_container, lv_color_hex(pal->background), 0);
    }
}

void apply_files_layout(void)
{
    if (files_scr == nullptr || files_container == nullptr) {
        return;
    }

    int32_t width = lv_display_get_horizontal_resolution(nullptr);
    int32_t height = lv_display_get_vertical_resolution(nullptr);

    int32_t top = 2 * UI_BAR_HEIGHT;
    int32_t container_h = std::max<int32_t>(height - top, 100);

    lv_obj_set_size(files_container, width, container_h);
    lv_obj_set_pos(files_container, 0, top);

    if (files_bar != nullptr) {
        lv_obj_set_width(files_bar, width);
    }
}

void resolution_cb(lv_event_t *event)
{
    (void)event;
    apply_files_layout();
    render_content();
}

void render_content(void)
{
    if (files_container == nullptr) {
        return;
    }

    lv_obj_clean(files_container);
    const ui_palette_t *pal = ui_theme_get();

    if (entries.empty()) {
        empty_label = lv_label_create(files_container);
        lv_label_set_text(empty_label, "Nenhum arquivo ou pasta encontrado");
        lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_14_latin1, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(pal->text_muted), 0);
        lv_obj_center(empty_label);
        return;
    }

    if (current_view_mode == ViewMode::ICONS) {
        /* Modo Grade de Icones */
        lv_obj_set_flex_flow(files_container, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(files_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(files_container, 16, 0);
        lv_obj_set_style_pad_row(files_container, 16, 0);
        lv_obj_set_style_pad_column(files_container, 16, 0);

        for (const auto &entry : entries) {
            lv_obj_t *tile = lv_obj_create(files_container);
            lv_obj_set_size(tile, 96, 100);
            lv_obj_set_style_bg_color(tile, lv_color_hex(pal->surface), 0);
            lv_obj_set_style_bg_color(tile, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(tile, 1, 0);
            lv_obj_set_style_border_color(tile, lv_color_hex(pal->border), 0);
            lv_obj_set_style_radius(tile, 10, 0);
            lv_obj_set_style_pad_all(tile, 4, 0);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_add_event_cb(tile, item_click_cb, LV_EVENT_CLICKED, (void *)entry.name.c_str());

            lv_obj_t *icon_label = lv_label_create(tile);
            lv_label_set_text(icon_label, entry.is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
            lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_28_latin1, 0);
            lv_obj_set_style_text_color(icon_label, lv_color_hex(entry.is_dir ? pal->accent : pal->text_muted), 0);

            lv_obj_t *name_label = lv_label_create(tile);
            lv_label_set_text(name_label, entry.name.c_str());
            lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(name_label, 88);
            lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(name_label, lv_color_hex(pal->text), 0);
        }
    } else {
        /* Modo Lista Detalhada */
        lv_obj_set_flex_flow(files_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(files_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(files_container, 8, 0);
        lv_obj_set_style_pad_row(files_container, 4, 0);

        int32_t width = lv_display_get_horizontal_resolution(nullptr);
        int32_t item_w = width - 24;

        for (const auto &entry : entries) {
            lv_obj_t *item = lv_obj_create(files_container);
            lv_obj_set_size(item, item_w, 42);
            lv_obj_set_style_bg_color(item, lv_color_hex(pal->surface), 0);
            lv_obj_set_style_bg_color(item, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(item, 1, 0);
            lv_obj_set_style_border_color(item, lv_color_hex(pal->border), 0);
            lv_obj_set_style_radius(item, 6, 0);
            lv_obj_set_style_pad_left(item, 12, 0);
            lv_obj_set_style_pad_right(item, 12, 0);
            lv_obj_set_style_pad_top(item, 0, 0);
            lv_obj_set_style_pad_bottom(item, 0, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_add_event_cb(item, item_click_cb, LV_EVENT_CLICKED, (void *)entry.name.c_str());

            /* Linha flex com colunas */
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *icon = lv_label_create(item);
            lv_label_set_text(icon, entry.is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(icon, lv_color_hex(entry.is_dir ? pal->accent : pal->text_muted), 0);
            lv_obj_set_style_margin_right(icon, 8, 0);

            lv_obj_t *name = lv_label_create(item);
            lv_label_set_text(name, entry.name.c_str());
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_obj_set_flex_grow(name, 1);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(name, lv_color_hex(pal->text), 0);

            char size_buf[32];
            format_file_size(entry.size, entry.is_dir, size_buf, sizeof(size_buf));
            lv_obj_t *size_lbl = lv_label_create(item);
            lv_label_set_text(size_lbl, size_buf);
            lv_obj_set_width(size_lbl, 90);
            lv_obj_set_style_text_align(size_lbl, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_font(size_lbl, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(size_lbl, lv_color_hex(pal->text_muted), 0);
            lv_obj_set_style_margin_right(size_lbl, 16, 0);

            char date_buf[32];
            format_file_date(entry.mtime, date_buf, sizeof(date_buf));
            lv_obj_t *date_lbl = lv_label_create(item);
            lv_label_set_text(date_lbl, date_buf);
            lv_obj_set_width(date_lbl, 140);
            lv_obj_set_style_text_align(date_lbl, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_font(date_lbl, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(date_lbl, lv_color_hex(pal->text_muted), 0);
        }
    }
}

} // namespace

lv_obj_t *ui_files_create(void)
{
    files_scr = lv_obj_create(nullptr);

    files_bar = lv_obj_create(files_scr);
    lv_obj_set_size(files_bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_align(files_bar, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(files_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(files_bar, 1, 0);
    lv_obj_set_style_border_side(files_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(files_bar, 0, 0);
    lv_obj_set_style_shadow_width(files_bar, 0, 0);
    lv_obj_set_style_pad_all(files_bar, 0, 0);
    lv_obj_clear_flag(files_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(files_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(files_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Botao Voltar */
    files_back_btn = lv_obj_create(files_bar);
    lv_obj_set_size(files_back_btn, 36, 36);
    lv_obj_set_style_radius(files_back_btn, 8, 0);
    lv_obj_set_style_border_width(files_back_btn, 1, 0);
    lv_obj_set_style_margin_left(files_back_btn, 6, 0);
    lv_obj_set_style_margin_right(files_back_btn, 6, 0);
    lv_obj_clear_flag(files_back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(files_back_btn, back_click_cb, LV_EVENT_CLICKED, nullptr);
    files_back_label = lv_label_create(files_back_btn);
    lv_label_set_text(files_back_label, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(files_back_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(files_back_label);

    /* Caminho Atual (flex grow) */
    files_path_label = lv_label_create(files_bar);
    lv_label_set_text(files_path_label, current_path.c_str());
    lv_label_set_long_mode(files_path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(files_path_label, 1);
    lv_obj_set_style_text_font(files_path_label, &lv_font_montserrat_14_latin1, 0);

    /* Botao Alternar Modo de Exibicao */
    files_view_btn = lv_obj_create(files_bar);
    lv_obj_set_size(files_view_btn, 36, 36);
    lv_obj_set_style_radius(files_view_btn, 8, 0);
    lv_obj_set_style_border_width(files_view_btn, 1, 0);
    lv_obj_set_style_margin_right(files_view_btn, 6, 0);
    lv_obj_clear_flag(files_view_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(files_view_btn, toggle_view_click_cb, LV_EVENT_CLICKED, nullptr);
    files_view_label = lv_label_create(files_view_btn);
    lv_label_set_text(files_view_label, LV_SYMBOL_LIST);
    lv_obj_set_style_text_font(files_view_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(files_view_label);

    /* Botao Fechar */
    files_close_btn = lv_obj_create(files_bar);
    lv_obj_set_size(files_close_btn, 36, 36);
    lv_obj_set_style_radius(files_close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(files_close_btn, 1, 0);
    lv_obj_set_style_margin_right(files_close_btn, 6, 0);
    lv_obj_clear_flag(files_close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(files_close_btn, close_click_cb, LV_EVENT_CLICKED, nullptr);
    files_close_label = lv_label_create(files_close_btn);
    lv_label_set_text(files_close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(files_close_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(files_close_label);

    /* Container de conteudo (pastas e arquivos) */
    files_container = lv_obj_create(files_scr);
    lv_obj_set_style_border_width(files_container, 0, 0);
    lv_obj_set_style_radius(files_container, 0, 0);
    lv_obj_set_scroll_dir(files_container, LV_DIR_VER);

    apply_files_layout();
    lv_display_add_event_cb(lv_display_get_default(), resolution_cb, LV_EVENT_RESOLUTION_CHANGED, nullptr);
    apply_files_theme();

    load_directory("/sdcard");
    return files_scr;
}

void ui_files_refresh_theme(void)
{
    apply_files_theme();
    render_content();
}

void ui_files_apply_layout(void)
{
    apply_files_layout();
}

void ui_files_open_path(const char *path)
{
    load_directory(path != nullptr ? path : "/sdcard");
}
