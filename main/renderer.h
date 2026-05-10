#pragma once

#include "layout.h"
#include "lvgl.h"

/**
 * Initialize the LVGL renderer.
 * Called once after LVGL is set up.
 */
void renderer_init(void);

/**
 * Render a layout tree into an LVGL parent container.
 * Creates LVGL objects for each layout box.
 *
 * @param layout_root  The layout tree to render
 * @param parent       LVGL object to render into (scrollable container)
 */
void renderer_render(const layout_box_t *layout_root, lv_obj_t *parent);

/**
 * Clear all rendered content from the parent container.
 */
void renderer_clear(lv_obj_t *parent);

/**
 * Set callback for link clicks.
 * The callback receives the href string of the clicked link.
 */
void renderer_set_link_callback(void (*cb)(const char *href));
