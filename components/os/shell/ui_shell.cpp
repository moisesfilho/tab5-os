#include "ui_shell.h"
#include "ui_bar.h"
#include "ui_keyboard.h"
#include "ui_desktop.h"
#include "ui_wifi.h"
#include "ui_files.h"
#include "ui_bluetooth.h"
#include "ui_terminal.h"
#include "ui_camera.h"
#include "ui_gallery.h"
#include "ui_fileserver.h"
#include "ui_recorder.h"
#include "ui_chat.h"
#include "ui_music.h"
#include "ui_calendar.h"
#include "ui_screensaver.h"
#include "ui_screen_off.h"
#include "ui_theme.h"
#include "ui_font.h"
#include "app_registry.h"
#include "file_assoc.h"
#include "ui_installer.h"
#include "ui_storage_view.h"
#include "tab5_package_mgr.h"
#include "tab5_ui_host.h"

namespace {

lv_obj_t *desktop_scr = nullptr;
lv_obj_t *wifi_scr = nullptr;
lv_obj_t *files_scr = nullptr;
lv_obj_t *bt_scr = nullptr;
lv_obj_t *terminal_scr = nullptr;
lv_obj_t *camera_scr = nullptr;
lv_obj_t *gallery_scr = nullptr;
lv_obj_t *gallery_caller_scr = nullptr;
lv_obj_t *fileserver_scr = nullptr;
lv_obj_t *recorder_scr = nullptr;
lv_obj_t *recorder_caller_scr = nullptr;
lv_obj_t *chat_scr = nullptr;
lv_obj_t *chat_caller_scr = nullptr;
lv_obj_t *music_scr = nullptr;
lv_obj_t *music_caller_scr = nullptr;
lv_obj_t *calendar_scr = nullptr;
lv_obj_t *storage_scr = nullptr;
lv_obj_t *storage_caller_scr = nullptr;
lv_obj_t *splash = nullptr;
lv_obj_t *splash_label = nullptr;

void inactivity_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_screen_off_check_inactivity();
    if (!ui_screen_off_is_active()) {
        ui_screensaver_check_inactivity();
    }
}

void splash_timer_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);

    if (splash != nullptr) {
        lv_obj_delete(splash);
        splash = nullptr;
        splash_label = nullptr;
    }
}

/* Overlay acima de tudo (inclusive da barra) com o nome do sistema. */
void splash_start(void)
{
    splash = lv_obj_create(lv_layer_top());
    lv_obj_set_size(splash, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(splash, lv_color_hex(ui_theme_get()->background), 0);
    lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash, 0, 0);
    lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);

    splash_label = lv_label_create(splash);
    lv_label_set_text(splash_label, "tab5_os");
    lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_28_latin1, 0);
    lv_obj_set_style_text_color(splash_label, lv_color_hex(ui_theme_get()->text), 0);
    lv_obj_center(splash_label);

    lv_timer_create(splash_timer_cb, 1500, nullptr);
}

/* Dispara o on_close do app cuja tela esta saindo. Apps sem recurso pesado
 * (notas, wifi, files, bluetooth) nao possuem on_close e sao ignorados. */
void notify_app_closed(lv_obj_t *scr)
{
    if (scr == terminal_scr) {
        ui_terminal_on_close();
    } else if (scr == camera_scr) {
        ui_camera_on_close();
    } else if (scr == gallery_scr) {
        ui_gallery_on_close();
    } else if (scr == fileserver_scr) {
        ui_fileserver_on_close();
    } else if (scr == recorder_scr) {
        ui_recorder_on_close();
    } else if (scr == chat_scr) {
        ui_chat_on_close();
    } else if (scr == music_scr) {
        ui_music_on_close();
    }
}

/* Unica porta de troca de tela: notifica o app ativo antes de carregar outra
 * tela, cobrindo navegacao via botao fechar, icones de status e with_file. */
void shell_load_scr(lv_obj_t *target)
{
    if (target == nullptr) {
        target = desktop_scr;
    }
    lv_obj_t *act = lv_disp_get_scr_act(NULL);
    if (act != nullptr && act != target) {
        notify_app_closed(act);
    }
    lv_disp_load_scr(target);
}

} // namespace

