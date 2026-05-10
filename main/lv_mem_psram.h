#pragma once

#include <stddef.h>

/**
 * PSRAM-backed memory allocator for LVGL.
 * Configured via CONFIG_LV_MEM_CUSTOM in sdkconfig.defaults.
 */

void *lv_malloc_custom(size_t size);
void lv_free_custom(void *ptr);
