/*
 * Um app de mentira com um campo de texto (text-input-v3) para testar o teclado na tela: abre uma janela, liga o text-input quando ganha o
 * foco do teclado, guarda o que chega e imprime, uma linha por evento:
 *   campo ligado | texto <conteúdo todo agora> | tecla <código> <press|solta> | desligado
 *   cliente_texto SEGUNDOS [proposito] [dica]   (o propósito e a dica que o campo declara)
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

#include "text-input-unstable-v3-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define LARG 400
#define ALT 200

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_seat *seat;
static struct zwp_text_input_manager_v3 *gerente;
static struct zwp_text_input_v3 *ti;
static struct wl_surface *superficie;
static struct wl_keyboard *teclado;
static bool configurado;
static char texto[512];
static int cursor;
static uint32_t proposito, dica;
/* o que chegou até o done */
static char novo_commit[256];
static bool tem_commit;
static uint32_t apaga_antes, apaga_depois;
static uint32_t serial_ti;

static void imprime_texto(void) {
	printf("texto [%s]\n", texto);
	fflush(stdout);
}

static void envia_estado(void) {
	zwp_text_input_v3_set_surrounding_text(ti, texto, cursor, cursor);
	zwp_text_input_v3_set_content_type(ti, dica, proposito);
	zwp_text_input_v3_commit(ti);
	serial_ti++;
}

static void ti_entrou(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s) {
	zwp_text_input_v3_enable(t);
	envia_estado();
	puts("campo ligado");
	fflush(stdout);
}
static void ti_saiu(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s) {
	puts("desligado");
	fflush(stdout);
}
static void ti_preedit(void *d, struct zwp_text_input_v3 *t, const char *x, int32_t a, int32_t b) {}
static void ti_commit(void *d, struct zwp_text_input_v3 *t, const char *x) {
	snprintf(novo_commit, sizeof(novo_commit), "%s", x ? x : "");
	tem_commit = true;
}
static void ti_apagar(void *d, struct zwp_text_input_v3 *t, uint32_t antes, uint32_t depois) {
	apaga_antes = antes;
	apaga_depois = depois;
}
static void ti_feito(void *d, struct zwp_text_input_v3 *t, uint32_t serial) {
	int len = (int)strlen(texto);
	if (apaga_antes || apaga_depois) {
		int ini = cursor - (int)apaga_antes < 0 ? 0 : cursor - (int)apaga_antes;
		int fim = cursor + (int)apaga_depois > len ? len : cursor + (int)apaga_depois;
		memmove(texto + ini, texto + fim, (size_t)(len - fim + 1));
		cursor = ini;
	}
	if (tem_commit) {
		size_t n = strlen(novo_commit);
		memmove(texto + cursor + n, texto + cursor, strlen(texto + cursor) + 1);
		memcpy(texto + cursor, novo_commit, n);
		cursor += (int)n;
	}
	if (apaga_antes || apaga_depois || tem_commit) {
		imprime_texto();
		envia_estado();
	}
	apaga_antes = apaga_depois = 0;
	tem_commit = false;
}
static const struct zwp_text_input_v3_listener ti_ouvinte = {
	.enter = ti_entrou, .leave = ti_saiu, .preedit_string = ti_preedit, .commit_string = ti_commit, .delete_surrounding_text = ti_apagar, .done = ti_feito,
};

static void k_mapa(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t tam) { close(fd); }
static void k_entrou(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sup, struct wl_array *t) {}
static void k_saiu(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sup) {}
static void k_tecla(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t tecla, uint32_t estado) {
	printf("tecla %u %s\n", tecla, estado == WL_KEYBOARD_KEY_STATE_PRESSED ? "press" : "solta");
	fflush(stdout);
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t g) {}
static void k_repete(void *d, struct wl_keyboard *k, int32_t a, int32_t b) {}
static const struct wl_keyboard_listener k_ouvinte = { k_mapa, k_entrou, k_saiu, k_tecla, k_mods, k_repete };

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !teclado) {
		teclado = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(teclado, &k_ouvinte, NULL);
	}
}
static void seat_nome(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_ouvinte = { seat_caps, seat_nome };

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t serial) { xdg_wm_base_pong(w, serial); }
static const struct xdg_wm_base_listener wm_ouvinte = { wm_ping };

static struct wl_buffer *buffer_cor(void) {
	int fd = memfd_create("campo", 0);
	size_t n = (size_t)LARG * ALT * 4;
	if (ftruncate(fd, (off_t)n) != 0) {
		exit(3);
	}
	uint32_t *px = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	for (size_t i = 0; i < (size_t)LARG * ALT; i++) {
		px[i] = 0xfff0f0f0;
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
		wl_surface_attach(superficie, buffer_cor(), 0, 0);
		wl_surface_commit(superficie);
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
	} else if (!strcmp(iface, "zwp_text_input_manager_v3")) {
		gerente = wl_registry_bind(r, nome, &zwp_text_input_manager_v3_interface, 1);
	}
}
static void global_sai(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener reg_ouvinte = { global, global_sai };

int main(int argc, char **argv) {
	int segundos = argc > 1 ? atoi(argv[1]) : 10;
	proposito = argc > 2 ? (uint32_t)atoi(argv[2]) : 0;
	dica = argc > 3 ? (uint32_t)atoi(argv[3]) : 0;
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "sem Wayland\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(display);
	wl_registry_add_listener(reg, &reg_ouvinte, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm || !wm || !seat || !gerente) {
		fprintf(stderr, "faltou um global\n");
		return 2;
	}
	ti = zwp_text_input_manager_v3_get_text_input(gerente, seat);
	zwp_text_input_v3_add_listener(ti, &ti_ouvinte, NULL);
	superficie = wl_compositor_create_surface(compositor);
	struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, superficie);
	xdg_surface_add_listener(xs, &xs_ouvinte, NULL);
	struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
	xdg_toplevel_add_listener(tl, &tl_ouvinte, NULL);
	xdg_toplevel_set_title(tl, "campo");
	wl_surface_commit(superficie);
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
	puts("fim");
	return 0;
}
