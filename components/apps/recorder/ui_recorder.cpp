#include "ui_recorder.h"
#include "app_registry.h"
#include "audio_recorder.h"
#include "ui_bar.h"
#include "ui_app_bar.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_theme.h"
#include "esp_log.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

static const char *TAG = "tab5_ui_recorder";

namespace {

struct AudioFileItem {
    std::string filename;
    std::string fullpath;
    size_t size_bytes;
    time_t mtime;
};

lv_obj_t *recorder_scr = nullptr;
ui_app_bar_t recorder_app_bar = {};
lv_obj_t *refresh_btn = nullptr;
lv_obj_t *refresh_label = nullptr;

lv_obj_t *main_container = nullptr;

/* Card de Gravacao */
lv_obj_t *rec_card = nullptr;
lv_obj_t *rec_title_label = nullptr;
lv_obj_t *rec_time_label = nullptr;
lv_obj_t *rec_action_btn = nullptr;
lv_obj_t *rec_action_label = nullptr;
lv_obj_t *rec_info_label = nullptr;

/* Card do Player */
lv_obj_t *play_card = nullptr;
lv_obj_t *play_file_label = nullptr;
lv_obj_t *play_bar = nullptr;
lv_obj_t *play_ctrl_row = nullptr;
lv_obj_t *play_toggle_btn = nullptr;
lv_obj_t *play_toggle_label = nullptr;
lv_obj_t *play_stop_btn = nullptr;
lv_obj_t *play_stop_label = nullptr;
lv_obj_t *play_time_label = nullptr;

/* Card da Lista */
lv_obj_t *list_card = nullptr;
lv_obj_t *list_title_label = nullptr;
lv_obj_t *list_container = nullptr;
lv_obj_t *empty_label = nullptr;

/* Modal de Exclusao */
lv_obj_t *confirm_modal = nullptr;
std::string s_file_to_delete;

std::vector<AudioFileItem> s_recordings;
lv_timer_t *s_update_timer = nullptr;
audio_recorder_state_t s_last_state = AUDIO_RECORDER_STATE_IDLE;
std::string s_active_file;

void scan_recordings_directory(void);
void render_recordings_list(void);
void show_delete_modal(const std::string &filepath, const std::string &filename);
void hide_delete_modal(void);
void apply_recorder_layout(void);
void apply_recorder_theme(void);

void format_time_mmss(uint32_t sec, char *buf, size_t buf_len)
{
    uint32_t m = sec / 60;
    uint32_t s = sec % 60;
    snprintf(buf, buf_len, "%02u:%02u", (unsigned)m, (unsigned)s);
}

void back_btn_cb(lv_event_t *event)
{
    (void)event;
    ui_shell_close_recorder();
}

void refresh_btn_cb(lv_event_t *event)
{
    (void)event;
    scan_recordings_directory();
    render_recordings_list();
}

void rec_action_btn_cb(lv_event_t *event)
{
    (void)event;
    audio_recorder_status_t st;
    audio_recorder_get_status(&st);

    if (st.state == AUDIO_RECORDER_STATE_RECORDING) {
        audio_recorder_stop_recording();
        scan_recordings_directory();
        render_recordings_list();
    } else {
        if (st.state == AUDIO_RECORDER_STATE_PLAYING || st.state == AUDIO_RECORDER_STATE_PAUSED) {
            audio_recorder_stop_playback();
        }
        char out_path[256] = {0};
        audio_recorder_start_recording(out_path, sizeof(out_path));
        s_active_file = out_path;
    }
}

void play_toggle_btn_cb(lv_event_t *event)
{
    (void)event;
    audio_recorder_status_t st;
    audio_recorder_get_status(&st);

    if (st.state == AUDIO_RECORDER_STATE_PLAYING) {
        audio_recorder_pause_playback();
    } else if (st.state == AUDIO_RECORDER_STATE_PAUSED) {
        audio_recorder_resume_playback();
    } else if (!s_active_file.empty()) {
        audio_recorder_start_playback(s_active_file.c_str());
    }
}

void play_stop_btn_cb(lv_event_t *event)
{
    (void)event;
    audio_recorder_stop_playback();
}

void play_item_cb(lv_event_t *event)
{
    const char *filepath = (const char *)lv_event_get_user_data(event);
    if (filepath) {
        s_active_file = filepath;
        audio_recorder_start_playback(filepath);
        const char *slash = strrchr(filepath, '/');
        const char *name = slash ? (slash + 1) : filepath;
        char buf[128];
        snprintf(buf, sizeof(buf), "Tocando: %s", name);
        lv_label_set_text(play_file_label, buf);
    }
}

void delete_item_cb(lv_event_t *event)
{
    const char *filepath = (const char *)lv_event_get_user_data(event);
    if (filepath) {
        const char *slash = strrchr(filepath, '/');
        const char *name = slash ? (slash + 1) : filepath;
        show_delete_modal(filepath, name);
    }
}

void modal_btn_cb(lv_event_t *event)
{
    const char *action = (const char *)lv_event_get_user_data(event);
    if (action != nullptr && strcmp(action, "delete") == 0) {
        if (!s_file_to_delete.empty()) {
            audio_recorder_status_t st;
            audio_recorder_get_status(&st);
            if (s_active_file == s_file_to_delete &&
                (st.state == AUDIO_RECORDER_STATE_PLAYING || st.state == AUDIO_RECORDER_STATE_PAUSED)) {
                audio_recorder_stop_playback();
                s_active_file.clear();
                lv_label_set_text(play_file_label, "Nenhum áudio em reprodução");
            }
            unlink(s_file_to_delete.c_str());
            ESP_LOGI(TAG, "Arquivo excluido: %s", s_file_to_delete.c_str());
            s_file_to_delete.clear();
            scan_recordings_directory();
            render_recordings_list();
        }
    }
    hide_delete_modal();
}

void show_delete_modal(const std::string &filepath, const std::string &filename)
{
    hide_delete_modal();
    s_file_to_delete = filepath;

    const ui_palette_t *pal = ui_theme_get();

    confirm_modal = lv_obj_create(recorder_scr);
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
    lv_label_set_text(title, "Excluir Gravação?");
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

void scan_recordings_directory(void)
{
    s_recordings.clear();
    const char *dir_path = "/sdcard/gravacoes";

    DIR *d = opendir(dir_path);
    if (!d) {
        mkdir(dir_path, 0755);
        d = opendir(dir_path);
    }

    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
                const char *dot = strrchr(entry->d_name, '.');
                if (dot && (strcasecmp(dot, ".wav") == 0 || strcasecmp(dot, ".pcm") == 0)) {
                    std::string fullpath = std::string(dir_path) + "/" + entry->d_name;
                    struct stat st;
                    size_t sz = 0;
                    time_t mt = 0;
                    if (stat(fullpath.c_str(), &st) == 0) {
                        sz = st.st_size;
                        mt = st.st_mtime;
                    }
                    s_recordings.push_back({entry->d_name, fullpath, sz, mt});
                }
            }
        }
        closedir(d);
    }

    /* Ordena decrescentemente (mais recente primeiro) */
    std::sort(s_recordings.begin(), s_recordings.end(), [](const AudioFileItem &a, const AudioFileItem &b) {
        if (a.mtime != b.mtime) {
            return a.mtime > b.mtime;
        }
        return a.filename > b.filename;
    });
}

