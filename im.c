#include "orbit.h"

#include <stdlib.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/log.h>

/*
 * Escrever nas aplicações sem teclado físico.
 *
 * Teclado virtual (zwp_virtual_keyboard_v1): o wtype e um teclado no ecrã mandam teclas como
 * se fossem um teclado a mais no seat, com o mapa deles.
 *
 * Método de entrada (zwp_input_method_v2 ↔ zwp_text_input_v3): a aplicação diz quando um campo
 * de texto ganha o foco (enable) e o que está escrito à volta; o Orbit passa isso ao método de
 * entrada (activate), que pode abrir o teclado no ecrã e devolve o texto pronto (commit_string),
 * o texto a meio (preedit) e o que apagar. Um método de entrada de cada vez; os outros recebem
 * unavailable. Se ele agarrar o teclado, as teclas físicas vão para ele (menos as dos teclados
 * virtuais, que podem ser dele mesmo e dariam a volta).
 * As janelas de sugestões do método de entrada (popup surface) ainda não aparecem.
 *
 * Ponteiro virtual (zwlr_virtual_pointer_v1): um rato a mais ligado ao cursor, para um controle
 * remoto (o celular a mexer o rato do PC) e para os testes.
 */

struct orbit_text_input {
	struct wl_list link;
	struct wlr_text_input_v3 *input;
	struct wl_listener enable;
	struct wl_listener commit;
	struct wl_listener disable;
	struct wl_listener destroy;
};

static struct {
	struct orbit_server *server;
	struct wl_list inputs;
	struct orbit_text_input *active;
	struct wlr_input_method_v2 *method;
	struct wlr_input_method_keyboard_grab_v2 *grab;
	struct wl_listener new_text_input;
	struct wl_listener new_input_method;
	struct wl_listener method_commit;
	struct wl_listener method_grab;
	struct wl_listener method_destroy;
	struct wl_listener grab_destroy;
	struct wl_listener focus_change;
	struct wl_listener new_virtual_keyboard;
	struct wl_listener new_virtual_pointer;
} relay;

static bool same_client(struct wlr_text_input_v3 *input, struct wlr_surface *surface) {
	return surface && wl_resource_get_client(input->resource) == wl_resource_get_client(surface->resource);
}

/* O estado do campo (texto à volta, tipo de conteúdo) para o método de entrada. */
static void send_state(struct wlr_text_input_v3 *input) {
	struct wlr_input_method_v2 *method = relay.method;
	if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
		wlr_input_method_v2_send_surrounding_text(method, input->current.surrounding.text ? input->current.surrounding.text : "",
			input->current.surrounding.cursor, input->current.surrounding.anchor);
	}
	wlr_input_method_v2_send_text_change_cause(method, input->current.text_change_cause);
	if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) {
		wlr_input_method_v2_send_content_type(method, input->current.content_type.hint,
			input->current.content_type.purpose);
	}
	wlr_input_method_v2_send_done(method);
}

static void deactivate(void) {
	relay.active = NULL;
	if (relay.method) {
		wlr_input_method_v2_send_deactivate(relay.method);
		wlr_input_method_v2_send_done(relay.method);
	}
}

static void text_input_enable(struct wl_listener *listener, void *data) {
	struct orbit_text_input *text = wl_container_of(listener, text, enable);
	relay.active = text;
	if (relay.method) {
		wlr_input_method_v2_send_activate(relay.method);
		send_state(text->input);
	}
}

static void text_input_commit(struct wl_listener *listener, void *data) {
	struct orbit_text_input *text = wl_container_of(listener, text, commit);
	if (relay.active == text && relay.method && text->input->current_enabled) {
		send_state(text->input);
	}
}

static void text_input_disable(struct wl_listener *listener, void *data) {
	struct orbit_text_input *text = wl_container_of(listener, text, disable);
	if (relay.active == text) {
		deactivate();
	}
}

