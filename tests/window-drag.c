#include "window-drag.h"
#include <assert.h>

int main(void)
{
	assert(desk_drag_resize_edges(10, 10, 300, 300) == (WLR_EDGE_TOP | WLR_EDGE_LEFT));
	assert(desk_drag_resize_edges(150, 10, 300, 300) == WLR_EDGE_TOP);
	assert(desk_drag_resize_edges(290, 10, 300, 300) == (WLR_EDGE_TOP | WLR_EDGE_RIGHT));
	assert(desk_drag_resize_edges(10, 150, 300, 300) == WLR_EDGE_LEFT);
	assert(desk_drag_resize_edges(149, 150, 300, 300) == WLR_EDGE_LEFT);
	assert(desk_drag_resize_edges(150, 150, 300, 300) == WLR_EDGE_RIGHT);
	assert(desk_drag_resize_edges(290, 150, 300, 300) == WLR_EDGE_RIGHT);
	assert(desk_drag_resize_edges(10, 290, 300, 300) == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT));
	assert(desk_drag_resize_edges(150, 290, 300, 300) == WLR_EDGE_BOTTOM);
	assert(desk_drag_resize_edges(290, 290, 300, 300) == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT));
	assert(desk_drag_resize_edges(100, 100, 300, 300) == WLR_EDGE_LEFT);
	assert(desk_drag_resize_edges(200, 200, 300, 300) == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT));
	return 0;
}
