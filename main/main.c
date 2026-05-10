#include "browser.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Minimal Browser ===");
    ESP_LOGI(TAG, "Initializing...");

    esp_err_t ret = browser_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Browser initialization failed!");
        /* Still enter the LVGL loop so the user can see the error on screen */
    }

    /* Enter the LVGL main loop - never returns */
    browser_run();
}
