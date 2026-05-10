#pragma once

#include "esp_event.h"

/**
 * Initialize Wi-Fi in station mode and connect to the configured AP.
 * Blocks until connected or timeout (15s).
 *
 * @return ESP_OK on success, ESP_FAIL on timeout
 */
esp_err_t wifi_sta_init(void);

/**
 * Check if Wi-Fi is currently connected.
 */
bool wifi_sta_is_connected(void);
