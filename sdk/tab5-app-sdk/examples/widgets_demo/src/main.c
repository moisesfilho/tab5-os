/**
 * @file main.c
 * @brief Aplicação de Demonstração de Widgets Genéricos para Tab5 OS
 */

#include "tab5_sdk.h"
#include <stdio.h>

static tab5_ui_obj_t s_lbl_status = TAB5_UI_INVALID_OBJ;
static tab5_ui_obj_t s_slider = TAB5_UI_INVALID_OBJ;

static void on_ui_event(tab5_ui_obj_t obj, uint32_t event_type, int32_t event_val)
{
    char buf[64];
    if (event_type == TAB5_UI_EVENT_CLICKED) {
        tab5_system_log(2, "widgets_demo", "Botão clicado!");
        tab5_ui_label_set_text(s_lbl_status, "Status: Botao Clicado");
        tab5_sound_play_beep(1500, 80);
    } else if (event_type == TAB5_UI_EVENT_VALUE_CHANGED) {
        snprintf(buf, sizeof(buf), "Status: Valor = %d", (int)event_val);
        tab5_ui_label_set_text(s_lbl_status, buf);
    }
}

static void init_ui_widgets(void)
{
    tab5_system_log(2, "widgets_demo", "Inicializando UI com widgets desacoplados");
    tab5_ui_app_bar_set_title("Widgets Demo");

    tab5_ui_obj_t scr = tab5_ui_get_screen();

    // Contêiner principal com layout flexível em coluna
    tab5_ui_obj_t cont = tab5_ui_container_create(scr);
    tab5_ui_obj_set_size(cont, TAB5_UI_PCT(95), TAB5_UI_PCT(80));
    tab5_ui_obj_set_align(cont, TAB5_UI_ALIGN_TOP_MID, 0, 90);
    tab5_ui_obj_set_flex_flow(cont, TAB5_UI_FLEX_FLOW_COLUMN);
    tab5_ui_obj_set_pad(cont, 16);
    tab5_ui_obj_set_gap(cont, 12);

    // Label de status
    s_lbl_status = tab5_ui_label_create(cont, "Status: Pronto");

    // Botão interativo
    tab5_ui_obj_t btn = tab5_ui_btn_create(cont, "Pressione Aqui");
    tab5_ui_obj_set_size(btn, 160, 44);

    // Switch
    tab5_ui_obj_t sw = tab5_ui_switch_create(cont);
    tab5_ui_switch_set_state(sw, true);

    // Slider
    s_slider = tab5_ui_slider_create(cont, 0, 100);
    tab5_ui_slider_set_value(s_slider, 50);
    tab5_ui_obj_set_size(s_slider, 200, 20);

    // Lista de itens
    tab5_ui_obj_t list = tab5_ui_list_create(cont);
    tab5_ui_obj_set_size(list, TAB5_UI_PCT(100), 120);
    tab5_ui_list_add_btn(list, "LV_SYMBOL_OK", "Item A");
    tab5_ui_list_add_btn(list, "LV_SYMBOL_FILE", "Item B");

    tab5_ui_show_toast("Widgets Demo Carregado!", 2000);
}

TAB5_APP_EXPORT void tab5_app_on_ui_event(tab5_ui_obj_t obj, uint32_t event_type, int32_t event_val)
{
    on_ui_event(obj, event_type, event_val);
}

TAB5_APP_EXPORT void tab5_app_on_theme_changed(bool dark)
{
    (void)dark;
    tab5_ui_clear_content();
    init_ui_widgets();
}

TAB5_APP_EXPORT int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tab5_system_log(2, "widgets_demo", "main() invocado");
    init_ui_widgets();
    return 0;
}
