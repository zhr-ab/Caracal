#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * Simplified DOM node types.
 */
typedef enum {
    DOM_NODE_ELEMENT,
    DOM_NODE_TEXT,
} dom_node_type_t;

/**
 * Simplified DOM node structure.
 * Only stores what we need for block layout + rendering.
 */
typedef struct dom_node {
    dom_node_type_t type;

    /* Element-specific fields (type == DOM_NODE_ELEMENT) */
    char *tag;              /* Lowercase tag name: "p", "h1", "a", "img", "hr", etc. */
    char *href;             /* <a href="..."> */
    char *src;              /* <img src="..."> */
    char *alt;              /* <img alt="..."> */

    /* Text-specific fields (type == DOM_NODE_TEXT) */
    char *text;             /* Text content */

    /* Tree structure */
    struct dom_node *parent;
    struct dom_node **children;
    int child_count;
    int child_capacity;
} dom_node_t;

/**
 * Parse an HTML string into a simplified DOM tree.
 * Uses gumbo-parser internally.
 *
 * @param html  Null-terminated HTML string (will not be modified)
 * @return Root node of the DOM tree, or NULL on failure.
 *         Caller must free with dom_tree_free().
 */
dom_node_t *html_parse(const char *html);

/**
 * Free a DOM tree and all associated memory.
 */
void dom_tree_free(dom_node_t *root);

/**
 * Debug: print DOM tree to ESP_LOGI.
 */
void dom_tree_print(const dom_node_t *node, int depth);
