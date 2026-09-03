#include "ui_files_view.h"
#include "ui_bar.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "file_assoc.h"
#include "wifi_storage.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
static const char *TAG = "tab5_ui_files";
#else
#include <cstdio>
#endif

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

struct FileEntry {
    std::string name;
    bool is_dir;
    size_t size;
    time_t mtime;
};

struct ui_files_view_s {
    lv_obj_t *parent_screen;
    lv_obj_t *container;
    lv_obj_t *empty_label;
    ui_app_bar_t app_bar;
    lv_obj_t *view_btn;
    lv_obj_t *view_label;
    lv_obj_t *hidden_btn;
    lv_obj_t *hidden_label;
    ui_files_view_mode_t current_view_mode;
    bool show_hidden;
    std::string current_path;
    std::vector<FileEntry> entries;
};

namespace {

static const char *NVS_NS_FILES = "tab5";
static const char *NVS_KEY_HIDDEN = "files_hidden";

static bool load_files_show_hidden(void)
{
    bool show = false;
#ifdef ESP_PLATFORM
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS_FILES, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(nvs, NVS_KEY_HIDDEN, &v) == ESP_OK) {
            show = (v != 0);
        }
        nvs_close(nvs);
    }
#endif
    return show;
}

static void save_files_show_hidden(bool show)
{
#ifdef ESP_PLATFORM
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS_FILES, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_HIDDEN, show ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
#endif
}

static void format_file_size(size_t size, bool is_dir, char *buf, size_t buf_len)
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

static void format_file_date(time_t mtime, char *buf, size_t buf_len)
{
    if (mtime == 0) {
        std::snprintf(buf, buf_len, "--/--/---- --:--");
        return;
    }
    struct tm timeinfo;
    localtime_r(&mtime, &timeinfo);
    std::strftime(buf, buf_len, "%d/%m/%Y %H:%M", &timeinfo);
}

static void render_content(ui_files_view_t *view);
static void load_directory(ui_files_view_t *view, const std::string &path);

struct ItemClickData {
    ui_files_view_t *view;
    std::string name;
};

static void item_click_cb(lv_event_t *event)
{
    auto *data = static_cast<ItemClickData *>(lv_event_get_user_data(event));
    if (data == nullptr || data->view == nullptr) {
        return;
    }

    ui_files_view_t *view = data->view;
    const std::string name = data->name;

    if (name == "..") {
        size_t last_slash = view->current_path.find_last_of('/');
        if (last_slash != std::string::npos && last_slash > 0) {
            std::string parent = view->current_path.substr(0, last_slash);
            if (parent.length() < 7) { /* menor que /sdcard */
                parent = "/sdcard";
            }
            load_directory(view, parent);
        } else {
            load_directory(view, "/sdcard");
        }
        return;
    }

    std::string next_path = view->current_path;
    if (next_path.back() != '/') {
        next_path += "/";
    }
    next_path += name;

    struct stat st;
    if (stat(next_path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            load_directory(view, next_path);
        } else {
            /* Tenta abrir o arquivo com o app associado a sua extensao */
            file_assoc_open(next_path.c_str());
        }
    }
}

static void toggle_view_click_cb(lv_event_t *event)
{
    auto *view = static_cast<ui_files_view_t *>(lv_event_get_user_data(event));
    if (view == nullptr) {
        return;
    }

    if (view->current_view_mode == UI_FILES_VIEW_ICONS) {
        view->current_view_mode = UI_FILES_VIEW_LIST;
        if (view->view_label != nullptr) {
            lv_label_set_text(view->view_label, LV_SYMBOL_IMAGE);
        }
    } else {
        view->current_view_mode = UI_FILES_VIEW_ICONS;
        if (view->view_label != nullptr) {
            lv_label_set_text(view->view_label, LV_SYMBOL_LIST);
        }
    }
    render_content(view);
}

