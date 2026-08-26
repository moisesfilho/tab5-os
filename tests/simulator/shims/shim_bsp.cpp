#include "bsp/esp-bsp.h"

#include <pthread.h>
#include <ctime>
#include <cerrno>

namespace {

pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;

} // namespace

extern "C" bool bsp_display_lock(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return pthread_mutex_trylock(&display_mutex) == 0;
    }

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    return pthread_mutex_timedlock(&display_mutex, &ts) == 0;
}

extern "C" void bsp_display_unlock(void)
{
    pthread_mutex_unlock(&display_mutex);
}

extern "C" int bsp_display_brightness_set(int brightness_percent)
{
    (void)brightness_percent;
    return 0;
}

extern "C" int bsp_sdcard_mount(void)
{
    /* O "SD" e o tmpdir do path_redirect; nada para montar. */
    return 0;
}
