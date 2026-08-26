#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY = 0,
    NVS_READWRITE = 1,
} nvs_open_mode_t;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);

esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value);
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value);
esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);

esp_err_t nvs_commit(nvs_handle_t handle);

#ifdef __cplusplus
}
#endif
