#include "display_storage.h"
#include "wifi_storage.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "tab5_disp_storage";

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
    if (wifi_storage_mount() != ESP_OK) {
        return ESP_FAIL;
    }

    FILE *f = fopen(DISPLAY_CFG_PATH, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "falha ao abrir %s para gravacao", DISPLAY_CFG_PATH);
        return ESP_FAIL;
    }

    fprintf(f, "rotation=%d\n", (int)rot);
    fclose(f);
    ESP_LOGI(TAG, "rotacao salva no SD: %d", (int)rot);
    return ESP_OK;
}