void ui_shell_init(void)
{
    desktop_scr = lv_disp_get_scr_act(NULL);

    /* Barra do sistema e teclado vivem no layer top, acima de qualquer
     * tela; sao criados uma unica vez. */
    ui_bar_init(lv_layer_top());
    ui_keyboard_create(lv_layer_top());

    /* Inicializa subsistemas de registro e associacao de arquivos */
    app_registry_init();
    file_assoc_init();
    tab5_package_mgr_init();
    ui_installer_init();

    /* Cada aplicacao registra seu manifesto (icone, launch, extensoes) no SO */
    ui_wifi_register();
    ui_files_register();
    ui_bluetooth_register();
    ui_terminal_register();
    ui_camera_register();
    ui_gallery_register();
    ui_fileserver_register();
    ui_recorder_register();
    ui_chat_register();
    ui_music_register();
    ui_calendar_register();
    ui_storage_view_register();

    /* Varre pacotes embutidos e instalados no SD */
    tab5_package_mgr_scan_and_register_all();

    /* Cria a area de trabalho dinamicamente a partir dos apps registrados */
    ui_desktop_create(desktop_scr);
    wifi_scr = ui_wifi_create();
    files_scr = ui_files_create();
    bt_scr = ui_bluetooth_create();
    terminal_scr = ui_terminal_create();
    camera_scr = ui_camera_create();
    gallery_scr = ui_gallery_create();
    fileserver_scr = ui_fileserver_create();
    recorder_scr = ui_recorder_create();
    chat_scr = ui_chat_create();
    music_scr = ui_music_create();
    calendar_scr = ui_calendar_create();
    storage_scr = ui_storage_view_create();
    ui_screensaver_init();
    ui_screen_off_init();

    lv_timer_create(inactivity_timer_cb, 1000, nullptr);

    splash_start();
}

void ui_shell_open_wifi(void)
{
    ui_keyboard_hide();
    shell_load_scr(wifi_scr);
}

void ui_shell_close_wifi(void)
{
    ui_keyboard_hide();
    shell_load_scr(desktop_scr);
}

void ui_shell_open_files(void)
{
    ui_keyboard_hide();
    ui_files_open_path("/sdcard");
    shell_load_scr(files_scr);
}

void ui_shell_close_files(void)
{
    ui_keyboard_hide();
    shell_load_scr(desktop_scr);
}

void ui_shell_open_bluetooth(void)
{
    ui_keyboard_hide();
    shell_load_scr(bt_scr);
}

void ui_shell_close_bluetooth(void)
{
    ui_keyboard_hide();
    shell_load_scr(desktop_scr);
}

void ui_shell_open_terminal(void)
{
    ui_keyboard_hide();
    shell_load_scr(terminal_scr);
    ui_terminal_apply_layout();
}

void ui_shell_close_terminal(void)
{
    ui_keyboard_hide();
    shell_load_scr(desktop_scr);
}

void ui_shell_open_camera(void)
{
    ui_keyboard_hide();
    shell_load_scr(camera_scr);
    ui_camera_on_open();
}

void ui_shell_close_camera(void)
{
    ui_keyboard_hide();
    shell_load_scr(desktop_scr);
}

void ui_shell_open_gallery(void)
{
    ui_keyboard_hide();
    gallery_caller_scr = desktop_scr;
    shell_load_scr(gallery_scr);
    ui_gallery_on_open();
}

void ui_shell_open_gallery_with_file(const char *filepath)
{
    ui_keyboard_hide();
    lv_obj_t *act = lv_disp_get_scr_act(NULL);
    gallery_caller_scr = (act != nullptr && act != gallery_scr) ? act : desktop_scr;
    ui_gallery_open_file(filepath);
    shell_load_scr(gallery_scr);
}

void ui_shell_close_gallery(void)
{
    ui_keyboard_hide();
    lv_obj_t *target = (gallery_caller_scr != nullptr) ? gallery_caller_scr : desktop_scr;
    gallery_caller_scr = nullptr;
    shell_load_scr(target);
}

void ui_shell_open_fileserver(void)
{
    ui_keyboard_hide();
    shell_load_scr(fileserver_scr);
    ui_fileserver_on_open();
}

void ui_shell_close_fileserver(void)
{
    ui_keyboard_hide();
    shell_load_scr(desktop_scr);
}

void ui_shell_open_recorder(void)
{
    ui_keyboard_hide();
    recorder_caller_scr = desktop_scr;
    shell_load_scr(recorder_scr);
    ui_recorder_on_open();
}

