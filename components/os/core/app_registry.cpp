#include "app_registry.h"
#include "file_assoc.h"
#include "esp_log.h"
#include <vector>
#include <memory>
#include <cstring>
#include <mutex>
#include <cstdlib>

static const char *TAG = "tab5_app_registry";

struct owned_app_t {
    app_desc_t desc{};
    std::string id;
    std::string name;
    std::string icon_symbol;
    std::string icon_bg_color;
    std::vector<std::string> extensions;
    std::vector<const char *> extension_ptrs;

    explicit owned_app_t(const app_desc_t &source)
        : desc(source), id(source.id), name(source.name),
          icon_symbol(source.icon_symbol != nullptr ? source.icon_symbol : ""),
          icon_bg_color(source.icon_bg_color != nullptr ? source.icon_bg_color : "")
    {
        desc.id = id.c_str();
        desc.name = name.c_str();
        desc.icon_symbol = source.icon_symbol != nullptr ? icon_symbol.c_str() : nullptr;
        desc.icon_bg_color = source.icon_bg_color != nullptr ? icon_bg_color.c_str() : nullptr;
        if (source.file_extensions != nullptr) {
            for (const char *const *extension = source.file_extensions; *extension != nullptr; ++extension) {
                extensions.emplace_back(*extension);
            }
            extension_ptrs.reserve(extensions.size() + 1);
            for (const auto &extension : extensions) {
                extension_ptrs.push_back(extension.c_str());
            }
            extension_ptrs.push_back(nullptr);
            desc.file_extensions = extension_ptrs.data();
        }
    }
};

app_desc_snapshot_t::app_desc_snapshot_t(const app_desc_t &source)
    : desc(source), owned_id(source.id != nullptr ? source.id : ""),
      owned_name(source.name != nullptr ? source.name : ""),
      owned_icon_symbol(source.icon_symbol != nullptr ? source.icon_symbol : ""),
      owned_icon_bg_color(source.icon_bg_color != nullptr ? source.icon_bg_color : "")
{
    if (source.file_extensions != nullptr) {
        for (const char *const *extension = source.file_extensions; *extension != nullptr; ++extension)
            extensions.emplace_back(*extension);
    }
    rebind();
}

app_desc_snapshot_t::app_desc_snapshot_t(const app_desc_snapshot_t &other)
    : desc(other.desc), owned_id(other.owned_id), owned_name(other.owned_name),
      owned_icon_symbol(other.owned_icon_symbol), owned_icon_bg_color(other.owned_icon_bg_color),
      extensions(other.extensions)
{
    rebind();
}

app_desc_snapshot_t &app_desc_snapshot_t::operator=(const app_desc_snapshot_t &other)
{
    if (this != &other) {
        desc = other.desc;
        owned_id = other.owned_id;
        owned_name = other.owned_name;
        owned_icon_symbol = other.owned_icon_symbol;
        owned_icon_bg_color = other.owned_icon_bg_color;
        extensions = other.extensions;
        rebind();
    }
    return *this;
}

app_desc_snapshot_t::app_desc_snapshot_t(app_desc_snapshot_t &&other) noexcept
    : desc(other.desc), owned_id(std::move(other.owned_id)), owned_name(std::move(other.owned_name)),
      owned_icon_symbol(std::move(other.owned_icon_symbol)), owned_icon_bg_color(std::move(other.owned_icon_bg_color)),
      extensions(std::move(other.extensions))
{
    rebind();
    other.desc = {};
    other.extension_ptrs.clear();
}

app_desc_snapshot_t &app_desc_snapshot_t::operator=(app_desc_snapshot_t &&other) noexcept
{
    if (this != &other) {
        desc = other.desc;
        owned_id = std::move(other.owned_id);
        owned_name = std::move(other.owned_name);
        owned_icon_symbol = std::move(other.owned_icon_symbol);
        owned_icon_bg_color = std::move(other.owned_icon_bg_color);
        extensions = std::move(other.extensions);
        rebind();
        other.desc = {};
        other.extension_ptrs.clear();
    }
    return *this;
}

