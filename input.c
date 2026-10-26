#include "orbit.h"
#include <wlr/backend.h>

#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/util/log.h>

static struct xkb_context *shared_context;
static struct xkb_keymap *shared_keymap;

void orbit_xkb_prepare(void) {
	if (shared_keymap) {
		return;
	}
	shared_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!shared_context) {
		wlr_log(WLR_ERROR, "contexto XKB falhou");
		return;
	}
	struct xkb_rule_names names = {
		.rules = getenv("XKB_DEFAULT_RULES"),
		.model = getenv("XKB_DEFAULT_MODEL"),
		.layout = getenv("XKB_DEFAULT_LAYOUT"),
		.variant = getenv("XKB_DEFAULT_VARIANT"),
		.options = getenv("XKB_DEFAULT_OPTIONS"),
	};
	shared_keymap = xkb_keymap_new_from_names(shared_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!shared_keymap) {
		wlr_log(WLR_ERROR, "keymap XKB falhou");
	}
}

void orbit_xkb_finish(void) {
	if (shared_keymap) {
		xkb_keymap_unref(shared_keymap);
		shared_keymap = NULL;
	}
	if (shared_context) {
		xkb_context_unref(shared_context);
		shared_context = NULL;
	}
}

/*
 * Gestos da borda do telemóvel, com o rato ou com o dedo: começam numa faixa de 28 px da
 * borda. Da esquerda para a direita recua um espaço, da direita para a esquerda avança, e de
 * baixo para cima liga ou desliga o par. O gesto não chega à aplicação.
 */
#define ORBIT_EDGE 28
#define ORBIT_GESTURE_DIST 72

bool orbit_gesture_begin(struct orbit_server *server, double lx, double ly) {
	if (server->session_locked) {
		return false;
	}
	struct orbit_output *phone = output_at(server, lx, ly);
	if (!phone || !output_is_phone(phone->wlr)) {
		return false;
	}
	struct wlr_box box;
	wlr_output_layout_get_box(server->layout, phone->wlr, &box);
	double x = lx - box.x;
	double y = ly - box.y;
	int edge = 0;
	if (x >= 0 && x < ORBIT_EDGE) {
		edge = 1;
	} else if (box.width > ORBIT_EDGE && x > box.width - ORBIT_EDGE) {
		edge = 2;
	} else if (box.height > ORBIT_EDGE && y > box.height - ORBIT_EDGE) {
		edge = 3;
	}
	if (!edge) {
		return false;
	}
	server->gesture = edge;
	server->gesture_done = false;
	server->gesture_x = lx;
	server->gesture_y = ly;
	server->gesture_output = phone;
	return true;
}

void orbit_gesture_motion(struct orbit_server *server, double lx, double ly) {
	struct orbit_output *output = server->gesture_output;
	if (!output || server->gesture_done) {
		return;
	}
	double dx = lx - server->gesture_x;
	double dy = ly - server->gesture_y;
	if (server->gesture == 1 && dx > ORBIT_GESTURE_DIST) {
		server->gesture_done = true;
		if (output->tag > 0) {
			output->tag -= 1;
			orbit_arrange(server);
			focus_top(server, output);
		}
	} else if (server->gesture == 2 && dx < -ORBIT_GESTURE_DIST) {
		server->gesture_done = true;
		if (output->tag + 1 < ORBIT_TAGS) {
			output->tag += 1;
			orbit_arrange(server);
			focus_top(server, output);
		}
	} else if (server->gesture == 3 && dy < -ORBIT_GESTURE_DIST) {
		server->gesture_done = true;
		server->phone_split = !server->phone_split;
		server->split_top = NULL;
		server->split_bottom = NULL;
		orbit_arrange(server);
	}
}

void orbit_gesture_end(struct orbit_server *server) {
	server->gesture = 0;
	server->gesture_output = NULL;
}

