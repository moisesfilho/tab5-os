/* Mocks de link FreeRTOS para o build host (tests/host).
 *
 * Proposito: permitir compilar tab5_nvs_worker.cpp com ESP_PLATFORM em host e
 * exercitar a SEMANTICA real das primitivas usadas pelo worker (fila de 1
 * slot, semaforo binario, mutex com dono e task estatica) com threads do
 * C++11.  Nao ha stack switching real: cada "task" roda em uma std::thread
 * detached; o contrato de stack estatica em SRAM e coberto estaticamente por
 * tests/test_nvs_worker_contracts.py sobre o fonte de producao.
 *
 * Regras de fidelidade (nao aceitamos falso positivo):
 *  - xSemaphoreCreateBinaryStatic comeca com contador 0 (vazio), como no
 *    FreeRTOS real; o worker da o primeiro give no ensure_worker.
 *  - xSemaphoreGive de semaforo binario em contador 1 e no-op; de mutex fora
 *    do dono falha (pdFAIL).
 *  - Timeout em ticks; 1 tick = 1 ms (pdMS_TO_TICKS no stub).
 *  - A identidade do handle e o endereco do storage estatico (como o
 *    FreeRTOS real usa o Static*_t como armazenamento do objeto). */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hostmock {
namespace freertos {
namespace {

struct QueueState {
    size_t capacity;
    size_t item_size;
    std::deque<std::vector<uint8_t>> items;
    std::mutex mtx;
    std::condition_variable not_empty;
    std::condition_variable not_full;
};

struct SemState {
    bool is_mutex;
    uint32_t count; /* binario/mutex: 0 = indisponivel; 1 = livre */
    std::thread::id owner;
    std::mutex mtx;
    std::condition_variable cv;
};

struct TaskState {
    std::string name;
    uint32_t stack_depth_words;
    std::thread thread;
};

std::mutex s_registry_mutex;
std::map<QueueHandle_t, QueueState *> s_queues;
std::map<SemaphoreHandle_t, SemState *> s_sems;
std::map<TaskHandle_t, TaskState *> s_tasks;

template <typename T> T *registry_lookup(const std::map<void *, T *> &map, void *key)
{
    auto it = map.find(key);
    return it == map.end() ? nullptr : it->second;
}

} // namespace

size_t created_task_count()
{
    std::lock_guard<std::mutex> lock(s_registry_mutex);
    return s_tasks.size();
}

bool has_task(const char *name, uint32_t stack_depth_words)
{
    std::lock_guard<std::mutex> lock(s_registry_mutex);
    for (const auto &kv : s_tasks) {
        if (kv.second->name == name) {
            return kv.second->stack_depth_words == stack_depth_words;
        }
    }
    return false;
}

size_t created_queue_count()
{
    std::lock_guard<std::mutex> lock(s_registry_mutex);
    return s_queues.size();
}

bool has_single_slot_byte_queue()
{
    std::lock_guard<std::mutex> lock(s_registry_mutex);
    for (const auto &kv : s_queues) {
        if (kv.second->capacity == 1 && kv.second->item_size == 1) {
            return true;
        }
    }
    return false;
}

size_t created_semaphore_count()
{
    std::lock_guard<std::mutex> lock(s_registry_mutex);
    return s_sems.size();
}

bool has_mutex_semaphore()
{
    std::lock_guard<std::mutex> lock(s_registry_mutex);
    for (const auto &kv : s_sems) {
        if (kv.second->is_mutex) {
            return true;
        }
    }
    return false;
}

} // namespace freertos
} // namespace hostmock

