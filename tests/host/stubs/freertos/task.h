#pragma once

/* Subconjunto de freertos/task.h usado por tab5_nvs_worker.cpp. */

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xSTATIC_TCB {
    void *_opaque[16]; /* storage opaco; o mock usa o endereço como identidade */
} StaticTask_t;

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode, const char *pcName, uint32_t ulStackDepth, void *pvParameters,
                               UBaseType_t uxPriority, StackType_t *puxStackBuffer, StaticTask_t *pxTaskBuffer);

#ifdef __cplusplus
}
#endif