static void process_cursor_motion(struct orbit_server *server, uint32_t time) {
	orbit_idle_activity(server); /* mexer o rato conta mesmo fora das janelas */
	if (server->cursor_mode == ORBIT_CURSOR_GESTURE) {
		orbit_gesture_motion(server, server->cursor->x, server->cursor->y);
		return;
	}
	if (server->cursor_mode == ORBIT_CURSOR_MOVE && server->grabbed && server->grabbed->mapped) {
		struct orbit_view *view = server->grabbed;
		if (view->floating) {
			view->x = server->grab_gx + (int)(server->cursor->x - server->grab_x);
			view->y = server->grab_gy + (int)(server->cursor->y - server->grab_y);
			view->sliding = false;
			view->placed = true;
			view->slide_x = view->x;
			view->slide_y = view->y;
			wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
		}
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = NULL;
	view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!surface) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
		wlr_seat_pointer_clear_focus(server->seat);
		return;
	}
	wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
}

static void on_cursor_motion(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void on_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void on_cursor_button(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	orbit_idle_activity(server);
	server->super_tap = false; /* Super+clique não é o Super sozinho */

	if (server->cursor_mode == ORBIT_CURSOR_MOVE && event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		struct orbit_view *view = server->grabbed;
		server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
		server->grabbed = NULL;
		if (view && view->mapped) {
			struct orbit_output *dest = output_at(server, server->cursor->x, server->cursor->y);
			if (dest && dest != view->output) {
				orbit_split_release(server, view);
				view->output = dest;
				view->tag = dest->tag;
				view->fullscreen = false;
			}
			orbit_arrange(server);
			orbit_focus(view);
		}
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
		return;
	}

	if (server->cursor_mode == ORBIT_CURSOR_GESTURE && event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
		orbit_gesture_end(server);
		return;
	}

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct orbit_view *view = view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
		uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		if (!(mods & WLR_MODIFIER_ALT) && orbit_gesture_begin(server, server->cursor->x, server->cursor->y)) {
			server->cursor_mode = ORBIT_CURSOR_GESTURE;
			return;
		}
		if (view && (mods & WLR_MODIFIER_ALT)) {
			server->cursor_mode = ORBIT_CURSOR_MOVE;
			server->grabbed = view;
			server->grab_x = server->cursor->x;
			server->grab_y = server->cursor->y;
			server->grab_gx = view->x;
			server->grab_gy = view->y;
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "fleur");
			orbit_focus(view);
			return;
		}
		if (view) {
			orbit_focus(view);
		} else if (surface) {
			orbit_layer_offer_keyboard(surface);
		}
	}
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void on_cursor_axis(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	orbit_idle_activity(server);
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
		event->delta, event->delta_discrete, event->source, event->relative_direction);
}

