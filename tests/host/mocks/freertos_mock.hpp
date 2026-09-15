#pragma once

#include <cstddef>

#include "freertos/FreeRTOS.h"

/* Observabilidade do kernel mock (mocks/freertos_mock.cpp) para os testes
 * comportamentais do tab5_nvs_worker: permite verificar que a task foi
 * criada estaticamente com a stack pedida, a fila tem 1 slot de 1 byte e os
 * semaforos estaticos existem. */

namespace hostmock {
namespace freertos {

size_t created_task_count();
bool has_task(const char *name, uint32_t stack_depth_words);
size_t created_queue_count();
bool has_single_slot_byte_queue();
size_t created_semaphore_count();
bool has_mutex_semaphore();

} // namespace freertos
} // namespace hostmock
