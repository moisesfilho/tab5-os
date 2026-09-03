/**
 * @file tab5_sys_host.h
 * @brief Hardware, System Services & Notification Bindings
 */

#pragma once

#include "include/tab5_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

tab5_err_t tab5_sys_host_get_battery(tab5_battery_info_t *out_info);
tab5_err_t tab5_sys_host_get_wifi(tab5_wifi_info_t *out_info);
tab5_err_t tab5_sys_host_get_bt(tab5_bt_info_t *out_info);
tab5_err_t tab5_sys_host_get_time(int64_t *out_epoch_ms, struct tm *out_time);
tab5_err_t tab5_sys_host_beep(uint32_t freq_hz, uint32_t duration_ms);
void tab5_sys_host_log(int level, const char *tag, const char *message);

#ifdef __cplusplus
}
#endif
