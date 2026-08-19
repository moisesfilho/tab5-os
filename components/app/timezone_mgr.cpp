#include "timezone_mgr.h"
#include "wifi_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static const char *TAG = "tab5_tz_mgr";
static const char *NVS_NAMESPACE = "tab5";
static const char *NVS_KEY_TZ = "tz_offset";

static int s_tz_offset = TIMEZONE_DEFAULT_OFFSET;

namespace {

void apply_posix_tz(int offset_hours)
{
    char tz_buf[32];
    /*
     * Formato POSIX TZ: O sinal é invertido em relação a UTC.
     * UTC-3 (Brasília) -> "UTC+3"
     * UTC+5 -> "UTC-5"
     * UTC 0 -> "UTC0"
     */
    if (offset_hours == 0) {
        snprintf(tz_buf, sizeof(tz_buf), "UTC0");
    } else if (offset_hours < 0) {
        snprintf(tz_buf, sizeof(tz_buf), "UTC+%d", -offset_hours);
    } else {
        snprintf(tz_buf, sizeof(tz_buf), "UTC-%d", offset_hours);
    }

    setenv("TZ", tz_buf, 1);
    tzset();
    ESP_LOGI(TAG, "Fuso horário configurado: offset=%d (%s)", offset_hours, tz_buf);
}

void sync_sd_tz(int offset_hours)
{
    if (wifi_storage_mount() != ESP_OK) {
        return;
    }

    int fd = open(TIMEZONE_CFG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        ESP_LOGW(TAG, "Falha ao abrir %s para gravação", TIMEZONE_CFG_PATH);
        return;
    }

    FILE *f = fdopen(fd, "w");
    if (f == nullptr) {
        close(fd);
        ESP_LOGW(TAG, "Falha ao associar stream para %s", TIMEZONE_CFG_PATH);
        return;
    }

    fprintf(f, "timezone=%d\n", offset_hours);
    fclose(f);
}

bool load_sd_tz(int *out_offset)
{
    if (wifi_storage_mount() != ESP_OK) {
        return false;
    }

    FILE *f = fopen(TIMEZONE_CFG_PATH, "r");
    if (f == nullptr) {
        return false;
    }

    char line[64];
    bool found = false;
    while (fgets(line, sizeof(line), f) != nullptr) {
        char *eq = strchr(line, '=');
        if (eq == nullptr) {
            continue;
        }
        *eq = '\0';
        char *val = eq + 1;
        val[strcspn(val, "\r\n")] = '\0';
        if (strcmp(line, "timezone") == 0) {
            *out_offset = (int)strtol(val, nullptr, 10);
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

} // namespace

esp_err_t timezone_mgr_init(void)
{
    int offset = TIMEZONE_DEFAULT_OFFSET;
    bool loaded = false;

    /* 1. Tenta carregar do NVS */
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        int32_t val = 0;
        if (nvs_get_i32(handle, NVS_KEY_TZ, &val) == ESP_OK) {
            offset = (int)val;
            loaded = true;
            ESP_LOGI(TAG, "Fuso horário carregado do NVS: %d", offset);
        }
        nvs_close(handle);
    }

    /* 2. Se não encontrou no NVS, tenta ler do cartão SD */
    if (!loaded) {
        int sd_offset = 0;
        if (load_sd_tz(&sd_offset)) {
            offset = sd_offset;
            loaded = true;
            ESP_LOGI(TAG, "Fuso horário carregado do SD: %d", offset);
        }
    }

    if (offset < TIMEZONE_MIN_OFFSET || offset > TIMEZONE_MAX_OFFSET) {
        offset = TIMEZONE_DEFAULT_OFFSET;
    }

    s_tz_offset = offset;
    apply_posix_tz(s_tz_offset);

    return ESP_OK;
}

int timezone_mgr_get_offset(void)
{
    return s_tz_offset;
}

esp_err_t timezone_mgr_set_offset(int offset_hours)
{
    if (offset_hours < TIMEZONE_MIN_OFFSET) {
        offset_hours = TIMEZONE_MIN_OFFSET;
    }
    if (offset_hours > TIMEZONE_MAX_OFFSET) {
        offset_hours = TIMEZONE_MAX_OFFSET;
    }

    s_tz_offset = offset_hours;
    apply_posix_tz(s_tz_offset);

    /* Persiste no NVS */
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i32(handle, NVS_KEY_TZ, s_tz_offset);
        nvs_commit(handle);
        nvs_close(handle);
    }

    /* Persiste no SD */
    sync_sd_tz(s_tz_offset);

    return ESP_OK;
}

struct tm *timezone_mgr_get_localtime(struct tm *out_tm)
{
    if (out_tm == nullptr) {
        return nullptr;
    }
    time_t now = time(nullptr);
    return localtime_r(&now, out_tm);
}

void timezone_mgr_format_offset(int offset_hours, char *buf, size_t buf_size)
{
    if (buf == nullptr || buf_size == 0) {
        return;
    }
    if (offset_hours == 0) {
        snprintf(buf, buf_size, "0");
    } else if (offset_hours > 0) {
        snprintf(buf, buf_size, "+%d", offset_hours);
    } else {
        snprintf(buf, buf_size, "%d", offset_hours);
    }
}