static void toggle_hidden_click_cb(lv_event_t *event)
{
    auto *view = static_cast<ui_files_view_t *>(lv_event_get_user_data(event));
    if (view == nullptr) {
        return;
    }

    view->show_hidden = !view->show_hidden;
    if (view->hidden_label != nullptr) {
        lv_label_set_text(view->hidden_label, view->show_hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    }
    save_files_show_hidden(view->show_hidden);
    load_directory(view, view->current_path);
}

static void load_directory(ui_files_view_t *view, const std::string &path)
{
    if (view == nullptr) {
        return;
    }

    view->current_path = path.empty() ? "/sdcard" : path;
    view->entries.clear();

#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "HEAP_DIAG list_dir \"%s\": internal=%zu dma=%zu dma_largest=%zu", view->current_path.c_str(),
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
#endif

    if (wifi_storage_mount() == ESP_OK) {
        DIR *dir = opendir(view->current_path.c_str());
        if (dir != nullptr) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                /* Ocultos iniciam com '.' e ficam escondidos por padrao (toggle na barra) */
                if (!view->show_hidden && entry->d_name[0] == '.') {
                    continue;
                }
                std::string full_path = view->current_path;
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
                view->entries.push_back(fe);
            }
            closedir(dir);
        }
    }

    /* Ordena: pastas primeiro, depois arquivos alfabeticamente */
    std::sort(view->entries.begin(), view->entries.end(), [](const FileEntry &a, const FileEntry &b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir > b.is_dir;
        }
        return a.name < b.name;
    });

    /* Se estiver em um subdiretorio, insere entrada ".." no inicio para retorno */
    if (view->current_path != "/sdcard" && view->current_path != "/sdcard/") {
        FileEntry parent_fe;
        parent_fe.name = "..";
        parent_fe.is_dir = true;
        parent_fe.size = 0;
        parent_fe.mtime = 0;
        view->entries.insert(view->entries.begin(), parent_fe);
    }

    if (view->app_bar.bar != nullptr) {
        std::string app_title = "Arquivos - " + view->current_path;
        ui_app_bar_set_title(&view->app_bar, app_title.c_str());
    }

    render_content(view);
}

