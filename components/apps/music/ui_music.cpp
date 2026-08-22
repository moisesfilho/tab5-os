#include "ui_music.h"
#include "app_registry.h"
#include "music_player.h"
#include "ui_app_bar.h"
#include "ui_bar.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "wifi_storage.h"
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

static const char *TAG = "tab5_ui_music";

namespace {

enum class ItemType { PARENT_DIR, DIRECTORY, AUDIO_FILE };

struct MusicListItem {
    ItemType type;
    std::string name;
    std::string fullpath;
    size_t size_bytes;
    time_t mtime;
    int playlist_index; /* -1 para pastas; >= 0 para arquivos de áudio */
};

struct MusicFileItem {
    std::string filename;
    std::string fullpath;
    size_t size_bytes;
    time_t mtime;
};

lv_obj_t *music_scr = nullptr;
ui_app_bar_t music_app_bar = {};
lv_obj_t *refresh_btn = nullptr;
lv_obj_t *refresh_label = nullptr;

lv_obj_t *main_container = nullptr;

/* Card do Player (Tocando Agora) */
lv_obj_t *play_card = nullptr;
lv_obj_t *play_card_title = nullptr;
lv_obj_t *play_file_label = nullptr;
lv_obj_t *play_info_label = nullptr;
lv_obj_t *play_bar = nullptr;
lv_obj_t *play_time_label = nullptr;
lv_obj_t *play_ctrl_row = nullptr;
lv_obj_t *play_repeat_btn = nullptr;
lv_obj_t *play_repeat_label = nullptr;
lv_obj_t *play_prev_btn = nullptr;
lv_obj_t *play_prev_label = nullptr;
lv_obj_t *play_toggle_btn = nullptr;
lv_obj_t *play_toggle_label = nullptr;
lv_obj_t *play_stop_btn = nullptr;
lv_obj_t *play_stop_label = nullptr;
lv_obj_t *play_next_btn = nullptr;
lv_obj_t *play_next_label = nullptr;
lv_obj_t *play_loop_btn = nullptr;
lv_obj_t *play_loop_label = nullptr;

bool s_repeat_one = false;
bool s_loop_all = false;

/* Volume */
lv_obj_t *vol_row = nullptr;
lv_obj_t *vol_icon_label = nullptr;
lv_obj_t *vol_slider = nullptr;
lv_obj_t *vol_value_label = nullptr;

/* Card da Lista de Músicas */
lv_obj_t *list_card = nullptr;
lv_obj_t *list_header_row = nullptr;
lv_obj_t *folder_up_btn = nullptr;
lv_obj_t *folder_up_label = nullptr;
lv_obj_t *list_title_label = nullptr;
lv_obj_t *list_container = nullptr;
lv_obj_t *empty_label = nullptr;

/* Modal de Exclusão */
lv_obj_t *confirm_modal = nullptr;
std::string s_file_to_delete;

std::string s_current_folder = "/sdcard/musica";
std::vector<MusicListItem> s_display_items;
std::vector<MusicFileItem> s_playlist;
int s_current_track_index = -1;
lv_timer_t *s_update_timer = nullptr;
music_player_state_t s_last_state = MUSIC_PLAYER_STATE_IDLE;
std::string s_active_file;

void scan_music_directory(void);
void render_music_list(void);
void show_delete_modal(const std::string &filepath, const std::string &filename);
void hide_delete_modal(void);
void play_track_at_index(int index);
void folder_click_cb(lv_event_t *event);
void folder_up_btn_cb(lv_event_t *event);

void format_time_mmss(uint32_t sec, char *buf, size_t buf_len)
{
    uint32_t m = sec / 60;
    uint32_t s = sec % 60;
    snprintf(buf, buf_len, "%02u:%02u", (unsigned)m, (unsigned)s);
}

void format_file_size(size_t bytes, char *buf, size_t buf_len)
{
    if (bytes < 1024) {
        snprintf(buf, buf_len, "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, buf_len, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(buf, buf_len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }
}

void back_btn_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_close_music();
}

void refresh_btn_cb(lv_event_t *event)
{
    (void)event;
    scan_music_directory();
    render_music_list();
}

void play_toggle_btn_cb(lv_event_t *event)
{
    (void)event;
    printf("[MUSIC_UI] play_toggle_btn_cb pressionado\n");
    music_player_status_t st;
    music_player_get_status(&st);

    if (st.state == MUSIC_PLAYER_STATE_PLAYING) {
        music_player_pause();
    } else if (st.state == MUSIC_PLAYER_STATE_PAUSED) {
        music_player_resume();
    } else if (s_current_track_index >= 0 && s_current_track_index < (int)s_playlist.size()) {
        play_track_at_index(s_current_track_index);
    } else if (!s_playlist.empty()) {
        play_track_at_index(0);
    }
}

void play_stop_btn_cb(lv_event_t *event)
{
    (void)event;
    music_player_stop();
    lv_bar_set_value(play_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(play_time_label, "00:00 / 00:00");
    ui_app_bar_set_title(&music_app_bar, "Música");
}

void update_repeat_btn_style(void)
{
    if (play_repeat_btn == nullptr || play_repeat_label == nullptr) {
        return;
    }
    const ui_palette_t *pal = ui_theme_get();
    if (s_repeat_one) {
        lv_obj_set_style_bg_color(play_repeat_btn, lv_color_hex(pal->accent), 0);
        lv_obj_set_style_text_color(play_repeat_label, lv_color_white(), 0);
    } else {
        lv_obj_set_style_bg_color(play_repeat_btn, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_text_color(play_repeat_label, lv_color_hex(pal->text_muted), 0);
    }
}

void update_loop_btn_style(void)
{
    if (play_loop_btn == nullptr || play_loop_label == nullptr) {
        return;
    }
    const ui_palette_t *pal = ui_theme_get();
    if (s_loop_all) {
        lv_obj_set_style_bg_color(play_loop_btn, lv_color_hex(pal->accent), 0);
        lv_obj_set_style_text_color(play_loop_label, lv_color_white(), 0);
    } else {
        lv_obj_set_style_bg_color(play_loop_btn, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_text_color(play_loop_label, lv_color_hex(pal->text_muted), 0);
    }
}

void play_repeat_btn_cb(lv_event_t *event)
{
    (void)event;
    s_repeat_one = !s_repeat_one;
    update_repeat_btn_style();
}

void play_loop_btn_cb(lv_event_t *event)
{
    (void)event;
    s_loop_all = !s_loop_all;
    update_loop_btn_style();
}

void play_prev_btn_cb(lv_event_t *event)
{
    (void)event;
    if (s_playlist.empty()) {
        return;
    }
    if (s_current_track_index <= 0) {
        play_track_at_index((int)s_playlist.size() - 1);
    } else {
        play_track_at_index(s_current_track_index - 1);
    }
}

void play_next_btn_cb(lv_event_t *event)
{
    (void)event;
    if (s_playlist.empty()) {
        return;
    }
    if (s_current_track_index + 1 >= (int)s_playlist.size()) {
        if (s_loop_all) {
            play_track_at_index(0);
        }
    } else {
        play_track_at_index(s_current_track_index + 1);
    }
}

void play_track_at_index(int index)
{
    if (index < 0 || index >= (int)s_playlist.size()) {
        return;
    }
    s_current_track_index = index;
    s_active_file = s_playlist[index].fullpath;

    music_player_start(s_active_file.c_str());

    char buf[128];
    snprintf(buf, sizeof(buf), "Tocando: %s", s_playlist[index].filename.c_str());
    lv_label_set_text(play_file_label, buf);
    lv_label_set_text(play_info_label, "Carregando áudio...");

    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "Música - %s", s_playlist[index].filename.c_str());
    ui_app_bar_set_title(&music_app_bar, title_buf);
}

void play_item_cb(lv_event_t *event)
{
    int pl_index = (int)(intptr_t)lv_event_get_user_data(event);
    play_track_at_index(pl_index);
}

void folder_click_cb(lv_event_t *event)
{
    int item_idx = (int)(intptr_t)lv_event_get_user_data(event);
    if (item_idx >= 0 && item_idx < (int)s_display_items.size()) {
        s_current_folder = s_display_items[item_idx].fullpath;
        scan_music_directory();
        render_music_list();
        if (list_container != nullptr) {
            lv_obj_scroll_to_y(list_container, 0, LV_ANIM_OFF);
        }
    }
}

void folder_up_btn_cb(lv_event_t *event)
{
    (void)event;
    if (s_current_folder != "/sdcard") {
        size_t last_slash = s_current_folder.find_last_of('/');
        std::string parent = "/sdcard";
        if (last_slash != std::string::npos && last_slash > 0) {
            parent = s_current_folder.substr(0, last_slash);
            if (parent.length() < 7) {
                parent = "/sdcard";
            }
        }
        s_current_folder = parent;
        scan_music_directory();
        render_music_list();
        if (list_container != nullptr) {
            lv_obj_scroll_to_y(list_container, 0, LV_ANIM_OFF);
        }
    }
}

void vol_slider_cb(lv_event_t *event)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(event);
    int val = (int)lv_slider_get_value(slider);
    music_player_set_volume(val);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(vol_value_label, buf);
}

void delete_item_cb(lv_event_t *event)
{
    int item_idx = (int)(intptr_t)lv_event_get_user_data(event);
    if (item_idx >= 0 && item_idx < (int)s_display_items.size()) {
        const auto &item = s_display_items[item_idx];
        if (item.type == ItemType::AUDIO_FILE) {
            show_delete_modal(item.fullpath, item.name);
        }
    }
}

void modal_btn_cb(lv_event_t *event)
{
    const char *action = (const char *)lv_event_get_user_data(event);
    if (action != nullptr && strcmp(action, "delete") == 0) {
        if (!s_file_to_delete.empty()) {
            music_player_status_t st;
            music_player_get_status(&st);
            if (s_active_file == s_file_to_delete &&
                (st.state == MUSIC_PLAYER_STATE_PLAYING || st.state == MUSIC_PLAYER_STATE_PAUSED)) {
                music_player_stop();
                s_active_file.clear();
                s_current_track_index = -1;
                lv_label_set_text(play_file_label, "Nenhuma música em reprodução");
                lv_label_set_text(play_info_label, "");
                lv_label_set_text(play_time_label, "00:00 / 00:00");
                lv_bar_set_value(play_bar, 0, LV_ANIM_OFF);
                ui_app_bar_set_title(&music_app_bar, "Música");
            }
            unlink(s_file_to_delete.c_str());
            ESP_LOGI(TAG, "Arquivo excluido: %s", s_file_to_delete.c_str());
            s_file_to_delete.clear();
            scan_music_directory();
            render_music_list();
        }
    }
    hide_delete_modal();
}

void show_delete_modal(const std::string &filepath, const std::string &filename)
{
    hide_delete_modal();
    s_file_to_delete = filepath;

    const ui_palette_t *pal = ui_theme_get();

    confirm_modal = lv_obj_create(music_scr);
    lv_obj_set_size(confirm_modal, 380, 200);
    lv_obj_center(confirm_modal);
    lv_obj_set_style_bg_color(confirm_modal, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(confirm_modal, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(confirm_modal, 2, 0);
    lv_obj_set_style_radius(confirm_modal, 12, 0);
    lv_obj_set_style_shadow_width(confirm_modal, 30, 0);
    lv_obj_set_style_shadow_opa(confirm_modal, LV_OPA_60, 0);
    lv_obj_set_flex_flow(confirm_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(confirm_modal, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(confirm_modal);
    lv_label_set_text(title, "Excluir Música?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(pal->text), 0);

    lv_obj_t *msg = lv_label_create(confirm_modal);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, 340);
    char buf[128];
    snprintf(buf, sizeof(buf), "Deseja excluir \"%s\"?", filename.c_str());
    lv_label_set_text(msg, buf);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(pal->text_muted), 0);

    lv_obj_t *btn_row = lv_obj_create(confirm_modal);
    lv_obj_set_size(btn_row, 340, 48);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_cancel = lv_button_create(btn_row);
    lv_obj_set_size(btn_cancel, 120, 38);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_radius(btn_cancel, 8, 0);
    lv_obj_add_event_cb(btn_cancel, modal_btn_cb, LV_EVENT_CLICKED, (void *)"cancel");
    lv_obj_t *lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, "Cancelar");
    lv_obj_set_style_text_color(lbl_c, lv_color_hex(pal->text), 0);
    lv_obj_center(lbl_c);

    lv_obj_t *btn_del = lv_button_create(btn_row);
    lv_obj_set_size(btn_del, 120, 38);
    lv_obj_set_style_bg_color(btn_del, lv_color_hex(0xE53935), 0);
    lv_obj_set_style_radius(btn_del, 8, 0);
    lv_obj_add_event_cb(btn_del, modal_btn_cb, LV_EVENT_CLICKED, (void *)"delete");
    lv_obj_t *lbl_d = lv_label_create(btn_del);
    lv_label_set_text(lbl_d, "Excluir");
    lv_obj_set_style_text_color(lbl_d, lv_color_white(), 0);
    lv_obj_center(lbl_d);
}

void hide_delete_modal(void)
{
    if (confirm_modal != nullptr) {
        lv_obj_delete(confirm_modal);
        confirm_modal = nullptr;
    }
}

void scan_music_directory(void)
{
    s_display_items.clear();
    s_playlist.clear();

    if (s_current_folder.empty()) {
        s_current_folder = "/sdcard/musica";
    }

    DIR *d = opendir(s_current_folder.c_str());
    if (!d && s_current_folder == "/sdcard/musica") {
        mkdir(s_current_folder.c_str(), 0755);
        d = opendir(s_current_folder.c_str());
    }

    /* Se não estiver na raiz do SD (/sdcard), adiciona a opção de subir de nível (..) */
    if (s_current_folder != "/sdcard") {
        size_t last_slash = s_current_folder.find_last_of('/');
        std::string parent = "/sdcard";
        if (last_slash != std::string::npos && last_slash > 0) {
            parent = s_current_folder.substr(0, last_slash);
            if (parent.length() < 7) {
                parent = "/sdcard";
            }
        }
        s_display_items.push_back({ItemType::PARENT_DIR, "..", parent, 0, 0, -1});
    }

    std::vector<MusicListItem> dir_items;
    std::vector<MusicListItem> file_items;

    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (entry->d_name[0] == '.') {
                continue; /* Arquivos ou pastas ocultas */
            }

            std::string fullpath = s_current_folder;
            if (fullpath.back() != '/') {
                fullpath += "/";
            }
            fullpath += entry->d_name;

            struct stat st;
            bool is_dir = false;
            size_t sz = 0;
            time_t mt = 0;
            if (stat(fullpath.c_str(), &st) == 0) {
                is_dir = S_ISDIR(st.st_mode);
                sz = st.st_size;
                mt = st.st_mtime;
            } else if (entry->d_type == DT_DIR) {
                is_dir = true;
            }

            if (is_dir) {
                dir_items.push_back({ItemType::DIRECTORY, entry->d_name, fullpath, 0, mt, -1});
            } else {
                const char *dot = strrchr(entry->d_name, '.');
                if (dot && (strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0)) {
                    file_items.push_back({ItemType::AUDIO_FILE, entry->d_name, fullpath, sz, mt, -1});
                }
            }
        }
        closedir(d);
    }

    /* Ordena pastas e arquivos alfabeticamente */
    std::sort(dir_items.begin(), dir_items.end(),
              [](const MusicListItem &a, const MusicListItem &b) { return a.name < b.name; });
    std::sort(file_items.begin(), file_items.end(),
              [](const MusicListItem &a, const MusicListItem &b) { return a.name < b.name; });

    /* Constrói a lista completa de exibição */
    for (const auto &dir : dir_items) {
        s_display_items.push_back(dir);
    }

    for (auto &file : file_items) {
        int pl_idx = (int)s_playlist.size();
        file.playlist_index = pl_idx;
        s_playlist.push_back({file.name, file.fullpath, file.size_bytes, file.mtime});
        s_display_items.push_back(file);
    }

    ESP_LOGI(TAG, "scan_music_directory em '%s': %zu pastas, %zu musicas", s_current_folder.c_str(), dir_items.size(),
             s_playlist.size());

    /* Atualiza índice da música ativa na pasta atual */
    s_current_track_index = -1;
    if (!s_active_file.empty()) {
        for (size_t i = 0; i < s_playlist.size(); ++i) {
            if (s_playlist[i].fullpath == s_active_file) {
                s_current_track_index = (int)i;
                break;
            }
        }
    }
}

void render_music_list(void)
{
    if (list_container == nullptr) {
        return;
    }

    lv_obj_clean(list_container);

    const ui_palette_t *pal = ui_theme_get();

    /* Atualiza visibilidade do botão de subir pasta no cabeçalho */
    if (folder_up_btn != nullptr) {
        if (s_current_folder == "/sdcard") {
            lv_obj_add_flag(folder_up_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(folder_up_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Nome legível da pasta */
    const char *folder_display = s_current_folder.c_str();
    if (s_current_folder == "/sdcard/musica") {
        folder_display = "Músicas (musica/)";
    } else if (s_current_folder == "/sdcard") {
        folder_display = "Raiz (sdcard/)";
    }

    char count_buf[96];
    snprintf(count_buf, sizeof(count_buf), LV_SYMBOL_DIRECTORY " %s (%zu)", folder_display, s_playlist.size());
    lv_label_set_text(list_title_label, count_buf);

    if (s_display_items.empty()) {
        empty_label = lv_label_create(list_container);
        lv_label_set_text(empty_label, "Nenhuma música ou pasta encontrada\n(Copie arquivos .mp3 ou .wav)");
        lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_14_latin1, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_pad_all(empty_label, 12, 0);
        return;
    }

    for (size_t i = 0; i < s_display_items.size(); ++i) {
        const auto &item = s_display_items[i];
        lv_obj_t *row = lv_obj_create(list_container);
        lv_obj_set_size(row, lv_pct(100), 52);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        if (item.type == ItemType::PARENT_DIR) {
            /* Linha compacta com ícone para subir nível */
            lv_obj_set_style_bg_color(row, lv_color_hex(pal->surface_alt), 0);
            lv_obj_set_style_border_color(row, lv_color_hex(pal->border), 0);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, folder_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

            lv_obj_t *info_col = lv_obj_create(row);
            lv_obj_set_size(info_col, lv_pct(100), lv_pct(100));
            lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(info_col, 0, 0);
            lv_obj_set_style_pad_all(info_col, 0, 0);
            lv_obj_clear_flag(info_col, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(info_col, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(info_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *lbl_back = lv_label_create(info_col);
            lv_label_set_text(lbl_back, LV_SYMBOL_LEFT "  ..");
            lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(lbl_back, lv_color_hex(pal->accent), 0);
        } else if (item.type == ItemType::DIRECTORY) {
            /* Linha de Subpasta */
            lv_obj_set_style_bg_color(row, lv_color_hex(pal->surface), 0);
            lv_obj_set_style_border_color(row, lv_color_hex(pal->border), 0);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, folder_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

            lv_obj_t *info_col = lv_obj_create(row);
            lv_obj_set_size(info_col, lv_pct(85), lv_pct(100));
            lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(info_col, 0, 0);
            lv_obj_set_style_pad_all(info_col, 0, 0);
            lv_obj_clear_flag(info_col, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(info_col, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(info_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *lbl_name = lv_label_create(info_col);
            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), LV_SYMBOL_DIRECTORY " %s", item.name.c_str());
            lv_label_set_text(lbl_name, name_buf);
            lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
            lv_obj_set_width(lbl_name, lv_pct(100));
            lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(lbl_name, lv_color_hex(pal->text), 0);

            lv_obj_t *lbl_sub = lv_label_create(info_col);
            lv_label_set_text(lbl_sub, "Pasta de músicas");
            lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(lbl_sub, lv_color_hex(pal->text_muted), 0);

            /* Ícone de Seta para Entrar */
            lv_obj_t *lbl_arrow = lv_label_create(row);
            lv_label_set_text(lbl_arrow, LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(lbl_arrow, lv_color_hex(pal->text_muted), 0);
            lv_obj_clear_flag(lbl_arrow, LV_OBJ_FLAG_CLICKABLE);
        } else {
            /* Linha de Arquivo de Áudio */
            lv_obj_set_style_bg_color(row, lv_color_hex(pal->surface), 0);
            lv_obj_set_style_border_color(row, lv_color_hex(pal->border), 0);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, play_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)item.playlist_index);

            /* Ícone e Nome */
            lv_obj_t *info_col = lv_obj_create(row);
            lv_obj_set_size(info_col, lv_pct(65), lv_pct(100));
            lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(info_col, 0, 0);
            lv_obj_set_style_pad_all(info_col, 0, 0);
            lv_obj_clear_flag(info_col, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(info_col, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(info_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *lbl_name = lv_label_create(info_col);
            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), LV_SYMBOL_AUDIO " %s", item.name.c_str());
            lv_label_set_text(lbl_name, name_buf);
            lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
            lv_obj_set_width(lbl_name, lv_pct(100));
            lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(lbl_name, lv_color_hex(pal->text), 0);

            lv_obj_t *lbl_sz = lv_label_create(info_col);
            char sz_buf[64];
            format_file_size(item.size_bytes, sz_buf, sizeof(sz_buf));
            lv_label_set_text(lbl_sz, sz_buf);
            lv_obj_set_style_text_font(lbl_sz, &lv_font_montserrat_14_latin1, 0);
            lv_obj_set_style_text_color(lbl_sz, lv_color_hex(pal->text_muted), 0);

            /* Botões de Ação (Play e Delete) */
            lv_obj_t *actions_row = lv_obj_create(row);
            lv_obj_set_size(actions_row, lv_pct(32), lv_pct(100));
            lv_obj_set_style_bg_opa(actions_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(actions_row, 0, 0);
            lv_obj_set_style_pad_all(actions_row, 0, 0);
            lv_obj_clear_flag(actions_row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(actions_row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_flex_flow(actions_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(actions_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            /* Botao Play */
            lv_obj_t *btn_p = lv_button_create(actions_row);
            lv_obj_set_size(btn_p, 40, 36);
            lv_obj_set_style_bg_color(btn_p, lv_color_hex(pal->accent), 0);
            lv_obj_set_style_radius(btn_p, 6, 0);
            lv_obj_set_style_pad_all(btn_p, 0, 0);
            lv_obj_add_event_cb(btn_p, play_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)item.playlist_index);
            lv_obj_t *lbl_p = lv_label_create(btn_p);
            lv_label_set_text(lbl_p, LV_SYMBOL_PLAY);
            lv_obj_set_style_text_color(lbl_p, lv_color_white(), 0);
            lv_obj_clear_flag(lbl_p, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_center(lbl_p);

            /* Botao Excluir */
            lv_obj_t *btn_del = lv_button_create(actions_row);
            lv_obj_set_size(btn_del, 40, 36);
            lv_obj_set_style_bg_color(btn_del, lv_color_hex(pal->surface_alt), 0);
            lv_obj_set_style_radius(btn_del, 6, 0);
            lv_obj_set_style_pad_all(btn_del, 0, 0);
            lv_obj_add_event_cb(btn_del, delete_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_t *lbl_del = lv_label_create(btn_del);
            lv_label_set_text(lbl_del, LV_SYMBOL_TRASH);
            lv_obj_set_style_text_color(lbl_del, lv_color_hex(0xE53935), 0);
            lv_obj_clear_flag(lbl_del, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_center(lbl_del);
        }
    }
}

void music_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    music_player_status_t st;
    music_player_get_status(&st);

    if (st.state != s_last_state) {
        if (s_last_state == MUSIC_PLAYER_STATE_PLAYING && st.state == MUSIC_PLAYER_STATE_IDLE) {
            /* Música terminou normalmente */
            if (s_repeat_one && s_current_track_index >= 0) {
                s_last_state = st.state;
                play_track_at_index(s_current_track_index);
                return;
            } else if (s_current_track_index >= 0 && s_current_track_index + 1 < (int)s_playlist.size()) {
                s_last_state = st.state;
                play_track_at_index(s_current_track_index + 1);
                return;
            } else if (s_loop_all && !s_playlist.empty()) {
                s_last_state = st.state;
                play_track_at_index(0);
                return;
            }
        }
        s_last_state = st.state;
        if (st.state == MUSIC_PLAYER_STATE_PLAYING) {
            lv_label_set_text(play_toggle_label, LV_SYMBOL_PAUSE);
            lv_obj_set_style_bg_color(play_toggle_btn, lv_color_hex(ui_theme_get()->accent), 0);
        } else {
            lv_label_set_text(play_toggle_label, LV_SYMBOL_PLAY);
            lv_obj_set_style_bg_color(play_toggle_btn, lv_color_hex(ui_theme_get()->surface_alt), 0);
        }
    }

    if (st.state == MUSIC_PLAYER_STATE_PLAYING || st.state == MUSIC_PLAYER_STATE_PAUSED) {
        char cur_str[16], tot_str[16], time_buf[64];
        format_time_mmss(st.current_time_sec, cur_str, sizeof(cur_str));
        format_time_mmss(st.total_time_sec, tot_str, sizeof(tot_str));
        snprintf(time_buf, sizeof(time_buf), "%s / %s", cur_str, tot_str);
        lv_label_set_text(play_time_label, time_buf);

        if (st.total_time_sec > 0) {
            int32_t pct = (int32_t)((st.current_time_sec * 100) / st.total_time_sec);
            if (pct > 100)
                pct = 100;
            lv_bar_set_value(play_bar, pct, LV_ANIM_OFF);
        }

        char info_buf[64];
        snprintf(info_buf, sizeof(info_buf), "%u Hz • %u ch • %u-bit", (unsigned)st.sample_rate, (unsigned)st.channels,
                 (unsigned)st.bits_per_sample);
        lv_label_set_text(play_info_label, info_buf);
    }
}

static void resolution_cb(lv_event_t *event)
{
    (void)event;
    ui_music_apply_layout();
}

} // namespace

void ui_music_register(void)
{
    static const char *const s_music_exts[] = {"mp3", "wav", nullptr};
    static const app_desc_t s_music_app = {
        .id = "music",
        .name = "Música",
        .icon_symbol = LV_SYMBOL_AUDIO,
        .icon_builder = nullptr,
        .icon_theme_refresh = nullptr,
        .on_launch = ui_shell_open_music,
        .file_extensions = s_music_exts,
        .on_open_file = ui_shell_open_music_with_file,
    };
    app_registry_register(&s_music_app);
}

lv_obj_t *ui_music_create(void)
{
    music_scr = lv_obj_create(NULL);
    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(music_scr, lv_color_hex(pal->background), 0);
    lv_obj_set_style_pad_all(music_scr, 0, 0);
    lv_obj_clear_flag(music_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Barra padronizada do aplicativo */
    music_app_bar = ui_app_bar_create(music_scr, "Música", back_btn_cb, nullptr);
    refresh_btn =
        ui_app_bar_add_action_button(&music_app_bar, LV_SYMBOL_REFRESH, refresh_btn_cb, nullptr, &refresh_label);

    /* Container Principal */
    main_container = lv_obj_create(music_scr);
    lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_container, 0, 0);
    lv_obj_set_style_pad_all(main_container, 8, 0);
    lv_obj_set_style_pad_gap(main_container, 8, 0);
    lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);

    /* --- Card do Player (Tocando Agora) --- */
    play_card = lv_obj_create(main_container);
    lv_obj_set_style_bg_color(play_card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(play_card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(play_card, 1, 0);
    lv_obj_set_style_radius(play_card, 10, 0);
    lv_obj_set_style_pad_all(play_card, 12, 0);
    lv_obj_set_flex_flow(play_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(play_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    play_card_title = lv_label_create(play_card);
    lv_label_set_text(play_card_title, "Tocando Agora");
    lv_obj_set_style_text_font(play_card_title, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(play_card_title, lv_color_hex(pal->accent), 0);

    play_file_label = lv_label_create(play_card);
    lv_label_set_text(play_file_label, "Nenhuma música em reprodução");
    lv_label_set_long_mode(play_file_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(play_file_label, lv_pct(95));
    lv_obj_set_style_text_font(play_file_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(play_file_label, lv_color_hex(pal->text), 0);
    lv_obj_set_style_text_align(play_file_label, LV_TEXT_ALIGN_CENTER, 0);

    play_info_label = lv_label_create(play_card);
    lv_label_set_text(play_info_label, "");
    lv_obj_set_style_text_font(play_info_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(play_info_label, lv_color_hex(pal->text_muted), 0);

    /* Barra de Progresso com trilha visivel e borda delimitadora */
    play_bar = lv_bar_create(play_card);
    lv_obj_set_size(play_bar, lv_pct(90), 12);
    lv_bar_set_range(play_bar, 0, 100);
    lv_bar_set_value(play_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(play_bar, 6, 0);
    lv_obj_set_style_radius(play_bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(play_bar, 1, 0);
    lv_obj_set_style_border_color(play_bar, lv_color_hex(ui_theme_is_dark() ? 0x475569 : 0xCBD5E1), 0);
    lv_obj_set_style_bg_color(play_bar, lv_color_hex(ui_theme_is_dark() ? 0x2A3441 : 0xE2E8F0), 0);
    lv_obj_set_style_bg_color(play_bar, lv_color_hex(pal->accent), LV_PART_INDICATOR);

    play_time_label = lv_label_create(play_card);
    lv_label_set_text(play_time_label, "00:00 / 00:00");
    lv_obj_set_style_text_font(play_time_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(play_time_label, lv_color_hex(pal->text_muted), 0);

    /* Botoes de Controle (Repetir 1, Prev, Play/Pause, Stop, Next, Loop Playlist) */
    play_ctrl_row = lv_obj_create(play_card);
    lv_obj_set_size(play_ctrl_row, lv_pct(98), 52);
    lv_obj_set_style_bg_opa(play_ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(play_ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(play_ctrl_row, 0, 0);
    lv_obj_clear_flag(play_ctrl_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(play_ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(play_ctrl_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(play_ctrl_row, 8, 0);

    /* Botao Repetir 1 Musica */
    play_repeat_btn = lv_button_create(play_ctrl_row);
    lv_obj_set_size(play_repeat_btn, 46, 44);
    lv_obj_set_style_radius(play_repeat_btn, 8, 0);
    lv_obj_add_event_cb(play_repeat_btn, play_repeat_btn_cb, LV_EVENT_CLICKED, nullptr);
    play_repeat_label = lv_label_create(play_repeat_btn);
    lv_label_set_text(play_repeat_label, LV_SYMBOL_REFRESH "1");
    lv_obj_center(play_repeat_label);
    update_repeat_btn_style();

    /* Botao Anterior */
    play_prev_btn = lv_button_create(play_ctrl_row);
    lv_obj_set_size(play_prev_btn, 46, 44);
    lv_obj_set_style_bg_color(play_prev_btn, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_radius(play_prev_btn, 8, 0);
    lv_obj_add_event_cb(play_prev_btn, play_prev_btn_cb, LV_EVENT_CLICKED, nullptr);
    play_prev_label = lv_label_create(play_prev_btn);
    lv_label_set_text(play_prev_label, LV_SYMBOL_PREV);
    lv_obj_set_style_text_color(play_prev_label, lv_color_hex(pal->text), 0);
    lv_obj_center(play_prev_label);

    /* Botao Play / Pause */
    play_toggle_btn = lv_button_create(play_ctrl_row);
    lv_obj_set_size(play_toggle_btn, 54, 44);
    lv_obj_set_style_bg_color(play_toggle_btn, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_radius(play_toggle_btn, 8, 0);
    lv_obj_add_event_cb(play_toggle_btn, play_toggle_btn_cb, LV_EVENT_CLICKED, nullptr);
    play_toggle_label = lv_label_create(play_toggle_btn);
    lv_label_set_text(play_toggle_label, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_toggle_label, lv_color_white(), 0);
    lv_obj_center(play_toggle_label);

    /* Botao Stop */
    play_stop_btn = lv_button_create(play_ctrl_row);
    lv_obj_set_size(play_stop_btn, 46, 44);
    lv_obj_set_style_bg_color(play_stop_btn, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_radius(play_stop_btn, 8, 0);
    lv_obj_add_event_cb(play_stop_btn, play_stop_btn_cb, LV_EVENT_CLICKED, nullptr);
    play_stop_label = lv_label_create(play_stop_btn);
    lv_label_set_text(play_stop_label, LV_SYMBOL_STOP);
    lv_obj_set_style_text_color(play_stop_label, lv_color_hex(pal->text), 0);
    lv_obj_center(play_stop_label);

    /* Botao Proximo */
    play_next_btn = lv_button_create(play_ctrl_row);
    lv_obj_set_size(play_next_btn, 46, 44);
    lv_obj_set_style_bg_color(play_next_btn, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_radius(play_next_btn, 8, 0);
    lv_obj_add_event_cb(play_next_btn, play_next_btn_cb, LV_EVENT_CLICKED, nullptr);
    play_next_label = lv_label_create(play_next_btn);
    lv_label_set_text(play_next_label, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_color(play_next_label, lv_color_hex(pal->text), 0);
    lv_obj_center(play_next_label);

    /* Botao Loop Playlist (Voltar ao inicio) */
    play_loop_btn = lv_button_create(play_ctrl_row);
    lv_obj_set_size(play_loop_btn, 46, 44);
    lv_obj_set_style_radius(play_loop_btn, 8, 0);
    lv_obj_add_event_cb(play_loop_btn, play_loop_btn_cb, LV_EVENT_CLICKED, nullptr);
    play_loop_label = lv_label_create(play_loop_btn);
    lv_label_set_text(play_loop_label, LV_SYMBOL_LOOP);
    lv_obj_center(play_loop_label);
    update_loop_btn_style();

    /* Slider de Volume */
    vol_row = lv_obj_create(play_card);
    lv_obj_set_size(vol_row, lv_pct(90), 38);
    lv_obj_set_style_bg_opa(vol_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_row, 0, 0);
    lv_obj_set_style_pad_all(vol_row, 0, 0);
    lv_obj_clear_flag(vol_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(vol_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    vol_icon_label = lv_label_create(vol_row);
    lv_label_set_text(vol_icon_label, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(vol_icon_label, lv_color_hex(pal->text_muted), 0);

    vol_slider = lv_slider_create(vol_row);
    lv_obj_set_size(vol_slider, lv_pct(65), 10);
    lv_slider_set_range(vol_slider, 0, 100);
    lv_slider_set_value(vol_slider, music_player_get_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(vol_slider, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_bg_color(vol_slider, lv_color_hex(pal->accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vol_slider, lv_color_hex(pal->accent), LV_PART_KNOB);
    lv_obj_add_event_cb(vol_slider, vol_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    vol_value_label = lv_label_create(vol_row);
    char v_buf[16];
    snprintf(v_buf, sizeof(v_buf), "%d%%", music_player_get_volume());
    lv_label_set_text(vol_value_label, v_buf);
    lv_obj_set_style_text_font(vol_value_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(vol_value_label, lv_color_hex(pal->text_muted), 0);

    /* --- Card da Lista de Músicas e Pastas --- */
    list_card = lv_obj_create(main_container);
    lv_obj_set_style_bg_color(list_card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(list_card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(list_card, 1, 0);
    lv_obj_set_style_radius(list_card, 10, 0);
    lv_obj_set_style_pad_all(list_card, 8, 0);
    lv_obj_set_flex_flow(list_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(list_card, LV_OBJ_FLAG_SCROLLABLE);

    list_header_row = lv_obj_create(list_card);
    lv_obj_set_size(list_header_row, lv_pct(100), 38);
    lv_obj_set_style_bg_opa(list_header_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_header_row, 0, 0);
    lv_obj_set_style_pad_all(list_header_row, 0, 0);
    lv_obj_clear_flag(list_header_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(list_header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(list_header_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(list_header_row, 8, 0);

    folder_up_btn = lv_button_create(list_header_row);
    lv_obj_set_size(folder_up_btn, 36, 32);
    lv_obj_set_style_bg_color(folder_up_btn, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_radius(folder_up_btn, 6, 0);
    lv_obj_set_style_pad_all(folder_up_btn, 0, 0);
    lv_obj_add_event_cb(folder_up_btn, folder_up_btn_cb, LV_EVENT_CLICKED, nullptr);

    folder_up_label = lv_label_create(folder_up_btn);
    lv_label_set_text(folder_up_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(folder_up_label, lv_color_hex(pal->accent), 0);
    lv_obj_center(folder_up_label);

    list_title_label = lv_label_create(list_header_row);
    lv_label_set_text(list_title_label, "Músicas e Pastas");
    lv_label_set_long_mode(list_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(list_title_label, 1);
    lv_obj_set_style_text_font(list_title_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(list_title_label, lv_color_hex(pal->accent), 0);

    list_container = lv_obj_create(list_card);
    lv_obj_set_size(list_container, lv_pct(100), lv_pct(88));
    lv_obj_set_style_bg_opa(list_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_container, 0, 0);
    lv_obj_set_style_pad_all(list_container, 4, 0);
    lv_obj_set_style_pad_gap(list_container, 6, 0);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);

    ui_music_apply_layout();
    lv_display_add_event_cb(lv_display_get_default(), resolution_cb, LV_EVENT_RESOLUTION_CHANGED, nullptr);

    s_update_timer = lv_timer_create(music_update_timer_cb, 300, nullptr);

    return music_scr;
}

void ui_music_on_open(void)
{
    wifi_storage_mount();
    music_player_init();
    ui_music_apply_layout();
    ui_music_refresh_theme();
    scan_music_directory();
    render_music_list();
    if (s_update_timer) {
        lv_timer_resume(s_update_timer);
    }
}

void ui_music_on_close(void)
{
    hide_delete_modal();
    /* O s_update_timer permanece ativo para gerenciar a transição de faixas em segundo plano */
}

void ui_music_open_file(const char *filepath)
{
    if (filepath == nullptr) {
        return;
    }

    std::string path_str = filepath;
    size_t last_slash = path_str.find_last_of('/');
    if (last_slash != std::string::npos && last_slash > 0) {
        s_current_folder = path_str.substr(0, last_slash);
    } else {
        s_current_folder = "/sdcard/musica";
    }

    ui_music_on_open();
    s_active_file = filepath;

    for (size_t i = 0; i < s_playlist.size(); ++i) {
        if (s_playlist[i].fullpath == filepath) {
            s_current_track_index = (int)i;
            play_track_at_index((int)i);
            return;
        }
    }

    const char *slash = strrchr(filepath, '/');
    const char *name = slash ? (slash + 1) : filepath;
    music_player_start(filepath);
    char buf[128];
    snprintf(buf, sizeof(buf), "Tocando: %s", name);
    lv_label_set_text(play_file_label, buf);
}

void ui_music_refresh_theme(void)
{
    if (music_scr == nullptr) {
        return;
    }
    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(music_scr, lv_color_hex(pal->background), 0);
    ui_app_bar_refresh_theme(&music_app_bar);

    if (play_card != nullptr) {
        lv_obj_set_style_bg_color(play_card, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(play_card, lv_color_hex(pal->border), 0);
        lv_obj_set_style_text_color(play_card_title, lv_color_hex(pal->accent), 0);
        lv_obj_set_style_text_color(play_file_label, lv_color_hex(pal->text), 0);
        lv_obj_set_style_text_color(play_info_label, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_text_color(play_time_label, lv_color_hex(pal->text_muted), 0);
        uint32_t bar_track = ui_theme_is_dark() ? 0x2A3441 : 0xE2E8F0;
        uint32_t bar_border = ui_theme_is_dark() ? 0x475569 : 0xCBD5E1;
        lv_obj_set_style_bg_color(play_bar, lv_color_hex(bar_track), 0);
        lv_obj_set_style_border_color(play_bar, lv_color_hex(bar_border), 0);
        lv_obj_set_style_border_width(play_bar, 1, 0);
        lv_obj_set_style_bg_color(play_bar, lv_color_hex(pal->accent), LV_PART_INDICATOR);

        lv_obj_set_style_bg_color(play_prev_btn, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_text_color(play_prev_label, lv_color_hex(pal->text), 0);

        lv_obj_set_style_bg_color(play_stop_btn, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_text_color(play_stop_label, lv_color_hex(pal->text), 0);

        lv_obj_set_style_bg_color(play_next_btn, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_text_color(play_next_label, lv_color_hex(pal->text), 0);

        update_repeat_btn_style();
        update_loop_btn_style();

        lv_obj_set_style_text_color(vol_icon_label, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_bg_color(vol_slider, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_bg_color(vol_slider, lv_color_hex(pal->accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(vol_slider, lv_color_hex(pal->accent), LV_PART_KNOB);
        lv_obj_set_style_text_color(vol_value_label, lv_color_hex(pal->text_muted), 0);
    }

    if (list_card != nullptr) {
        lv_obj_set_style_bg_color(list_card, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(list_card, lv_color_hex(pal->border), 0);
        if (folder_up_btn != nullptr) {
            lv_obj_set_style_bg_color(folder_up_btn, lv_color_hex(pal->surface_alt), 0);
            lv_obj_set_style_text_color(folder_up_label, lv_color_hex(pal->accent), 0);
        }
        lv_obj_set_style_text_color(list_title_label, lv_color_hex(pal->accent), 0);
    }

    render_music_list();
}

void ui_music_apply_layout(void)
{
    if (music_scr == nullptr || main_container == nullptr || music_app_bar.bar == nullptr) {
        return;
    }

    int32_t w = lv_display_get_horizontal_resolution(nullptr);
    int32_t h = lv_display_get_vertical_resolution(nullptr);

    if (w <= 0 || h <= 0) {
        w = 1280;
        h = 720;
    }

    /* 1. Barra de título do app: posicionada logo abaixo da barra de status do SO */
    int32_t top = UI_BAR_HEIGHT;
    lv_obj_set_size(music_app_bar.bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_align(music_app_bar.bar, LV_ALIGN_TOP_MID, 0, top);

    /* 2. Container de Conteúdo: abaixo da barra de título do app */
    int32_t content_top = top + UI_BAR_HEIGHT;
    int32_t container_h = std::max<int32_t>(h - content_top, 100);

    lv_obj_set_pos(main_container, 0, content_top);
    lv_obj_set_size(main_container, w, container_h);

    if (w >= h) {
        /* Paisagem: 2 colunas lado a lado */
        lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(play_card, (w * 45) / 100 - 12, container_h - 16);
        lv_obj_set_size(list_card, (w * 55) / 100 - 12, container_h - 16);
    } else {
        /* Retrato: 2 linhas empilhadas */
        lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(play_card, w - 16, (container_h * 46) / 100 - 12);
        lv_obj_set_size(list_card, w - 16, (container_h * 54) / 100 - 12);
    }
}
