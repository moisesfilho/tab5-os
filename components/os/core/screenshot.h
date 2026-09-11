#pragma once

#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREENSHOT_RESULT_OK = 0,
    SCREENSHOT_RESULT_BUSY,
    SCREENSHOT_RESULT_TIMEOUT,
    SCREENSHOT_RESULT_ERROR,
} screenshot_result_t;

screenshot_result_t screenshot_take(void);
const char *screenshot_get_last_path(void);
screenshot_result_t screenshot_wait_for_completion(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
