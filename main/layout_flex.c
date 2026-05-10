#include "layout_flex.h"
#include "layout_grid.h"
#include "layout.h"
#include "css_style.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "layout_flex";

#define BODY_LINE_HEIGHT  18
#define HR_HEIGHT         4
#define HR_MARGIN         10
#define IMG_PLACEHOLDER_H 60

/* ---- Helpers ---- */

static char *str_dup_psram(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = (char *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (d) memcpy(d, s, len);
    return d;
}

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

static int text_width_approx(const char *text, int font_size)
{
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ) {
        if (*p < 0x80) {
            w += (font_size * 6 + 5) / 10;
            p++;
        } else {
            int bytes = (*p < 0xE0) ? 2 : (*p < 0xF0) ? 3 : 4;
            for (int i = 0; i < bytes && *p; i++) p++;
            w += font_size;
        }
    }
    return w;
}

static const int HEADING_SIZES[] = {16, 28, 24, 20, 18, 16, 16};

/* ---- Flex item measurement ---- */

typedef struct {
    const dom_node_t *node;
    int natural_width;    /* Intrinsic/content width */
    int natural_height;   /* Intrinsic/content height */
    int flex_grow;
    int flex_shrink;
    int flex_basis;       /* -1 = auto */
    int final_x;
    int final_y;
    int final_width;
    int final_height;
} flex_item_t;

/* Measure a single flex item's natural size */
static void measure_item(const dom_node_t *node, int cross_size, bool is_row,
                         int *out_w, int *out_h)
{
    if (!node) { *out_w = 0; *out_h = 0; return; }

    if (node->type == DOM_NODE_TEXT) {
        int font_size = 14;
        int tw = text_width_approx(node->text ? node->text : "", font_size);
        int line_h = font_size + 4;
        /* Wrap text within cross_size for column flex */
        int avail = is_row ? (cross_size > 0 ? cross_size : 480) : 480;
        int lines = 1;
        if (avail > 0 && tw > avail) {
            lines = (tw + avail - 1) / avail;
        }
        *out_w = tw > avail ? avail : tw;
        *out_h = line_h * lines;
        return;
    }

    /* Element node */
    int heading = get_heading_level(node->tag);
    int font_size = heading > 0 ? HEADING_SIZES[heading] : HEADING_SIZES[0];
    int line_h = font_size + 4;

    /* Specific elements */
    if (node->tag && strcmp(node->tag, "img") == 0) {
        *out_w = cross_size > 0 ? cross_size : 120;
        *out_h = IMG_PLACEHOLDER_H;
        return;
    }
    if (node->tag && strcmp(node->tag, "hr") == 0) {
        *out_w = cross_size > 0 ? cross_size : 0;
        *out_h = HR_HEIGHT;
        return;
    }

    /* Block element: measure children stacked vertically */
    int max_child_w = 0;
    int total_child_h = 0;
    int avail = cross_size > 0 ? cross_size : 480;

    /* If child is also flex/grid, recursively measure */
    if (node->style.display == CSS_DISPLAY_FLEX ||
        node->style.display == CSS_DISPLAY_GRID) {
        /* Rough estimate for nested containers */
        *out_w = node->style.width > 0 ? node->style.width : avail;
        *out_h = node->style.height > 0 ? node->style.height : 100;
        return;
    }

    for (int i = 0; i < node->child_count; i++) {
        int cw, ch;
        measure_item(node->children[i], avail, is_row, &cw, &ch);
        if (cw > max_child_w) max_child_w = cw;
        total_child_h += ch + 4;  /* margin */
    }

    /* Add padding */
    max_child_w += node->style.padding * 2;
    total_child_h += node->style.padding * 2;

    *out_w = node->style.width > 0 ? node->style.width : (max_child_w > 0 ? max_child_w : line_h);
    *out_h = node->style.height > 0 ? node->style.height : (total_child_h > 0 ? total_child_h : line_h);
}

/* ---- Main flex layout algorithm ---- */

