#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "css_style.h"

/**
 * Simplified DOM node types.
 */
typedef enum {
    DOM_NODE_ELEMENT,
    DOM_NODE_TEXT,
} dom_node_type_t;

/**
 * Simplified DOM node structure.
 * Now includes CSS style for Flex/Grid support.
 */
typedef struct dom_node {
    dom_node_type_t type;

    /* Element-specific fields (type == DOM_NODE_ELEMENT) */
    char *tag;              /* Lowercase tag name */
    char *href;             /* <a href="..."> */
    char *src;              /* <img src="..."> */
    char *alt;              /* <img alt="..."> */
    css_style_t style;      /* Parsed CSS style from style attribute */

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
 * Uses gumbo-parser internally. Extracts CSS style attributes.
 *
 * @param html  Null-terminated HTML string
 * @return Root node of the DOM tree, or NULL on failure.
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
