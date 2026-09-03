/**
 * @file main.c
 * @brief Exemplo Hello Wasm
 */

#include "tab5_sdk.h"

static void on_init(void)
{
    tab5_system_log(2, "hello_wasm", "Hello Wasm carregado e executando no WAMR!");
    tab5_ui_app_bar_set_title("Hello Wasm");
    tab5_ui_show_toast("Hello from isolated WebAssembly!", 3000);
}

TAB5_APP_EXPORT int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tab5_lifecycle_callbacks_t cbs = {
        .on_init = on_init,
        .on_resume = NULL,
        .on_pause = NULL,
        .on_destroy = NULL,
        .on_open_file = NULL,
    };

    tab5_lifecycle_register(&cbs);
    return 0;
}
