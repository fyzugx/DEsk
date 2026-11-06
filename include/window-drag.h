#ifndef DESK_WINDOW_DRAG_H
#define DESK_WINDOW_DRAG_H

#include <stdint.h>
#include <wlr/util/edges.h>

static inline uint32_t desk_drag_resize_edges(double x, double y, int width, int height)
{
	uint32_t edges = 0;
	if (y < height / 3.0) {
		edges = WLR_EDGE_TOP;
	} else if (y >= 2.0 * height / 3.0) {
		edges = WLR_EDGE_BOTTOM;
	}
	if (x < width / 3.0) {
		edges |= WLR_EDGE_LEFT;
	} else if (x >= 2.0 * width / 3.0) {
		edges |= WLR_EDGE_RIGHT;
	} else if (!edges) {
		edges = x < width / 2.0 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
	}
	return edges;
}

#endif
