#pragma once

#include "esp_err.h"

/* Replica a semantica do ESP_RETURN_ON_ERROR: avalia a expressao e
 * retorna o codigo de erro caso nao seja ESP_OK. */
#define ESP_RETURN_ON_ERROR(x, tag, format, ...)                                                                       \
    do {                                                                                                               \
        esp_err_t _esp_check_err = (esp_err_t)(x);                                                                     \
        (void)(tag);                                                                                                   \
        (void)(format);                                                                                                \
        if (_esp_check_err != ESP_OK) {                                                                                \
            return _esp_check_err;                                                                                     \
        }                                                                                                              \
    } while (0)
