#pragma once

/* FreeRTOS minimo para o simulador host: apenas o que a UI usa
 * (pdMS_TO_TICKS, vTaskDelay e secoes criticas no-op). */

#include <cstdint>
#include <unistd.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdPASS 1

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
