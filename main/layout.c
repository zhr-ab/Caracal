#include "layout.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "layout";

/* Font sizes for headings (approximate line height in pixels) */
static const int HEADING_SIZES[] = {
    16,  /* body text */
    28,  /* h1 */
    24,  /* h2 */
    20,  /* h3 */
    18,  /* h4 */
    16,  /* h5 */
    16,  /* h6 */
};

#define BODY_LINE_HEIGHT   18
#define PARAGRAPH_MARGIN   8
#define HR_HEIGHT          4
#define HR_MARGIN          10
#define IMG_PLACEHOLDER_H  60
#define INDENT_SIZE        8

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

static bool is_block_tag(const char *tag)
{
    if (!tag) return false;
    /* Block-level elements that create their own box */
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
    if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0') {
        return tag[1] - '0';
    }
    return 0;
}

/* Approximate text width at body font size.
 * For CJK characters, each is ~font_size wide.
 * For ASCII, each is ~font_size * 0.6 wide.
 */
static int text_width_approx(const char *text, int font_size)
{
    int w = 0;
    bool prev_cjk = false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ) {
        if (*p < 0x80) {
            /* ASCII */
            w += (font_size * 6 + 5) / 10;
            prev_cjk = false;
            p++;
        } else {
            /* UTF-8 multibyte - assume CJK or similar wide char */
            int bytes = (*p < 0xE0) ? 2 : (*p < 0xF0) ? 3 : 4;
            for (int i = 0; i < bytes && *p; i++) p++;
            w += font_size;
            prev_cjk = true;
        }
    }
    return w;
}

/* ---- layout state ---- */

typedef struct {
    int cursor_y;       /* Current Y position */
    int content_width;  /* Available width */
    int line_x;         /* Current X position on line */
    int line_height;    /* Current line height */
    const char *current_href;  /* If we're inside an <a> tag */
    bool in_pre;        /* Inside <pre> tag */
} layout_state_t;

/* Forward declarations */
static void layout_node(const dom_node_t *node, layout_box_t *parent, layout_state_t *state);
static void flush_inline(layout_box_t *parent, layout_state_t *state);

/* ---- inline text accumulation ---- */
/* We accumulate inline text content and create line boxes when needed */

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
} inline_accum_t;

static inline_accum_t s_accum = {0};

static void accum_reset(void)
{
    if (s_accum.buf) {
        s_accum.buf[0] = '\0';
        s_accum.len = 0;
    }
    s_accum.has_link = false;
    s_accum.href = NULL;
    s_accum.is_image = false;
    s_accum.img_src = NULL;
    s_accum.img_alt = NULL;
    s_accum.heading_level = 0;
    s_accum.is_hr = false;
}

