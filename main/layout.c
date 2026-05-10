#include "layout.h"
#include "layout_flex.h"
#include "layout_grid.h"
#include "css_style.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "layout";

static const int HEADING_SIZES[] = {16, 28, 24, 20, 18, 16, 16};

#define BODY_LINE_HEIGHT   18
#define PARAGRAPH_MARGIN   8
#define HR_HEIGHT          4
#define HR_MARGIN          10
#define IMG_PLACEHOLDER_H  60

/* ---- helpers ---- */

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

static char *str_dup_psram(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = (char *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (d) memcpy(d, s, len);
    return d;
}

static bool is_block_tag(const char *tag)
{
    if (!tag) return false;
    static const char *block_tags[] = {
        "p", "div", "h1", "h2", "h3", "h4", "h5", "h6",
        "ul", "ol", "li", "blockquote", "pre",
        "table", "tr", "td", "th",
        "section", "article", "header", "footer", "nav", "main",
        "hr", "br",
        NULL
    };
    for (int i = 0; block_tags[i]; i++) {
        if (strcmp(tag, block_tags[i]) == 0) return true;
    }
    return false;
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
        if (*p < 0x80) { w += (font_size * 6 + 5) / 10; p++; }
        else { int bytes = (*p < 0xE0) ? 2 : (*p < 0xF0) ? 3 : 4;
               for (int i = 0; i < bytes && *p; i++) p++; w += font_size; }
    }
    return w;
}

/* ---- inline accumulation (same as base version) ---- */

typedef struct {
    char *buf;
    int len;
    int capacity;
    bool has_link;
    const char *href;
    bool is_image;
    const char *img_src;
    const char *img_alt;
    int heading_level;
    bool is_hr;
    bool is_pre;
} inline_accum_t;

static inline_accum_t s_accum = {0};

static void accum_reset(void)
{
    if (s_accum.buf) { s_accum.buf[0] = '\0'; s_accum.len = 0; }
    s_accum.has_link = false; s_accum.href = NULL;
    s_accum.is_image = false; s_accum.img_src = NULL; s_accum.img_alt = NULL;
    s_accum.heading_level = 0; s_accum.is_hr = false; s_accum.is_pre = false;
}

