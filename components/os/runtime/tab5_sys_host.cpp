/**
 * @file tab5_sys_host.cpp
 * @brief Implementação dos Bindings de Hardware e Sistema
 */

#include "tab5_sys_host.h"
#include <cstring>
#include <cstdio>
#include <sys/time.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "battery_reader.h"
#include "wifi_mgr.h"
#include "bt_mgr.h"
#include "bsp/m5stack_tab5.h"
#endif

tab5_err_t tab5_sys_host_get_battery(tab5_battery_info_t *out_info)
{
    if (out_info == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));

#ifdef ESP_PLATFORM
    battery_status_t bstat = {};
    if (battery_reader_get_status(&bstat)) {
        out_info->percent = bstat.percent;
        out_info->is_charging = (bstat.source == BATTERY_SOURCE_CHARGING);
        out_info->is_present = (bstat.source != BATTERY_SOURCE_NO_BATTERY);
        out_info->voltage_mv = (uint32_t)bstat.voltage_mv;
        return TAB5_OK;
    }
    return TAB5_ERR_NOT_FOUND;
#else
    out_info->percent = 85;
    out_info->is_charging = false;
    out_info->is_present = true;
    out_info->voltage_mv = 3950;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_sys_host_get_wifi(tab5_wifi_info_t *out_info)
{
    if (out_info == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));

#ifdef ESP_PLATFORM
    wifi_status_t status = {};
    if (wifi_mgr_get_status(&status) == ESP_OK) {
        out_info->is_connected = status.connected;
        if (out_info->is_connected) {
            strncpy(out_info->ssid, status.ssid, sizeof(out_info->ssid) - 1);
            strncpy(out_info->ip_addr, status.ip, sizeof(out_info->ip_addr) - 1);
            out_info->rssi = 0;
        }
        return TAB5_OK;
    }
    return TAB5_ERR_FAIL;
#else
    out_info->is_connected = true;
    strncpy(out_info->ssid, "Tab5-WiFi-Test", sizeof(out_info->ssid) - 1);
    strncpy(out_info->ip_addr, "192.168.1.100", sizeof(out_info->ip_addr) - 1);
    out_info->rssi = -55;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_sys_host_get_bt(tab5_bt_info_t *out_info)
{
    if (out_info == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));

#ifdef ESP_PLATFORM
    bt_status_t status = {};
    if (bt_mgr_get_status(&status) == ESP_OK) {
        out_info->is_enabled = bt_mgr_is_enabled();
        out_info->connected_devices = status.connected_count;
        return TAB5_OK;
    }
    return TAB5_ERR_FAIL;
#else
    out_info->is_enabled = true;
    out_info->connected_devices = 1;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_sys_host_get_time(int64_t *out_epoch_ms, struct tm *out_time)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    if (out_epoch_ms != nullptr) {
        *out_epoch_ms = (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
    }

    if (out_time != nullptr) {
        time_t now = tv.tv_sec;
        localtime_r(&now, out_time);
    }

    return TAB5_OK;
}

tab5_err_t tab5_sys_host_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    (void)freq_hz;
    (void)duration_ms;
#ifdef ESP_PLATFORM
    // Se a plataforma tiver gerador de tom/bsp audio
    return TAB5_OK;
#else
    return TAB5_OK;
#endif
}

void tab5_sys_host_log(int level, const char *tag, const char *message)
{
    if (tag == nullptr || message == nullptr) {
        return;
    }
#ifdef ESP_PLATFORM
    switch (level) {
    case 0:
        ESP_LOGE(tag, "%s", message);
        break;
    case 1:
        ESP_LOGW(tag, "%s", message);
        break;
    case 2:
        ESP_LOGI(tag, "%s", message);
        break;
    default:
        ESP_LOGD(tag, "%s", message);
        break;
    }
#else
    const char *prefix = (level == 0) ? "[E]" : (level == 1) ? "[W]" : (level == 2) ? "[I]" : "[D]";
    printf("%s [%s] %s\n", prefix, tag, message);
#endif
}
