#include "orbit.h"

#include <time.h>
#include <wlr/types/wlr_scene.h>

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void apply_visual(struct orbit_view *view, int x, int y) {
	int b = view->server->border_px;
	bool inset = view->border && view->border->node.enabled && b > 0;
	wlr_scene_node_set_position(&view->scene_tree->node, inset ? x + b : x, inset ? y + b : y);
	if (inset) {
		wlr_scene_node_set_position(&view->border->node, x, y);
	}
	view->slide_x = x;
	view->slide_y = y;
}

static int slide_tick(void *data) {
	struct orbit_server *server = data;
	uint32_t now = now_ms();
	uint32_t span = server->anim_ms > 0 ? (uint32_t)server->anim_ms : 1;
	bool more = false;
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->sliding || !view->mapped) {
			continue;
		}
		uint32_t dt = now - view->slide_start;
		if (dt >= span) {
			apply_visual(view, view->slide_to_x, view->slide_to_y);
			view->sliding = false;
			continue;
		}
		float t = (float)dt / (float)span;
		int x = view->slide_from_x + (int)((view->slide_to_x - view->slide_from_x) * t);
		int y = view->slide_from_y + (int)((view->slide_to_y - view->slide_from_y) * t);
		apply_visual(view, x, y);
		more = true;
	}
	if (more && server->slide_timer) {
		wl_event_source_timer_update(server->slide_timer, 16);
	}
	return 0;
}

void orbit_slide(struct orbit_view *view, int x, int y) {
	struct orbit_server *server = view->server;
	if (!view->placed || server->anim_ms <= 0) {
		view->placed = true;
		view->sliding = false;
		view->slide_to_x = x;
		view->slide_to_y = y;
		apply_visual(view, x, y);
		return;
	}
	if (!view->sliding && view->slide_x == x && view->slide_y == y) {
		return;
	}
	view->slide_from_x = view->slide_x;
	view->slide_from_y = view->slide_y;
	view->slide_to_x = x;
	view->slide_to_y = y;
	view->slide_start = now_ms();
	view->sliding = true;
	if (!server->slide_timer && server->display) {
		server->slide_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->display), slide_tick, server);
	}
	if (server->slide_timer) {
		wl_event_source_timer_update(server->slide_timer, 16);
	}
}

void orbit_slide_stop(struct orbit_view *view) {
	view->sliding = false;
	view->placed = false;
}
