#pragma once

#include "html_dom.h"
#include <stdbool.h>

/**
 * Layout box types.
 */
typedef enum {
    LAYOUT_BLOCK,
    LAYOUT_INLINE,
    LAYOUT_LINE,
} layout_box_type_t;

/**
 * A single layout box - the output of the layout engine.
 */
typedef struct layout_box {
    layout_box_type_t type;

    /* Position and dimensions */
    int x, y;
    int width, height;

    /* Content */
    const char *text;
    const char *href;
    const char *src;
    const char *alt;
    bool is_image;
    bool is_hr;
    int heading_level;
    bool is_pre;
    bool is_link;
    bool is_flex_container;   /* Pro: marks flex containers */
    bool is_grid_container;   /* Pro: marks grid containers */

    /* LVGL object reference (set during rendering) */
    void *lv_obj;

    /* Tree structure */
    struct layout_box **children;
    int child_count;
    int child_capacity;
} layout_box_t;

/**
 * Compute the layout for a DOM tree.
 * Dispatches to block, flex, or grid layout based on CSS display property.
 *
 * @param dom_root  Root of the DOM tree
 * @param width     Available content width (pixels)
 * @return Root layout box, or NULL on failure.
 */
layout_box_t *layout_compute(const dom_node_t *dom_root, int width);

/**
 * Free a layout tree.
 */
void layout_free(layout_box_t *root);

/**
 * Get total content height of the layout tree.
 */
int layout_total_height(const layout_box_t *root);
