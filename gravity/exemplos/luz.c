/*
 * lumna-luz: a luz noturna (Painel → Telas e modos → Luz noturna). Lê ~/.config/lumna/luz.ini (servicos/luz.h) e ajusta
 * a gama de cada tela pelo wlr-gamma-control do Orbit: mais quente à noite, aos poucos nos 30 minutos de cada ponta.
 * Fica vivo enquanto a luz age (o compositor devolve a cor normal sozinho quando o programa sai ou solta a tela). Sobe com a sessão:
 *   lumna-luz &
 */
#define _GNU_SOURCE
#include <glib-unix.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "luz.h"
#include "wlr-gamma-control-unstable-v1-client-protocol.h"

struct saida {
	struct wl_output *wl;
	struct zwlr_gamma_control_v1 *ctrl;
	uint32_t tam;
};

static struct wl_display *display;
static struct zwlr_gamma_control_manager_v1 *gerente;
static struct saida saidas[8];
static int nsaidas;
static struct luz_cfg cfg;
static int aplicado = LUZ_NORMAL;
static bool avisou;
static char *ini;

static void aplicar_saida(struct saida *s, int kelvin) {
	if (!s->ctrl || s->tam == 0) {
		return;
	}
	size_t bytes = (size_t)s->tam * 3 * sizeof(uint16_t);
	int fd = memfd_create("lumna-luz", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)bytes) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		return;
	}
	uint16_t *t = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (t == MAP_FAILED) {
		close(fd);
		return;
	}
	luz_rampa(kelvin, t, t + s->tam, t + 2 * s->tam, s->tam);
	munmap(t, bytes);
	zwlr_gamma_control_v1_set_gamma(s->ctrl, fd);
	close(fd);
}

static void gama_tamanho(void *d, struct zwlr_gamma_control_v1 *c, uint32_t tam) {
	struct saida *s = d;
	s->tam = tam;
	aplicar_saida(s, aplicado);
	wl_display_flush(display);
}

static void gama_falhou(void *d, struct zwlr_gamma_control_v1 *c) {
	struct saida *s = d;
	if (!avisou) {
		fprintf(stderr, "lumna-luz: o compositor não aceitou a gama desta tela (outro programa de cor já a controla, ou a tela não tem gama)\n");
		avisou = true;
	}
	zwlr_gamma_control_v1_destroy(s->ctrl);
	s->ctrl = NULL;
	s->tam = 0;
}

static const struct zwlr_gamma_control_v1_listener gama_ouvinte = { .gamma_size = gama_tamanho, .failed = gama_falhou };

static void soltar_todas(void) {
	for (int i = 0; i < nsaidas; i++) {
		if (saidas[i].ctrl) {
			zwlr_gamma_control_v1_destroy(saidas[i].ctrl); /* destruir devolve a cor de antes */
			saidas[i].ctrl = NULL;
			saidas[i].tam = 0;
		}
	}
	wl_display_flush(display);
}

/* o que a luz pede neste minuto: age nas telas (ou as solta, se é a cor normal) */
static void atualizar(bool forcar) {
	time_t agora = time(NULL);
	struct tm t;
	localtime_r(&agora, &t);
	int k = luz_kelvin_agora(&cfg, t.tm_hour * 60 + t.tm_min);
	if (!forcar && k == aplicado) {
		return;
	}
	aplicado = k;
	if (k >= LUZ_NORMAL) {
		soltar_todas();
		return;
	}
	for (int i = 0; i < nsaidas; i++) {
		if (!saidas[i].ctrl) {
			saidas[i].ctrl = zwlr_gamma_control_manager_v1_get_gamma_control(gerente, saidas[i].wl);
			zwlr_gamma_control_v1_add_listener(saidas[i].ctrl, &gama_ouvinte, &saidas[i]); /* o gamma_size chega e aplica */
		} else {
			aplicar_saida(&saidas[i], k);
		}
	}
	wl_display_flush(display);
}

static void registo_global(void *d, struct wl_registry *r, uint32_t nome, const char *iface, uint32_t versao) {
	if (!strcmp(iface, zwlr_gamma_control_manager_v1_interface.name)) {
		gerente = wl_registry_bind(r, nome, &zwlr_gamma_control_manager_v1_interface, 1);
	} else if (!strcmp(iface, wl_output_interface.name) && nsaidas < 8) {
		saidas[nsaidas++].wl = wl_registry_bind(r, nome, &wl_output_interface, 1);
	}
}

static void registo_saiu(void *d, struct wl_registry *r, uint32_t nome) {}

static const struct wl_registry_listener registo_ouvinte = { .global = registo_global, .global_remove = registo_saiu };

static gboolean wayland_pronto(gint fd, GIOCondition cond, gpointer d) {
	if (cond & (G_IO_ERR | G_IO_HUP) || wl_display_dispatch(display) < 0) {
		g_main_loop_quit(d);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean ini_mudou(gint fd, GIOCondition cond, gpointer d) {
	char buf[4096];
	if (read(fd, buf, sizeof(buf)) > 0) {
		luz_ler(&cfg);
		atualizar(true);
	}
	return G_SOURCE_CONTINUE;
}

static gboolean passou(gpointer d) {
	atualizar(false); /* a cada 30 s: a transição de 30 minutos anda sozinha */
	return G_SOURCE_CONTINUE;
}

static gboolean sair(gpointer laco) {
	g_main_loop_quit(laco);
	return G_SOURCE_REMOVE;
}

int main(void) {
	const char *config = getenv("XDG_CONFIG_HOME");
	ini = config && *config ? g_build_filename(config, "lumna", "luz.ini", NULL) : g_build_filename(g_get_home_dir(), ".config", "lumna", "luz.ini", NULL);
	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "lumna-luz: sem compositor Wayland\n");
		return 1;
	}
	struct wl_registry *registo = wl_display_get_registry(display);
	wl_registry_add_listener(registo, &registo_ouvinte, NULL);
	wl_display_roundtrip(display);
	if (!gerente) {
		fprintf(stderr, "lumna-luz: o compositor não tem wlr-gamma-control; a luz noturna não funciona aqui\n");
		return 1;
	}
	luz_ler(&cfg);
	GMainLoop *laco = g_main_loop_new(NULL, FALSE);
	g_unix_fd_add(wl_display_get_fd(display), G_IO_IN | G_IO_ERR | G_IO_HUP, wayland_pronto, laco);
	int in = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	char *pasta = g_path_get_dirname(ini);
	g_mkdir_with_parents(pasta, 0700);
	if (in >= 0 && inotify_add_watch(in, pasta, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE) >= 0) {
		g_unix_fd_add(in, G_IO_IN, ini_mudou, NULL);
	}
	g_free(pasta);
	g_timeout_add_seconds(30, passou, NULL);
	g_unix_signal_add(SIGTERM, sair, laco);
	g_unix_signal_add(SIGINT, sair, laco);
	atualizar(true);
	g_main_loop_run(laco);
	soltar_todas();
	return 0;
}
