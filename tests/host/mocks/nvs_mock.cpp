#include "nvs_mock.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
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

/* nvs_open mapeia handle -> namespace; get/set/commit operam no ns do
 * handle, como no NVS real. */
std::map<nvs_handle_t, std::string> &handle_namespaces()
{
    static std::map<nvs_handle_t, std::string> instance;
    return instance;
}

std::mutex &handle_mutex()
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

/* Stall one-shot: a proxima chamada a nvs_open() dorme antes de retornar. */
std::atomic<uint32_t> s_stall_open_ms{0};
std::atomic<bool> s_stall_consumed{false};
std::mutex s_stall_mutex;
std::condition_variable s_stall_cv;

/* Instrumentacao do nvs_commit. */
std::atomic<uint64_t> s_commit_count{0};
std::mutex s_commit_thread_mutex;
std::thread::id s_last_commit_thread;

} // namespace

namespace hostmock {

void nvs_reset()
{
    std::lock_guard<std::mutex> lock(store_mutex());
    store().clear();
    std::lock_guard<std::mutex> handle_lock(handle_mutex());
    handle_namespaces().clear();
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

void nvs_stall_next_open_once(uint32_t delay_ms)
{
    s_stall_open_ms.store(delay_ms);
    s_stall_consumed.store(false);
}

void nvs_stall_clear()
{
    s_stall_open_ms.store(0);
    s_stall_consumed.store(true);
    s_stall_cv.notify_all();
}

bool nvs_stall_was_consumed()
{
    return s_stall_consumed.load();
}

bool nvs_wait_stall_consumed(uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(s_stall_mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!s_stall_consumed.load()) {
        if (s_stall_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            return s_stall_consumed.load();
        }
    }
    return true;
}

uint64_t nvs_commit_call_count()
{
    return s_commit_count.load();
}

std::thread::id nvs_last_commit_thread()
{
    std::lock_guard<std::mutex> lock(s_commit_thread_mutex);
    return s_last_commit_thread;
}

} // namespace hostmock

extern "C" {

namespace {

std::string namespace_of(nvs_handle_t handle)
{
    std::lock_guard<std::mutex> lock(handle_mutex());
    const auto it = handle_namespaces().find(handle);
    return it == handle_namespaces().end() ? std::string() : it->second;
}

} // namespace

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    if (namespace_name == nullptr || out_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Semantica real: abrir READONLY em namespace inexistente falha. */
    if (open_mode == NVS_READONLY && !namespace_exists(namespace_name)) {
        return ESP_ERR_NOT_FOUND;
    }
    const uint32_t stall_ms = s_stall_open_ms.exchange(0);
    if (stall_ms > 0) {
        /* Marca o stall como consumido ANTES do sleep: o teste usa isso como
         * handshake para saber que o worker ja esta preso dentro do nvs_open
         * (e que o semaforo "available" esta retido pela requisicao em voo),
         * enquanto o sleep ainda nao terminou. */
        s_stall_consumed.store(true);
        s_stall_cv.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(stall_ms));
    }
    static uint32_t next_handle = 1;
    *out_handle = next_handle++;
    {
        std::lock_guard<std::mutex> lock(handle_mutex());
        handle_namespaces()[*out_handle] = namespace_name;
    }
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    std::lock_guard<std::mutex> lock(handle_mutex());
    handle_namespaces().erase(handle);
}

esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value)
{
    const std::string ns = namespace_of(handle);
    int32_t value = 0;
    if (!hostmock::nvs_read_i32(ns, key != nullptr ? key : "", &value)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out_value != nullptr) {
        *out_value = value;
    }
    return ESP_OK;
}

esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value)
{
    hostmock::nvs_seed_i32(namespace_of(handle), key != nullptr ? key : "", value);
    return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)
{
    const std::string ns = namespace_of(handle);
    int32_t value = 0;
    if (!hostmock::nvs_read_i32(ns, key != nullptr ? key : "", &value)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out_value != nullptr) {
        *out_value = (uint32_t)value;
    }
    return ESP_OK;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
    hostmock::nvs_seed_i32(namespace_of(handle), key != nullptr ? key : "", (int32_t)value);
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value)
{
    const std::string ns = namespace_of(handle);
    uint8_t value = 0;
    if (!hostmock::nvs_read_u8(ns, key != nullptr ? key : "", &value)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out_value != nullptr) {
        *out_value = value;
    }
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    hostmock::nvs_seed_u8(namespace_of(handle), key != nullptr ? key : "", value);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    s_commit_count.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(s_commit_thread_mutex);
        s_last_commit_thread = std::this_thread::get_id();
    }
    return ESP_OK;
}

} // extern "C"