void ui_shell_open_recorder_with_file(const char *filepath)
{
    ui_keyboard_hide();
    lv_obj_t *act = lv_disp_get_scr_act(NULL);
    recorder_caller_scr = (act != nullptr && act != recorder_scr) ? act : desktop_scr;
    ui_recorder_open_file(filepath);
    shell_load_scr(recorder_scr);
    ui_recorder_on_open();
}

void ui_shell_close_recorder(void)
{
    ui_keyboard_hide();
    lv_obj_t *target = (recorder_caller_scr != nullptr) ? recorder_caller_scr : desktop_scr;
    recorder_caller_scr = nullptr;
    shell_load_scr(target);
}

void ui_shell_open_chat(void)
{
    ui_keyboard_hide();
    chat_caller_scr = desktop_scr;
    shell_load_scr(chat_scr);
    ui_chat_on_open();
}

void ui_shell_close_chat(void)
{
    ui_keyboard_hide();
    lv_obj_t *target = (chat_caller_scr != nullptr) ? chat_caller_scr : desktop_scr;
    chat_caller_scr = nullptr;
    shell_load_scr(target);
}

void ui_shell_open_music(void)
{
    ui_keyboard_hide();
    music_caller_scr = desktop_scr;
    shell_load_scr(music_scr);
    ui_music_on_open();
}

void ui_shell_open_music_with_file(const char *filepath)
{
    ui_keyboard_hide();
    lv_obj_t *act = lv_disp_get_scr_act(NULL);
    music_caller_scr = (act != nullptr && act != music_scr) ? act : desktop_scr;
    ui_music_open_file(filepath);
    shell_load_scr(music_scr);
}

void ui_shell_close_music(void)
{
    ui_keyboard_hide();
    lv_obj_t *target = (music_caller_scr != nullptr) ? music_caller_scr : desktop_scr;
    music_caller_scr = nullptr;
    shell_load_scr(target);
}

void ui_shell_open_calendar(void)
{
    ui_keyboard_hide();
    shell_load_scr(calendar_scr);
    ui_calendar_on_open();
}

void ui_shell_close_calendar(void)
{
    ui_keyboard_hide();
    shell_load_scr(desktop_scr);
}

void ui_shell_open_storage(void)
{
    ui_keyboard_hide();
    storage_caller_scr = desktop_scr;
    shell_load_scr(storage_scr);
    ui_storage_view_on_open();
}

void ui_shell_close_storage(void)
{
    ui_keyboard_hide();
    lv_obj_t *target = (storage_caller_scr != nullptr) ? storage_caller_scr : desktop_scr;
    storage_caller_scr = nullptr;
    shell_load_scr(target);
}

void ui_shell_refresh_theme(void)
{
    ui_desktop_refresh_theme();
    ui_wifi_refresh_theme();
    ui_files_refresh_theme();
    ui_bluetooth_refresh_theme();
    ui_terminal_refresh_theme();
    ui_camera_refresh_theme();
    ui_gallery_refresh_theme();
    ui_fileserver_refresh_theme();
    ui_recorder_refresh_theme();
    ui_chat_refresh_theme();
    ui_music_refresh_theme();
    ui_calendar_refresh_theme();
    ui_storage_view_refresh_theme();

    if (splash != nullptr) {
        lv_obj_set_style_bg_color(splash, lv_color_hex(ui_theme_get()->background), 0);
        lv_obj_set_style_text_color(splash_label, lv_color_hex(ui_theme_get()->text), 0);
    }
}

void ui_shell_notify_keyboard_layout(void)
{
    lv_obj_t *act = lv_disp_get_scr_act(NULL);
    if (act == wifi_scr) {
        ui_wifi_apply_layout();
    } else if (act == files_scr) {
        ui_files_apply_layout();
    } else if (act == bt_scr) {
        ui_bluetooth_apply_layout();
    } else if (act == terminal_scr) {
        ui_terminal_apply_layout();
    } else if (act == camera_scr) {
        ui_camera_apply_layout();
    } else if (act == gallery_scr) {
        ui_gallery_apply_layout();
    } else if (act == fileserver_scr) {
        ui_fileserver_apply_layout();
    } else if (act == recorder_scr) {
        ui_recorder_apply_layout();
    } else if (act == chat_scr) {
        ui_chat_apply_layout();
    } else if (act == music_scr) {
        ui_music_apply_layout();
    } else if (act == calendar_scr) {
        ui_calendar_apply_layout();
    } else if (act == storage_scr) {
        ui_storage_view_apply_layout();
    } else {
        tab5_ui_host_apply_layout();
    }
}
