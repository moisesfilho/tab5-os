#include "wifi_storage.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "esp_check.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "tab5_wifi_storage";

static bool s_mounted = false;

esp_err_t wifi_storage_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bsp_sdcard_mount(), TAG, "mount SD falhou");
    if (mkdir("/sdcard/tab5_os", 0755) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir tab5_os falhou (errno=%d)", errno);
    }
    s_mounted = true;
    ESP_LOGI(TAG, "SD montado em /sdcard");
    return ESP_OK;
}

esp_err_t wifi_storage_load_all(wifi_saved_list_t *list)
{
    if (list == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    list->count = 0;

    FILE *f = fopen(WIFI_CFG_PATH, "r");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char line[160];
    char cur_ssid[33] = "";
    char cur_pwd[65] = "";

    while (fgets(line, sizeof(line), f) != NULL) {
        /* Trata linhas vazias e comentarios */
        char *start = line;
        while (*start == ' ' || *start == '\t') {
            start++;
        }
        if (*start == '#' || *start == ';' || *start == '\r' || *start == '\n' || *start == '\0') {
            continue;
        }

        /* Suporte a secao [nome_da_rede] */
        if (*start == '[') {
            char *end = strchr(start, ']');
            if (end != NULL) {
                if (cur_ssid[0] != '\0' && list->count < WIFI_MAX_SAVED_NETWORKS) {
                    snprintf(list->items[list->count].ssid, sizeof(list->items[list->count].ssid), "%s", cur_ssid);
                    // codeql[cpp/cleartext-storage-buffer]
                    snprintf(list->items[list->count].password, sizeof(list->items[list->count].password), "%s",
                             cur_pwd);
                    list->count++;
                    cur_pwd[0] = '\0';
                }
                size_t len = end - (start + 1);
                if (len >= sizeof(cur_ssid)) {
                    len = sizeof(cur_ssid) - 1;
                }
                strncpy(cur_ssid, start + 1, len);
                cur_ssid[len] = '\0';
                continue;
            }
        }

        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char *key = start;
        char *value = eq + 1;
        value[strcspn(value, "\r\n")] = '\0';

        /* Trim em key */
        char *key_end = key + strlen(key) - 1;
        while (key_end > key && (*key_end == ' ' || *key_end == '\t')) {
            *key_end = '\0';
            key_end--;
        }

        if (strcmp(key, "ssid") == 0) {
            if (cur_ssid[0] != '\0' && list->count < WIFI_MAX_SAVED_NETWORKS) {
                snprintf(list->items[list->count].ssid, sizeof(list->items[list->count].ssid), "%s", cur_ssid);
                // codeql[cpp/cleartext-storage-buffer]
                snprintf(list->items[list->count].password, sizeof(list->items[list->count].password), "%s", cur_pwd);
                list->count++;
                cur_pwd[0] = '\0';
            }
            snprintf(cur_ssid, sizeof(cur_ssid), "%s", value);
        } else if (strcmp(key, "password") == 0 || strcmp(key, "pwd") == 0) {
            snprintf(cur_pwd, sizeof(cur_pwd), "%s", value);
        }
    }

    if (cur_ssid[0] != '\0' && list->count < WIFI_MAX_SAVED_NETWORKS) {
        snprintf(list->items[list->count].ssid, sizeof(list->items[list->count].ssid), "%s", cur_ssid);
        // codeql[cpp/cleartext-storage-buffer]
        snprintf(list->items[list->count].password, sizeof(list->items[list->count].password), "%s", cur_pwd);
        list->count++;
    }

    fclose(f);
    return ESP_OK;
}

esp_err_t wifi_storage_save_all(const wifi_saved_list_t *list)
{
    if (list == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int fd = open(WIFI_CFG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return ESP_FAIL;
    }
    FILE *f = fdopen(fd, "w");
    if (f == NULL) {
        close(fd);
        return ESP_FAIL;
    }

    for (int i = 0; i < list->count; i++) {
        // codeql[cpp/cleartext-storage-file]
        fprintf(f, "ssid=%s\npassword=%s\n\n", list->items[i].ssid, list->items[i].password);
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t wifi_storage_load(wifi_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_saved_list_t list;
    esp_err_t err = wifi_storage_load_all(&list);
    if (err != ESP_OK || list.count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(cfg->ssid, sizeof(cfg->ssid), "%s", list.items[0].ssid);
    snprintf(cfg->password, sizeof(cfg->password), "%s", list.items[0].password);
    return ESP_OK;
}

esp_err_t wifi_storage_save(const wifi_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return wifi_storage_add_or_update(cfg->ssid, cfg->password);
}

esp_err_t wifi_storage_add_or_update(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_saved_list_t list;
    if (wifi_storage_load_all(&list) != ESP_OK) {
        list.count = 0;
    }

    int found_idx = -1;
    for (int i = 0; i < list.count; i++) {
        if (strcmp(list.items[i].ssid, ssid) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx >= 0) {
        /* Atualiza a senha da rede ja existente */
        snprintf(list.items[found_idx].password, sizeof(list.items[found_idx].password), "%s",
                 password != NULL ? password : "");
    } else {
        if (list.count >= WIFI_MAX_SAVED_NETWORKS) {
            /* Se lotou, substitui a mais antiga (posicao 0) deslocando a fila */
            for (int i = 0; i < list.count - 1; i++) {
                list.items[i] = list.items[i + 1];
            }
            list.count = WIFI_MAX_SAVED_NETWORKS - 1;
        }
        snprintf(list.items[list.count].ssid, sizeof(list.items[list.count].ssid), "%s", ssid);
        snprintf(list.items[list.count].password, sizeof(list.items[list.count].password), "%s",
                 password != NULL ? password : "");
        list.count++;
    }

    return wifi_storage_save_all(&list);
}

esp_err_t wifi_storage_remove(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_saved_list_t list;
    if (wifi_storage_load_all(&list) != ESP_OK || list.count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    int found_idx = -1;
    for (int i = 0; i < list.count; i++) {
        if (strcmp(list.items[i].ssid, ssid) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    for (int i = found_idx; i < list.count - 1; i++) {
        list.items[i] = list.items[i + 1];
    }
    list.count--;

    return wifi_storage_save_all(&list);
}

bool wifi_storage_find(const char *ssid, char *out_password, size_t max_len)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return false;
    }

    wifi_saved_list_t list;
    if (wifi_storage_load_all(&list) != ESP_OK) {
        return false;
    }

    for (int i = 0; i < list.count; i++) {
        if (strcmp(list.items[i].ssid, ssid) == 0) {
            if (out_password != NULL && max_len > 0) {
                snprintf(out_password, max_len, "%s", list.items[i].password);
            }
            return true;
        }
    }
    return false;
}
