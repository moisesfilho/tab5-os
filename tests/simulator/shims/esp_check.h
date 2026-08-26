#pragma once

#include "esp_err.h"

#define ESP_RETURN_ON_ERROR(x, tag, format, ...)                                                                       \
    do {                                                                                                               \
        esp_err_t _esp_check_err = (esp_err_t)(x);                                                                     \
        if (_esp_check_err != ESP_OK) {                                                                                \
            return _esp_check_err;                                                                                     \
        }                                                                                                              \
    } while (0)