extern "C" {

QueueHandle_t xQueueCreateStatic(UBaseType_t uxQueueLength, UBaseType_t uxItemSize, uint8_t *,
                                 StaticQueue_t *pxStaticQueue)
{
    auto *queue = new hostmock::freertos::QueueState();
    queue->capacity = uxQueueLength;
    queue->item_size = uxItemSize;
    std::lock_guard<std::mutex> lock(hostmock::freertos::s_registry_mutex);
    hostmock::freertos::s_queues[(QueueHandle_t)pxStaticQueue] = queue;
    return (QueueHandle_t)pxStaticQueue;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait)
{
    auto *queue = hostmock::freertos::registry_lookup(hostmock::freertos::s_queues, xQueue);
    if (queue == nullptr) {
        return pdFAIL;
    }
    std::unique_lock<std::mutex> lock(queue->mtx);
    const auto deadline = xTicksToWait == portMAX_DELAY
                              ? std::chrono::steady_clock::time_point::max()
                              : std::chrono::steady_clock::now() + std::chrono::milliseconds(xTicksToWait);
    while (queue->items.size() >= queue->capacity) {
        if (deadline == std::chrono::steady_clock::time_point::max()) {
            queue->not_full.wait(lock);
        } else if (queue->not_full.wait_until(lock, deadline) == std::cv_status::timeout) {
            return pdFAIL;
        }
    }
    std::vector<uint8_t> bytes(queue->item_size);
    std::memcpy(bytes.data(), pvItemToQueue, queue->item_size);
    queue->items.push_back(std::move(bytes));
    queue->not_empty.notify_one();
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
    auto *queue = hostmock::freertos::registry_lookup(hostmock::freertos::s_queues, xQueue);
    if (queue == nullptr) {
        return pdFAIL;
    }
    std::unique_lock<std::mutex> lock(queue->mtx);
    const auto deadline = xTicksToWait == portMAX_DELAY
                              ? std::chrono::steady_clock::time_point::max()
                              : std::chrono::steady_clock::now() + std::chrono::milliseconds(xTicksToWait);
    while (queue->items.empty()) {
        if (deadline == std::chrono::steady_clock::time_point::max()) {
            queue->not_empty.wait(lock);
        } else if (queue->not_empty.wait_until(lock, deadline) == std::cv_status::timeout) {
            return pdFAIL;
        }
    }
    std::memcpy(pvBuffer, queue->items.front().data(), queue->item_size);
    queue->items.pop_front();
    queue->not_full.notify_one();
    return pdPASS;
}

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *pxStaticSemaphore)
{
    auto *sem = new hostmock::freertos::SemState();
    sem->is_mutex = false;
    sem->count = 0; /* binario comeca vazio, como no FreeRTOS real */
    std::lock_guard<std::mutex> lock(hostmock::freertos::s_registry_mutex);
    hostmock::freertos::s_sems[(SemaphoreHandle_t)pxStaticSemaphore] = sem;
    return (SemaphoreHandle_t)pxStaticSemaphore;
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *pxStaticSemaphore)
{
    auto *sem = new hostmock::freertos::SemState();
    sem->is_mutex = true;
    sem->count = 1; /* mutex comeca livre */
    std::lock_guard<std::mutex> lock(hostmock::freertos::s_registry_mutex);
    hostmock::freertos::s_sems[(SemaphoreHandle_t)pxStaticSemaphore] = sem;
    return (SemaphoreHandle_t)pxStaticSemaphore;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
    auto *sem = hostmock::freertos::registry_lookup(hostmock::freertos::s_sems, xSemaphore);
    if (sem == nullptr) {
        return pdFAIL;
    }
    std::unique_lock<std::mutex> lock(sem->mtx);
    const auto deadline = xTicksToWait == portMAX_DELAY
                              ? std::chrono::steady_clock::time_point::max()
                              : std::chrono::steady_clock::now() + std::chrono::milliseconds(xTicksToWait);
    while (true) {
        if (sem->is_mutex) {
            if (sem->count == 1) {
                sem->count = 0;
                sem->owner = std::this_thread::get_id();
                return pdPASS;
            }
        } else if (sem->count > 0) {
            sem->count = 0;
            return pdPASS;
        }
        if (deadline == std::chrono::steady_clock::time_point::max()) {
            sem->cv.wait(lock);
        } else if (sem->cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            if (sem->is_mutex) {
                if (sem->count == 1) {
                    sem->count = 0;
                    sem->owner = std::this_thread::get_id();
                    return pdPASS;
                }
            } else if (sem->count > 0) {
                sem->count = 0;
                return pdPASS;
            }
            return pdFAIL;
        }
    }
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    auto *sem = hostmock::freertos::registry_lookup(hostmock::freertos::s_sems, xSemaphore);
    if (sem == nullptr) {
        return pdFAIL;
    }
    std::lock_guard<std::mutex> lock(sem->mtx);
    if (sem->is_mutex) {
        if (sem->owner != std::this_thread::get_id()) {
            return pdFAIL; /* mutex so pode ser devolvido pelo dono */
        }
        sem->owner = std::thread::id();
        sem->count = 1;
        sem->cv.notify_one();
        return pdPASS;
    }
    if (sem->count == 0) {
        sem->count = 1;
        sem->cv.notify_one();
    }
    return pdPASS;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode, const char *pcName, uint32_t ulStackDepth, void *pvParameters,
                               UBaseType_t, StackType_t *, StaticTask_t *pxTaskBuffer)
{
    auto *task = new hostmock::freertos::TaskState{pcName != nullptr ? pcName : "", ulStackDepth, std::thread()};
    task->thread = std::thread([pxTaskCode, pvParameters]() { pxTaskCode(pvParameters); });
    task->thread.detach();
    std::lock_guard<std::mutex> lock(hostmock::freertos::s_registry_mutex);
    hostmock::freertos::s_tasks[(TaskHandle_t)pxTaskBuffer] = task;
    return (TaskHandle_t)pxTaskBuffer;
}

} // extern "C"