static void text_input_destroy(struct wl_listener *listener, void *data) {
	struct orbit_text_input *text = wl_container_of(listener, text, destroy);
	if (relay.active == text) {
		deactivate();
	}
	wl_list_remove(&text->enable.link);
	wl_list_remove(&text->commit.link);
	wl_list_remove(&text->disable.link);
	wl_list_remove(&text->destroy.link);
	wl_list_remove(&text->link);
	free(text);
}

static void new_text_input(struct wl_listener *listener, void *data) {
	struct wlr_text_input_v3 *input = data;
	struct orbit_text_input *text = calloc(1, sizeof(*text));
	if (!text) {
		wlr_log(WLR_ERROR, "sem memória para um campo de texto");
		return;
	}
	text->input = input;
	text->enable.notify = text_input_enable;
	wl_signal_add(&input->events.enable, &text->enable);
	text->commit.notify = text_input_commit;
	wl_signal_add(&input->events.commit, &text->commit);
	text->disable.notify = text_input_disable;
	wl_signal_add(&input->events.disable, &text->disable);
	text->destroy.notify = text_input_destroy;
	wl_signal_add(&input->events.destroy, &text->destroy);
	wl_list_insert(&relay.inputs, &text->link);
	struct wlr_surface *focus = relay.server->seat->keyboard_state.focused_surface;
	if (same_client(input, focus)) {
		wlr_text_input_v3_send_enter(input, focus);
	}
}

/* O foco do teclado mudou: o campo da janela velha sai, o da nova entra. */
static void keyboard_focus_change(struct wl_listener *listener, void *data) {
	struct wlr_seat_keyboard_focus_change_event *event = data;
	struct orbit_text_input *text;
	wl_list_for_each(text, &relay.inputs, link) {
		if (text->input->focused_surface && text->input->focused_surface != event->new_surface) {
			if (relay.active == text) {
				deactivate();
			}
			wlr_text_input_v3_send_leave(text->input);
		}
		if (same_client(text->input, event->new_surface) && text->input->focused_surface != event->new_surface) {
			wlr_text_input_v3_send_enter(text->input, event->new_surface);
		}
	}
}

/* O método de entrada mandou texto: vai para o campo activo. */
static void method_commit(struct wl_listener *listener, void *data) {
	struct wlr_input_method_v2 *method = data;
	struct orbit_text_input *text = relay.active;
	if (!text) {
		return;
	}
	struct wlr_text_input_v3 *input = text->input;
	if (method->current.preedit.text) {
		wlr_text_input_v3_send_preedit_string(input, method->current.preedit.text,
			method->current.preedit.cursor_begin, method->current.preedit.cursor_end);
	}
	if (method->current.commit_text) {
		wlr_text_input_v3_send_commit_string(input, method->current.commit_text);
	}
	if (method->current.delete.before_length || method->current.delete.after_length) {
		wlr_text_input_v3_send_delete_surrounding_text(input, method->current.delete.before_length,
			method->current.delete.after_length);
	}
	wlr_text_input_v3_send_done(input);
}

static void grab_destroy(struct wl_listener *listener, void *data) {
	wl_list_remove(&relay.grab_destroy.link);
	relay.grab = NULL;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(relay.server->seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_modifiers(relay.server->seat, &keyboard->modifiers);
	}
}

static void method_grab(struct wl_listener *listener, void *data) {
	struct wlr_input_method_keyboard_grab_v2 *grab = data;
	relay.grab = grab;
	wlr_input_method_keyboard_grab_v2_set_keyboard(grab, wlr_seat_get_keyboard(relay.server->seat));
	relay.grab_destroy.notify = grab_destroy;
	wl_signal_add(&grab->events.destroy, &relay.grab_destroy);
}

static void method_destroy(struct wl_listener *listener, void *data) {
	wl_list_remove(&relay.method_commit.link);
	wl_list_remove(&relay.method_grab.link);
	wl_list_remove(&relay.method_destroy.link);
	relay.method = NULL;
	wlr_log(WLR_INFO, "método de entrada saiu");
}

