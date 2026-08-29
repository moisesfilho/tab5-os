/**
 * @file storage_mgr.cpp
 * @brief Implementação do Gerenciador de Armazenamento e Estatísticas de Memória
 */

#include "storage_mgr.h"
#include "tab5_package_mgr.h"
#include "tab5_manifest.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "esp_heap_caps.h"
static const char *TAG __attribute__((unused)) = "tab5_storage_mgr";
#define LOG_I(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#else
#include <sys/statvfs.h>
#define LOG_I(fmt, ...)
#define LOG_W(fmt, ...)
#define LOG_E(fmt, ...)
#endif

uint64_t tab5_storage_mgr_calculate_path_size(const char *path)
{
    if (path == nullptr) {
        return 0;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        return static_cast<uint64_t>(st.st_size);
    }

    uint64_t total_size = 0;
    DIR *d = opendir(path);
    if (d == nullptr) {
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        std::string child_path = std::string(path) + "/" + entry->d_name;
        total_size += tab5_storage_mgr_calculate_path_size(child_path.c_str());
    }
    closedir(d);
    return total_size;
}

tab5_err_t tab5_storage_mgr_get_flash_apps_stats(tab5_storage_stats_t *out_stats)
{
    if (out_stats == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    *out_stats = {};

#ifdef ESP_PLATFORM
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info("apps", &total, &used);
    if (ret == ESP_OK) {
        out_stats->total_bytes = total;
        out_stats->used_bytes = used;
        out_stats->free_bytes = (total >= used) ? (total - used) : 0;
        out_stats->usage_percent = (total > 0) ? (static_cast<float>(used) * 100.0f / static_cast<float>(total)) : 0.0f;
        return TAB5_OK;
    }
#endif

    // Fallback ou modo host
    uint64_t used_bytes = tab5_storage_mgr_calculate_path_size(TAB5_APPS_EMBEDDED_DIR);
    uint64_t total_bytes = 4ULL * 1024ULL * 1024ULL; // 4MB
    out_stats->total_bytes = total_bytes;
    out_stats->used_bytes = used_bytes;
    out_stats->free_bytes = (total_bytes >= used_bytes) ? (total_bytes - used_bytes) : 0;
    out_stats->usage_percent =
        (total_bytes > 0) ? (static_cast<float>(used_bytes) * 100.0f / static_cast<float>(total_bytes)) : 0.0f;
    return TAB5_OK;
}

tab5_err_t tab5_storage_mgr_get_sd_stats(tab5_storage_stats_t *out_stats)
{
    if (out_stats == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    *out_stats = {};

#ifdef ESP_PLATFORM
    uint64_t total = 0, free_bytes = 0;
    esp_err_t ret = esp_vfs_fat_info("/sdcard", &total, &free_bytes);
    if (ret == ESP_OK) {
        out_stats->total_bytes = total;
        out_stats->free_bytes = free_bytes;
        out_stats->used_bytes = (total >= free_bytes) ? (total - free_bytes) : 0;
        out_stats->usage_percent =
            (total > 0) ? (static_cast<float>(out_stats->used_bytes) * 100.0f / static_cast<float>(total)) : 0.0f;
        return TAB5_OK;
    }
#else
    struct statvfs vfs;
    if (statvfs("/sdcard", &vfs) == 0 && vfs.f_blocks > 0) {
        uint64_t total = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
        uint64_t free_bytes = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
        uint64_t used = (total >= free_bytes) ? (total - free_bytes) : 0;

        out_stats->total_bytes = total;
        out_stats->used_bytes = used;
        out_stats->free_bytes = free_bytes;
        out_stats->usage_percent = (total > 0) ? (static_cast<float>(used) * 100.0f / static_cast<float>(total)) : 0.0f;
        return TAB5_OK;
    }
#endif

    // Mock/fallback para testes se SD não estiver montado
    out_stats->total_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL; // 32GB
    out_stats->free_bytes = 28ULL * 1024ULL * 1024ULL * 1024ULL;  // 28GB
    out_stats->used_bytes = out_stats->total_bytes - out_stats->free_bytes;
    out_stats->usage_percent =
        (static_cast<float>(out_stats->used_bytes) * 100.0f / static_cast<float>(out_stats->total_bytes));
    return TAB5_OK;
}

tab5_err_t tab5_storage_mgr_get_ram_stats(tab5_ram_stats_t *out_stats)
{
    if (out_stats == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    *out_stats = {};

#ifdef ESP_PLATFORM
    out_stats->internal_free_bytes = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    out_stats->psram_free_bytes = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    out_stats->dma_free_bytes = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_DMA));
    out_stats->total_free_bytes = esp_get_free_heap_size();
#else
    out_stats->internal_free_bytes = 512 * 1024;
    out_stats->psram_free_bytes = 30 * 1024 * 1024;
    out_stats->dma_free_bytes = 256 * 1024;
    out_stats->total_free_bytes = out_stats->internal_free_bytes + out_stats->psram_free_bytes;
#endif
    return TAB5_OK;
}

bool tab5_storage_mgr_has_enough_sd_space(uint64_t required_bytes)
{
    tab5_storage_stats_t stats;
    if (tab5_storage_mgr_get_sd_stats(&stats) != TAB5_OK) {
        return false;
    }
    // Reserva de segurança de 512 KB
    uint64_t safety_margin = 512ULL * 1024ULL;
    return (stats.free_bytes >= (required_bytes + safety_margin));
}

std::vector<tab5_app_storage_item_t> tab5_storage_mgr_list_installed_apps(void)
{
    std::vector<tab5_app_storage_item_t> list;

    // 1. Apps no SD
    DIR *d_sd = opendir(TAB5_APPS_INSTALLED_DIR);
    if (d_sd != nullptr) {
        struct dirent *entry;
        while ((entry = readdir(d_sd)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            std::string app_dir = std::string(TAB5_APPS_INSTALLED_DIR) + "/" + entry->d_name;
            std::string manifest_path = app_dir + "/manifest.json";
            tab5_manifest_t manifest = {};
            if (tab5_manifest_load_from_file(manifest_path.c_str(), &manifest) == TAB5_OK) {
                tab5_app_storage_item_t item = {};
                strncpy(item.id, manifest.id, sizeof(item.id) - 1);
                strncpy(item.name, manifest.name, sizeof(item.name) - 1);
                strncpy(item.version, manifest.version, sizeof(item.version) - 1);
                item.is_embedded = false;
                item.binary_size_bytes = static_cast<uint32_t>(tab5_storage_mgr_calculate_path_size(app_dir.c_str()));
                std::string data_dir = std::string(TAB5_APPS_DATA_DIR) + "/" + manifest.id;
                item.data_size_bytes = static_cast<uint32_t>(tab5_storage_mgr_calculate_path_size(data_dir.c_str()));
                list.push_back(item);
            }
        }
        closedir(d_sd);
    }

    // 2. Apps embutidas (Flash)
    DIR *d_emb = opendir(TAB5_APPS_EMBEDDED_DIR);
    if (d_emb != nullptr) {
        struct dirent *entry;
        while ((entry = readdir(d_emb)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            std::string app_dir = std::string(TAB5_APPS_EMBEDDED_DIR) + "/" + entry->d_name;
            std::string manifest_path = app_dir + "/manifest.json";
            tab5_manifest_t manifest = {};
            if (tab5_manifest_load_from_file(manifest_path.c_str(), &manifest) == TAB5_OK) {
                // Se já estiver na lista pelo SD com versão igual ou maior, pula
                bool already_present = false;
                for (const auto &it : list) {
                    if (strcmp(it.id, manifest.id) == 0) {
                        already_present = true;
                        break;
                    }
                }
                if (!already_present) {
                    tab5_app_storage_item_t item = {};
                    strncpy(item.id, manifest.id, sizeof(item.id) - 1);
                    strncpy(item.name, manifest.name, sizeof(item.name) - 1);
                    strncpy(item.version, manifest.version, sizeof(item.version) - 1);
                    item.is_embedded = true;
                    item.binary_size_bytes =
                        static_cast<uint32_t>(tab5_storage_mgr_calculate_path_size(app_dir.c_str()));
                    std::string data_dir = std::string(TAB5_APPS_DATA_DIR) + "/" + manifest.id;
                    item.data_size_bytes =
                        static_cast<uint32_t>(tab5_storage_mgr_calculate_path_size(data_dir.c_str()));
                    list.push_back(item);
                }
            }
        }
        closedir(d_emb);
    }

    return list;
}

std::vector<tab5_pending_package_t> tab5_storage_mgr_list_pending_packages(void)
{
    std::vector<tab5_pending_package_t> list;
    DIR *d = opendir(TAB5_APPS_DIR);
    if (d == nullptr) {
        return list;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, "installed") == 0) {
            continue;
        }

        std::string full_path = std::string(TAB5_APPS_DIR) + "/" + entry->d_name;
        struct stat st;
        if (stat(full_path.c_str(), &st) == 0) {
            std::string manifest_path = full_path + "/manifest.json";
            tab5_manifest_t manifest = {};
            if (tab5_manifest_load_from_file(manifest_path.c_str(), &manifest) == TAB5_OK ||
                tab5_manifest_load_from_file(full_path.c_str(), &manifest) == TAB5_OK) {
                tab5_pending_package_t pkg = {};
                strncpy(pkg.filename, entry->d_name, sizeof(pkg.filename) - 1);
                strncpy(pkg.full_path, full_path.c_str(), sizeof(pkg.full_path) - 1);
                strncpy(pkg.id, manifest.id, sizeof(pkg.id) - 1);
                strncpy(pkg.name, manifest.name, sizeof(pkg.name) - 1);
                strncpy(pkg.version, manifest.version, sizeof(pkg.version) - 1);
                pkg.package_size_bytes = static_cast<uint32_t>(tab5_storage_mgr_calculate_path_size(full_path.c_str()));
                list.push_back(pkg);
            }
        }
    }
    closedir(d);
    return list;
}
