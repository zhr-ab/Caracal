#pragma once

#include "lvgl.h"

/**
 * Initialize the ST7796S LCD display and create an LVGL display driver.
 * Uses SPI interface configured via Kconfig pins.
 *
 * @return LVGL display object
 */
lv_disp_t *display_init(void);

/**
 * Get the horizontal resolution.
 */
int display_get_h_res(void);

/**
 * Get the vertical resolution.
 */
int display_get_v_res(void);
