#pragma once

/* Subconjunto de freertos/queue.h usado por tab5_nvs_worker.cpp. */

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xSTATIC_QUEUE {
    void *_opaque[8]; /* storage opaco; o mock usa o endereço como identidade */
} StaticQueue_t;

typedef void *QueueHandle_t;

QueueHandle_t xQueueCreateStatic(UBaseType_t uxQueueLength, UBaseType_t uxItemSize, uint8_t *pucQueueStorage,
                                 StaticQueue_t *pxStaticQueue);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait);

#ifdef __cplusplus
}
#endif
