#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apenas o prototipo usado por wifi_storage.cpp; a implementacao mock
 * fica em tests/host/mocks/bsp_mock.cpp. */
esp_err_t bsp_sdcard_mount(void);

#ifdef __cplusplus
}
#endif
