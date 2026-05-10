#pragma once

#include "html_dom.h"
#include "layout.h"

/**
 * Compute Grid layout for a DOM element with display:grid.
 * Produces layout boxes positioned in a grid pattern.
 *
 * @param node       DOM element with display:grid
 * @param parent     Parent layout box to add children to
 * @param container_x  X offset of the grid container
 * @param container_y  Y offset (updated on return)
 * @param avail_width  Available width for the container
 * @param avail_height Available height (unused, -1 for auto)
 */
void layout_grid(const dom_node_t *node, layout_box_t *parent,
                 int container_x, int *container_y,
                 int avail_width, int avail_height);
