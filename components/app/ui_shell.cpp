#include "ui_shell.h"
#include "ui_bar.h"
#include "ui_keyboard.h"
#include "ui_desktop.h"
#include "ui_notas.h"
#include "ui_wifi.h"
#include "ui_files.h"
#include "ui_bluetooth.h"
#include "ui_theme.h"
#include "ui_font.h"
#include "file_assoc.h"

namespace {

lv_obj_t *desktop_scr = nullptr;
lv_obj_t *notas_scr = nullptr;
lv_obj_t *notas_caller_scr = nullptr;
lv_obj_t *wifi_scr = nullptr;
lv_obj_t *files_scr = nullptr;
lv_obj_t *bt_scr = nullptr;
lv_obj_t *splash = nullptr;
lv_obj_t *splash_label = nullptr;

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

} // namespace

void ui_shell_init(void)
{
    desktop_scr = lv_disp_get_scr_act(NULL);

    /* Barra do sistema e teclado vivem no layer top, acima de qualquer
     * tela; sao criados uma unica vez. */
    ui_bar_init(lv_layer_top());
    ui_keyboard_create(lv_layer_top());

    file_assoc_init();

    ui_desktop_create(desktop_scr);
    notas_scr = ui_notas_create();
    wifi_scr = ui_wifi_create();
    files_scr = ui_files_create();
    bt_scr = ui_bluetooth_create();

    splash_start();
}

void ui_shell_open_notas(void)
{
    ui_keyboard_hide();
    notas_caller_scr = desktop_scr;
    lv_disp_load_scr(notas_scr);
    ui_notas_on_open();
}

void ui_shell_open_notas_with_file(const char *filepath)
{
    ui_keyboard_hide();
    /* Salva a tela atualmente ativa (ex: files_scr) para retorno ao fechar */
    lv_obj_t *act = lv_disp_get_scr_act(NULL);
    notas_caller_scr = (act != nullptr && act != notas_scr) ? act : desktop_scr;
    ui_notas_open_file(filepath);
    lv_disp_load_scr(notas_scr);
    ui_notas_on_open();
}

void ui_shell_close_notas(void)
{
    ui_keyboard_hide();
    lv_obj_t *target = (notas_caller_scr != nullptr) ? notas_caller_scr : desktop_scr;
    notas_caller_scr = nullptr;
    lv_disp_load_scr(target);
}

void ui_shell_open_wifi(void)
{
    ui_keyboard_hide();
    lv_disp_load_scr(wifi_scr);
}

void ui_shell_close_wifi(void)
{
    ui_keyboard_hide();
    lv_disp_load_scr(desktop_scr);
}

void ui_shell_open_files(void)
{
    ui_keyboard_hide();
    ui_files_open_path("/sdcard");
    lv_disp_load_scr(files_scr);
}

void ui_shell_close_files(void)
{
    ui_keyboard_hide();
    lv_disp_load_scr(desktop_scr);
}

void ui_shell_open_bluetooth(void)
{
    ui_keyboard_hide();
    lv_disp_load_scr(bt_scr);
}

void ui_shell_close_bluetooth(void)
{
    ui_keyboard_hide();
    lv_disp_load_scr(desktop_scr);
}

void ui_shell_refresh_theme(void)
{
    ui_desktop_refresh_theme();
    ui_notas_refresh_theme();
    ui_wifi_refresh_theme();
    ui_files_refresh_theme();
    ui_bluetooth_refresh_theme();

    if (splash != nullptr) {
        lv_obj_set_style_bg_color(splash, lv_color_hex(ui_theme_get()->background), 0);
        lv_obj_set_style_text_color(splash_label, lv_color_hex(ui_theme_get()->text), 0);
    }
}

void ui_shell_notify_keyboard_layout(void)
{
    lv_obj_t *act = lv_disp_get_scr_act(NULL);
    if (act == notas_scr) {
        ui_notas_apply_layout();
    } else if (act == wifi_scr) {
        ui_wifi_apply_layout();
    } else if (act == files_scr) {
        ui_files_apply_layout();
    } else if (act == bt_scr) {
        ui_bluetooth_apply_layout();
    }
}
