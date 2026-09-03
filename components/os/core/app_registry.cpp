#include "app_registry.h"
#include "file_assoc.h"
#include "esp_log.h"
#include <vector>
#include <cstring>

static const char *TAG = "tab5_app_registry";

static std::vector<app_desc_t> s_apps;

void app_registry_init(void)
{
    s_apps.clear();
}

esp_err_t app_registry_register(const app_desc_t *desc)
{
    if (desc == nullptr || desc->id == nullptr || desc->name == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Evita registrar duplicados */
    for (const auto &app : s_apps) {
        if (strcmp(app.id, desc->id) == 0) {
            ESP_LOGW(TAG, "Aplicacao ja registrada: %s", desc->id);
            return ESP_ERR_INVALID_STATE;
        }
    }

    s_apps.push_back(*desc);
    ESP_LOGI(TAG, "Aplicacao registrada: %s (\"%s\")", desc->id, desc->name);

    /* Registra automaticamente as extensões associadas */
    if (desc->file_extensions != nullptr && desc->on_open_file != nullptr) {
        for (int i = 0; desc->file_extensions[i] != nullptr; i++) {
            file_assoc_register(desc->file_extensions[i], desc->on_open_file);
        }
    }

    return ESP_OK;
}

int app_registry_get_count(void)
{
    return static_cast<int>(s_apps.size());
}

const app_desc_t *app_registry_get_by_index(int index)
{
    if (index < 0 || index >= static_cast<int>(s_apps.size())) {
        return nullptr;
    }
    return &s_apps[index];
}

const app_desc_t *app_registry_find_by_id(const char *id)
{
    if (id == nullptr) {
        return nullptr;
    }
    for (const auto &app : s_apps) {
        if (strcmp(app.id, id) == 0) {
            return &app;
        }
    }
    return nullptr;
}

esp_err_t app_registry_unregister(const char *id)
{
    if (id == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    for (auto it = s_apps.begin(); it != s_apps.end(); ++it) {
        if (strcmp(it->id, id) == 0) {
            ESP_LOGI(TAG, "Aplicacao desregistrada: %s", id);
            s_apps.erase(it);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

const std::vector<app_desc_t> &app_registry_get_all(void)
{
    return s_apps;
}
