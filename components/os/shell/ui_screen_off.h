#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inicializa o módulo de desligamento de tela e cria a tela dedicada */
void ui_screen_off_init(void);

/* Ativa o desligamento de tela (backlight 0 + tela preta) */
void ui_screen_off_show(void);

/* Desativa o desligamento de tela e restaura brilho e tela anterior */
void ui_screen_off_hide(void);

/* Retorna se a tela está atualmente desligada */
bool ui_screen_off_is_active(void);

/* Configura o timeout de inatividade em segundos (0 = desativado) */
void ui_screen_off_set_timeout(uint32_t seconds);

/* Retorna o timeout de inatividade configurado em segundos (0 = desativado) */
uint32_t ui_screen_off_get_timeout(void);

/* Verifica periodicamente a inatividade do LVGL e desliga a tela se necessário */
void ui_screen_off_check_inactivity(void);

/* Notifica o módulo de atividade externa (mouse, teclado, etc.) para despertar */
void ui_screen_off_wake_up(void);

#ifdef __cplusplus
}
#endif
