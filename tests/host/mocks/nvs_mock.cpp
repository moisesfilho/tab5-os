#include "nvs_mock.hpp"

#include <map>
#include <mutex>
#include <utility>

#include "nvs.h"

namespace {

struct Entry {
    int32_t i32 = 0;
    uint8_t u8 = 0;
};

using NvsKey = std::pair<std::string, std::string>;

std::map<NvsKey, Entry> &store()
{
    static std::map<NvsKey, Entry> instance;
    return instance;
}

std::mutex &store_mutex()
{
    static std::mutex mutex;
    return mutex;
}

bool namespace_exists(const std::string &ns)
{
    std::lock_guard<std::mutex> lock(store_mutex());
    for (const auto &kv : store()) {
        if (kv.first.first == ns) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace hostmock {

void nvs_reset()
{
    std::lock_guard<std::mutex> lock(store_mutex());
    store().clear();
}

void nvs_seed_i32(const std::string &ns, const std::string &key, int32_t value)
{
    std::lock_guard<std::mutex> lock(store_mutex());
    Entry entry;
    entry.i32 = value;
    store()[{ns, key}] = entry;
}

void nvs_seed_u8(const std::string &ns, const std::string &key, uint8_t value)
{
    std::lock_guard<std::mutex> lock(store_mutex());
    Entry entry;
    entry.u8 = value;
    store()[{ns, key}] = entry;
}

bool nvs_read_i32(const std::string &ns, const std::string &key, int32_t *out_value)
{
    std::lock_guard<std::mutex> lock(store_mutex());
    const auto it = store().find({ns, key});
    if (it == store().end()) {
        return false;
    }
    if (out_value != nullptr) {
        *out_value = it->second.i32;
    }
    return true;
}

bool nvs_read_u8(const std::string &ns, const std::string &key, uint8_t *out_value)
{
    std::lock_guard<std::mutex> lock(store_mutex());
    const auto it = store().find({ns, key});
    if (it == store().end()) {
        return false;
    }
    if (out_value != nullptr) {
        *out_value = it->second.u8;
    }
    return true;
}

} // namespace hostmock

extern "C" {

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    if (namespace_name == nullptr || out_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Semantica real: abrir READONLY em namespace inexistente falha. */
    if (open_mode == NVS_READONLY && !namespace_exists(namespace_name)) {
        return ESP_ERR_NOT_FOUND;
    }
    static uint32_t next_handle = 1;
    *out_handle = next_handle++;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value)
{
    (void)handle;
    int32_t value = 0;
    if (!hostmock::nvs_read_i32("tab5", key != nullptr ? key : "", &value)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out_value != nullptr) {
        *out_value = value;
    }
    return ESP_OK;
}

esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value)
{
    (void)handle;
    hostmock::nvs_seed_i32("tab5", key != nullptr ? key : "", value);
    return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)
{
    (void)handle;
    int32_t value = 0;
    if (!hostmock::nvs_read_i32("tab5", key != nullptr ? key : "", &value)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out_value != nullptr) {
        *out_value = (uint32_t)value;
    }
    return ESP_OK;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
    (void)handle;
    hostmock::nvs_seed_i32("tab5", key != nullptr ? key : "", (int32_t)value);
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value)
{
    (void)handle;
    uint8_t value = 0;
    if (!hostmock::nvs_read_u8("tab5", key != nullptr ? key : "", &value)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out_value != nullptr) {
        *out_value = value;
    }
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    (void)handle;
    hostmock::nvs_seed_u8("tab5", key != nullptr ? key : "", value);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

} // extern "C"
