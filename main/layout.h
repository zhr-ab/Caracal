#pragma once

#include "html_dom.h"
#include <stdbool.h>

/**
 * Layout box types.
 */
typedef enum {
    LAYOUT_BLOCK,       /* Block-level element: <p>, <h1>, <div>, etc. */
    LAYOUT_INLINE,      /* Inline content within a block: text, <a>, <img> */
    LAYOUT_LINE,        /* A line of inline content within a block */
} layout_box_type_t;

/**
 * A single layout box - the output of the layout engine.
 * Each box corresponds to a renderable unit on screen.
 */
typedef struct layout_box {
    layout_box_type_t type;

    /* Position and dimensions */
    int x, y;
    int width, height;

    /* Content */
    const char *text;       /* Text to render (points into DOM, not owned) */
    const char *href;       /* If this is a link */
    const char *src;        /* If this is an image */
    const char *alt;        /* Image alt text */
    bool is_image;          /* true for <img> placeholder */
    bool is_hr;             /* true for <hr> separator */
    int heading_level;      /* 0=not heading, 1-6 for <h1>-<h6> */
    bool is_pre;            /* preformatted text */

    /* Link tracking */
    bool is_link;

    /* LVGL object reference (set during rendering) */
    void *lv_obj;

    /* Tree structure */
    struct layout_box **children;
    int child_count;
    int child_capacity;
} layout_box_t;

/**
 * Compute the block layout for a DOM tree.
 * Produces a flat-ish list of layout boxes suitable for rendering.
 *
 * @param dom_root  Root of the DOM tree
 * @param width     Available content width (pixels)
 * @return Root layout box, or NULL on failure.
 *         Caller must free with layout_free().
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
