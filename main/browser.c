#include "browser.h"
#include "wifi_sta.h"
#include "http_fetch.h"
#include "html_dom.h"
#include "layout.h"
#include "renderer.h"
#include "display.h"
#include "touch.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "browser";

static char *psram_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = (char *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (d) memcpy(d, s, len);
    return d;
}

/* UI elements */
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_url_bar = NULL;
static lv_obj_t *s_url_label = NULL;
static lv_obj_t *s_status_bar = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_content_area = NULL;
static lv_obj_t *s_loading_label = NULL;

static char *s_current_url = NULL;

/* ---- URL resolution ---- */

static char *resolve_url(const char *href, const char *base)
{
    if (!href) return NULL;
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
        return psram_strdup(href);
    }
    if (!base) return psram_strdup(href);
    const char *last_slash = strrchr(base, '/');
    if (!last_slash) return psram_strdup(href);

    if (href[0] == '/' && href[1] == '/') {
        const char *proto_end = strstr(base, "://");
        if (proto_end) {
            int proto_len = proto_end - base + 1;
            char *result = (char *)heap_caps_malloc(proto_len + strlen(href) + 1,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (result) { memcpy(result, base, proto_len); strcpy(result + proto_len, href); }
            return result;
        }
    }
    if (href[0] == '/') {
        const char *proto_end = strstr(base, "://");
        if (proto_end) {
            proto_end += 3;
            const char *path_start = strchr(proto_end, '/');
            int host_len = path_start ? (path_start - base) : strlen(base);
            char *result = (char *)heap_caps_malloc(host_len + strlen(href) + 1,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (result) { memcpy(result, base, host_len); strcpy(result + host_len, href); }
            return result;
        }
    }
    int base_dir_len = last_slash - base + 1;
    char *result = (char *)heap_caps_malloc(base_dir_len + strlen(href) + 1,
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (result) { memcpy(result, base, base_dir_len); strcpy(result + base_dir_len, href); }
    return result;
}

/* ---- Link click callback ---- */

static void on_link_click(const char *href)
{
    if (!href) return;
    ESP_LOGI(TAG, "Navigating to: %s", href);
    char *resolved = resolve_url(href, s_current_url);
    if (resolved) { browser_navigate(resolved); heap_caps_free(resolved); }
}

/* ---- UI creation ---- */

static void create_ui(void)
{
    int h_res = display_get_h_res();
    int v_res = display_get_v_res();
    s_screen = lv_scr_act();

    /* URL bar */
    s_url_bar = lv_obj_create(s_screen);
    lv_obj_set_pos(s_url_bar, 0, 0);
    lv_obj_set_size(s_url_bar, h_res, 30);
    lv_obj_set_style_bg_color(s_url_bar, lv_color_hex(0x1A237E), 0);
    lv_obj_set_style_bg_opa(s_url_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_url_bar, 0, 0);
    lv_obj_set_style_radius(s_url_bar, 0, 0);
    lv_obj_set_style_pad_all(s_url_bar, 2, 0);
    lv_obj_clear_flag(s_url_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_url_label = lv_label_create(s_url_bar);
    lv_obj_set_style_text_color(s_url_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_url_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_url_label, "Caracal Pro - Ready");
    lv_obj_align(s_url_label, LV_ALIGN_LEFT_MID, 4, 0);

    /* Status bar */
    s_status_bar = lv_obj_create(s_screen);
    lv_obj_set_pos(s_status_bar, 0, 30);
    lv_obj_set_size(s_status_bar, h_res, 20);
    lv_obj_set_style_bg_color(s_status_bar, lv_color_hex(0x283593), 0);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_status_bar, 0, 0);
    lv_obj_set_style_radius(s_status_bar, 0, 0);
    lv_obj_set_style_pad_all(s_status_bar, 1, 0);
    lv_obj_clear_flag(s_status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_status_label = lv_label_create(s_status_bar);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xBBDEFB), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_status_label, "");
    lv_obj_align(s_status_label, LV_ALIGN_LEFT_MID, 4, 0);

    /* Content area */
    s_content_area = lv_obj_create(s_screen);
    lv_obj_set_pos(s_content_area, 0, 50);
    lv_obj_set_size(s_content_area, h_res, v_res - 50);
    lv_obj_set_style_bg_color(s_content_area, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_content_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_content_area, 0, 0);
    lv_obj_set_style_radius(s_content_area, 0, 0);
    lv_obj_set_style_pad_all(s_content_area, 4, 0);
    lv_obj_set_scrollbar_mode(s_content_area, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(s_content_area, LV_DIR_VER);

    /* Loading indicator */
    s_loading_label = lv_label_create(s_content_area);
    lv_obj_set_style_text_color(s_loading_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(s_loading_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_loading_label, "Loading...");
    lv_obj_align(s_loading_label, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_add_flag(s_loading_label, LV_OBJ_FLAG_HIDDEN);
}

static void set_status(const char *msg) { if (s_status_label) lv_label_set_text(s_status_label, msg); }
static void set_url(const char *url) { if (s_url_label) lv_label_set_text(s_url_label, url ? url : ""); }
static void show_loading(bool show) {
    if (!s_loading_label) return;
    if (show) lv_obj_clear_flag(s_loading_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_loading_label, LV_OBJ_FLAG_HIDDEN);
}

/* ---- Public API ---- */

esp_err_t browser_init(void)
{
    ESP_LOGI(TAG, "Initializing Caracal Pro...");
    lv_init();
    ESP_LOGI(TAG, "Free PSRAM: %d bytes", (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    display_init();
    touch_init();
    create_ui();
    renderer_init();
    renderer_set_link_callback(on_link_click);

    set_status("Connecting to Wi-Fi...");
    esp_err_t ret = wifi_sta_init();
    if (ret != ESP_OK) { set_status("Wi-Fi failed!"); return ret; }
    set_status("Wi-Fi connected");

    ret = browser_navigate(CONFIG_BROWSER_DEFAULT_URL);
    ESP_LOGI(TAG, "Caracal Pro initialized");
    return ret;
}

esp_err_t browser_navigate(const char *url)
{
    if (!url) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Navigating to: %s", url);

    bool is_https = (strncmp(url, "https://", 8) == 0);
    set_url(url);
    set_status(is_https ? "Fetching (TLS)..." : "Fetching...");
    show_loading(true);
    lv_task_handler();

    if (s_current_url) { heap_caps_free(s_current_url); s_current_url = NULL; }
    s_current_url = psram_strdup(url);

    /* 1. Fetch */
    char *html = NULL;
    size_t html_len = 0;
    esp_err_t ret = http_fetch(url, &html, &html_len);
    if (ret != ESP_OK || !html) {
        set_status("Fetch failed!"); show_loading(false); return ret;
    }
    ESP_LOGI(TAG, "Fetched %d bytes (%s)", (int)html_len, is_https ? "HTTPS" : "HTTP");
    set_status("Parsing...");

    /* 2. Parse */
    dom_node_t *dom = html_parse(html);
    if (!dom) { set_status("Parse failed!"); show_loading(false); heap_caps_free(html); return ESP_FAIL; }
    heap_caps_free(html);
    set_status("Laying out...");

    /* 3. Layout */
    int content_width = display_get_h_res() - 8;
    layout_box_t *layout = layout_compute(dom, content_width);
    if (!layout) { set_status("Layout failed!"); show_loading(false); dom_tree_free(dom); return ESP_FAIL; }
    set_status("Rendering...");

    /* 4. Render */
    renderer_clear(s_content_area);
    renderer_render(layout, s_content_area);
    show_loading(false);

    int total_h = layout_total_height(layout);
    char status_buf[64];
    snprintf(status_buf, sizeof(status_buf), "OK - %dpx %s", total_h, is_https ? "[TLS]" : "");
    set_status(status_buf);

    /* 5. Cleanup */
    layout_free(layout);
    dom_tree_free(dom);
    ESP_LOGI(TAG, "Page rendered (%dpx, %s)", total_h, is_https ? "HTTPS" : "HTTP");
    return ESP_OK;
}

const char *browser_get_current_url(void) { return s_current_url; }
lv_obj_t *browser_get_screen(void) { return s_screen; }

void browser_run(void)
{
    ESP_LOGI(TAG, "Entering main loop");
    while (1) { lv_task_handler(); vTaskDelay(pdMS_TO_TICKS(5)); }
}
