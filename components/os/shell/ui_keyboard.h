#pragma once

#include "lvgl.h"

void ui_keyboard_create(lv_obj_t *parent);
void ui_keyboard_attach(lv_obj_t *ta);
void ui_keyboard_hide(void);
void ui_keyboard_refresh_theme(void);
bool ui_keyboard_is_visible(void);
int32_t ui_keyboard_get_height(void);
void ui_keyboard_notify_hardware_change(void);
void ui_keyboard_inject_char(char c);
void ui_keyboard_inject_key(uint32_t key);

/* Injeta uma tecla com byte de modificador (bit0=Ctrl, bit2=Alt).
 * Quando o modificador estiver presente, a tecla e entregue como
 * LV_EVENT_KEY ao objeto focado para que widgets/apps possam responder
 * a atalhos. Sem modificador, delega para ui_keyboard_inject_key(). */
void ui_keyboard_inject_key_ex(uint32_t key, uint8_t modifier);

/* True se existe um teclado fisico conectado (BLE ou I2C Ext.Port1),
 * ocultando o teclado virtual automaticamente. */
bool ui_keyboard_is_hardware_connected(void);
