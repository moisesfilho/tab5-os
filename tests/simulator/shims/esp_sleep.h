#pragma once

/* Sleep/deep-sleep nao existem no simulador: os pontos de chamada viram
 * no-op (a UI so inclui o header ou encerra a "sessao"). */

#include <cstdio>

inline void esp_deep_sleep_start(void)
{
    printf("sim: esp_deep_sleep_start() ignorado\n");
}
