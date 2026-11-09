/*
 * Arrastar e soltar entre aplicativos (wl_data_device). Um cliente pede para começar um arraste (com o aperto do mouse ou o dedo que o
 * originou); o wlroots cuida do protocolo (a oferta, o enter/motion/leave/drop do cliente que está embaixo); aqui ficam três coisas:
 *   - validar o pedido (o serial tem de ser do aperto de mouse ou de um dedo em baixo) e começar a pegada certa;
 *   - o ícone do arraste, que o cliente desenha e que acompanha o ponteiro ou o dedo, por cima de tudo;
 *   - com o dedo, passar o foco do toque para a superfície que está embaixo dele (no mouse o foco do ponteiro já anda sozinho).
 */
#include "orbit.h"

#include <stdlib.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

struct orbit_drag_icon {
	struct orbit_server *server;
	struct wlr_drag_icon *icon;
	struct wlr_scene_tree *tree;
	struct wl_listener destroy;
};

static struct orbit_drag_icon *atual; /* um arraste de cada vez */

/* o ícone do arraste não recebe o ponteiro: o que está debaixo dele é o destino (sem isto o teste de "o que há aqui" acharia o próprio ícone) */
static bool sem_entrada(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
	(void)buffer;
	(void)sx;
	(void)sy;
	return false;
}

static void tree_sem_entrada(struct wlr_scene_tree *tree) {
	struct wlr_scene_node *node;
	wl_list_for_each(node, &tree->children, link) {
		if (node->type == WLR_SCENE_NODE_BUFFER) {
			wlr_scene_buffer_from_node(node)->point_accepts_input = sem_entrada;
		} else if (node->type == WLR_SCENE_NODE_TREE) {
			tree_sem_entrada(wlr_scene_tree_from_node(node));
		}
	}
}

static void icon_position(struct orbit_server *server, struct orbit_drag_icon *d) {
	struct wlr_drag *drag = d->icon->drag;
	double x = server->cursor->x, y = server->cursor->y;
	if (drag->grab_type == WLR_DRAG_GRAB_KEYBOARD_TOUCH) {
		x = server->touch_x;
		y = server->touch_y;
	}
	wlr_scene_node_set_position(&d->tree->node, (int)x, (int)y);
	tree_sem_entrada(d->tree);
}

void orbit_dnd_move(struct orbit_server *server) {
	if (atual) {
		icon_position(server, atual);
	}
}

static void icon_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct orbit_drag_icon *d = wl_container_of(listener, d, destroy);
	wl_list_remove(&d->destroy.link);
	if (atual == d) {
		atual = NULL;
	}
	free(d);
}

static void seat_start_drag(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, start_drag);
	struct wlr_drag *drag = data;
	if (!drag->icon) {
		return;
	}
	struct orbit_drag_icon *d = calloc(1, sizeof(*d));
	if (!d) {
		return;
	}
	d->server = server;
	d->icon = drag->icon;
	d->tree = wlr_scene_drag_icon_create(server->drag_tree, drag->icon);
	if (!d->tree) {
		free(d);
		return;
	}
	wlr_scene_node_raise_to_top(&server->drag_tree->node);
	d->destroy.notify = icon_destroy;
	wl_signal_add(&drag->icon->events.destroy, &d->destroy);
	atual = d;
	icon_position(server, d);
}

static void seat_request_start_drag(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, request_start_drag);
	struct wlr_seat_request_start_drag_event *event = data;
	if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
		wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
		return;
	}
	struct wlr_touch_point *point;
	if (wlr_seat_validate_touch_grab_serial(server->seat, event->origin, event->serial, &point)) {
		wlr_seat_start_touch_drag(server->seat, event->drag, event->serial, point);
		return;
	}
	wlr_log(WLR_INFO, "arrastar: serial %u não é de um aperto de mouse nem de um dedo em baixo (origem %p)", event->serial, (void *)event->origin);
	wlr_data_source_destroy(event->drag->source);
}

/* um dedo andou durante um arraste dele: o foco do toque vai para o que está embaixo (o cliente de destino vê enter/motion/leave) */
bool orbit_dnd_touch_motion(struct orbit_server *server, uint32_t time, int32_t id, double lx, double ly, double *ox, double *oy) {
	struct wlr_drag *drag = server->seat->drag;
	if (!drag || drag->grab_type != WLR_DRAG_GRAB_KEYBOARD_TOUCH || drag->touch_id != id) {
		return false;
	}
	double sx = 0, sy = 0;
	struct wlr_surface *surface = NULL;
	view_at(server, lx, ly, &surface, &sx, &sy);
	if (surface) {
		wlr_seat_touch_point_focus(server->seat, surface, time, id, sx, sy);
		*ox = lx - sx;
		*oy = ly - sy;
		wlr_seat_touch_notify_motion(server->seat, time, id, sx, sy);
	} else {
		wlr_seat_touch_point_clear_focus(server->seat, time, id);
	}
	server->touch_x = lx;
	server->touch_y = ly;
	orbit_dnd_move(server);
	return true;
}

void orbit_dnd_bind(struct orbit_server *server) {
	server->drag_tree = wlr_scene_tree_create(&server->scene->tree);
	server->request_start_drag.notify = seat_request_start_drag;
	wl_signal_add(&server->seat->events.request_start_drag, &server->request_start_drag);
	server->start_drag.notify = seat_start_drag;
	wl_signal_add(&server->seat->events.start_drag, &server->start_drag);
}
