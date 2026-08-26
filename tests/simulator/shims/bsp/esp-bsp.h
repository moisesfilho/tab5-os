#pragma once

/* Shim de bsp/esp-bsp.h para o simulador: apenas a API de display usada
 * pelos modulos de UI (lock/unlock e backlight). Implementacao em shim_bsp.cpp.
 * No device este header puxa FreeRTOS/esp_log transitivamente; aqui o shim
 * cumpre o mesmo papel. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);
int bsp_display_brightness_set(int brightness_percent);
int bsp_sdcard_mount(void);

#ifdef __cplusplus
}
#endif
