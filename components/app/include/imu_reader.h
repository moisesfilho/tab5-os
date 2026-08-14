#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t imu_reader_start(lv_display_t *disp);

#ifdef __cplusplus
}
#endif
