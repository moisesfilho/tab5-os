#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inicializa o módulo de screensaver e cria a tela dedicada */
void ui_screensaver_init(void);

/* Ativa o protetor de tela */
void ui_screensaver_show(void);

/* Desativa o protetor de tela e restaura a tela anterior */
void ui_screensaver_hide(void);

/* Retorna se o screensaver está atualmente ativo na tela */
bool ui_screensaver_is_active(void);

/* Configura o timeout de inatividade em segundos (0 = desativado) */
void ui_screensaver_set_timeout(uint32_t seconds);

/* Retorna o timeout de inatividade configurado em segundos (0 = desativado) */
uint32_t ui_screensaver_get_timeout(void);

/* Verifica periodicamente a inatividade do LVGL e dispara o screensaver se necessário */
void ui_screensaver_check_inactivity(void);

/* Notifica o screensaver de atividade externa (mouse, teclado, etc.) para despertar */
void ui_screensaver_wake_up(void);

#ifdef __cplusplus
}
#endif
