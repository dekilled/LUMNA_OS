#include "orbit.h"

#include <linux/input-event-codes.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/log.h>

/*
 * Toque. O ecrã táctil está agarrado ao cursor só para as coordenadas (mapeado ao DSI); os
 * eventos de toque são tratados aqui:
 *  - um dedo que começa na faixa da borda do telemóvel é um gesto (input.c) e não chega à
 *    aplicação;
 *  - uma superfície que conhece o wl_touch (GTK, WebKit, Qt, foot...) recebe o toque de verdade,
 *    com vários dedos;
 *  - uma que não conhece recebe o primeiro dedo como o botão esquerdo do rato.
 * O cursor some enquanto se toca (no telemóvel não há rato a mostrar).
 */

#define ORBIT_TOUCH_POINTS 10

struct touch_point {
	bool used;
	int32_t id;
	bool gesture;
	bool emulated;
	/* origem da superfície tocada, no layout: sx = lx - ox */
	double ox, oy;
};

static struct touch_point points[ORBIT_TOUCH_POINTS];

static struct touch_point *point_find(int32_t id) {
	for (int i = 0; i < ORBIT_TOUCH_POINTS; i++) {
		if (points[i].used && points[i].id == id) {
			return &points[i];
		}
	}
	return NULL;
}

static struct touch_point *point_new(int32_t id) {
	struct touch_point *old = point_find(id);
	if (old) {
		return old;
	}
	for (int i = 0; i < ORBIT_TOUCH_POINTS; i++) {
		if (!points[i].used) {
			points[i] = (struct touch_point){ .used = true, .id = id };
			return &points[i];
		}
	}
	wlr_log(WLR_ERROR, "toque: mais de %d dedos, o %d.º fica de fora", ORBIT_TOUCH_POINTS, id);
	return NULL;
}

static bool emulating(void) {
	for (int i = 0; i < ORBIT_TOUCH_POINTS; i++) {
		if (points[i].used && points[i].emulated) {
			return true;
		}
	}
	return false;
}

void orbit_touch_down(struct orbit_server *server, uint32_t time, int32_t id, double lx, double ly) {
	orbit_idle_activity(server);
	struct touch_point *point = point_new(id);
	if (!point) {
		return;
	}
	wlr_cursor_unset_image(server->cursor);
	if (orbit_gesture_begin(server, lx, ly)) {
		point->gesture = true;
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct orbit_view *view = view_at(server, lx, ly, &surface, &sx, &sy);
	if (view) {
		orbit_focus(view);
	} else if (surface) {
		orbit_layer_offer_keyboard(surface);
	}
	if (!surface) {
		point->used = false;
		return;
	}
	point->ox = lx - sx;
	point->oy = ly - sy;
	if (wlr_surface_accepts_touch(server->seat, surface)) {
		wlr_seat_touch_notify_down(server->seat, surface, time, id, sx, sy);
		return;
	}
	if (emulating()) {
		point->used = false;
		return;
	}
	point->emulated = true;
	wlr_cursor_warp(server->cursor, NULL, lx, ly);
	wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	wlr_seat_pointer_notify_button(server->seat, time, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	wlr_seat_pointer_notify_frame(server->seat);
}

void orbit_touch_motion(struct orbit_server *server, uint32_t time, int32_t id, double lx, double ly) {
	orbit_idle_activity(server);
	struct touch_point *point = point_find(id);
	if (!point) {
		return;
	}
	if (point->gesture) {
		orbit_gesture_motion(server, lx, ly);
		return;
	}
	double sx = lx - point->ox;
	double sy = ly - point->oy;
	if (point->emulated) {
		wlr_cursor_warp(server->cursor, NULL, lx, ly);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
		wlr_seat_pointer_notify_frame(server->seat);
		return;
	}
	wlr_seat_touch_notify_motion(server->seat, time, id, sx, sy);
}

void orbit_touch_up(struct orbit_server *server, uint32_t time, int32_t id) {
	struct touch_point *point = point_find(id);
	if (!point) {
		return;
	}
	if (point->gesture) {
		orbit_gesture_end(server);
	} else if (point->emulated) {
		wlr_seat_pointer_notify_button(server->seat, time, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
		wlr_seat_pointer_notify_frame(server->seat);
	} else {
		wlr_seat_touch_notify_up(server->seat, time, id);
	}
	point->used = false;
}

void orbit_touch_frame(struct orbit_server *server) {
	wlr_seat_touch_notify_frame(server->seat);
}

static void on_touch_down(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, touch_down);
	struct wlr_touch_down_event *event = data;
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y, &lx, &ly);
	orbit_touch_down(server, event->time_msec, event->touch_id, lx, ly);
}

static void on_touch_motion(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, touch_motion);
	struct wlr_touch_motion_event *event = data;
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y, &lx, &ly);
	orbit_touch_motion(server, event->time_msec, event->touch_id, lx, ly);
}

static void on_touch_up(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, touch_up);
	struct wlr_touch_up_event *event = data;
	orbit_touch_up(server, event->time_msec, event->touch_id);
}

/* O sistema desistiu do dedo (outra coisa ficou com o toque): para a aplicação é um levantar. */
static void on_touch_cancel(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, touch_cancel);
	struct wlr_touch_cancel_event *event = data;
	orbit_touch_up(server, event->time_msec, event->touch_id);
}

static void on_touch_frame(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, touch_frame);
	orbit_touch_frame(server);
}

void orbit_touch_bind(struct orbit_server *server) {
	server->touch_down.notify = on_touch_down;
	wl_signal_add(&server->cursor->events.touch_down, &server->touch_down);
	server->touch_motion.notify = on_touch_motion;
	wl_signal_add(&server->cursor->events.touch_motion, &server->touch_motion);
	server->touch_up.notify = on_touch_up;
	wl_signal_add(&server->cursor->events.touch_up, &server->touch_up);
	server->touch_cancel.notify = on_touch_cancel;
	wl_signal_add(&server->cursor->events.touch_cancel, &server->touch_cancel);
	server->touch_frame.notify = on_touch_frame;
	wl_signal_add(&server->cursor->events.touch_frame, &server->touch_frame);
}
