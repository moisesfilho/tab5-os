#include "ui_shell.h"
#include "ui_bar.h"
#include "ui_keyboard.h"
#include "ui_desktop.h"
#include "ui_chat_view.h"
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
lv_obj_t *chat_scr = nullptr;
lv_obj_t *chat_caller_scr = nullptr;
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
    if (scr == chat_scr) {
        ui_chat_on_close();
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

    /* Cada aplicacao registra seu manifesto (icone, launch, extensoes) no SO.
     * Apps migrados para pacotes WASM sao registrados pelo tab5_package_mgr.
     * Apenas Armazenamento continua nativo (sem pacote equivalente). */
    ui_storage_view_register();

    /* Varre pacotes embutidos e instalados no SD */
    tab5_package_mgr_scan_and_register_all();

    /* Cria a area de trabalho dinamicamente a partir dos apps registrados */
    ui_desktop_create(desktop_scr);
    chat_scr = ui_chat_create();
    storage_scr = ui_storage_view_create();
    ui_screensaver_init();
    ui_screen_off_init();

    lv_timer_create(inactivity_timer_cb, 1000, nullptr);

    splash_start();
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
    ui_chat_refresh_theme();
    ui_storage_view_refresh_theme();
    tab5_ui_host_refresh_theme();

    if (splash != nullptr) {
        lv_obj_set_style_bg_color(splash, lv_color_hex(ui_theme_get()->background), 0);
        lv_obj_set_style_text_color(splash_label, lv_color_hex(ui_theme_get()->text), 0);
    }
}

void ui_shell_notify_keyboard_layout(void)
{
    lv_obj_t *act = lv_disp_get_scr_act(NULL);
    if (act == storage_scr) {
        ui_storage_view_apply_layout();
    } else {
        tab5_ui_host_apply_layout();
    }
}
