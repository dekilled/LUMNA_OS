#include "ponteiro.h"

#include <glib.h>
#include <string.h>
#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

static struct wl_display *tela;
static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *gerente;
static struct zwlr_virtual_pointer_v1 *ptr;

static void global(void *data, struct wl_registry *reg, uint32_t nome, const char *interface, uint32_t versao) {
	if (!strcmp(interface, wl_seat_interface.name) && !seat) {
		seat = wl_registry_bind(reg, nome, &wl_seat_interface, 1);
	} else if (!strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name)) {
		gerente = wl_registry_bind(reg, nome, &zwlr_virtual_pointer_manager_v1_interface, 1);
	}
}

static void global_some(void *data, struct wl_registry *reg, uint32_t nome) {
}

static const struct wl_registry_listener ouvinte = { global, global_some };

static void desligar(void) {
	if (tela) {
		wl_display_disconnect(tela);
	}
	tela = NULL;
	seat = NULL;
	gerente = NULL;
	ptr = NULL;
}

static bool ligar(void) {
	if (ptr) {
		return true;
	}
	tela = wl_display_connect(NULL);
	if (!tela) {
		return false;
	}
	struct wl_registry *reg = wl_display_get_registry(tela);
	wl_registry_add_listener(reg, &ouvinte, NULL);
	wl_display_roundtrip(tela);
	if (!gerente || !seat) {
		desligar();
		return false;
	}
	ptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(gerente, seat);
	return wl_display_flush(tela) >= 0;
}

static uint32_t agora(void) {
	return (uint32_t)(g_get_monotonic_time() / 1000);
}

/* manda o que se pediu; se a conexão caiu, esquece tudo para religar na próxima */
static bool enviar(void) {
	zwlr_virtual_pointer_v1_frame(ptr);
	if (wl_display_flush(tela) < 0 || wl_display_get_error(tela) != 0) {
		desligar();
		return false;
	}
	return true;
}

bool ponteiro_mover(double dx, double dy) {
	if (!ligar()) {
		return false;
	}
	zwlr_virtual_pointer_v1_motion(ptr, agora(), wl_fixed_from_double(dx), wl_fixed_from_double(dy));
	return enviar();
}

bool ponteiro_botao(int botao, bool apertado) {
	if (!ligar()) {
		return false;
	}
	uint32_t codigo = botao == PONTEIRO_DIREITO ? BTN_RIGHT : botao == PONTEIRO_MEIO ? BTN_MIDDLE : BTN_LEFT;
	zwlr_virtual_pointer_v1_button(ptr, agora(), codigo, apertado ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
	return enviar();
}

bool ponteiro_rolar(double dx, double dy) {
	if (!ligar()) {
		return false;
	}
	uint32_t t = agora();
	zwlr_virtual_pointer_v1_axis_source(ptr, WL_POINTER_AXIS_SOURCE_FINGER);
	if (dy != 0) {
		zwlr_virtual_pointer_v1_axis(ptr, t, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(dy));
	}
	if (dx != 0) {
		zwlr_virtual_pointer_v1_axis(ptr, t, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(dx));
	}
	return enviar();
}
