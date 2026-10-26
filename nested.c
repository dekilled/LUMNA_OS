#include "nested.h"

#include <string.h>
#include <wayland-client.h>

#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"

static struct zwp_keyboard_shortcuts_inhibit_manager_v1 *gerente;
static struct wl_seat *seat;
static struct wl_display *conhecido;

static void global(void *dados, struct wl_registry *reg, uint32_t nome, const char *interface, uint32_t versao) {
	if (!strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name)) {
		gerente = wl_registry_bind(reg, nome, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
	} else if (!strcmp(interface, wl_seat_interface.name) && !seat) {
		seat = wl_registry_bind(reg, nome, &wl_seat_interface, 1);
	}
}

static void global_some(void *dados, struct wl_registry *reg, uint32_t nome) {
}

static const struct wl_registry_listener ouvinte = { global, global_some };

/* o que o compositor de fora tem: numa fila à parte, para não mexer na do backend do wlroots */
static bool descobrir(struct wl_display *remote) {
	if (conhecido == remote) {
		return gerente && seat;
	}
	conhecido = remote;
	gerente = NULL;
	seat = NULL;
	struct wl_event_queue *fila = wl_display_create_queue(remote);
	struct wl_display *embrulho = wl_proxy_create_wrapper(remote);
	wl_proxy_set_queue((struct wl_proxy *)embrulho, fila);
	struct wl_registry *reg = wl_display_get_registry(embrulho);
	wl_proxy_wrapper_destroy(embrulho);
	wl_registry_add_listener(reg, &ouvinte, NULL);
	wl_display_roundtrip_queue(remote, fila);
	/* o que se ligou volta para a fila de sempre: os eventos deles não têm quem ouça e não se acumulam */
	if (gerente) {
		wl_proxy_set_queue((struct wl_proxy *)gerente, NULL);
	}
	if (seat) {
		wl_proxy_set_queue((struct wl_proxy *)seat, NULL);
	}
	wl_proxy_set_queue((struct wl_proxy *)reg, NULL);
	wl_display_roundtrip_queue(remote, fila);
	wl_event_queue_destroy(fila);
	return gerente && seat;
}

bool orbit_nested_inhibit(struct wl_display *remote, struct wl_surface *surface) {
	if (!remote || !surface || !descobrir(remote)) {
		return false;
	}
	zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(gerente, surface, seat);
	return wl_display_flush(remote) >= 0;
}
