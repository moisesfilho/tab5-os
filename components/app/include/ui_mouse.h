#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inicializa o driver de entrada de ponteiro (LV_INDEV_TYPE_POINTER) e o cursor visual */
void ui_mouse_init(void);

/* Injeta movimento relativo e estado de botoes do mouse/touchpad */
void ui_mouse_inject_motion(int8_t dx, int8_t dy, uint8_t buttons, int8_t wheel);

/* Injeta um clique instantaneo (tap-to-click) no LVGL */
void ui_mouse_inject_click(void);

/* Notifica conexao ou desconexao de um mouse/touchpad para exibir/ocultar cursor */
void ui_mouse_set_connected(bool connected);

/* Retorna se o mouse/touchpad esta conectado e ativo */
bool ui_mouse_is_connected(void);

#ifdef __cplusplus
}
#endif
