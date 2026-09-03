#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t http_file_server_start(void);
esp_err_t http_file_server_stop(void);
bool http_file_server_is_running(void);
uint16_t http_file_server_get_port(void);

#ifdef __cplusplus
}
#endif
