#include "bsp/esp-bsp.h"

/* Mock do BSP: em host o "cartao SD" e o tmpdir gerenciado pelo
 * path_redirect, logo o mount sempre tem sucesso. */

extern "C" esp_err_t bsp_sdcard_mount(void)
{
    return ESP_OK;
}