void layout_flex(const dom_node_t *node, layout_box_t *parent,
                 int container_x, int *container_y,
                 int avail_width, int avail_height)
{
    if (!node) return;
    const css_style_t *style = &node->style;
    bool is_row = (style->flex_direction == CSS_FLEX_DIR_ROW);
    int gap = style->gap;
    int pad = style->padding;

    int main_size = is_row ? avail_width : avail_height;
    int cross_size = is_row ? avail_height : avail_width;
    if (cross_size <= 0) cross_size = 320;

    /* Inner content area after padding */
    int inner_main = main_size - pad * 2;
    int inner_cross = cross_size - pad * 2;

    /* 1. Collect and measure flex items */
    int item_count = 0;
    /* Count element children that are not whitespace-only text */
    for (int i = 0; i < node->child_count; i++) {
        const dom_node_t *child = node->children[i];
        if (child->type == DOM_NODE_TEXT) {
            const char *t = child->text;
            if (t) { bool all_sp = true; for (; *t; t++) if (*t!=' '&&*t!='\t'&&*t!='\n'&&*t!='\r'){all_sp=false;break;} if (all_sp) continue; }
        }
        item_count++;
    }

    if (item_count == 0) {
        *container_y += pad * 2;
        return;
    }

    flex_item_t *items = (flex_item_t *)heap_caps_malloc(
        sizeof(flex_item_t) * item_count,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!items) return;

    int idx = 0;
    for (int i = 0; i < node->child_count; i++) {
        const dom_node_t *child = node->children[i];
        if (child->type == DOM_NODE_TEXT) {
            const char *t = child->text;
            if (t) { bool all_sp = true; for (; *t; t++) if (*t!=' '&&*t!='\t'&&*t!='\n'&&*t!='\r'){all_sp=false;break;} if (all_sp) continue; }
        }
        items[idx].node = child;
        items[idx].flex_grow = (child->type == DOM_NODE_ELEMENT) ? child->style.flex_grow : 0;
        items[idx].flex_shrink = (child->type == DOM_NODE_ELEMENT) ? child->style.flex_shrink : 1;
        items[idx].flex_basis = (child->type == DOM_NODE_ELEMENT) ? child->style.flex_basis : -1;
        measure_item(child, inner_cross, is_row,
                     &items[idx].natural_width, &items[idx].natural_height);
        idx++;
    }

    /* 2. Distribute main-axis space */
    int total_natural_main = 0;
    int total_grow = 0;
    for (int i = 0; i < item_count; i++) {
        int natural_main = is_row ? items[i].natural_width : items[i].natural_height;
        int basis = items[i].flex_basis;
        if (basis >= 0) natural_main = basis;
        total_natural_main += natural_main;
        if (items[i].flex_grow > 0) total_grow += items[i].flex_grow;
    }
    total_natural_main += gap * (item_count > 1 ? item_count - 1 : 0);

    int free_space = inner_main - total_natural_main;

    /* 3. Position items along main axis */
    int pos = pad;  /* Start after padding */

    for (int i = 0; i < item_count; i++) {
        int natural_main = is_row ? items[i].natural_width : items[i].natural_height;
        int basis = items[i].flex_basis;
        if (basis >= 0) natural_main = basis;
        int natural_cross = is_row ? items[i].natural_height : items[i].natural_width;

        int final_main = natural_main;

        /* Apply flex-grow */
        if (free_space > 0 && items[i].flex_grow > 0 && total_grow > 0) {
            final_main += (free_space * items[i].flex_grow) / total_grow;
        }
        /* Apply flex-shrink */
        if (free_space < 0 && items[i].flex_shrink > 0) {
            int shrink = (-free_space * items[i].flex_shrink) / (item_count);
            final_main -= shrink;
            if (final_main < 20) final_main = 20;
        }

        /* Compute cross size based on align-items */
        int final_cross = natural_cross;
        if (style->align_items == CSS_ALIGN_STRETCH) {
            final_cross = inner_cross;
        } else if (style->align_items == CSS_ALIGN_CENTER) {
            /* Cross position will be centered below */
        }

        /* Compute positions */
        int item_x, item_y;
        if (is_row) {
            item_x = container_x + pos;
            items[i].final_width = final_main;
            items[i].final_height = final_cross;

            /* Align on cross axis */
            if (style->align_items == CSS_ALIGN_CENTER) {
                item_y = *container_y + pad + (inner_cross - final_cross) / 2;
            } else if (style->align_items == CSS_ALIGN_END) {
                item_y = *container_y + pad + inner_cross - final_cross;
            } else {
                item_y = *container_y + pad;
            }
            items[i].final_x = item_x;
            items[i].final_y = item_y;
        } else {
            item_y = *container_y + pos;
            items[i].final_width = final_cross;
            items[i].final_height = final_main;

            if (style->align_items == CSS_ALIGN_CENTER) {
                item_x = container_x + pad + (inner_cross - final_cross) / 2;
            } else if (style->align_items == CSS_ALIGN_END) {
                item_x = container_x + pad + inner_cross - final_cross;
            } else {
                item_x = container_x + pad;
            }
            items[i].final_x = item_x;
            items[i].final_y = item_y;
        }

        pos += final_main + gap;
    }

    /* 4. Apply justify-content */
    if (style->justify_content == CSS_JUSTIFY_CENTER && free_space > 0) {
        int offset = free_space / 2;
        for (int i = 0; i < item_count; i++) {
            if (is_row) items[i].final_x += offset;
            else        items[i].final_y += offset;
        }
    } else if (style->justify_content == CSS_JUSTIFY_END && free_space > 0) {
        int offset = free_space;
        for (int i = 0; i < item_count; i++) {
            if (is_row) items[i].final_x += offset;
            else        items[i].final_y += offset;
        }
    } else if (style->justify_content == CSS_JUSTIFY_SPACE_BETWEEN && item_count > 1 && free_space > 0) {
        int spacing = free_space / (item_count - 1);
        for (int i = 1; i < item_count; i++) {
            if (is_row) items[i].final_x = items[i-1].final_x + items[i-1].final_width + gap + spacing;
            else        items[i].final_y = items[i-1].final_y + items[i-1].final_height + gap + spacing;
        }
    }

    /* 5. Create layout boxes for each flex item */
    int max_cross = 0;
    for (int i = 0; i < item_count; i++) {
        const dom_node_t *child = items[i].node;

        if (child->type == DOM_NODE_ELEMENT && child->style.display == CSS_DISPLAY_NONE) {
            continue;  /* Skip hidden items */
        }

        layout_box_t *box = box_new(LAYOUT_BLOCK);
        box->x = items[i].final_x;
        box->y = items[i].final_y;
        box->width = items[i].final_width;
        box->height = items[i].final_height;

        if (child->type == DOM_NODE_TEXT) {
            box->text = child->text ? str_dup_psram(child->text) : NULL;
            box->type = LAYOUT_LINE;
        } else {
            /* Element */
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

            /* If child is flex/grid, recurse */
            if (child->style.display == CSS_DISPLAY_FLEX) {
                int sub_y = box->y;
                layout_flex(child, box, box->x, &sub_y, box->width, box->height);
                box->height = sub_y - box->y;
            } else if (child->style.display == CSS_DISPLAY_GRID) {
                int sub_y = box->y;
                layout_grid(child, box, box->x, &sub_y, box->width, box->height);
                box->height = sub_y - box->y;
            } else {
                /* Render text children inside this box */
                for (int j = 0; j < child->child_count; j++) {
                    if (child->children[j]->type == DOM_NODE_TEXT && child->children[j]->text) {
                        layout_box_t *line = box_new(LAYOUT_LINE);
                        line->text = str_dup_psram(child->children[j]->text);
                        line->heading_level = heading;
                        line->href = child->href;
                        line->is_link = (child->href != NULL);
                        line->x = 0;
                        line->y = 0;
                        line->width = box->width;
                        line->height = HEADING_SIZES[heading > 0 ? heading : 0] + 4;
                        box_add_child(box, line);
                    }
                }
            }
        }

        box_add_child(parent, box);
        int cross = is_row ? (items[i].final_y + items[i].final_height - *container_y)
                           : (items[i].final_x + items[i].final_width - container_x);
        if (cross > max_cross) max_cross = cross;
    }

    /* 6. Update container Y */
    if (is_row) {
        *container_y += max_cross + pad;
    } else {
        int total_h = pad * 2;
        for (int i = 0; i < item_count; i++) total_h += items[i].final_height + gap;
        *container_y += total_h;
    }

    heap_caps_free(items);
    ESP_LOGD(TAG, "Flex layout done: %d items, dir=%s",
             item_count, is_row ? "row" : "col");
}