static void accum_ensure(int extra)
{
    if (!s_accum.buf) {
        s_accum.capacity = 256;
        s_accum.buf = (char *)heap_caps_malloc(s_accum.capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_accum.buf[0] = '\0'; s_accum.len = 0;
    }
    while (s_accum.len + extra + 1 >= s_accum.capacity) {
        s_accum.capacity *= 2;
        s_accum.buf = (char *)heap_caps_realloc(s_accum.buf, s_accum.capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
}

static void accum_append(const char *text)
{
    int len = strlen(text);
    accum_ensure(len);
    memcpy(s_accum.buf + s_accum.len, text, len);
    s_accum.len += len;
    s_accum.buf[s_accum.len] = '\0';
}

static void accum_free(void)
{
    if (s_accum.buf) { heap_caps_free(s_accum.buf); s_accum.buf = NULL; }
    s_accum.capacity = 0; s_accum.len = 0;
}

/* Emit accumulated inline content */
static void emit_accumulated(layout_box_t *parent, int *cursor_y, int content_width)
{
    if (s_accum.is_hr) {
        layout_box_t *box = box_new(LAYOUT_BLOCK);
        box->is_hr = true; box->x = 0; box->y = *cursor_y;
        box->width = content_width; box->height = HR_HEIGHT;
        box_add_child(parent, box);
        *cursor_y += HR_HEIGHT + HR_MARGIN;
        accum_reset(); return;
    }
    if (s_accum.is_image) {
        layout_box_t *box = box_new(LAYOUT_BLOCK);
        box->is_image = true; box->src = s_accum.img_src; box->alt = s_accum.img_alt;
        box->x = 0; box->y = *cursor_y;
        box->width = content_width; box->height = IMG_PLACEHOLDER_H;
        if (s_accum.has_link) { box->href = s_accum.href; box->is_link = true; }
        box_add_child(parent, box);
        *cursor_y += IMG_PLACEHOLDER_H + PARAGRAPH_MARGIN;
        accum_reset(); return;
    }
    if (!s_accum.buf || s_accum.len == 0) { accum_reset(); return; }

    int font_size = s_accum.heading_level > 0 ? HEADING_SIZES[s_accum.heading_level] : HEADING_SIZES[0];
    int line_h = font_size + 4;
    int y = *cursor_y + PARAGRAPH_MARGIN;
    const char *text = s_accum.buf;
    const char *line_start = text;
    const char *word_start = text;
    int current_line_width = 0;

    while (*word_start) {
        const char *word_end = word_start;
        if ((unsigned char)*word_end >= 0x80) {
            int bytes = ((unsigned char)*word_end < 0xE0) ? 2 : ((unsigned char)*word_end < 0xF0) ? 3 : 4;
            for (int i = 0; i < bytes && *word_end; i++) word_end++;
        } else {
            while (*word_end && *word_end != ' ' && *word_end != '\n') word_end++;
        }
        int word_len = word_end - word_start;
        char word[128];
        if (word_len >= (int)sizeof(word)) word_len = sizeof(word) - 1;
        memcpy(word, word_start, word_len); word[word_len] = '\0';
        int word_w = text_width_approx(word, font_size);

        if (current_line_width > 0 && current_line_width + word_w > content_width) {
            int line_len = word_start - line_start;
            if (line_len > 0) {
                char *lt = (char *)heap_caps_malloc(line_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                memcpy(lt, line_start, line_len); lt[line_len] = '\0';
                layout_box_t *line = box_new(LAYOUT_LINE);
                line->text = lt; line->href = s_accum.href; line->is_link = s_accum.has_link;
                line->heading_level = s_accum.heading_level; line->is_pre = s_accum.is_pre;
                line->x = 0; line->y = y; line->width = content_width; line->height = line_h;
                box_add_child(parent, line); y += line_h;
            }
            line_start = word_start; current_line_width = word_w;
        } else { current_line_width += word_w; }
        if (*word_end == ' ') word_end++;
        word_start = word_end;
    }
    int remaining = s_accum.buf + s_accum.len - line_start;
    if (remaining > 0) {
        char *lt = (char *)heap_caps_malloc(remaining + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        memcpy(lt, line_start, remaining); lt[remaining] = '\0';
        layout_box_t *line = box_new(LAYOUT_LINE);
        line->text = lt; line->href = s_accum.href; line->is_link = s_accum.has_link;
        line->heading_level = s_accum.heading_level; line->is_pre = s_accum.is_pre;
        line->x = 0; line->y = y; line->width = content_width; line->height = line_h;
        box_add_child(parent, line); y += line_h;
    }
    *cursor_y = y + PARAGRAPH_MARGIN;
    accum_reset();
}

/* ---- recursive layout with flex/grid dispatch ---- */

static void layout_node(const dom_node_t *node, layout_box_t *parent,
                         int *cursor_y, int content_width,
                         const char *current_href, bool in_pre);

static void layout_node(const dom_node_t *node, layout_box_t *parent,
                         int *cursor_y, int content_width,
                         const char *current_href, bool in_pre)
{
    if (!node) return;

    if (node->type == DOM_NODE_TEXT) {
        if (node->text && *node->text) accum_append(node->text);
        return;
    }

    /* Element node — check CSS display property */
    if (node->style.display == CSS_DISPLAY_NONE) return;

    /* === FLEX dispatch === */
    if (node->style.display == CSS_DISPLAY_FLEX) {
        emit_accumulated(parent, cursor_y, content_width);
        layout_box_t *flex_box = box_new(LAYOUT_BLOCK);
        flex_box->is_flex_container = true;
        flex_box->x = 0;
        flex_box->y = *cursor_y;
        flex_box->width = content_width;
        flex_box->height = 0;  /* Will be set by layout_flex */
        box_add_child(parent, flex_box);
        int saved_y = *cursor_y;
        layout_flex(node, flex_box, 0, cursor_y, content_width, -1);
        flex_box->height = *cursor_y - saved_y;
        return;
    }

    /* === GRID dispatch === */
    if (node->style.display == CSS_DISPLAY_GRID) {
        emit_accumulated(parent, cursor_y, content_width);
        layout_box_t *grid_box = box_new(LAYOUT_BLOCK);
        grid_box->is_grid_container = true;
        grid_box->x = 0;
        grid_box->y = *cursor_y;
        grid_box->width = content_width;
        grid_box->height = 0;
        box_add_child(parent, grid_box);
        int saved_y = *cursor_y;
        layout_grid(node, grid_box, 0, cursor_y, content_width, -1);
        grid_box->height = *cursor_y - saved_y;
        return;
    }

    /* === Default block layout (same as base Caracal) === */
    int heading = get_heading_level(node->tag);

    if (node->tag && strcmp(node->tag, "br") == 0) { emit_accumulated(parent, cursor_y, content_width); return; }
    if (node->tag && strcmp(node->tag, "hr") == 0) { emit_accumulated(parent, cursor_y, content_width); s_accum.is_hr = true; emit_accumulated(parent, cursor_y, content_width); return; }
    if (node->tag && strcmp(node->tag, "img") == 0) {
        emit_accumulated(parent, cursor_y, content_width);
        s_accum.is_image = true; s_accum.img_src = node->src; s_accum.img_alt = node->alt;
        if (current_href) { s_accum.has_link = true; s_accum.href = current_href; }
        emit_accumulated(parent, cursor_y, content_width); return;
    }

    if (is_block_tag(node->tag)) emit_accumulated(parent, cursor_y, content_width);

    const char *prev_href = current_href;
    bool entered_link = false;
    if (node->tag && strcmp(node->tag, "a") == 0 && node->href) {
        current_href = node->href; s_accum.has_link = true; s_accum.href = node->href; entered_link = true;
    }
    if (heading > 0) s_accum.heading_level = heading;
    bool was_in_pre = in_pre;
    if (node->tag && strcmp(node->tag, "pre") == 0) { in_pre = true; s_accum.is_pre = true; }

    for (int i = 0; i < node->child_count; i++)
        layout_node(node->children[i], parent, cursor_y, content_width, current_href, in_pre);

    if (is_block_tag(node->tag)) emit_accumulated(parent, cursor_y, content_width);
    if (entered_link) { emit_accumulated(parent, cursor_y, content_width); current_href = prev_href; }
    if (heading > 0) s_accum.heading_level = 0;
    if (node->tag && strcmp(node->tag, "pre") == 0) in_pre = was_in_pre;
}

/* ---- public API ---- */

layout_box_t *layout_compute(const dom_node_t *dom_root, int width)
{
    if (!dom_root) return NULL;

    layout_box_t *root = box_new(LAYOUT_BLOCK);
    if (!root) return NULL;
    root->width = width; root->height = 0;

    accum_reset();

    /* Find <body> */
    const dom_node_t *body = dom_root;
    if (dom_root->type == DOM_NODE_ELEMENT && dom_root->tag) {
        for (int i = 0; i < dom_root->child_count; i++) {
            dom_node_t *child = dom_root->children[i];
            if (child->type == DOM_NODE_ELEMENT && child->tag && strcmp(child->tag, "body") == 0) {
                body = child; break;
            }
            for (int j = 0; j < child->child_count; j++) {
                dom_node_t *gc = child->children[j];
                if (gc->type == DOM_NODE_ELEMENT && gc->tag && strcmp(gc->tag, "body") == 0) {
                    body = gc; break;
                }
            }
        }
    }

    int cursor_y = 0;
    layout_node(body, root, &cursor_y, width, NULL, false);
    emit_accumulated(root, &cursor_y, width);

    root->height = cursor_y;
    accum_free();
    ESP_LOGI(TAG, "Layout computed: %dpx total height", root->height);
    return root;
}

static void layout_free_recursive(layout_box_t *box)
{
    if (!box) return;
    for (int i = 0; i < box->child_count; i++) layout_free_recursive(box->children[i]);
    if (box->children) heap_caps_free(box->children);
    if (box->text) heap_caps_free((void *)box->text);
    heap_caps_free(box);
}

void layout_free(layout_box_t *root) { layout_free_recursive(root); }

int layout_total_height(const layout_box_t *root) { return root ? root->height : 0; }
