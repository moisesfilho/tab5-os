#pragma once

/* Stub de FreeRTOS para o build host. Define apenas o subconjunto usado por
 * tab5_nvs_worker.cpp; a SEMÂNTICA das primitivas (filas, semáforos binários,
 * mutex com dono, tasks estáticas) é implementada de verdade por
 * mocks/freertos_mock.cpp com std::thread/std::mutex/condition_variable —
 * nada de "falso positivo": timeout e fila síncrona são exercitados. */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t TickType_t;
typedef unsigned long UBaseType_t;
typedef long BaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

/* portMAX_DELAY = bloqueio indefinido; no mock 1 tick = 1 ms. */
#define portMAX_DELAY 0xffffffffUL
#define portTICK_PERIOD_MS 1u
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#ifdef __cplusplus
}
#endif

#include "portmacro.h"
