#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * CSS display property values.
 */
typedef enum {
    CSS_DISPLAY_BLOCK,
    CSS_DISPLAY_FLEX,
    CSS_DISPLAY_GRID,
    CSS_DISPLAY_INLINE,
    CSS_DISPLAY_NONE,
} css_display_t;

/**
 * Flex-direction values.
 */
typedef enum {
    CSS_FLEX_DIR_ROW,
    CSS_FLEX_DIR_COLUMN,
} css_flex_direction_t;

/**
 * Justify-content values.
 */
typedef enum {
    CSS_JUSTIFY_START,
    CSS_JUSTIFY_CENTER,
    CSS_JUSTIFY_END,
    CSS_JUSTIFY_SPACE_BETWEEN,
} css_justify_t;

/**
 * Align-items values.
 */
typedef enum {
    CSS_ALIGN_START,
    CSS_ALIGN_CENTER,
    CSS_ALIGN_END,
    CSS_ALIGN_STRETCH,
} css_align_t;

/**
 * Flex-wrap values.
 */
typedef enum {
    CSS_FLEX_NOWRAP,
    CSS_FLEX_WRAP,
} css_wrap_t;

#define CSS_GRID_MAX_TRACKS  12

/**
 * Parsed CSS style for a single DOM element.
 * All sizes in pixels; -1 means "auto".
 */
typedef struct {
    css_display_t display;

    /* Flex properties */
    css_flex_direction_t flex_direction;
    css_justify_t justify_content;
    css_align_t align_items;
    css_wrap_t flex_wrap;

    /* Grid properties */
    int grid_col_count;
    int grid_row_count;
    int grid_col_sizes[CSS_GRID_MAX_TRACKS];
    int grid_row_sizes[CSS_GRID_MAX_TRACKS];
    bool grid_col_is_percent[CSS_GRID_MAX_TRACKS];
    bool grid_row_is_percent[CSS_GRID_MAX_TRACKS];

    /* Common box model */
    int gap;
    int padding;
    int margin;
    int width;          /* -1 = auto */
    int height;         /* -1 = auto */
    int min_width;      /* -1 = auto */
    int min_height;     /* -1 = auto */

    /* Flex item properties */
    int flex_grow;
    int flex_shrink;
    int flex_basis;     /* -1 = auto */

    /* Font size (px) */
    int font_size;      /* -1 = inherit */

    /* Text color (0xRRGGBB, -1 = inherit) */
    int text_color;

    /* Whether any style was explicitly set */
    bool has_style;
} css_style_t;

/**
 * Return a default (no-style) css_style_t.
 */
css_style_t css_style_default(void);

/**
 * Parse a CSS style attribute string into a css_style_t.
 * Handles property: value pairs separated by semicolons.
 *
 * @param style_str  The raw style attribute value (e.g. "display:flex; gap:8px")
 * @return Parsed style struct
 */
css_style_t css_style_parse(const char *style_str);

/**
 * Get the display string for logging.
 */
const char *css_display_str(css_display_t d);
