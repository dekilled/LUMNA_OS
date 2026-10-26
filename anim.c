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

static void hide_after_leave(struct orbit_view *view) {
	view->leaving = false;
	view->sliding = false;
	view->placed = false;
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);
	if (view->border) {
		wlr_scene_node_set_enabled(&view->border->node, false);
	}
}

static int slide_tick(void *data) {
	struct orbit_server *server = data;
	uint32_t now = now_ms();
	bool more = false;
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->sliding || !view->mapped) {
			continue;
		}
		uint32_t dt = now - view->slide_start;
		uint32_t span = view->slide_span > 0 ? view->slide_span : 1;
		if (dt >= span) {
			apply_visual(view, view->slide_to_x, view->slide_to_y);
			view->sliding = false;
			if (view->leaving) {
				hide_after_leave(view);
			}
			continue;
		}
		float t = (float)dt / (float)span;
		t = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); /* desacelera no fim */
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
	if (view->sliding && view->slide_to_x == x && view->slide_to_y == y) {
		/* já vai para lá: recomeçar a cada arrumação deixava-a parada a meio caminho */
		return;
	}
	view->slide_from_x = view->slide_x;
	view->slide_from_y = view->slide_y;
	view->slide_to_x = x;
	view->slide_to_y = y;
	view->slide_start = now_ms();
	view->slide_span = (uint32_t)server->anim_ms;
	view->leaving = false;
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
	view->leaving = false;
	view->placed = false;
}

/* a troca de espaço anda mais devagar que o mosaico: é o movimento que se acompanha com o olho */
static void slide_go(struct orbit_view *view, int fx, int fy, int tx, int ty) {
	struct orbit_server *server = view->server;
	view->slide_from_x = fx;
	view->slide_from_y = fy;
	view->slide_to_x = tx;
	view->slide_to_y = ty;
	view->slide_start = now_ms();
	view->slide_span = (uint32_t)server->anim_ms * 2;
	view->sliding = true;
	if (!server->slide_timer && server->display) {
		server->slide_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->display), slide_tick, server);
	}
	if (server->slide_timer) {
		wl_event_source_timer_update(server->slide_timer, 16);
	}
}

/* sai de cena deslizando dx na horizontal e some ao chegar */
void orbit_slide_leave(struct orbit_view *view, int dx) {
	view->leaving = true;
	slide_go(view, view->slide_x, view->slide_y, view->slide_x + dx, view->slide_y);
}

/* a janela já está posta no destino: volta dx para o lado e desliza até ele */
void orbit_slide_enter(struct orbit_view *view, int dx) {
	int x = view->slide_x, y = view->slide_y;
	view->leaving = false;
	apply_visual(view, x + dx, y);
	slide_go(view, x + dx, y, x, y);
}
