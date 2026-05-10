#include "http_fetch.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "http_fetch";

#define MAX_HTML_SIZE  CONFIG_BROWSER_MAX_HTML_SIZE
#define HTTP_TIMEOUT   CONFIG_BROWSER_HTTP_TIMEOUT_MS

typedef struct {
    char  *buf;
    size_t len;
    size_t capacity;
} fetch_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    fetch_ctx_t *ctx = (fetch_ctx_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (ctx->len + evt->data_len >= ctx->capacity) {
            ESP_LOGE(TAG, "Response too large (max %d bytes)", MAX_HTML_SIZE);
            return ESP_FAIL;
        }
        memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
        ctx->len += evt->data_len;
        ctx->buf[ctx->len] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* TLS certificate verification callback */
static esp_err_t tls_cert_cb(void *arg, const char *cert_pem, size_t cert_len)
{
    /* Skip verification if configured */
    return ESP_OK;
}

esp_err_t http_fetch(const char *url, char **out_buf, size_t *out_len)
{
    ESP_LOGI(TAG, "Fetching: %s", url);

    char *buf = (char *)heap_caps_malloc(MAX_HTML_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes from PSRAM", MAX_HTML_SIZE);
        return ESP_ERR_NO_MEM;
    }

    fetch_ctx_t ctx = { .buf = buf, .len = 0, .capacity = MAX_HTML_SIZE };
    buf[0] = '\0';

    bool is_https = (strncmp(url, "https://", 8) == 0);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &ctx,
        .timeout_ms = HTTP_TIMEOUT,
        .buffer_size = 4096,
        .disable_auto_redirect = false,
        .max_redirection_count = 3,
    };

    /* HTTPS configuration */
    if (is_https) {
#if CONFIG_BROWSER_SKIP_TLS_VERIFY
        config.skip_cert_common_name_check = true;
        /* Don't set cert_pem — skip certificate verification entirely */
        ESP_LOGI(TAG, "HTTPS: skipping certificate verification");
#else
        /* For production: load CA cert from SPIFFS or embedded PEM */
        config.cert_pem = NULL;  /* Add your CA cert here */
        config.skip_cert_common_name_check = false;
#endif
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        heap_caps_free(buf);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        heap_caps_free(buf);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    int content_len = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "HTTP status=%d, fetched=%d bytes, content-length=%d",
             status, (int)ctx.len, content_len);

    esp_http_client_cleanup(client);

    if (status >= 400) {
        ESP_LOGE(TAG, "HTTP error: %d", status);
        heap_caps_free(buf);
        return ESP_FAIL;
    }

    *out_buf = buf;
    *out_len = ctx.len;
    return ESP_OK;
}
