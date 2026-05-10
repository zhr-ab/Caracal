#include "lv_mem_psram.h"
#include "esp_heap_caps.h"
#include <string.h>

void *lv_malloc_custom(size_t size)
{
    /* Try PSRAM first, fall back to internal RAM */
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

void lv_free_custom(void *ptr)
{
    if (ptr) {
        heap_caps_free(ptr);
    }
}
