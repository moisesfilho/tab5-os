#pragma once

#include <stdint.h>

#include "tab5_sdk.h"

/* NVS is deliberately kept off the WASM call stack.  These functions are
 * synchronous to their callers, but the actual flash operation is performed
 * by the dedicated native worker task. */
tab5_err_t tab5_nvs_worker_get_u8(const char *ns, const char *key, uint8_t *out_val);
tab5_err_t tab5_nvs_worker_set_u8(const char *ns, const char *key, uint8_t val);
