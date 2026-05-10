#include "renderer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "renderer";

#define COLOR_BG            lv_color_hex(0xFFFFFF)
#define COLOR_TEXT           lv_color_hex(0x222222)
#define COLOR_LINK           lv_color_hex(0x0000EE)
#define COLOR_HEADING        lv_color_hex(0x111111)
#define COLOR_HR             lv_color_hex(0xCCCCCC)
#define COLOR_IMG_BORDER     lv_color_hex(0x888888)
#define COLOR_IMG_BG         lv_color_hex(0xE0E0E0)
#define COLOR_IMG_TEXT       lv_color_hex(0x666666)
#define COLOR_FLEX_BG        lv_color_hex(0xF5F5F5)
#define COLOR_GRID_BG        lv_color_hex(0xF8F8F8)

static void (*s_link_click_cb)(const char *href) = NULL;

void renderer_init(void) { ESP_LOGI(TAG, "Renderer initialized"); }

void renderer_set_link_callback(void (*cb)(const char *href)) { s_link_click_cb = cb; }

static void link_click_handler(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    char *href = (char *)lv_obj_get_user_data(obj);
    if (href && s_link_click_cb) {
        ESP_LOGI(TAG, "Link clicked: %s", href);
        s_link_click_cb(href);
    }
}

static lv_obj_t *create_text_label(lv_obj_t *parent, const char *text,
                                    int x, int y, int width,
                                    int heading_level, bool is_link,
                                    const char *href)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);

    const lv_font_t *font = &lv_font_montserrat_14;
    switch (heading_level) {
    case 1: font = &lv_font_montserrat_24; break;
    case 2: font = &lv_font_montserrat_20; break;
    case 3: font = &lv_font_montserrat_20; break;
    case 4: font = &lv_font_montserrat_16; break;
    default: font = &lv_font_montserrat_14; break;
    }
    lv_obj_set_style_text_font(label, font, 0);

    if (is_link) {
        lv_obj_set_style_text_color(label, COLOR_LINK, 0);
        lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_UNDERLINE, 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
        if (href) {
            char *href_copy = (char *)heap_caps_malloc(strlen(href) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (href_copy) { strcpy(href_copy, href); lv_obj_set_user_data(label, href_copy); }
            lv_obj_add_event_cb(label, link_click_handler, LV_EVENT_CLICKED, NULL);
        }
    } else if (heading_level > 0) {
        lv_obj_set_style_text_color(label, COLOR_HEADING, 0);
    } else {
        lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
    }

    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static lv_obj_t *create_image_placeholder(lv_obj_t *parent, int x, int y,
                                           int width, int height,
                                           const char *alt, bool is_link,
                                           const char *href)
{
    lv_obj_t *img_box = lv_obj_create(parent);
    lv_obj_set_pos(img_box, x, y);
    lv_obj_set_size(img_box, width, height);
    lv_obj_set_style_bg_color(img_box, COLOR_IMG_BG, 0);
    lv_obj_set_style_bg_opa(img_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(img_box, COLOR_IMG_BORDER, 0);
    lv_obj_set_style_border_width(img_box, 2, 0);
    lv_obj_set_style_radius(img_box, 0, 0);
    lv_obj_set_style_pad_all(img_box, 4, 0);
    lv_obj_clear_flag(img_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *alt_label = lv_label_create(img_box);
    lv_obj_set_style_text_font(alt_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(alt_label, COLOR_IMG_TEXT, 0);
    lv_obj_align(alt_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(alt_label, alt && *alt ? alt : "[IMAGE]");

    if (is_link && href) {
        lv_obj_add_flag(img_box, LV_OBJ_FLAG_CLICKABLE);
        char *href_copy = (char *)heap_caps_malloc(strlen(href) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (href_copy) { strcpy(href_copy, href); lv_obj_set_user_data(img_box, href_copy); }
        lv_obj_add_event_cb(img_box, link_click_handler, LV_EVENT_CLICKED, NULL);
    }
    return img_box;
}

static lv_obj_t *create_hr(lv_obj_t *parent, int x, int y, int width)
{
    lv_obj_t *hr = lv_obj_create(parent);
    lv_obj_set_pos(hr, x, y);
    lv_obj_set_size(hr, width, 2);
    lv_obj_set_style_bg_color(hr, COLOR_HR, 0);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hr, 0, 0);
    lv_obj_set_style_radius(hr, 0, 0);
    lv_obj_set_style_pad_all(hr, 0, 0);
    lv_obj_clear_flag(hr, LV_OBJ_FLAG_SCROLLABLE);
    return hr;
}

/* Pro: render flex/grid container with background tint */
static void render_container_bg(lv_obj_t *parent, const layout_box_t *box,
                                 lv_color_t bg_color)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, box->x, box->y);
    lv_obj_set_size(cont, box->width, box->height);
    lv_obj_set_style_bg_color(cont, bg_color, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Render children inside this container with offset */
    for (int i = 0; i < box->child_count; i++) {
        const layout_box_t *child = box->children[i];
        /* Children positions are absolute; offset them into container */
        int rel_x = child->x - box->x;
        int rel_y = child->y - box->y;

        if (child->is_hr) {
            create_hr(cont, rel_x, rel_y, child->width);
        } else if (child->is_image) {
            create_image_placeholder(cont, rel_x, rel_y, child->width, child->height,
                                     child->alt, child->is_link, child->href);
        } else if (child->text && *child->text) {
            create_text_label(cont, child->text, rel_x, rel_y, child->width,
                              child->heading_level, child->is_link, child->href);
        }
    }
}

/* Recursively free user_data from LVGL objects */
static void free_obj_user_data(lv_obj_t *parent)
{
    uint32_t cnt = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        void *ud = lv_obj_get_user_data(child);
        if (ud) { heap_caps_free(ud); lv_obj_set_user_data(child, NULL); }
        free_obj_user_data(child);
    }
}

/* ---- public API ---- */

void renderer_render(const layout_box_t *layout_root, lv_obj_t *parent)
{
    if (!layout_root || !parent) return;

    ESP_LOGI(TAG, "Rendering layout tree with %d children", layout_root->child_count);
    lv_obj_set_style_bg_color(parent, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    for (int i = 0; i < layout_root->child_count; i++) {
        const layout_box_t *box = layout_root->children[i];

        if (box->is_hr) {
            create_hr(parent, box->x, box->y, box->width);
            continue;
        }
        if (box->is_image) {
            create_image_placeholder(parent, box->x, box->y, box->width, box->height,
                                     box->alt, box->is_link, box->href);
            continue;
        }
        /* Pro: flex/grid container */
        if (box->is_flex_container) {
            render_container_bg(parent, box, COLOR_FLEX_BG);
            continue;
        }
        if (box->is_grid_container) {
            render_container_bg(parent, box, COLOR_GRID_BG);
            continue;
        }
        if (box->text && *box->text) {
            create_text_label(parent, box->text, box->x, box->y, box->width,
                              box->heading_level, box->is_link, box->href);
        }
    }
    ESP_LOGI(TAG, "Rendering complete");
}

void renderer_clear(lv_obj_t *parent)
{
    if (!parent) return;
    free_obj_user_data(parent);
    lv_obj_clean(parent);
}