void render_recordings_list(void)
{
    if (list_container == nullptr) {
        return;
    }

    lv_obj_clean(list_container);

    const ui_palette_t *pal = ui_theme_get();

    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "Gravações Salvas (%zu)", s_recordings.size());
    lv_label_set_text(list_title_label, count_buf);

    if (s_recordings.empty()) {
        empty_label = lv_label_create(list_container);
        lv_label_set_text(empty_label, "Nenhuma gravação em /sdcard/gravacoes/");
        lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_14_latin1, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(pal->text_muted), 0);
        lv_obj_set_style_pad_all(empty_label, 12, 0);
        return;
    }

    for (const auto &item : s_recordings) {
        lv_obj_t *row = lv_obj_create(list_container);
        lv_obj_set_size(row, lv_pct(100), 52);
        lv_obj_set_style_bg_color(row, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(pal->border), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        /* Icone e Nome */
        lv_obj_t *info_col = lv_obj_create(row);
        lv_obj_set_size(info_col, lv_pct(65), lv_pct(100));
        lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(info_col, 0, 0);
        lv_obj_set_style_pad_all(info_col, 0, 0);
        lv_obj_clear_flag(info_col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(info_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *lbl_name = lv_label_create(info_col);
        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), LV_SYMBOL_AUDIO " %s", item.filename.c_str());
        lv_label_set_text(lbl_name, name_buf);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_name, lv_pct(100));
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14_latin1, 0);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(pal->text), 0);

        lv_obj_t *lbl_sub = lv_label_create(info_col);
        char sz_buf[64];
        if (item.size_bytes < 1024 * 1024) {
            snprintf(sz_buf, sizeof(sz_buf), "%.1f KB", (float)item.size_bytes / 1024.0F);
        } else {
            snprintf(sz_buf, sizeof(sz_buf), "%.2f MB", (float)item.size_bytes / (1024.0F * 1024.0F));
        }
        lv_label_set_text(lbl_sub, sz_buf);
        lv_obj_set_style_text_color(lbl_sub, lv_color_hex(pal->text_muted), 0);

        /* Acoes: Play e Trash */
        lv_obj_t *actions_row = lv_obj_create(row);
        lv_obj_set_size(actions_row, lv_pct(32), lv_pct(100));
        lv_obj_set_style_bg_opa(actions_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(actions_row, 0, 0);
        lv_obj_set_style_pad_all(actions_row, 0, 0);
        lv_obj_clear_flag(actions_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(actions_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(actions_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *btn_p = lv_button_create(actions_row);
        lv_obj_set_size(btn_p, 40, 36);
        lv_obj_set_style_bg_color(btn_p, lv_color_hex(pal->accent_soft), 0);
        lv_obj_set_style_radius(btn_p, 6, 0);
        lv_obj_add_event_cb(btn_p, play_item_cb, LV_EVENT_CLICKED, (void *)item.fullpath.c_str());
        lv_obj_t *lbl_p = lv_label_create(btn_p);
        lv_label_set_text(lbl_p, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(lbl_p, lv_color_hex(pal->accent), 0);
        lv_obj_center(lbl_p);

        lv_obj_t *btn_t = lv_button_create(actions_row);
        lv_obj_set_size(btn_t, 40, 36);
        lv_obj_set_style_bg_color(btn_t, lv_color_hex(pal->surface_alt), 0);
        lv_obj_set_style_radius(btn_t, 6, 0);
        lv_obj_set_style_margin_left(btn_t, 6, 0);
        lv_obj_add_event_cb(btn_t, delete_item_cb, LV_EVENT_CLICKED, (void *)item.fullpath.c_str());
        lv_obj_t *lbl_t = lv_label_create(btn_t);
        lv_label_set_text(lbl_t, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(lbl_t, lv_color_hex(0xE53935), 0);
        lv_obj_center(lbl_t);
    }
}

void timer_status_cb(lv_timer_t *timer)
{
    (void)timer;

    if (rec_time_label == nullptr || play_time_label == nullptr) {
        return;
    }

    audio_recorder_status_t st;
    audio_recorder_get_status(&st);

    const ui_palette_t *pal = ui_theme_get();

    /* Atualizacao do Painel de Gravacao */
    if (st.state == AUDIO_RECORDER_STATE_RECORDING) {
        char time_buf[64];
        char cur_buf[16];
        char tot_buf[16];
        format_time_mmss(st.current_time_sec, cur_buf, sizeof(cur_buf));
        format_time_mmss(st.total_time_sec, tot_buf, sizeof(tot_buf));
        snprintf(time_buf, sizeof(time_buf), "%s / %s", cur_buf, tot_buf);
        lv_label_set_text(rec_time_label, time_buf);
        lv_obj_set_style_text_color(rec_time_label, lv_color_hex(0xE53935), 0);

        lv_label_set_text(rec_action_label, LV_SYMBOL_STOP "  Parar e Salvar");
        lv_obj_set_style_bg_color(rec_action_btn, lv_color_hex(0xE53935), 0);
        lv_obj_set_style_text_color(rec_action_label, lv_color_white(), 0);
    } else {
        if (s_last_state == AUDIO_RECORDER_STATE_RECORDING) {
            // Acabou de parar de gravar
            scan_recordings_directory();
            render_recordings_list();
        }
        lv_label_set_text(rec_time_label, "00:00 / 05:00");
        lv_obj_set_style_text_color(rec_time_label, lv_color_hex(pal->text), 0);

        lv_label_set_text(rec_action_label, LV_SYMBOL_AUDIO "  Gravar");
        lv_obj_set_style_bg_color(rec_action_btn, lv_color_hex(pal->accent), 0);
        lv_obj_set_style_text_color(rec_action_label, lv_color_white(), 0);
    }

    /* Atualizacao do Painel do Player */
    if (st.state == AUDIO_RECORDER_STATE_PLAYING || st.state == AUDIO_RECORDER_STATE_PAUSED) {
        char cur_buf[16];
        char tot_buf[16];
        char time_buf[64];
        format_time_mmss(st.current_time_sec, cur_buf, sizeof(cur_buf));
        format_time_mmss(st.total_time_sec, tot_buf, sizeof(tot_buf));
        snprintf(time_buf, sizeof(time_buf), "%s / %s", cur_buf, tot_buf);
        lv_label_set_text(play_time_label, time_buf);

        int32_t pct = (st.total_time_sec > 0) ? (st.current_time_sec * 100 / st.total_time_sec) : 0;
        if (pct > 100)
            pct = 100;
        lv_bar_set_value(play_bar, pct, LV_ANIM_OFF);

        if (st.state == AUDIO_RECORDER_STATE_PLAYING) {
            lv_label_set_text(play_toggle_label, LV_SYMBOL_PAUSE);
        } else {
            lv_label_set_text(play_toggle_label, LV_SYMBOL_PLAY);
        }
    } else {
        if (s_last_state == AUDIO_RECORDER_STATE_PLAYING || s_last_state == AUDIO_RECORDER_STATE_PAUSED) {
            // Acabou de encerrar a reproducao
            lv_bar_set_value(play_bar, 0, LV_ANIM_OFF);
            lv_label_set_text(play_time_label, "00:00 / 00:00");
            lv_label_set_text(play_toggle_label, LV_SYMBOL_PLAY);
        }
    }

    s_last_state = st.state;
}

void resolution_cb(lv_event_t *event)
{
    (void)event;
    apply_recorder_layout();
}

void apply_recorder_layout(void)
{
    if (recorder_scr == nullptr || main_container == nullptr || recorder_app_bar.bar == nullptr) {
        return;
    }

    int32_t height = lv_display_get_vertical_resolution(nullptr);
    if (height <= 0) {
        height = 720;
    }

    int32_t top = UI_BAR_HEIGHT;
    lv_obj_set_size(recorder_app_bar.bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_align(recorder_app_bar.bar, LV_ALIGN_TOP_MID, 0, top);

    int32_t content_top = top + UI_BAR_HEIGHT;
    int32_t container_h = std::max<int32_t>(height - content_top, 100);

    lv_obj_set_pos(main_container, 0, content_top);
    lv_obj_set_size(main_container, lv_pct(100), container_h);
}

void apply_recorder_theme(void)
{
    if (recorder_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(recorder_scr, lv_color_hex(pal->background), 0);

    ui_app_bar_refresh_theme(&recorder_app_bar);

    if (rec_card) {
        lv_obj_set_style_bg_color(rec_card, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(rec_card, lv_color_hex(pal->border), 0);
    }
    if (rec_title_label)
        lv_obj_set_style_text_color(rec_title_label, lv_color_hex(pal->text_muted), 0);
    if (rec_time_label && !audio_recorder_is_recording()) {
        lv_obj_set_style_text_color(rec_time_label, lv_color_hex(pal->text), 0);
    }
    if (rec_info_label)
        lv_obj_set_style_text_color(rec_info_label, lv_color_hex(pal->text_muted), 0);

    if (play_card) {
        lv_obj_set_style_bg_color(play_card, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(play_card, lv_color_hex(pal->border), 0);
    }
    if (play_file_label)
        lv_obj_set_style_text_color(play_file_label, lv_color_hex(pal->text), 0);
    if (play_bar) {
        uint32_t bar_track = ui_theme_is_dark() ? 0x2A3441 : 0xE2E8F0;
        uint32_t bar_border = ui_theme_is_dark() ? 0x475569 : 0xCBD5E1;
        lv_obj_set_style_bg_color(play_bar, lv_color_hex(bar_track), 0);
        lv_obj_set_style_border_color(play_bar, lv_color_hex(bar_border), 0);
        lv_obj_set_style_border_width(play_bar, 1, 0);
        lv_obj_set_style_bg_color(play_bar, lv_color_hex(pal->accent), LV_PART_INDICATOR);
    }
    if (play_toggle_btn)
        lv_obj_set_style_bg_color(play_toggle_btn, lv_color_hex(pal->accent_soft), 0);
    if (play_toggle_label)
        lv_obj_set_style_text_color(play_toggle_label, lv_color_hex(pal->accent), 0);
    if (play_stop_btn)
        lv_obj_set_style_bg_color(play_stop_btn, lv_color_hex(pal->surface_alt), 0);
    if (play_stop_label)
        lv_obj_set_style_text_color(play_stop_label, lv_color_hex(pal->text), 0);
    if (play_time_label)
        lv_obj_set_style_text_color(play_time_label, lv_color_hex(pal->text_muted), 0);

    if (list_card) {
        lv_obj_set_style_bg_color(list_card, lv_color_hex(pal->surface), 0);
        lv_obj_set_style_border_color(list_card, lv_color_hex(pal->border), 0);
    }
    if (list_title_label)
        lv_obj_set_style_text_color(list_title_label, lv_color_hex(pal->text), 0);

    render_recordings_list();
}

} // namespace

lv_obj_t *ui_recorder_create(void)
{
    recorder_scr = lv_obj_create(NULL);
    const ui_palette_t *pal = ui_theme_get();
    lv_obj_set_style_bg_color(recorder_scr, lv_color_hex(pal->background), 0);
    lv_obj_set_style_pad_all(recorder_scr, 0, 0);

    /* 1. Barra padronizada do Gravador com acao de Atualizar Lista */
    recorder_app_bar = ui_app_bar_create(recorder_scr, "Gravador de Voz", back_btn_cb, nullptr);
    refresh_btn =
        ui_app_bar_add_action_button(&recorder_app_bar, LV_SYMBOL_REFRESH, refresh_btn_cb, nullptr, &refresh_label);

    /* 2. Container Principal Rolavel */
    main_container = lv_obj_create(recorder_scr);
    lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_container, 0, 0);
    lv_obj_set_style_pad_all(main_container, 12, 0);
    lv_obj_set_scroll_dir(main_container, LV_DIR_VER);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Card 1: Gravador */
    rec_card = lv_obj_create(main_container);
    lv_obj_set_size(rec_card, lv_pct(100), 160);
    lv_obj_set_style_bg_color(rec_card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(rec_card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(rec_card, 1, 0);
    lv_obj_set_style_radius(rec_card, 12, 0);
    lv_obj_set_style_pad_all(rec_card, 10, 0);
    lv_obj_clear_flag(rec_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(rec_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rec_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    rec_title_label = lv_label_create(rec_card);
    lv_label_set_text(rec_title_label, "GRAVAÇÃO DE VOZ (MICROFONE)");
    lv_obj_set_style_text_font(rec_title_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(rec_title_label, lv_color_hex(pal->text_muted), 0);

    rec_time_label = lv_label_create(rec_card);
    lv_label_set_text(rec_time_label, "00:00 / 05:00");
    lv_obj_set_style_text_font(rec_time_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_set_style_text_color(rec_time_label, lv_color_hex(pal->text), 0);

    rec_action_btn = lv_button_create(rec_card);
    lv_obj_set_size(rec_action_btn, 220, 42);
    lv_obj_set_style_bg_color(rec_action_btn, lv_color_hex(pal->accent), 0);
    lv_obj_set_style_radius(rec_action_btn, 21, 0);
    lv_obj_add_event_cb(rec_action_btn, rec_action_btn_cb, LV_EVENT_CLICKED, nullptr);
    rec_action_label = lv_label_create(rec_action_btn);
    lv_label_set_text(rec_action_label, LV_SYMBOL_AUDIO "  Gravar");
    lv_obj_set_style_text_font(rec_action_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(rec_action_label, lv_color_white(), 0);
    lv_obj_center(rec_action_label);

    rec_info_label = lv_label_create(rec_card);
    lv_label_set_text(rec_info_label, "ES7210 - 16 kHz 16-bit WAV - Limite auto 5 min");
    lv_obj_set_style_text_font(rec_info_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(rec_info_label, lv_color_hex(pal->text_muted), 0);

    /* Card 2: Player de Audio */
    play_card = lv_obj_create(main_container);
    lv_obj_set_size(play_card, lv_pct(100), 120);
    lv_obj_set_style_bg_color(play_card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(play_card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(play_card, 1, 0);
    lv_obj_set_style_radius(play_card, 12, 0);
    lv_obj_set_style_pad_all(play_card, 10, 0);
    lv_obj_set_style_margin_top(play_card, 10, 0);
    lv_obj_clear_flag(play_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(play_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(play_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    play_file_label = lv_label_create(play_card);
    lv_label_set_text(play_file_label, "Nenhum áudio em reprodução");
    lv_label_set_long_mode(play_file_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(play_file_label, lv_pct(95));
    lv_obj_set_style_text_font(play_file_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(play_file_label, lv_color_hex(pal->text), 0);

    play_bar = lv_bar_create(play_card);
    lv_obj_set_size(play_bar, lv_pct(95), 10);
    lv_bar_set_range(play_bar, 0, 100);
    lv_bar_set_value(play_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(play_bar, 5, 0);
    lv_obj_set_style_radius(play_bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(play_bar, 1, 0);
    lv_obj_set_style_border_color(play_bar, lv_color_hex(ui_theme_is_dark() ? 0x475569 : 0xCBD5E1), 0);
    lv_obj_set_style_bg_color(play_bar, lv_color_hex(ui_theme_is_dark() ? 0x2A3441 : 0xE2E8F0), 0);
    lv_obj_set_style_bg_color(play_bar, lv_color_hex(pal->accent), LV_PART_INDICATOR);

    play_ctrl_row = lv_obj_create(play_card);
    lv_obj_set_size(play_ctrl_row, lv_pct(95), 36);
    lv_obj_set_style_bg_opa(play_ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(play_ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(play_ctrl_row, 0, 0);
    lv_obj_clear_flag(play_ctrl_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(play_ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(play_ctrl_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btns_cont = lv_obj_create(play_ctrl_row);
    lv_obj_set_size(btns_cont, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_set_style_bg_opa(btns_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btns_cont, 0, 0);
    lv_obj_set_style_pad_all(btns_cont, 0, 0);
    lv_obj_clear_flag(btns_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btns_cont, LV_FLEX_FLOW_ROW);

    play_toggle_btn = lv_button_create(btns_cont);
    lv_obj_set_size(play_toggle_btn, 38, 32);
    lv_obj_set_style_bg_color(play_toggle_btn, lv_color_hex(pal->accent_soft), 0);
    lv_obj_set_style_radius(play_toggle_btn, 6, 0);
    lv_obj_add_event_cb(play_toggle_btn, play_toggle_btn_cb, LV_EVENT_CLICKED, nullptr);
    play_toggle_label = lv_label_create(play_toggle_btn);
    lv_label_set_text(play_toggle_label, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_toggle_label, lv_color_hex(pal->accent), 0);
    lv_obj_center(play_toggle_label);

    play_stop_btn = lv_button_create(btns_cont);
    lv_obj_set_size(play_stop_btn, 38, 32);
    lv_obj_set_style_bg_color(play_stop_btn, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_radius(play_stop_btn, 6, 0);
    lv_obj_set_style_margin_left(play_stop_btn, 8, 0);
    lv_obj_add_event_cb(play_stop_btn, play_stop_btn_cb, LV_EVENT_CLICKED, nullptr);
    play_stop_label = lv_label_create(play_stop_btn);
    lv_label_set_text(play_stop_label, LV_SYMBOL_STOP);
    lv_obj_set_style_text_color(play_stop_label, lv_color_hex(pal->text), 0);
    lv_obj_center(play_stop_label);

    play_time_label = lv_label_create(play_ctrl_row);
    lv_label_set_text(play_time_label, "00:00 / 00:00");
    lv_obj_set_style_text_font(play_time_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(play_time_label, lv_color_hex(pal->text_muted), 0);

    /* Card 3: Lista de Gravacoes */
    list_card = lv_obj_create(main_container);
    lv_obj_set_size(list_card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(list_card, 220, 0);
    lv_obj_set_style_bg_color(list_card, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_border_color(list_card, lv_color_hex(pal->border), 0);
    lv_obj_set_style_border_width(list_card, 1, 0);
    lv_obj_set_style_radius(list_card, 12, 0);
    lv_obj_set_style_pad_all(list_card, 10, 0);
    lv_obj_set_style_margin_top(list_card, 10, 0);
    lv_obj_clear_flag(list_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(list_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    list_title_label = lv_label_create(list_card);
    lv_label_set_text(list_title_label, "Gravações Salvas (0)");
    lv_obj_set_style_text_font(list_title_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_set_style_text_color(list_title_label, lv_color_hex(pal->text), 0);

    list_container = lv_obj_create(list_card);
    lv_obj_set_size(list_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(list_container, 160, 0);
    lv_obj_set_style_bg_opa(list_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_container, 0, 0);
    lv_obj_set_style_pad_all(list_container, 4, 0);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    apply_recorder_layout();
    lv_display_add_event_cb(lv_display_get_default(), resolution_cb, LV_EVENT_RESOLUTION_CHANGED, nullptr);

    s_update_timer = lv_timer_create(timer_status_cb, 200, nullptr);

    return recorder_scr;
}

void ui_recorder_on_open(void)
{
    audio_recorder_init();
    apply_recorder_layout();
    scan_recordings_directory();
    render_recordings_list();
    if (s_update_timer) {
        lv_timer_resume(s_update_timer);
    }
}

void ui_recorder_on_close(void)
{
    audio_recorder_stop_recording();
    audio_recorder_stop_playback();
    hide_delete_modal();
    if (s_update_timer) {
        lv_timer_pause(s_update_timer);
    }
}

void ui_recorder_open_file(const char *filepath)
{
    if (filepath == nullptr) {
        return;
    }
    s_active_file = filepath;
    scan_recordings_directory();
    render_recordings_list();

    const char *slash = strrchr(filepath, '/');
    const char *name = slash ? (slash + 1) : filepath;
    char buf[128];
    snprintf(buf, sizeof(buf), "Tocando: %s", name);
    if (play_file_label) {
        lv_label_set_text(play_file_label, buf);
    }

    audio_recorder_start_playback(filepath);
}

void ui_recorder_refresh_theme(void)
{
    apply_recorder_theme();
}

void ui_recorder_apply_layout(void)
{
    apply_recorder_layout();
}

void ui_recorder_register(void)
{
    static const char *s_recorder_extensions[] = {"wav", "pcm", nullptr};
    static const app_desc_t s_recorder_desc = {
        .id = "recorder",
        .name = "Gravador",
        .icon_symbol = LV_SYMBOL_AUDIO,
        .icon_builder = nullptr,
        .icon_theme_refresh = nullptr,
        .on_launch = ui_shell_open_recorder,
        .file_extensions = s_recorder_extensions,
        .on_open_file = ui_shell_open_recorder_with_file,
    };
    app_registry_register(&s_recorder_desc);
}
