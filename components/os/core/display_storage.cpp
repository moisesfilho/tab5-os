#include "display_storage.h"
#include "wifi_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "tab5_disp_storage";
static const char *NVS_NAMESPACE = "tab5";
static const char *NVS_KEY_BRIGHTNESS = "brightness";

static void sync_sd_display_cfg(int rot, int brightness)
{
    if (wifi_storage_mount() != ESP_OK) {
        return;
    }

    int cur_rot = -1;
    int cur_br = -1;
    FILE *f = fopen(DISPLAY_CFG_PATH, "r");
    if (f != NULL) {
        char line[64];
        while (fgets(line, sizeof(line), f) != NULL) {
            char *eq = strchr(line, '=');
            if (eq == NULL) {
                continue;
            }
            *eq = '\0';
            char *value = eq + 1;
            value[strcspn(value, "\r\n")] = '\0';
            if (strcmp(line, "rotation") == 0) {
                cur_rot = (int)strtol(value, NULL, 10);
            } else if (strcmp(line, "brightness") == 0) {
                cur_br = (int)strtol(value, NULL, 10);
            }
        }
        fclose(f);
    }

    if (rot >= 0) {
        cur_rot = rot;
    }
    if (brightness >= 0) {
        cur_br = brightness;
    }

    int fd = open(DISPLAY_CFG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        ESP_LOGW(TAG, "falha ao abrir %s para gravacao", DISPLAY_CFG_PATH);
        return;
    }
    f = fdopen(fd, "w");
    if (f == NULL) {
        close(fd);
        ESP_LOGW(TAG, "falha ao associar stream para %s", DISPLAY_CFG_PATH);
        return;
    }

    if (cur_rot >= 0) {
        fprintf(f, "rotation=%d\n", cur_rot);
    }
    if (cur_br >= 0) {
        fprintf(f, "brightness=%d\n", cur_br);
    }
    fclose(f);
}

esp_err_t display_storage_load_rotation(lv_disp_rotation_t *rot)
{
    if (rot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (wifi_storage_mount() != ESP_OK) {
        return ESP_FAIL;
    }

    FILE *f = fopen(DISPLAY_CFG_PATH, "r");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char line[64];
    int read_rot = -1;
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char *value = eq + 1;
        value[strcspn(value, "\r\n")] = '\0';
        if (strcmp(line, "rotation") == 0) {
            read_rot = (int)strtol(value, NULL, 10);
        }
    }
    fclose(f);

    if (read_rot >= 0 && read_rot <= 3) {
        *rot = (lv_disp_rotation_t)read_rot;
        ESP_LOGI(TAG, "rotacao carregada do SD: %d", read_rot);
        return ESP_OK;
    }

    return ESP_ERR_INVALID_STATE;
}

esp_err_t display_storage_save_rotation(lv_disp_rotation_t rot)
{
    sync_sd_display_cfg((int)rot, -1);
    ESP_LOGI(TAG, "rotacao salva no SD: %d", (int)rot);
    return ESP_OK;
}

esp_err_t display_storage_load_brightness(int *percent)
{
    if (percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. Tenta carregar do NVS */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t val = 0;
        if (nvs_get_u8(nvs, NVS_KEY_BRIGHTNESS, &val) == ESP_OK) {
            nvs_close(nvs);
            if (val >= DISPLAY_MIN_BRIGHTNESS && val <= DISPLAY_MAX_BRIGHTNESS) {
                *percent = (int)val;
                ESP_LOGI(TAG, "brilho carregado do NVS: %d%%", *percent);
                return ESP_OK;
            }
        } else {
            nvs_close(nvs);
        }
    }

    /* 2. Fallback: tenta carregar do SD */
    if (wifi_storage_mount() == ESP_OK) {
        FILE *f = fopen(DISPLAY_CFG_PATH, "r");
        if (f != NULL) {
            char line[64];
            int read_br = -1;
            while (fgets(line, sizeof(line), f) != NULL) {
                char *eq = strchr(line, '=');
                if (eq == NULL) {
                    continue;
                }
                *eq = '\0';
                char *value = eq + 1;
                value[strcspn(value, "\r\n")] = '\0';
                if (strcmp(line, "brightness") == 0) {
                    read_br = (int)strtol(value, NULL, 10);
                }
            }
            fclose(f);

            if (read_br >= DISPLAY_MIN_BRIGHTNESS && read_br <= DISPLAY_MAX_BRIGHTNESS) {
                *percent = read_br;
                ESP_LOGI(TAG, "brilho carregado do SD: %d%%", read_br);
                return ESP_OK;
            }
        }
    }

    /* 3. Fallback padrao: 80% */
    *percent = DISPLAY_DEFAULT_BRIGHTNESS;
    ESP_LOGI(TAG, "brilho padrao adotado: %d%%", *percent);
    return ESP_OK;
}

esp_err_t display_storage_save_brightness(int percent)
{
    if (percent < DISPLAY_MIN_BRIGHTNESS) {
        percent = DISPLAY_MIN_BRIGHTNESS;
    } else if (percent > DISPLAY_MAX_BRIGHTNESS) {
        percent = DISPLAY_MAX_BRIGHTNESS;
    }

    /* 1. Salva no NVS */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_BRIGHTNESS, (uint8_t)percent);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    /* 2. Atualiza no SD se disponivel */
    sync_sd_display_cfg(-1, percent);

    ESP_LOGI(TAG, "brilho salvo: %d%%", percent);
    return ESP_OK;
}
