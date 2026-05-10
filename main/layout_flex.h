#pragma once

#include "html_dom.h"
#include "layout.h"

/**
 * Compute Flex layout for a DOM element with display:flex.
 * Produces layout boxes positioned according to flexbox algorithm.
 *
 * @param node       DOM element with display:flex
 * @param parent     Parent layout box to add children to
 * @param container_x  X offset of the flex container
 * @param container_y  Y offset (updated on return)
 * @param avail_width  Available width for the container
 * @param avail_height Available height (unused, -1 for auto)
 */
void layout_flex(const dom_node_t *node, layout_box_t *parent,
                 int container_x, int *container_y,
                 int avail_width, int avail_height);