static void new_input_method(struct wl_listener *listener, void *data) {
	struct wlr_input_method_v2 *method = data;
	if (relay.method) {
		wlr_log(WLR_INFO, "já há um método de entrada; o novo fica de fora");
		wlr_input_method_v2_send_unavailable(method);
		return;
	}
	relay.method = method;
	relay.method_commit.notify = method_commit;
	wl_signal_add(&method->events.commit, &relay.method_commit);
	relay.method_grab.notify = method_grab;
	wl_signal_add(&method->events.grab_keyboard, &relay.method_grab);
	relay.method_destroy.notify = method_destroy;
	wl_signal_add(&method->events.destroy, &relay.method_destroy);
	wlr_log(WLR_INFO, "método de entrada ligado");
	if (relay.active && relay.active->input->current_enabled) {
		wlr_input_method_v2_send_activate(method);
		send_state(relay.active->input);
	}
}

static bool from_virtual_keyboard(struct wlr_keyboard *keyboard) {
	return wlr_input_device_get_virtual_keyboard(&keyboard->base) != NULL;
}

bool orbit_im_forward_key(struct orbit_server *server, struct wlr_keyboard *keyboard,
		struct wlr_keyboard_key_event *event) {
	if (!relay.grab || from_virtual_keyboard(keyboard)) {
		return false;
	}
	wlr_input_method_keyboard_grab_v2_set_keyboard(relay.grab, keyboard);
	wlr_input_method_keyboard_grab_v2_send_key(relay.grab, event->time_msec, event->keycode, event->state);
	(void)server;
	return true;
}

bool orbit_im_forward_modifiers(struct orbit_server *server, struct wlr_keyboard *keyboard) {
	if (!relay.grab || from_virtual_keyboard(keyboard)) {
		return false;
	}
	wlr_input_method_keyboard_grab_v2_set_keyboard(relay.grab, keyboard);
	wlr_input_method_keyboard_grab_v2_send_modifiers(relay.grab, &keyboard->modifiers);
	(void)server;
	return true;
}

static void new_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct wlr_virtual_keyboard_v1 *virtual = data;
	orbit_keyboard_add(relay.server, &virtual->keyboard, false);
	wlr_log(WLR_INFO, "teclado virtual ligado");
}

static void new_virtual_pointer(struct wl_listener *listener, void *data) {
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	wlr_cursor_attach_input_device(relay.server->cursor, &event->new_pointer->pointer.base);
	wlr_log(WLR_INFO, "ponteiro virtual ligado");
}

bool orbit_im_bind(struct orbit_server *server) {
	relay.server = server;
	wl_list_init(&relay.inputs);
	struct wlr_text_input_manager_v3 *text_inputs = wlr_text_input_manager_v3_create(server->display);
	struct wlr_input_method_manager_v2 *methods = wlr_input_method_manager_v2_create(server->display);
	struct wlr_virtual_keyboard_manager_v1 *virtuals = wlr_virtual_keyboard_manager_v1_create(server->display);
	struct wlr_virtual_pointer_manager_v1 *pointers = wlr_virtual_pointer_manager_v1_create(server->display);
	if (!text_inputs || !methods || !virtuals || !pointers) {
		wlr_log(WLR_ERROR, "falha ao criar os protocolos de escrita");
		return false;
	}
	relay.new_text_input.notify = new_text_input;
	wl_signal_add(&text_inputs->events.text_input, &relay.new_text_input);
	relay.new_input_method.notify = new_input_method;
	wl_signal_add(&methods->events.input_method, &relay.new_input_method);
	relay.new_virtual_keyboard.notify = new_virtual_keyboard;
	wl_signal_add(&virtuals->events.new_virtual_keyboard, &relay.new_virtual_keyboard);
	relay.new_virtual_pointer.notify = new_virtual_pointer;
	wl_signal_add(&pointers->events.new_virtual_pointer, &relay.new_virtual_pointer);
	relay.focus_change.notify = keyboard_focus_change;
	wl_signal_add(&server->seat->keyboard_state.events.focus_change, &relay.focus_change);
	return true;
}
