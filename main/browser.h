#pragma once

#include "lvgl.h"
#include "esp_err.h"

/**
 * Initialize the browser: display, touch, Wi-Fi, LVGL UI.
 * Blocks until Wi-Fi is connected.
 *
 * @return ESP_OK on success
 */
esp_err_t browser_init(void);

/**
 * Navigate to a URL. Fetches HTML, parses, lays out, and renders.
 *
 * @param url  Full URL to navigate to
 * @return ESP_OK on success
 */
esp_err_t browser_navigate(const char *url);

/**
 * Get the current URL.
 */
const char *browser_get_current_url(void);

/**
 * Get the LVGL screen object (for the main task loop).
 */
lv_obj_t *browser_get_screen(void);

/**
 * Run the browser LVGL task loop (does not return).
 * Call this from app_main after browser_init.
 */
void browser_run(void);
