#pragma once

#include "esp_err.h"

/**
 * Fetch HTML content from the given URL via HTTP GET.
 * The returned buffer is allocated from PSRAM and must be freed by caller.
 *
 * @param url  The full URL to fetch (http://...)
 * @param out_buf  Pointer to receive the allocated buffer with HTML content (null-terminated)
 * @param out_len  Pointer to receive the content length (excluding null terminator)
 * @return ESP_OK on success
 */
esp_err_t http_fetch(const char *url, char **out_buf, size_t *out_len);