void app_desc_snapshot_t::rebind()
{
    desc.id = owned_id.c_str();
    desc.name = owned_name.c_str();
    desc.icon_symbol = desc.icon_symbol != nullptr ? owned_icon_symbol.c_str() : nullptr;
    desc.icon_bg_color = desc.icon_bg_color != nullptr ? owned_icon_bg_color.c_str() : nullptr;
    extension_ptrs.clear();
    if (desc.file_extensions != nullptr || !extensions.empty()) {
        extension_ptrs.reserve(extensions.size() + 1);
        for (const auto &extension : extensions)
            extension_ptrs.push_back(extension.c_str());
        extension_ptrs.push_back(nullptr);
        desc.file_extensions = extension_ptrs.data();
    } else {
        desc.file_extensions = nullptr;
    }
    id = desc.id;
    name = desc.name;
    icon_symbol = desc.icon_symbol;
    icon_bg_color = desc.icon_bg_color;
    icon_builder = desc.icon_builder;
    icon_theme_refresh = desc.icon_theme_refresh;
    on_launch = desc.on_launch;
    file_extensions = desc.file_extensions;
    on_open_file = desc.on_open_file;
}

static std::vector<std::unique_ptr<owned_app_t>> s_apps;
static std::recursive_mutex s_registry_mutex;

void app_registry_init(void)
{
    std::lock_guard<std::recursive_mutex> lock(s_registry_mutex);
    s_apps.clear();
}

