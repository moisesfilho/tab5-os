#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t imu_reader_start(lv_display_t *disp);

void imu_reader_set_rotation_enabled(bool enabled);

bool imu_reader_is_rotation_enabled(void);

#ifdef __cplusplus
}
#endif
