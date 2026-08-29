/**
 * @file main.c
 * @brief Exemplo Notas Wasm com Sandbox de Armazenamento
 */

#include "tab5_sdk.h"
#include <stdio.h>
#include <string.h>

static void save_action_cb(void *user_data)
{
    (void)user_data;
    char path[128];
    tab5_err_t err = tab5_storage_path_resolve("quick_note.txt", path, sizeof(path), true);
    if (err == TAB5_OK) {
        tab5_system_log(2, "notes_wasm", "Salvando nota em sandbox...");
        tab5_ui_show_toast("Nota salva com sucesso na sandbox!", 2000);
        tab5_sound_play_beep(1500, 100);
    } else {
        tab5_ui_show_toast("Erro de permissao ao salvar nota.", 2500);
    }
}

static void on_init(void)
{
    tab5_system_log(2, "notes_wasm", "Notas Wasm inicializada");
    tab5_ui_app_bar_set_title("Notas Wasm");
    tab5_ui_app_bar_add_action_button("LV_SYMBOL_SAVE", save_action_cb, NULL);
    tab5_ui_show_toast("Notas Wasm aberta", 1500);
}

static void on_open_file(const char *filepath)
{
    if (filepath != NULL) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Abrindo arquivo: %s", filepath);
        tab5_system_log(2, "notes_wasm", msg);
        tab5_ui_show_toast(msg, 3000);
    }
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
        .on_open_file = on_open_file,
    };

    tab5_lifecycle_register(&cbs);
    return 0;
}
