#include "html_dom.h"
#include "gumbo.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "html_dom";

/* ---- helpers ---- */

static char *str_dup_psram(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = (char *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (d) memcpy(d, s, len);
    return d;
}

static dom_node_t *node_new(dom_node_type_t type)
{
    dom_node_t *n = (dom_node_t *)heap_caps_malloc(sizeof(dom_node_t),
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!n) return NULL;
    memset(n, 0, sizeof(dom_node_t));
    n->type = type;
    n->style = css_style_default();
    n->child_capacity = 4;
    n->children = (dom_node_t **)heap_caps_malloc(
        sizeof(dom_node_t *) * n->child_capacity,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return n;
}

static void node_add_child(dom_node_t *parent, dom_node_t *child)
{
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity *= 2;
        parent->children = (dom_node_t **)heap_caps_realloc(
            parent->children,
            sizeof(dom_node_t *) * parent->child_capacity,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

static const char *get_attr(const GumboElement *elem, const char *name)
{
    for (unsigned int i = 0; i < elem->attributes.length; i++) {
        GumboAttribute *attr = (GumboAttribute *)elem->attributes.data[i];
        if (strcmp(attr->name, name) == 0) {
            return attr->value;
        }
    }
    return NULL;
}

/* ---- recursive DOM builder ---- */

static dom_node_t *build_dom(const GumboNode *gnode)
{
    if (!gnode) return NULL;

    switch (gnode->type) {
    case GUMBO_NODE_ELEMENT: {
        const GumboElement *gelem = &gnode->v.element;
        const char *tag = gumbo_normalized_tagname(gelem->tag);

        if (tag[0] == '\0' && gelem->original_tag.data && gelem->original_tag.length > 0) {
            const char *orig = gelem->original_tag.data;
            size_t orig_len = gelem->original_tag.length;
            if (orig[0] == '<') { orig++; orig_len--; }
            size_t tag_len = 0;
            while (tag_len < orig_len && orig[tag_len] != ' ' &&
                   orig[tag_len] != '>' && orig[tag_len] != '/') {
                tag_len++;
            }
            char *tag_buf = (char *)heap_caps_malloc(tag_len + 1,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (tag_buf) {
                memcpy(tag_buf, orig, tag_len);
                tag_buf[tag_len] = '\0';
                tag = tag_buf;
            }
        }

        if (gelem->tag == GUMBO_TAG_SCRIPT ||
            gelem->tag == GUMBO_TAG_STYLE  ||
            gelem->tag == GUMBO_TAG_HEAD) {
            return NULL;
        }

        dom_node_t *node = node_new(DOM_NODE_ELEMENT);
        if (!node) return NULL;
        node->tag = str_dup_psram(tag);

        if (gelem->tag == GUMBO_TAG_A) {
            const char *href = get_attr(gelem, "href");
            if (href) node->href = str_dup_psram(href);
        } else if (gelem->tag == GUMBO_TAG_IMG) {
            const char *src = get_attr(gelem, "src");
            const char *alt = get_attr(gelem, "alt");
            if (src) node->src = str_dup_psram(src);
            if (alt) node->alt = str_dup_psram(alt);
        }

        /* Parse CSS style attribute — this is the key addition for Pro */
        const char *style_str = get_attr(gelem, "style");
        if (style_str) {
            node->style = css_style_parse(style_str);
        }

        /* Auto-detect flex/grid for semantic tags without explicit style */
        if (!node->style.has_style) {
            if (gelem->tag == GUMBO_TAG_NAV) {
                node->style.display = CSS_DISPLAY_FLEX;
                node->style.flex_direction = CSS_FLEX_DIR_ROW;
                node->style.gap = 8;
                node->style.has_style = true;
            }
        }

        const GumboVector *children = &gelem->children;
        for (unsigned int i = 0; i < children->length; i++) {
            dom_node_t *child = build_dom((const GumboNode *)children->data[i]);
            if (child) node_add_child(node, child);
        }
        return node;
    }

    case GUMBO_NODE_TEXT:
    case GUMBO_NODE_WHITESPACE: {
        const char *text = gnode->v.text.text;
        bool all_space = true;
        for (const char *p = text; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                all_space = false;
                break;
            }
        }
        if (all_space) return NULL;

        dom_node_t *node = node_new(DOM_NODE_TEXT);
        if (!node) return NULL;
        char *clean = str_dup_psram(text);
        if (clean) {
            char *dst = clean;
            bool prev_space = false;
            for (const char *src = text; *src; src++) {
                if (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r') {
                    if (!prev_space) { *dst++ = ' '; prev_space = true; }
                } else {
                    *dst++ = *src;
                    prev_space = false;
                }
            }
            *dst = '\0';
        }
        node->text = clean;
        return node;
    }

    default:
        return NULL;
    }
}

/* ---- public API ---- */

dom_node_t *html_parse(const char *html)
{
    if (!html || !*html) {
        ESP_LOGW(TAG, "Empty HTML input");
        return NULL;
    }

    GumboOutput *output = gumbo_parse(html);
    if (!output || !output->root) {
        ESP_LOGE(TAG, "gumbo_parse failed");
        return NULL;
    }

    dom_node_t *root = build_dom(output->root);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return root;
}

static void node_free(dom_node_t *node)
{
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        node_free(node->children[i]);
    }
    if (node->children) heap_caps_free(node->children);
    if (node->tag)  heap_caps_free(node->tag);
    if (node->href) heap_caps_free(node->href);
    if (node->src)  heap_caps_free(node->src);
    if (node->alt)  heap_caps_free(node->alt);
    if (node->text) heap_caps_free(node->text);
    heap_caps_free(node);
}

void dom_tree_free(dom_node_t *root)
{
    node_free(root);
}

void dom_tree_print(const dom_node_t *node, int depth)
{
    if (!node) return;
    for (int i = 0; i < depth; i++) ESP_LOGI("DOM", "  ");
    if (node->type == DOM_NODE_TEXT) {
        ESP_LOGI("DOM", "TEXT: \"%s\"", node->text ? node->text : "");
    } else {
        ESP_LOGI("DOM", "<%s> display=%s%s%s", node->tag ? node->tag : "?",
                 css_display_str(node->style.display),
                 node->href ? " href=..." : "",
                 node->src  ? " src=..."  : "");
    }
    for (int i = 0; i < node->child_count; i++) {
        dom_tree_print(node->children[i], depth + 1);
    }
}
