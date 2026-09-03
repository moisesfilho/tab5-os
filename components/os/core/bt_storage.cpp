#include "bt_storage.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include "esp_log.h"
#include "wifi_storage.h"

static const char *TAG = "tab5_bt_storage";

static const char *type_to_str(bt_dev_type_t type)
{
    switch (type) {
    case BT_DEV_TYPE_KEYBOARD:
        return "keyboard";
    case BT_DEV_TYPE_MOUSE:
        return "mouse";
    case BT_DEV_TYPE_HEADPHONE:
        return "headphone";
    case BT_DEV_TYPE_GENERIC:
    default:
        return "generic";
    }
}

static bt_dev_type_t str_to_type(const char *str)
{
    if (strcasecmp(str, "keyboard") == 0) {
        return BT_DEV_TYPE_KEYBOARD;
    }
    if (strcasecmp(str, "mouse") == 0) {
        return BT_DEV_TYPE_MOUSE;
    }
    if (strcasecmp(str, "headphone") == 0 || strcasecmp(str, "audio") == 0) {
        return BT_DEV_TYPE_HEADPHONE;
    }
    return BT_DEV_TYPE_GENERIC;
}

static bt_saved_list_t s_cache = {};
static bool s_cache_valid = false;

esp_err_t bt_storage_load_all(bt_saved_list_t *list)
{
    if (list == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    list->count = 0;

    FILE *f = fopen(BT_CFG_PATH, "r");
    if (f == NULL) {
        if (s_cache_valid) {
            *list = s_cache;
            return ESP_OK;
        }
        return ESP_ERR_NOT_FOUND;
    }

    char line[160];
    bt_saved_device_t cur_dev = {};

    while (fgets(line, sizeof(line), f) != NULL) {
        char *start = line;
        while (*start == ' ' || *start == '\t') {
            start++;
        }
        if (*start == '#' || *start == ';' || *start == '\r' || *start == '\n' || *start == '\0') {
            continue;
        }

        /* Suporte a secao [MAC] */
        if (*start == '[') {
            char *end = strchr(start, ']');
            if (end != NULL) {
                if (cur_dev.mac[0] != '\0' && list->count < BT_MAX_SAVED_DEVICES) {
                    list->items[list->count++] = cur_dev;
                    memset(&cur_dev, 0, sizeof(cur_dev));
                }
                size_t len = end - (start + 1);
                if (len >= sizeof(cur_dev.mac)) {
                    len = sizeof(cur_dev.mac) - 1;
                }
                strncpy(cur_dev.mac, start + 1, len);
                cur_dev.mac[len] = '\0';
                cur_dev.paired = true;
                cur_dev.auto_connect = true;
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

        /* Trim na chave */
        char *key_end = key + strlen(key) - 1;
        while (key_end > key && (*key_end == ' ' || *key_end == '\t')) {
            *key_end = '\0';
            key_end--;
        }

        /* Trim no valor */
        while (*value == ' ' || *value == '\t') {
            value++;
        }

        if (strcmp(key, "mac") == 0) {
            if (cur_dev.mac[0] != '\0' && list->count < BT_MAX_SAVED_DEVICES) {
                list->items[list->count++] = cur_dev;
                memset(&cur_dev, 0, sizeof(cur_dev));
            }
            snprintf(cur_dev.mac, sizeof(cur_dev.mac), "%s", value);
            cur_dev.paired = true;
            cur_dev.auto_connect = true;
        } else if (strcmp(key, "name") == 0) {
            snprintf(cur_dev.name, sizeof(cur_dev.name), "%s", value);
        } else if (strcmp(key, "type") == 0) {
            cur_dev.type = str_to_type(value);
        } else if (strcmp(key, "addr_type") == 0) {
            cur_dev.addr_type = (uint8_t)atoi(value);
        } else if (strcmp(key, "paired") == 0) {
            cur_dev.paired = (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0);
        } else if (strcmp(key, "auto_connect") == 0) {
            cur_dev.auto_connect = (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0);
        }
    }

    if (cur_dev.mac[0] != '\0' && list->count < BT_MAX_SAVED_DEVICES) {
        list->items[list->count++] = cur_dev;
    }

    fclose(f);
    s_cache = *list;
    s_cache_valid = true;
    ESP_LOGI(TAG, "Carregados %d dispositivos de %s (cache atualizado)", list->count, BT_CFG_PATH);
    return ESP_OK;
}

esp_err_t bt_storage_save_all(const bt_saved_list_t *list)
{
    if (list == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cache = *list;
    s_cache_valid = true;

    /* Assegura que o diretório oculto exista */
    mkdir(TAB5_CONFIG_DIR, 0755);

    int fd = open(BT_CFG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        ESP_LOGE(TAG, "Falha ao abrir %s para gravacao", BT_CFG_PATH);
        return ESP_FAIL;
    }
    FILE *f = fdopen(fd, "w");
    if (f == NULL) {
        close(fd);
        ESP_LOGE(TAG, "Falha ao associar stream para %s", BT_CFG_PATH);
        return ESP_FAIL;
    }

    fprintf(f, "# tab5_os bluetooth configuration\n\n");
    for (int i = 0; i < list->count; i++) {
        const bt_saved_device_t *d = &list->items[i];
        if (d->mac[0] == '\0') {
            continue;
        }
        fprintf(f, "[%s]\n", d->mac);
        fprintf(f, "name = %s\n", d->name);
        fprintf(f, "type = %s\n", type_to_str(d->type));
        fprintf(f, "addr_type = %d\n", (int)d->addr_type);
        fprintf(f, "paired = %d\n", d->paired ? 1 : 0);
        fprintf(f, "auto_connect = %d\n\n", d->auto_connect ? 1 : 0);
    }

    fclose(f);
    ESP_LOGI(TAG, "Salvos %d dispositivos em %s", list->count, BT_CFG_PATH);
    return ESP_OK;
}

esp_err_t bt_storage_add_or_update(const bt_saved_device_t *dev)
{
    if (dev == NULL || dev->mac[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    bt_saved_list_t list = {};
    if (!s_cache_valid) {
        bt_storage_load_all(&list);
    } else {
        list = s_cache;
    }

    bool found = false;
    for (int i = 0; i < list.count; i++) {
        if (strcasecmp(list.items[i].mac, dev->mac) == 0) {
            list.items[i] = *dev;
            found = true;
            break;
        }
    }

    if (!found) {
        if (list.count >= BT_MAX_SAVED_DEVICES) {
            ESP_LOGW(TAG, "Limite de dispositivos salvos atingido (%d)", BT_MAX_SAVED_DEVICES);
            return ESP_ERR_NO_MEM;
        }
        list.items[list.count++] = *dev;
    }

    return bt_storage_save_all(&list);
}

esp_err_t bt_storage_remove(const char *mac)
{
    if (mac == NULL || mac[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    bt_saved_list_t list = {};
    if (!s_cache_valid) {
        if (bt_storage_load_all(&list) != ESP_OK) {
            return ESP_OK;
        }
    } else {
        list = s_cache;
    }

    bool found = false;
    for (int i = 0; i < list.count; i++) {
        if (strcasecmp(list.items[i].mac, mac) == 0) {
            for (int j = i; j < list.count - 1; j++) {
                list.items[j] = list.items[j + 1];
            }
            list.count--;
            found = true;
            break;
        }
    }

    if (found) {
        return bt_storage_save_all(&list);
    }
    return ESP_OK;
}

bool bt_storage_find(const char *mac, bt_saved_device_t *out_dev)
{
    if (mac == NULL || mac[0] == '\0') {
        return false;
    }

    if (!s_cache_valid) {
        bt_saved_list_t list = {};
        if (bt_storage_load_all(&list) != ESP_OK) {
            return false;
        }
    }

    for (int i = 0; i < s_cache.count; i++) {
        if (strcasecmp(s_cache.items[i].mac, mac) == 0) {
            if (out_dev != NULL) {
                *out_dev = s_cache.items[i];
            }
            return true;
        }
    }

    return false;
}
