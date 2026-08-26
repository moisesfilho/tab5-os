#pragma once

/* Shim do esp_heap_caps: mapeia para malloc/memalign comum.
 * MALLOC_CAP_* sao ignorados (host tem um heap so). */

#include <cstdlib>
#include <cstdint>

#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_DMA 8

inline void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps)
{
    (void)caps;
    void *ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
}

inline void heap_caps_free(void *ptr)
{
    free(ptr);
}

inline size_t heap_caps_get_free_size(uint32_t caps)
{
    (void)caps;
    return 256u * 1024u * 1024u;
}

inline size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    (void)caps;
    return 64u * 1024u * 1024u;
}
