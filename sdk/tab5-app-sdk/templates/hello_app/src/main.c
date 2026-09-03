/**
 * @file main.c
 * @brief Exemplo de Aplicação Hello World para Tab5 OS
 */

#include "tab5_sdk.h"
#include <stdio.h>

static void app_init(void)
{
    tab5_system_log(2, "hello_app", "Aplicacao Hello Tab5 inicializada com sucesso!");
    tab5_ui_app_bar_set_title("Hello Tab5");
    tab5_ui_show_toast("Bem-vindo ao Tab5 OS!", 2500);
    tab5_sound_play_beep(1200, 150);
}

static void app_resume(void)
{
    tab5_system_log(2, "hello_app", "Aplicacao retornou ao primeiro plano");
}

static void app_pause(void)
{
    tab5_system_log(2, "hello_app", "Aplicacao pausada");
}

static void app_destroy(void)
{
    tab5_system_log(2, "hello_app", "Aplicacao finalizada");
}

TAB5_APP_EXPORT int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tab5_lifecycle_callbacks_t cbs = {
        .on_init = app_init,
        .on_resume = app_resume,
        .on_pause = app_pause,
        .on_destroy = app_destroy,
        .on_open_file = NULL,
    };

    tab5_lifecycle_register(&cbs);
    return 0;
}
