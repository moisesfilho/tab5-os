/* lv_conf.h do simulador host.
 * Replica as opcoes do firmware (sdkconfig) relevantes para renderizacao:
 * RGB565 sem swap, snapshot habilitado, SW renderer, mesmas fontes default.
 * Tudo que nao esta definido aqui usa o default do lv_conf_internal
 * (widgets core ja vem habilitados por padrao). */

#pragma once

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Heap do LVGL: no device a UI aloca do PSRAM; no host damos folga.
 * Usa malloc do libc: evita o TLSF embutido (a free-list dele girou em
 * loop infinito neste setup; o allocator nao afeta a renderizacao). */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_MEM_SIZE (8u * 1024u * 1024u)

#define LV_USE_OS LV_OS_NONE

#define LV_USE_SNAPSHOT 1

#define LV_USE_SDL 1

/* Widgets usados pela UI do tab5_os (os demais herdam o default 1 do
 * lv_conf_internal; aqui apenas documentamos os essenciais). */
#define LV_USE_BAR 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_IMAGE 1
#define LV_USE_KEYBOARD 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1

/* Fonte default do LVGL (a app sobrescreve com as latin1 customizadas). */
#define LV_FONT_MONTSERRAT_14 1

#endif
