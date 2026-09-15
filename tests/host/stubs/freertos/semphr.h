#pragma once

/* Subconjunto de freertos/semphr.h usado por tab5_nvs_worker.cpp. */

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xSTATIC_SEMAPHORE {
    void *_opaque[8]; /* storage opaco; o mock usa o endereço como identidade */
} StaticSemaphore_t;

typedef void *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *pxStaticSemaphore);
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *pxStaticSemaphore);
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);

#ifdef __cplusplus
}
#endif