esp_err_t app_registry_register(const app_desc_t *desc)
{
    std::lock_guard<std::recursive_mutex> lock(s_registry_mutex);
    if (desc == nullptr || desc->id == nullptr || desc->name == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Evita registrar duplicados */
    for (const auto &app : s_apps) {
        if (strcmp(app->desc.id, desc->id) == 0) {
            ESP_LOGW(TAG, "Aplicacao ja registrada: %s", desc->id);
            return ESP_ERR_INVALID_STATE;
        }
    }

    auto owned = std::make_unique<owned_app_t>(*desc);
    const app_desc_t &stored = owned->desc;
    s_apps.push_back(std::move(owned));
    ESP_LOGI(TAG, "Aplicacao registrada: %s (\"%s\")", desc->id, desc->name);

    /* Registra automaticamente as extensões associadas */
    if (stored.file_extensions != nullptr && stored.on_open_file != nullptr) {
        for (int i = 0; stored.file_extensions[i] != nullptr; i++) {
            file_assoc_register(stored.file_extensions[i], stored.on_open_file);
        }
    }

    return ESP_OK;
}

int app_registry_get_count(void)
{
    std::lock_guard<std::recursive_mutex> lock(s_registry_mutex);
    return static_cast<int>(s_apps.size());
}

esp_err_t app_registry_get_by_index(int index, app_desc_t *out)
{
    std::lock_guard<std::recursive_mutex> lock(s_registry_mutex);
    if (out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (index < 0 || index >= static_cast<int>(s_apps.size())) {
        return ESP_ERR_NOT_FOUND;
    }
    const app_desc_snapshot_t snapshot(s_apps[index]->desc);
    *out = {};
    out->id = strdup(snapshot.desc.id);
    out->name = strdup(snapshot.desc.name);
    if (out->id == nullptr || out->name == nullptr) {
        app_registry_release_snapshot(out);
        return ESP_ERR_NO_MEM;
    }
    out->icon_symbol = snapshot.desc.icon_symbol != nullptr ? strdup(snapshot.desc.icon_symbol) : nullptr;
    out->icon_bg_color = snapshot.desc.icon_bg_color != nullptr ? strdup(snapshot.desc.icon_bg_color) : nullptr;
    if ((snapshot.desc.icon_symbol != nullptr && out->icon_symbol == nullptr) ||
        (snapshot.desc.icon_bg_color != nullptr && out->icon_bg_color == nullptr)) {
        app_registry_release_snapshot(out);
        return ESP_ERR_NO_MEM;
    }
    out->icon_builder = snapshot.desc.icon_builder;
    out->icon_theme_refresh = snapshot.desc.icon_theme_refresh;
    out->on_launch = snapshot.desc.on_launch;
    out->on_open_file = snapshot.desc.on_open_file;
    if (snapshot.desc.file_extensions != nullptr) {
        const size_t count = snapshot.extensions.size();
        auto **extensions = static_cast<const char **>(calloc(count + 1, sizeof(char *)));
        if (extensions == nullptr) {
            app_registry_release_snapshot(out);
            return ESP_ERR_NO_MEM;
        }
        for (size_t i = 0; i < count; ++i) {
            extensions[i] = strdup(snapshot.extensions[i].c_str());
            if (extensions[i] == nullptr) {
                out->file_extensions = extensions;
                app_registry_release_snapshot(out);
                return ESP_ERR_NO_MEM;
            }
        }
        out->file_extensions = extensions;
    }
    return ESP_OK;
}

void app_registry_release_snapshot(app_desc_t *snapshot)
{
    if (snapshot == nullptr)
        return;
    free(const_cast<char *>(snapshot->id));
    free(const_cast<char *>(snapshot->name));
    free(const_cast<char *>(snapshot->icon_symbol));
    free(const_cast<char *>(snapshot->icon_bg_color));
    if (snapshot->file_extensions != nullptr) {
        for (size_t i = 0; snapshot->file_extensions[i] != nullptr; ++i)
            free(const_cast<char *>(snapshot->file_extensions[i]));
        free(const_cast<void *>(static_cast<const void *>(snapshot->file_extensions)));
    }
    *snapshot = {};
}

esp_err_t app_registry_find_by_id(const char *id, app_desc_t *out)
{
    std::lock_guard<std::recursive_mutex> lock(s_registry_mutex);
    if (id == nullptr || out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    for (const auto &app : s_apps) {
        if (strcmp(app->desc.id, id) == 0) {
            const app_desc_snapshot_t snapshot(app->desc);
            *out = {};
            out->id = strdup(snapshot.desc.id);
            out->name = strdup(snapshot.desc.name);
            if (out->id == nullptr || out->name == nullptr) {
                app_registry_release_snapshot(out);
                return ESP_ERR_NO_MEM;
            }
            out->icon_symbol = snapshot.desc.icon_symbol != nullptr ? strdup(snapshot.desc.icon_symbol) : nullptr;
            out->icon_bg_color = snapshot.desc.icon_bg_color != nullptr ? strdup(snapshot.desc.icon_bg_color) : nullptr;
            if ((snapshot.desc.icon_symbol != nullptr && out->icon_symbol == nullptr) ||
                (snapshot.desc.icon_bg_color != nullptr && out->icon_bg_color == nullptr)) {
                app_registry_release_snapshot(out);
                return ESP_ERR_NO_MEM;
            }
            out->icon_builder = snapshot.desc.icon_builder;
            out->icon_theme_refresh = snapshot.desc.icon_theme_refresh;
            out->on_launch = snapshot.desc.on_launch;
            out->on_open_file = snapshot.desc.on_open_file;
            if (snapshot.desc.file_extensions != nullptr) {
                const size_t count = snapshot.extensions.size();
                auto **extensions = static_cast<const char **>(calloc(count + 1, sizeof(char *)));
                if (extensions == nullptr) {
                    app_registry_release_snapshot(out);
                    return ESP_ERR_NO_MEM;
                }
                for (size_t i = 0; i < count; ++i) {
                    extensions[i] = strdup(snapshot.extensions[i].c_str());
                    if (extensions[i] == nullptr) {
                        out->file_extensions = extensions;
                        app_registry_release_snapshot(out);
                        return ESP_ERR_NO_MEM;
                    }
                }
                out->file_extensions = extensions;
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_registry_unregister(const char *id)
{
    std::lock_guard<std::recursive_mutex> lock(s_registry_mutex);
    if (id == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    for (auto it = s_apps.begin(); it != s_apps.end(); ++it) {
        if (strcmp((*it)->desc.id, id) == 0) {
            ESP_LOGI(TAG, "Aplicacao desregistrada: %s", id);
            s_apps.erase(it);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

std::vector<app_desc_snapshot_t> app_registry_get_all(void)
{
    std::lock_guard<std::recursive_mutex> lock(s_registry_mutex);
    std::vector<app_desc_snapshot_t> snapshot;
    snapshot.reserve(s_apps.size());
    for (const auto &app : s_apps) {
        snapshot.emplace_back(app->desc);
    }
    return snapshot;
}
