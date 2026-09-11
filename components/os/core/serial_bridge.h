#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Dispatch one newline-delimited JSON command into a bounded response buffer. */
int serial_bridge_dispatch(const char *json_line, char *out, size_t out_sz);

/** Start the configured UART bridge task on the target. */
void serial_bridge_start(void);

#ifdef __cplusplus
}
#endif