static void accum_ensure(int extra)
{
    if (!s_accum.buf) {
        s_accum.capacity = 256;
        s_accum.buf = (char *)heap_caps_malloc(s_accum.capacity,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_accum.buf[0] = '\0';
        s_accum.len = 0;
    }
    while (s_accum.len + extra + 1 >= s_accum.capacity) {
        s_accum.capacity *= 2;
        s_accum.buf = (char *)heap_caps_realloc(s_accum.buf, s_accum.capacity,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
    if (s_accum.buf) {
        heap_caps_free(s_accum.buf);
        s_accum.buf = NULL;
    }
    s_accum.capacity = 0;
    s_accum.len = 0;
}

/* Emit the accumulated inline content as layout boxes */
static void emit_accumulated(layout_box_t *parent, layout_state_t *state)
{
    if (s_accum.is_hr) {
        layout_box_t *box = box_new(LAYOUT_BLOCK);
        box->is_hr = true;
        box->x = 0;
        box->y = state->cursor_y;
        box->width = state->content_width;
        box->height = HR_HEIGHT;
        box_add_child(parent, box);
        state->cursor_y += HR_HEIGHT + HR_MARGIN;
        accum_reset();
        return;
    }

    if (s_accum.is_image) {
        layout_box_t *box = box_new(LAYOUT_BLOCK);
        box->is_image = true;
        box->src = s_accum.img_src;
        box->alt = s_accum.img_alt;
        box->x = 0;
        box->y = state->cursor_y;
        box->width = state->content_width;
        box->height = IMG_PLACEHOLDER_H;
        if (s_accum.has_link) box->href = s_accum.href;
        box->is_link = s_accum.has_link;
        box_add_child(parent, box);
        state->cursor_y += IMG_PLACEHOLDER_H + PARAGRAPH_MARGIN;
        accum_reset();
        return;
    }

    if (!s_accum.buf || s_accum.len == 0) {
        accum_reset();
        return;
    }

    int font_size = s_accum.heading_level > 0
                    ? HEADING_SIZES[s_accum.heading_level]
                    : HEADING_SIZES[0];
    int line_h = font_size + 4;

    /* Word-wrap: split text into lines that fit the content width */
    const char *text = s_accum.buf;
    int y = state->cursor_y;

    /* Add top margin for headings and paragraphs */
    y += PARAGRAPH_MARGIN;

    const char *line_start = text;
    const char *word_start = text;
    int current_line_width = 0;

    while (*word_start) {
        /* Find next word boundary */
        const char *word_end = word_start;
        /* Skip to end of word (or CJK char) */
        if ((unsigned char)*word_end >= 0x80) {
            /* CJK: one character = one word */
            int bytes = ((unsigned char)*word_end < 0xE0) ? 2
                      : ((unsigned char)*word_end < 0xF0) ? 3 : 4;
            for (int i = 0; i < bytes && *word_end; i++) word_end++;
        } else {
            while (*word_end && *word_end != ' ' && *word_end != '\n') word_end++;
        }

        int word_len = word_end - word_start;
        char word[128];
        if (word_len >= (int)sizeof(word)) word_len = sizeof(word) - 1;
        memcpy(word, word_start, word_len);
        word[word_len] = '\0';

        int word_w = text_width_approx(word, font_size);

        if (current_line_width > 0 && current_line_width + word_w > state->content_width) {
            /* Emit current line */
            int line_len = word_start - line_start;
            if (line_len > 0) {
                char *line_text = (char *)heap_caps_malloc(line_len + 1,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                memcpy(line_text, line_start, line_len);
                line_text[line_len] = '\0';

                layout_box_t *line = box_new(LAYOUT_LINE);
                line->text = line_text;
                line->href = s_accum.href;
                line->is_link = s_accum.has_link;
                line->heading_level = s_accum.heading_level;
                line->is_pre = state->in_pre;
                line->x = 0;
                line->y = y;
                line->width = state->content_width;
                line->height = line_h;
                box_add_child(parent, line);

                y += line_h;
            }
            line_start = word_start;
            current_line_width = word_w;
        } else {
            current_line_width += word_w;
        }

        /* Skip space after word */
        if (*word_end == ' ') word_end++;

        word_start = word_end;
    }

    /* Emit last line */
    int remaining = s_accum.buf + s_accum.len - line_start;
    if (remaining > 0) {
        char *line_text = (char *)heap_caps_malloc(remaining + 1,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        memcpy(line_text, line_start, remaining);
        line_text[remaining] = '\0';

        layout_box_t *line = box_new(LAYOUT_LINE);
        line->text = line_text;
        line->href = s_accum.href;
        line->is_link = s_accum.has_link;
        line->heading_level = s_accum.heading_level;
        line->is_pre = state->in_pre;
        line->x = 0;
        line->y = y;
        line->width = state->content_width;
        line->height = line_h;
        box_add_child(parent, line);

        y += line_h;
    }

    state->cursor_y = y + PARAGRAPH_MARGIN;
    accum_reset();
}

/* ---- recursive layout ---- */

static void layout_node(const dom_node_t *node, layout_box_t *parent, layout_state_t *state)
{
    if (!node) return;

    if (node->type == DOM_NODE_TEXT) {
        /* Accumulate text content */
        if (node->text && *node->text) {
            accum_append(node->text);
        }
        return;
    }

    /* Element node */
    int heading = get_heading_level(node->tag);

    /* Handle specific elements */
    if (node->tag && strcmp(node->tag, "br") == 0) {
        /* Line break - emit current line */
        emit_accumulated(parent, state);
        return;
    }

    if (node->tag && strcmp(node->tag, "hr") == 0) {
        emit_accumulated(parent, state);  /* flush before hr */
        s_accum.is_hr = true;
        emit_accumulated(parent, state);
        return;
    }

    if (node->tag && strcmp(node->tag, "img") == 0) {
        emit_accumulated(parent, state);  /* flush before img */
        s_accum.is_image = true;
        s_accum.img_src = node->src;
        s_accum.img_alt = node->alt;
        if (state->current_href) {
            s_accum.has_link = true;
            s_accum.href = state->current_href;
        }
        emit_accumulated(parent, state);
        return;
    }

    /* Block elements: flush accumulated inline content first */
    if (is_block_tag(node->tag)) {
        emit_accumulated(parent, state);
    }

    /* Enter <a> tag */
    const char *prev_href = state->current_href;
    bool entered_link = false;
    if (node->tag && strcmp(node->tag, "a") == 0 && node->href) {
        state->current_href = node->href;
        s_accum.has_link = true;
        s_accum.href = node->href;
        entered_link = true;
    }

    /* Enter heading */
    if (heading > 0) {
        s_accum.heading_level = heading;
    }

    /* Enter <pre> */
    bool was_in_pre = state->in_pre;
    if (node->tag && strcmp(node->tag, "pre") == 0) {
        state->in_pre = true;
        s_accum.is_pre = true;
    }

    /* Recurse into children */
    for (int i = 0; i < node->child_count; i++) {
        layout_node(node->children[i], parent, state);
    }

    /* After block element: flush */
    if (is_block_tag(node->tag)) {
        if (s_accum.heading_level > 0) {
            /* Add extra margin after headings */
            emit_accumulated(parent, state);
        } else {
            emit_accumulated(parent, state);
        }
    }

    /* Exit <a> tag */
    if (entered_link) {
        emit_accumulated(parent, state);  /* flush link content */
        state->current_href = prev_href;
    }

    /* Exit heading */
    if (heading > 0) {
        s_accum.heading_level = 0;
    }

    /* Exit <pre> */
    if (node->tag && strcmp(node->tag, "pre") == 0) {
        state->in_pre = was_in_pre;
    }
}

/* ---- public API ---- */

layout_box_t *layout_compute(const dom_node_t *dom_root, int width)
{
    if (!dom_root) return NULL;

    layout_box_t *root = box_new(LAYOUT_BLOCK);
    if (!root) return NULL;
    root->width = width;
    root->height = 0;

    layout_state_t state = {
        .cursor_y = 0,
        .content_width = width,
        .line_x = 0,
        .line_height = BODY_LINE_HEIGHT,
        .current_href = NULL,
        .in_pre = false,
    };

    accum_reset();

    /* Find the <body> element, or use the root */
    const dom_node_t *body = dom_root;
    if (dom_root->type == DOM_NODE_ELEMENT && dom_root->tag) {
        /* Search for body tag */
        for (int i = 0; i < dom_root->child_count; i++) {
            dom_node_t *child = dom_root->children[i];
            if (child->type == DOM_NODE_ELEMENT && child->tag &&
                strcmp(child->tag, "body") == 0) {
                body = child;
                break;
            }
            /* Also check one more level (html > body) */
            for (int j = 0; j < child->child_count; j++) {
                dom_node_t *grandchild = child->children[j];
                if (grandchild->type == DOM_NODE_ELEMENT && grandchild->tag &&
                    strcmp(grandchild->tag, "body") == 0) {
                    body = grandchild;
                    break;
                }
            }
        }
    }

    layout_node(body, root, &state);

    /* Flush any remaining inline content */
    emit_accumulated(root, &state);

    root->height = state.cursor_y;

    accum_free();

    ESP_LOGI(TAG, "Layout computed: %dpx total height", root->height);
    return root;
}

static void layout_free_recursive(layout_box_t *box)
{
    if (!box) return;
    for (int i = 0; i < box->child_count; i++) {
        layout_free_recursive(box->children[i]);
    }
    if (box->children) heap_caps_free(box->children);
    /* Note: text strings in line boxes are separately allocated copies */
    if (box->text) heap_caps_free((void *)box->text);
    heap_caps_free(box);
}

void layout_free(layout_box_t *root)
{
    layout_free_recursive(root);
}

int layout_total_height(const layout_box_t *root)
{
    if (!root) return 0;
    return root->height;
}
