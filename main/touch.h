#pragma once

#include "lvgl.h"

/**
 * Initialize the touch controller via I2C.
 * Configured via Kconfig (SDA, SCL, INT pins and I2C address).
 *
 * @return LVGL input device, or NULL on failure
 */
lv_indev_t *touch_init(void);
