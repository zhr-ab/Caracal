#include "layout_grid.h"
#include "layout_flex.h"
#include "layout.h"
#include "css_style.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "layout_grid";

#define IMG_PLACEHOLDER_H 60
#define HEADING_SIZES_ARR 16, 28, 24, 20, 18, 16, 16

static layout_box_t *box_new(layout_box_type_t type)
{
    layout_box_t *b = (layout_box_t *)heap_caps_malloc(sizeof(layout_box_t),
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b) return NULL;
    memset(b, 0, sizeof(layout_box_t));
    b->type = type;
    b->child_capacity = 4;
    b->children = (layout_box_t **)heap_caps_malloc(
        sizeof(layout_box_t *) * b->child_capacity,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return b;
}

static void box_add_child(layout_box_t *parent, layout_box_t *child)
{
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity *= 2;
        parent->children = (layout_box_t **)heap_caps_realloc(
            parent->children,
            sizeof(layout_box_t *) * parent->child_capacity,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    parent->children[parent->child_count++] = child;
}

static int get_heading_level(const char *tag)
{
    if (!tag) return 0;
    if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0')
        return tag[1] - '0';
    return 0;
}

static char *str_dup_psram(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = (char *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (d) memcpy(d, s, len);
    return d;
}

/* ---- Grid layout algorithm ---- */

void layout_grid(const dom_node_t *node, layout_box_t *parent,
                 int container_x, int *container_y,
                 int avail_width, int avail_height)
{
    if (!node) return;
    const css_style_t *style = &node->style;
    int gap = style->gap;
    int pad = style->padding;
    int col_count = style->grid_col_count;
    int row_count = style->grid_row_count;

    /* If no template specified, infer column count from children */
    if (col_count <= 0) {
        /* Default: single column */
        col_count = 1;
    }

    /* Count visible children */
    int child_count = 0;
    for (int i = 0; i < node->child_count; i++) {
        const dom_node_t *child = node->children[i];
        if (child->type == DOM_NODE_TEXT) {
            const char *t = child->text;
            bool all_sp = true;
            if (t) { for (; *t; t++) if (*t!=' '&&*t!='\t'&&*t!='\n'&&*t!='\r'){all_sp=false;break;} }
            if (all_sp) continue;
        }
        if (child->type == DOM_NODE_ELEMENT && child->style.display == CSS_DISPLAY_NONE)
            continue;
        child_count++;
    }

    if (child_count == 0) {
        *container_y += pad * 2;
        return;
    }

    /* Calculate row count if not specified */
    if (row_count <= 0) {
        row_count = (child_count + col_count - 1) / col_count;
    }

    /* ---- Calculate track sizes ---- */
    int inner_width = avail_width - pad * 2;
    int *col_widths = (int *)heap_caps_malloc(sizeof(int) * col_count,
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int *row_heights = (int *)heap_caps_malloc(sizeof(int) * row_count,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!col_widths || !row_heights) {
        if (col_widths) heap_caps_free(col_widths);
        if (row_heights) heap_caps_free(row_heights);
        return;
    }

    /* Resolve column widths */
    int total_fixed = 0;
    int fr_count = 0;
    for (int c = 0; c < col_count; c++) {
        if (c < CSS_GRID_MAX_TRACKS) {
            int sz = style->grid_col_sizes[c];
            bool pct = style->grid_col_is_percent[c];
            if (sz == -2) {
                /* 1fr track */
                fr_count++;
                col_widths[c] = 0;
            } else if (sz > 0) {
                if (pct) {
                    col_widths[c] = (inner_width * sz) / 100;
                } else {
                    col_widths[c] = sz;
                }
                total_fixed += col_widths[c];
            } else {
                /* auto */
                fr_count++;
                col_widths[c] = 0;
            }
        } else {
            fr_count++;
            col_widths[c] = 0;
        }
    }

    /* Distribute remaining space to fr/auto tracks */
    int remaining = inner_width - total_fixed - gap * (col_count - 1);
    if (remaining < 0) remaining = 0;
    if (fr_count > 0) {
        int fr_size = remaining / fr_count;
        for (int c = 0; c < col_count; c++) {
            if (col_widths[c] == 0) col_widths[c] = fr_size;
        }
    }

    /* Default row heights — will be measured below */
    for (int r = 0; r < row_count; r++) {
        if (r < CSS_GRID_MAX_TRACKS && style->grid_row_sizes[r] > 0) {
            bool pct = style->grid_row_is_percent[r];
            if (pct) {
                row_heights[r] = (320 * style->grid_row_sizes[r]) / 100;
            } else {
                row_heights[r] = style->grid_row_sizes[r];
            }
        } else {
            row_heights[r] = 0;  /* Will be measured */
        }
    }

    /* ---- Collect children into grid cells ---- */
    typedef struct {
        const dom_node_t *node;
        int col;
        int row;
    } grid_cell_t;

    grid_cell_t *cells = (grid_cell_t *)heap_caps_malloc(
        sizeof(grid_cell_t) * child_count,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cells) {
        heap_caps_free(col_widths);
        heap_caps_free(row_heights);
        return;
    }

    int ci = 0;
    for (int i = 0; i < node->child_count && ci < child_count; i++) {
        const dom_node_t *child = node->children[i];
        if (child->type == DOM_NODE_TEXT) {
            const char *t = child->text;
            bool all_sp = true;
            if (t) { for (; *t; t++) if (*t!=' '&&*t!='\t'&&*t!='\n'&&*t!='\r'){all_sp=false;break;} }
            if (all_sp) continue;
        }
        if (child->type == DOM_NODE_ELEMENT && child->style.display == CSS_DISPLAY_NONE)
            continue;

        cells[ci].node = child;
        cells[ci].col = ci % col_count;
        cells[ci].row = ci / col_count;
        ci++;
    }

    /* ---- Measure row heights for auto rows ---- */
    static const int heading_sizes[] = {16, 28, 24, 20, 18, 16, 16};
    for (int r = 0; r < row_count; r++) {
        if (row_heights[r] > 0) continue;  /* Already set by template */

        int max_h = 20;  /* Minimum row height */
        for (int i = 0; i < child_count; i++) {
            if (cells[i].row != r) continue;
            const dom_node_t *child = cells[i].node;

            if (child->type == DOM_NODE_TEXT) {
                int len = child->text ? strlen(child->text) : 0;
                int col_w = col_widths[cells[i].col];
                int lines = 1;
                if (col_w > 0 && len > 0) {
                    lines = (len * 7 + col_w - 1) / col_w;  /* rough estimate */
                }
                int h = lines * 18;
                if (h > max_h) max_h = h;
            } else {
                int heading = get_heading_level(child->tag);
                int h = heading > 0 ? (heading_sizes[heading] + 8) : 24;

                if (child->tag && strcmp(child->tag, "img") == 0)
                    h = IMG_PLACEHOLDER_H;
                if (child->style.height > 0)
                    h = child->style.height;
                if (child->style.display == CSS_DISPLAY_FLEX ||
                    child->style.display == CSS_DISPLAY_GRID)
                    h = 100;  /* rough estimate for nested containers */

                if (h > max_h) max_h = h;
            }
        }
        row_heights[r] = max_h;
    }

    /* ---- Position items and create layout boxes ---- */
    for (int i = 0; i < child_count; i++) {
        const dom_node_t *child = cells[i].node;
        int col = cells[i].col;
    int row = cells[i].row;

        /* Calculate X position */
        int x = container_x + pad;
        for (int c = 0; c < col; c++) x += col_widths[c] + gap;

        /* Calculate Y position */
        int y = *container_y + pad;
        for (int r = 0; r < row; r++) y += row_heights[r] + gap;

        int w = col_widths[col];
        int h = row_heights[row];

        /* Create layout box */
        layout_box_t *box = box_new(LAYOUT_BLOCK);
        box->x = x;
        box->y = y;
        box->width = w;
        box->height = h;

        if (child->type == DOM_NODE_TEXT) {
            box->type = LAYOUT_LINE;
            box->text = child->text ? str_dup_psram(child->text) : NULL;
        } else {
            int heading = get_heading_level(child->tag);
            box->heading_level = heading;

            if (child->tag && strcmp(child->tag, "img") == 0) {
                box->is_image = true;
                box->src = child->src;
                box->alt = child->alt;
            }
            if (child->tag && strcmp(child->tag, "hr") == 0) {
                box->is_hr = true;
            }
            if (child->href) {
                box->is_link = true;
                box->href = child->href;
            }

            /* Recurse into flex/grid children */
            if (child->style.display == CSS_DISPLAY_FLEX) {
                int sub_y = y;
                layout_flex(child, box, x, &sub_y, w, h);
            } else if (child->style.display == CSS_DISPLAY_GRID) {
                int sub_y = y;
                layout_grid(child, box, x, &sub_y, w, h);
            } else {
                /* Block: render text children inside this cell */
                for (int j = 0; j < child->child_count; j++) {
                    if (child->children[j]->type == DOM_NODE_TEXT && child->children[j]->text) {
                        layout_box_t *line = box_new(LAYOUT_LINE);
                        line->text = str_dup_psram(child->children[j]->text);
                        line->heading_level = heading;
                        line->href = child->href;
                        line->is_link = (child->href != NULL);
                        line->x = 0;
                        line->y = 0;
                        line->width = w;
                        line->height = heading_sizes[heading > 0 ? heading : 0] + 4;
                        box_add_child(box, line);
                    }
                }
            }
        }

        box_add_child(parent, box);
    }

    /* Update container Y past the grid */
    int total_h = pad * 2;
    for (int r = 0; r < row_count; r++) total_h += row_heights[r];
    total_h += gap * (row_count > 1 ? row_count - 1 : 0);
    *container_y += total_h;

    heap_caps_free(cells);
    heap_caps_free(col_widths);
    heap_caps_free(row_heights);

    ESP_LOGD(TAG, "Grid layout done: %dx%d, %d items", col_count, row_count, child_count);
}
