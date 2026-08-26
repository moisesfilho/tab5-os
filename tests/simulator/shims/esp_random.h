#pragma once

/* RNG deterministico por padrao no simulador (seed fixa) para que
 * capturas de tela sejam reprodutiveis. */

#include <cstdint>
#include <cstdlib>

inline uint32_t esp_random(void)
{
    return (uint32_t)rand();
}
