/*
 * Um cliente Wayland mínimo para provar a trava do ponteiro do Orbit (pointer-constraints + relative-pointer): abre uma janela, e quando o mouse entra nela trava (lock)
 * ou confina (confine) o ponteiro. Imprime o que viu, uma linha por evento: enter, leave, motion X Y, locked, confined, rel DX DY, e no fim a soma dos relativos.
 *   cliente_trava lock|confine SEGUNDOS
 */
#define _GNU_SOURCE
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define LARG 300
#define ALT 300

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_seat *seat;
static struct zwp_pointer_constraints_v1 *constraints;
static struct zwp_relative_pointer_manager_v1 *relative;
static struct wl_surface *surface;
static struct wl_pointer *pointer;
static bool confinar, prendeu, configurado;
static double soma_x, soma_y;

static void rel_movimento(void *d, struct zwp_relative_pointer_v1 *r, uint32_t hi, uint32_t lo, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t ux, wl_fixed_t uy) {
	soma_x += wl_fixed_to_double(dx);
	soma_y += wl_fixed_to_double(dy);
}
static const struct zwp_relative_pointer_v1_listener rel_ouvinte = { .relative_motion = rel_movimento };

static void travou(void *d, struct zwp_locked_pointer_v1 *l) { puts("locked"); fflush(stdout); }
static void destravou(void *d, struct zwp_locked_pointer_v1 *l) { puts("unlocked"); fflush(stdout); }
static const struct zwp_locked_pointer_v1_listener trava_ouvinte = { .locked = travou, .unlocked = destravou };
static void confinou(void *d, struct zwp_confined_pointer_v1 *c) { puts("confined"); fflush(stdout); }
static void desconfinou(void *d, struct zwp_confined_pointer_v1 *c) { puts("unconfined"); fflush(stdout); }
static const struct zwp_confined_pointer_v1_listener confina_ouvinte = { .confined = confinou, .unconfined = desconfinou };

static void p_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sup, wl_fixed_t x, wl_fixed_t y) {
	puts("enter");
	fflush(stdout);
	if (prendeu) {
		return;
	}
	prendeu = true;
	struct zwp_relative_pointer_v1 *r = zwp_relative_pointer_manager_v1_get_relative_pointer(relative, p);
	zwp_relative_pointer_v1_add_listener(r, &rel_ouvinte, NULL);
	if (confinar) {
		struct zwp_confined_pointer_v1 *c = zwp_pointer_constraints_v1_confine_pointer(constraints, surface, p, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
		zwp_confined_pointer_v1_add_listener(c, &confina_ouvinte, NULL);
	} else {
		struct zwp_locked_pointer_v1 *l = zwp_pointer_constraints_v1_lock_pointer(constraints, surface, p, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
		zwp_locked_pointer_v1_add_listener(l, &trava_ouvinte, NULL);
	}
	wl_surface_commit(surface); /* a restrição vale (e a região, o tamanho da janela) a partir do commit */
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sup) { puts("leave"); fflush(stdout); }
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) { printf("motion %.0f %.0f\n", wl_fixed_to_double(x), wl_fixed_to_double(y)); fflush(stdout); }
static void p_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) {}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static void p_frame(void *d, struct wl_pointer *p) {}
static const struct wl_pointer_listener p_ouvinte = { p_enter, p_leave, p_motion, p_button, p_axis, p_frame };

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &p_ouvinte, NULL);
	}
}
static void seat_nome(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_ouvinte = { seat_caps, seat_nome };

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t serial) { xdg_wm_base_pong(w, serial); }
static const struct xdg_wm_base_listener wm_ouvinte = { wm_ping };

static struct wl_buffer *buffer_cor(void) {
	int fd = memfd_create("trava", 0);
	size_t n = (size_t)LARG * ALT * 4;
	ftruncate(fd, (off_t)n);
	uint32_t *px = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	for (size_t i = 0; i < (size_t)LARG * ALT; i++) {
		px[i] = 0xff7c4dcc;
	}
	munmap(px, n);
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)n);
	struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, LARG, ALT, LARG * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return b;
}

static void xs_config(void *d, struct xdg_surface *xs, uint32_t serial) {
	xdg_surface_ack_configure(xs, serial);
	if (!configurado) {
		configurado = true;
		wl_surface_attach(surface, buffer_cor(), 0, 0);
		wl_surface_commit(surface);
	}
}
static const struct xdg_surface_listener xs_ouvinte = { xs_config };
static void tl_config(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s) {}
static void tl_close(void *d, struct xdg_toplevel *t) { exit(0); }
static const struct xdg_toplevel_listener tl_ouvinte = { tl_config, tl_close };

static void global(void *d, struct wl_registry *r, uint32_t nome, const char *iface, uint32_t v) {
	if (!strcmp(iface, "wl_compositor")) {
		compositor = wl_registry_bind(r, nome, &wl_compositor_interface, 4);
	} else if (!strcmp(iface, "wl_shm")) {
		shm = wl_registry_bind(r, nome, &wl_shm_interface, 1);
	} else if (!strcmp(iface, "xdg_wm_base")) {
		wm = wl_registry_bind(r, nome, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wm, &wm_ouvinte, NULL);
	} else if (!strcmp(iface, "wl_seat")) {
		seat = wl_registry_bind(r, nome, &wl_seat_interface, 5);
		wl_seat_add_listener(seat, &seat_ouvinte, NULL);
	} else if (!strcmp(iface, "zwp_pointer_constraints_v1")) {
		constraints = wl_registry_bind(r, nome, &zwp_pointer_constraints_v1_interface, 1);
	} else if (!strcmp(iface, "zwp_relative_pointer_manager_v1")) {
		relative = wl_registry_bind(r, nome, &zwp_relative_pointer_manager_v1_interface, 1);
	}
}
static void global_sai(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener reg_ouvinte = { global, global_sai };

int main(int argc, char **argv) {
	confinar = argc > 1 && !strcmp(argv[1], "confine");
	int segundos = argc > 2 ? atoi(argv[2]) : 8;
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "sem Wayland\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(display);
	wl_registry_add_listener(reg, &reg_ouvinte, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm || !wm || !seat || !constraints || !relative) {
		fprintf(stderr, "faltou um global (constraints=%p relative=%p)\n", (void *)constraints, (void *)relative);
		return 2;
	}
	surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, surface);
	xdg_surface_add_listener(xs, &xs_ouvinte, NULL);
	struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
	xdg_toplevel_add_listener(tl, &tl_ouvinte, NULL);
	xdg_toplevel_set_title(tl, "trava");
	wl_surface_commit(surface);
	time_t fim = time(NULL) + segundos;
	while (time(NULL) < fim) {
		wl_display_flush(display);
		struct pollfd p = { .fd = wl_display_get_fd(display), .events = POLLIN };
		if (poll(&p, 1, 100) > 0) {
			wl_display_dispatch(display);
		} else {
			wl_display_dispatch_pending(display);
		}
	}
	printf("soma %.0f %.0f\nfim\n", soma_x, soma_y);
	return 0;
}
