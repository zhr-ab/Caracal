idf_component_register(SRCS "main.c"
                            "wifi_sta.c"
                            "http_fetch.c"
                            "html_dom.c"
                            "layout.c"
                            "renderer.c"
                            "display.c"
                            "touch.c"
                            "browser.c"
                            "lv_mem_psram.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_netif esp_event esp_http_client nvs_flash
                             esp_lcd driver lvgl gumbo spi_flash spiffs log
                    PRIV_REQUIRES esp_timer)
