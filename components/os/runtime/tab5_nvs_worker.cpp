#include "tab5_nvs_worker.h"

#if defined(ESP_PLATFORM)

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include <cstring>
#include <mutex>

namespace {

constexpr size_t NVS_NAME_SIZE = 16; /* NVS limits namespace/key to 15 chars. */
constexpr uint32_t REQUEST_TIMEOUT_MS = 2000;
constexpr uint32_t WORKER_STACK_WORDS = 2048; /* 8 KiB, statically in SRAM. */

struct Request {
    char ns[NVS_NAME_SIZE];
    char key[NVS_NAME_SIZE];
    uint8_t value;
    uint8_t result_value;
    bool is_set;
    tab5_err_t result;
    enum State : uint8_t { FREE, IN_FLIGHT, COMPLETED, ABANDONED } state;
};

StaticQueue_t s_queue_storage;
uint8_t s_queue_items[1];
QueueHandle_t s_queue = nullptr;
StaticSemaphore_t s_available_storage;
SemaphoreHandle_t s_available = nullptr;
StaticSemaphore_t s_done_storage;
SemaphoreHandle_t s_done = nullptr;
StaticSemaphore_t s_state_mutex_storage;
SemaphoreHandle_t s_state_mutex = nullptr;
StaticTask_t s_task_storage;
// cppcheck-suppress unknownMacro
StackType_t s_task_stack[WORKER_STACK_WORDS] DRAM_ATTR;
Request s_request{};
std::mutex s_init_mutex;
bool s_initialized = false;

void release_request_locked()
{
    s_request.state = Request::FREE;
    xSemaphoreGive(s_available);
}

void nvs_worker_task(void *)
{
    uint8_t slot;
    for (;;) {
        if (xQueueReceive(s_queue, &slot, portMAX_DELAY) != pdTRUE || slot != 0)
            continue;

        tab5_err_t result = TAB5_ERR_FAIL;
        uint8_t result_value = 0;
        nvs_handle_t handle = 0;
        if (s_request.is_set) {
            if (nvs_open(s_request.ns, NVS_READWRITE, &handle) == ESP_OK) {
                esp_err_t err = nvs_set_u8(handle, s_request.key, s_request.value);
                if (err == ESP_OK)
                    err = nvs_commit(handle);
                nvs_close(handle);
                result = err == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;
            }
        } else if (nvs_open(s_request.ns, NVS_READONLY, &handle) == ESP_OK) {
            esp_err_t err = nvs_get_u8(handle, s_request.key, &result_value);
            nvs_close(handle);
            result = err == ESP_OK ? TAB5_OK : TAB5_ERR_NOT_FOUND;
        } else {
            result = TAB5_ERR_NOT_FOUND;
        }

        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        s_request.result = result;
        s_request.result_value = result_value;
        if (s_request.state == Request::ABANDONED) {
            release_request_locked();
        } else {
            s_request.state = Request::COMPLETED;
            xSemaphoreGive(s_done);
        }
        xSemaphoreGive(s_state_mutex);
    }
}

bool ensure_worker()
{
    std::lock_guard<std::mutex> lock(s_init_mutex);
    if (s_initialized)
        return true;

    s_queue = xQueueCreateStatic(1, sizeof(uint8_t), s_queue_items, &s_queue_storage);
    s_available = xSemaphoreCreateBinaryStatic(&s_available_storage);
    s_done = xSemaphoreCreateBinaryStatic(&s_done_storage);
    s_state_mutex = xSemaphoreCreateMutexStatic(&s_state_mutex_storage);
    if (s_queue == nullptr || s_available == nullptr || s_done == nullptr || s_state_mutex == nullptr)
        return false;
    s_request.state = Request::FREE;
    xSemaphoreGive(s_available);
    if (xTaskCreateStatic(nvs_worker_task, "tab5_nvs", WORKER_STACK_WORDS, nullptr, 5, s_task_stack, &s_task_storage) ==
        nullptr)
        return false;
    s_initialized = true;
    return true;
}

tab5_err_t submit(bool is_set, const char *ns, const char *key, uint8_t value, uint8_t *out_value)
{
    if (ns == nullptr || key == nullptr || (!is_set && out_value == nullptr))
        return TAB5_ERR_INVALID_ARG;
    if (strnlen(ns, NVS_NAME_SIZE) >= NVS_NAME_SIZE || strnlen(key, NVS_NAME_SIZE) >= NVS_NAME_SIZE)
        return TAB5_ERR_INVALID_ARG;
    if (!ensure_worker())
        return TAB5_ERR_FAIL;

    const TickType_t timeout = pdMS_TO_TICKS(REQUEST_TIMEOUT_MS);
    if (xSemaphoreTake(s_available, timeout) != pdTRUE)
        return TAB5_ERR_TIMEOUT;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    while (xSemaphoreTake(s_done, 0) == pdTRUE) {
    }
    strncpy(s_request.ns, ns, sizeof(s_request.ns));
    strncpy(s_request.key, key, sizeof(s_request.key));
    s_request.value = value;
    s_request.result_value = 0;
    s_request.is_set = is_set;
    s_request.result = TAB5_ERR_FAIL;
    s_request.state = Request::IN_FLIGHT;
    xSemaphoreGive(s_state_mutex);

    uint8_t slot = 0;
    if (xQueueSend(s_queue, &slot, timeout) != pdTRUE) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        release_request_locked();
        xSemaphoreGive(s_state_mutex);
        return TAB5_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(s_done, timeout) != pdTRUE) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        if (s_request.state == Request::IN_FLIGHT)
            s_request.state = Request::ABANDONED;
        else if (s_request.state == Request::COMPLETED)
            release_request_locked();
        xSemaphoreGive(s_state_mutex);
        return TAB5_ERR_TIMEOUT;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    tab5_err_t result = s_request.result;
    if (!is_set && result == TAB5_OK)
        *out_value = s_request.result_value;
    release_request_locked();
    xSemaphoreGive(s_state_mutex);
    return result;
}

} // namespace

tab5_err_t tab5_nvs_worker_get_u8(const char *ns, const char *key, uint8_t *out_val)
{
    return submit(false, ns, key, 0, out_val);
}

tab5_err_t tab5_nvs_worker_set_u8(const char *ns, const char *key, uint8_t val)
{
    return submit(true, ns, key, val, nullptr);
}

#else

tab5_err_t tab5_nvs_worker_get_u8(const char *, const char *, uint8_t *out_val)
{
    if (out_val != nullptr)
        *out_val = 0;
    return TAB5_ERR_NOT_FOUND;
}

tab5_err_t tab5_nvs_worker_set_u8(const char *, const char *, uint8_t)
{
    return TAB5_OK;
}

#endif
