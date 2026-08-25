#pragma once

#include <cstdio>
#include <cstdlib>

/* No simulador nao ha reboot: aborta com mensagem visivel. */
inline void esp_restart(void)
{
    fprintf(stderr, "sim: esp_restart() chamado\n");
    abort();
}