static void render_content(ui_files_view_t *view)
{
    if (view == nullptr || view->container == nullptr) {
        return;
    }

    lv_obj_clean(view->container);
    const ui_palette_t *pal = ui_theme_get();

    if (view->entries.empty()) {
        view->empty_label = lv_label_create(view->container);
        lv_label_set_text(view->empty_label, "Nenhum arquivo ou pasta encontrado");
        lv_obj_set_style_text_font(view->empty_label, &lv_font_montserrat_18_latin1, 0);
        lv_obj_set_style_text_color(view->empty_label, lv_color_hex(pal->text_muted), 0);
        lv_obj_center(view->empty_label);
        return;
    }

    if (view->current_view_mode == UI_FILES_VIEW_ICONS) {
        /* Modo Grade de Icones */
        lv_obj_set_flex_flow(view->container, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(view->container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(view->container, 16, 0);
        lv_obj_set_style_pad_row(view->container, 16, 0);
        lv_obj_set_style_pad_column(view->container, 16, 0);

        for (const auto &entry : view->entries) {
            lv_obj_t *tile = lv_obj_create(view->container);
            lv_obj_set_size(tile, 112, 116);
            lv_obj_set_style_bg_color(tile, lv_color_hex(pal->surface), 0);
            lv_obj_set_style_bg_color(tile, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(tile, 1, 0);
            lv_obj_set_style_border_color(tile, lv_color_hex(pal->border), 0);
            lv_obj_set_style_radius(tile, 10, 0);
            lv_obj_set_style_pad_all(tile, 4, 0);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            auto *click_data = new ItemClickData{view, entry.name};
            lv_obj_add_event_cb(tile, [](lv_event_t *e) { item_click_cb(e); }, LV_EVENT_CLICKED, click_data);

            lv_obj_add_event_cb(
                tile,
                [](lv_event_t *e) {
                    auto *d = static_cast<ItemClickData *>(lv_event_get_user_data(e));
                    delete d;
                },
                LV_EVENT_DELETE, click_data);

            lv_obj_t *icon_label = lv_label_create(tile);
            lv_label_set_text(icon_label, entry.is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
            lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_28_latin1, 0);
            lv_obj_set_style_text_color(icon_label, lv_color_hex(entry.is_dir ? pal->accent : pal->text_muted), 0);

            lv_obj_t *name_label = lv_label_create(tile);
            lv_label_set_text(name_label, entry.name.c_str());
            lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(name_label, 104);
            lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_font(name_label, &lv_font_montserrat_18_latin1, 0);
            lv_obj_set_style_text_color(name_label, lv_color_hex(pal->text), 0);
        }
    } else {
        /* Modo Lista Detalhada */
        lv_obj_set_flex_flow(view->container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(view->container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(view->container, 8, 0);
        lv_obj_set_style_pad_row(view->container, 4, 0);

        int32_t width = lv_display_get_horizontal_resolution(nullptr);
        int32_t item_w = width - 24;

        for (const auto &entry : view->entries) {
            lv_obj_t *item = lv_obj_create(view->container);
            lv_obj_set_size(item, item_w, 52);
            lv_obj_set_style_bg_color(item, lv_color_hex(pal->surface), 0);
            lv_obj_set_style_bg_color(item, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(item, 1, 0);
            lv_obj_set_style_border_color(item, lv_color_hex(pal->border), 0);
            lv_obj_set_style_radius(item, 8, 0);
            lv_obj_set_style_pad_left(item, 12, 0);
            lv_obj_set_style_pad_right(item, 12, 0);
            lv_obj_set_style_pad_top(item, 4, 0);
            lv_obj_set_style_pad_bottom(item, 4, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

            auto *click_data = new ItemClickData{view, entry.name};
            lv_obj_add_event_cb(item, [](lv_event_t *e) { item_click_cb(e); }, LV_EVENT_CLICKED, click_data);

            lv_obj_add_event_cb(
                item,
                [](lv_event_t *e) {
                    auto *d = static_cast<ItemClickData *>(lv_event_get_user_data(e));
                    delete d;
                },
                LV_EVENT_DELETE, click_data);

            /* Linha flex com colunas */
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *icon = lv_label_create(item);
            lv_label_set_text(icon, entry.is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_18_latin1, 0);
            lv_obj_set_style_text_color(icon, lv_color_hex(entry.is_dir ? pal->accent : pal->text_muted), 0);
            lv_obj_set_style_margin_right(icon, 8, 0);

            lv_obj_t *name = lv_label_create(item);
            lv_label_set_text(name, entry.name.c_str());
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_obj_set_flex_grow(name, 1);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_18_latin1, 0);
            lv_obj_set_style_text_color(name, lv_color_hex(pal->text), 0);

            char size_buf[32];
            format_file_size(entry.size, entry.is_dir, size_buf, sizeof(size_buf));
            lv_obj_t *size_lbl = lv_label_create(item);
            lv_label_set_text(size_lbl, size_buf);
            lv_obj_set_width(size_lbl, 110);
            lv_obj_set_style_text_align(size_lbl, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_font(size_lbl, &lv_font_montserrat_18_latin1, 0);
            lv_obj_set_style_text_color(size_lbl, lv_color_hex(pal->text_muted), 0);
            lv_obj_set_style_margin_right(size_lbl, 16, 0);

            char date_buf[32];
            format_file_date(entry.mtime, date_buf, sizeof(date_buf));
            lv_obj_t *date_lbl = lv_label_create(item);
            lv_label_set_text(date_lbl, date_buf);
            lv_obj_set_width(date_lbl, 170);
            lv_obj_set_style_text_align(date_lbl, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_font(date_lbl, &lv_font_montserrat_18_latin1, 0);
            lv_obj_set_style_text_color(date_lbl, lv_color_hex(pal->text_muted), 0);
        }
    }
}

} // namespace

ui_files_view_t *ui_files_view_create(lv_obj_t *parent, ui_app_bar_t app_bar)
{
    if (parent == nullptr) {
        return nullptr;
    }

    auto *view = new ui_files_view_t();
    view->parent_screen = parent;
    view->app_bar = app_bar;
    view->current_view_mode = UI_FILES_VIEW_ICONS;
    view->show_hidden = load_files_show_hidden();
    view->current_path = "/sdcard";

    if (view->app_bar.bar != nullptr) {
        view->view_btn =
            ui_app_bar_add_action_button(&view->app_bar, LV_SYMBOL_LIST, toggle_view_click_cb, view, &view->view_label);
        view->hidden_btn =
            ui_app_bar_add_action_button(&view->app_bar, view->show_hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE,
                                         toggle_hidden_click_cb, view, &view->hidden_label);
    }

    view->container = lv_obj_create(parent);
    lv_obj_set_style_border_width(view->container, 0, 0);
    lv_obj_set_style_radius(view->container, 0, 0);
    lv_obj_set_scroll_dir(view->container, LV_DIR_VER);

    ui_files_view_apply_layout(view);
    ui_files_view_refresh_theme(view);
    load_directory(view, "/sdcard");

    return view;
}

void ui_files_view_open_path(ui_files_view_t *view, const char *path)
{
    if (view == nullptr) {
        return;
    }
    load_directory(view, path != nullptr ? path : "/sdcard");
}

void ui_files_view_refresh_theme(ui_files_view_t *view)
{
    if (view == nullptr || view->container == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(view->container, lv_color_hex(pal->background), 0);

    if (view->app_bar.bar != nullptr) {
        ui_app_bar_refresh_theme(&view->app_bar);
    }

    render_content(view);
}

void ui_files_view_apply_layout(ui_files_view_t *view)
{
    if (view == nullptr || view->container == nullptr) {
        return;
    }

    int32_t width = lv_display_get_horizontal_resolution(nullptr);
    int32_t height = lv_display_get_vertical_resolution(nullptr);

    int32_t top = 2 * UI_BAR_HEIGHT;
    int32_t container_h = std::max<int32_t>(height - top, 100);

    lv_obj_set_size(view->container, width, container_h);
    lv_obj_set_pos(view->container, 0, top);

    if (view->app_bar.bar != nullptr) {
        lv_obj_set_width(view->app_bar.bar, width);
    }

    render_content(view);
}

void ui_files_view_destroy(ui_files_view_t *view)
{
    if (view == nullptr) {
        return;
    }
    delete view;
}