static void on_cursor_frame(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

static void keyboard_key(struct wl_listener *listener, void *data) {
	struct orbit_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct orbit_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms = NULL;
	int count = 0;
	if (keyboard->wlr_keyboard->keymap) {
		count = xkb_keymap_key_get_syms_by_level(keyboard->wlr_keyboard->keymap, keycode, 0, 0, &syms);
	}
	bool handled = false;
	uint32_t mods = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	orbit_idle_activity(server);
	for (int i = 0; i < count; i++) {
		handled = orbit_keybinding_tap(server, syms[i], event->state == WL_KEYBOARD_KEY_STATE_PRESSED, mods) || handled;
	}
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && count > 0) {
		for (int i = 0; i < count; i++) {
			handled = orbit_keybinding(server, syms[i], mods) || handled;
		}
	}
	if (!handled && orbit_im_forward_key(server, keyboard->wlr_keyboard, event)) {
		return;
	}
	if (!handled) {
		struct orbit_view *focused = server->focused;
		if (focused && focused->mapped && focused->output && focused->tag != focused->output->tag) {
			orbit_unfocus(server);
			return;
		}
		wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_modifiers(struct wl_listener *listener, void *data) {
	struct orbit_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	if (orbit_im_forward_modifiers(keyboard->server, keyboard->wlr_keyboard)) {
		return;
	}
	orbit_cycle_end(keyboard->server, wlr_keyboard_get_modifiers(keyboard->wlr_keyboard));
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_destroy(struct wl_listener *listener, void *data) {
	struct orbit_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

/* wl_touch só com um ecrã táctil (ou o toque de teste do orbit-msg, ver ipc.c). */
void orbit_seat_capabilities(struct orbit_server *server) {
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
	if (!wl_list_empty(&server->touches) || server->virtual_touch) {
		caps |= WL_SEAT_CAPABILITY_TOUCH;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void touch_destroy(struct wl_listener *listener, void *data) {
	struct orbit_touch *touch = wl_container_of(listener, touch, destroy);
	struct orbit_server *server = touch->server;
	wl_list_remove(&touch->destroy.link);
	wl_list_remove(&touch->link);
	free(touch);
	orbit_seat_capabilities(server);
}

struct orbit_switch {
	struct wl_listener toggle;
	struct wl_listener destroy;
};

static void switch_toggle(struct wl_listener *listener, void *data) {
	struct wlr_switch_toggle_event *event = data;
	const char *state = event->switch_state == WLR_SWITCH_STATE_ON ? "ligado" : "desligado";
	if (event->switch_type == WLR_SWITCH_TYPE_LID) {
		wlr_log(WLR_INFO, "tampa %s", event->switch_state == WLR_SWITCH_STATE_ON ? "fechada" : "aberta");
	} else {
		wlr_log(WLR_INFO, "modo tablet %s", state);
	}
	(void)listener;
}

static void switch_destroy(struct wl_listener *listener, void *data) {
	struct orbit_switch *sw = wl_container_of(listener, sw, destroy);
	wl_list_remove(&sw->toggle.link);
	wl_list_remove(&sw->destroy.link);
	free(sw);
}

/*
 * Um teclado no seat. Os físicos usam o mapa partilhado; um teclado virtual (wtype, o teclado
 * no ecrã) traz o mapa dele e fica com esse.
 */
void orbit_keyboard_add(struct orbit_server *server, struct wlr_keyboard *wlr_keyboard, bool shared) {
	struct orbit_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (!keyboard) {
		wlr_log(WLR_ERROR, "sem memória para o teclado");
		return;
	}
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	if (shared) {
		orbit_xkb_prepare();
		if (shared_keymap) {
			wlr_keyboard_set_keymap(wlr_keyboard, shared_keymap);
		}
		wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
	}
	keyboard->modifiers.notify = keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_destroy;
	wl_signal_add(&wlr_keyboard->base.events.destroy, &keyboard->destroy);
	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
	orbit_seat_capabilities(server);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
		orbit_keyboard_add(server, wlr_keyboard_from_input_device(device), true);
	} else if (device->type == WLR_INPUT_DEVICE_POINTER || device->type == WLR_INPUT_DEVICE_TOUCH ||
		device->type == WLR_INPUT_DEVICE_TABLET) {
		wlr_cursor_attach_input_device(server->cursor, device);
		if (device->type == WLR_INPUT_DEVICE_TOUCH) {
			struct orbit_touch *touch = calloc(1, sizeof(*touch));
			if (!touch) {
				wlr_log(WLR_ERROR, "sem memória para o toque");
			} else {
				touch->server = server;
				touch->device = device;
				touch->destroy.notify = touch_destroy;
				wl_signal_add(&device->events.destroy, &touch->destroy);
				wl_list_insert(&server->touches, &touch->link);
				struct orbit_output *output;
				wl_list_for_each(output, &server->outputs, link) {
					if (output_is_phone(output->wlr)) {
						wlr_cursor_map_input_to_output(server->cursor, device, output->wlr);
						break;
					}
				}
			}
		}
	} else if (device->type == WLR_INPUT_DEVICE_TABLET_PAD) {
		wlr_log(WLR_INFO, "mesa digitalizadora sem botões próprios; o traço segue o cursor");
	} else if (device->type == WLR_INPUT_DEVICE_SWITCH) {
		struct orbit_switch *sw = calloc(1, sizeof(*sw));
		if (!sw) {
			wlr_log(WLR_ERROR, "sem memória para o interruptor");
		} else {
			struct wlr_switch *dev = wlr_switch_from_input_device(device);
			sw->toggle.notify = switch_toggle;
			wl_signal_add(&dev->events.toggle, &sw->toggle);
			sw->destroy.notify = switch_destroy;
			wl_signal_add(&device->events.destroy, &sw->destroy);
		}
	}
	orbit_seat_capabilities(server);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	if (server->seat->pointer_state.focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void orbit_input_bind(struct orbit_server *server) {
	server->cursor_motion.notify = on_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = on_cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
	server->cursor_button.notify = on_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = on_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = on_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	server->new_input.notify = server_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);
	server->request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
	server->request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);
}
