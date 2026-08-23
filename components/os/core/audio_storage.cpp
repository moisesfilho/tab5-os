#include "audio_storage.h"
#include "wifi_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "tab5_audio_storage";
static const char *NVS_NAMESPACE = "tab5";
static const char *NVS_KEY_VOLUME = "volume";

static int read_sd_volume(void)
{
    if (wifi_storage_mount() != ESP_OK) {
        return -1;
    }

    FILE *f = fopen(AUDIO_CFG_PATH, "r");
    if (f == NULL) {
        return -1;
    }

    char line[64];
    int vol = -1;
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char *value = eq + 1;
        value[strcspn(value, "\r\n")] = '\0';
        if (strcmp(line, "volume") == 0) {
            vol = (int)strtol(value, NULL, 10);
        }
    }
    fclose(f);

    if (vol < AUDIO_MIN_VOLUME || vol > AUDIO_MAX_VOLUME) {
        return -1;
    }
    return vol;
}

static void sync_sd_audio_cfg(int volume)
{
    if (wifi_storage_mount() != ESP_OK) {
        return;
    }

    FILE *f = fopen(AUDIO_CFG_PATH, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "falha ao abrir %s para gravacao", AUDIO_CFG_PATH);
        return;
    }
    fprintf(f, "volume=%d\n", volume);
    fclose(f);
}

esp_err_t audio_storage_load_volume(int *percent)
{
    if (percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. Tenta carregar do NVS */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t val = 0;
        if (nvs_get_u8(nvs, NVS_KEY_VOLUME, &val) == ESP_OK) {
            nvs_close(nvs);
            if (val <= AUDIO_MAX_VOLUME) {
                *percent = (int)val;
                ESP_LOGI(TAG, "volume carregado do NVS: %d%%", *percent);
                return ESP_OK;
            }
        } else {
            nvs_close(nvs);
        }
    }

    /* 2. Fallback: tenta carregar do SD */
    int sd_vol = read_sd_volume();
    if (sd_vol >= 0) {
        *percent = sd_vol;
        ESP_LOGI(TAG, "volume carregado do SD: %d%%", sd_vol);
        return ESP_OK;
    }

    /* 3. Fallback padrao: 80% */
    *percent = AUDIO_DEFAULT_VOLUME;
    ESP_LOGI(TAG, "volume padrao adotado: %d%%", *percent);
    return ESP_OK;
}

esp_err_t audio_storage_save_volume(int percent)
{
    if (percent < AUDIO_MIN_VOLUME) {
        percent = AUDIO_MIN_VOLUME;
    } else if (percent > AUDIO_MAX_VOLUME) {
        percent = AUDIO_MAX_VOLUME;
    }

    /* 1. Salva no NVS */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_VOLUME, (uint8_t)percent);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    /* 2. Atualiza no SD se disponivel */
    sync_sd_audio_cfg(percent);

    ESP_LOGI(TAG, "volume salvo: %d%%", percent);
    return ESP_OK;
}
