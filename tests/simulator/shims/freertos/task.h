#pragma once

#include "freertos/FreeRTOS.h"

inline void vTaskDelay(TickType_t ticks)
{
    usleep(ticks * 1000U);
}
