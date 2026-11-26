/*
 * Um cliente Wayland mínimo para provar o xdg-foreign do Orbit (v1 e v2). Faz o papel de dois processos, com duas conexões:
 *   A = o app (tem a janela-pai e a exporta),  B = o portal do seletor de arquivos (importa o handle e faz do seu diálogo filho dela).
 * Imprime uma linha por fato; o teste_foreign.sh confere as linhas:
 *   global NOME               o Orbit anuncia zxdg_exporter_v1/v2 e zxdg_importer_v1/v2
 *   vN handle ok              a exportação devolveu um handle não vazio
 *   vN import vivo            importar o handle devolve um objeto que NÃO é destruído (o pai existe) e set_parent_of não dá erro de protocolo
 *   vN handle ruim destruido  importar um handle que não existe: o Orbit responde "destroyed" (e não derruba o cliente)
 *   vN fim da exportacao      o app fecha a janela exportada: o importador recebe "destroyed"
 *   vcruzado ...              o handle exportado no v1 importado pelo v2 (o registro é um só)
 *   erro ...                  erro de protocolo (o Orbit derrubou a conexão)
 *   fim                       tudo rodou
 *   cliente_foreign
 */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-foreign-unstable-v1-client-protocol.h"
#include "xdg-foreign-unstable-v2-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define LARG 200
#define ALT 150

struct conn {
	const char *nome;
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm;
	struct zxdg_exporter_v1 *exp1;
	struct zxdg_importer_v1 *imp1;
	struct zxdg_exporter_v2 *exp2;
	struct zxdg_importer_v2 *imp2;
};

struct janela {
	struct conn *c;
	struct wl_surface *surface;
	struct xdg_surface *xs;
	struct xdg_toplevel *tl;
	bool configurada;
};

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t serial) { xdg_wm_base_pong(w, serial); }
static const struct xdg_wm_base_listener wm_ouvinte = { wm_ping };

static void global(void *d, struct wl_registry *r, uint32_t nome, const char *iface, uint32_t v) {
	struct conn *c = d;
	if (!strcmp(iface, "wl_compositor")) {
		c->compositor = wl_registry_bind(r, nome, &wl_compositor_interface, 4);
	} else if (!strcmp(iface, "wl_shm")) {
		c->shm = wl_registry_bind(r, nome, &wl_shm_interface, 1);
	} else if (!strcmp(iface, "xdg_wm_base")) {
		c->wm = wl_registry_bind(r, nome, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(c->wm, &wm_ouvinte, NULL);
	} else if (!strcmp(iface, "zxdg_exporter_v1")) {
		c->exp1 = wl_registry_bind(r, nome, &zxdg_exporter_v1_interface, 1);
	} else if (!strcmp(iface, "zxdg_importer_v1")) {
		c->imp1 = wl_registry_bind(r, nome, &zxdg_importer_v1_interface, 1);
	} else if (!strcmp(iface, "zxdg_exporter_v2")) {
		c->exp2 = wl_registry_bind(r, nome, &zxdg_exporter_v2_interface, 1);
	} else if (!strcmp(iface, "zxdg_importer_v2")) {
		c->imp2 = wl_registry_bind(r, nome, &zxdg_importer_v2_interface, 1);
	} else {
		return;
	}
	if (!strncmp(iface, "zxdg_", 5)) {
		if (c->nome[0] == 'A') {
			printf("global %s\n", iface);
		}
	}
}
static void global_sai(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener reg_ouvinte = { global, global_sai };

static void roundtrip(struct conn *c) {
	if (wl_display_roundtrip(c->display) < 0) {
		int e = wl_display_get_error(c->display);
		const struct wl_interface *iface = NULL;
		uint32_t id = 0;
		uint32_t code = wl_display_get_protocol_error(c->display, &iface, &id);
		printf("erro conexao %s: errno %d, protocolo %s codigo %u\n", c->nome, e, iface ? iface->name : "-", code);
		fflush(stdout);
		exit(3);
	}
}

static bool conectar(struct conn *c, const char *nome) {
	c->nome = nome;
	c->display = wl_display_connect(NULL);
	if (!c->display) {
		return false;
	}
	struct wl_registry *reg = wl_display_get_registry(c->display);
	wl_registry_add_listener(reg, &reg_ouvinte, c);
	roundtrip(c);
	return c->compositor && c->shm && c->wm;
}

static struct wl_buffer *buffer_cor(struct conn *c, uint32_t cor) {
	int fd = memfd_create("foreign", 0);
	size_t n = (size_t)LARG * ALT * 4;
	if (ftruncate(fd, (off_t)n) < 0) {
		exit(4);
	}
	uint32_t *px = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	for (size_t i = 0; i < (size_t)LARG * ALT; i++) {
		px[i] = cor;
	}
	munmap(px, n);
	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, (int)n);
	struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, LARG, ALT, LARG * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return b;
}

static void xs_config(void *d, struct xdg_surface *xs, uint32_t serial) {
	struct janela *j = d;
	xdg_surface_ack_configure(xs, serial);
	if (!j->configurada) {
		j->configurada = true;
		wl_surface_attach(j->surface, buffer_cor(j->c, 0xff7c4dcc), 0, 0);
		wl_surface_commit(j->surface);
	}
}
static const struct xdg_surface_listener xs_ouvinte = { xs_config };
static void tl_config(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s) {}
static void tl_close(void *d, struct xdg_toplevel *t) {}
static const struct xdg_toplevel_listener tl_ouvinte = { tl_config, tl_close };

/* uma janela normal, aberta e mapeada */
static struct janela *abrir(struct conn *c, const char *titulo) {
	struct janela *j = calloc(1, sizeof *j);
	j->c = c;
	j->surface = wl_compositor_create_surface(c->compositor);
	j->xs = xdg_wm_base_get_xdg_surface(c->wm, j->surface);
	xdg_surface_add_listener(j->xs, &xs_ouvinte, j);
	j->tl = xdg_surface_get_toplevel(j->xs);
	xdg_toplevel_add_listener(j->tl, &tl_ouvinte, j);
	xdg_toplevel_set_title(j->tl, titulo);
	wl_surface_commit(j->surface);
	roundtrip(c);
	roundtrip(c);
	return j;
}

static void fechar(struct janela *j) {
	xdg_toplevel_destroy(j->tl);
	xdg_surface_destroy(j->xs);
	wl_surface_destroy(j->surface);
	roundtrip(j->c);
}

/* ---- as duas versões do protocolo, o mesmo roteiro ---- */

static char handle1[256], handle2[256];
static bool destruido1, destruido2, destruido_ruim1, destruido_ruim2;

static void h1(void *d, struct zxdg_exported_v1 *e, const char *h) { snprintf(handle1, sizeof handle1, "%s", h); }
static const struct zxdg_exported_v1_listener exp1_ouvinte = { h1 };
static void h2(void *d, struct zxdg_exported_v2 *e, const char *h) { snprintf(handle2, sizeof handle2, "%s", h); }
static const struct zxdg_exported_v2_listener exp2_ouvinte = { h2 };

static void d1(void *d, struct zxdg_imported_v1 *i) { *(bool *)d = true; }
static const struct zxdg_imported_v1_listener imp1_ouvinte = { d1 };
static void d2(void *d, struct zxdg_imported_v2 *i) { *(bool *)d = true; }
static const struct zxdg_imported_v2_listener imp2_ouvinte = { d2 };

int main(void) {
	struct conn a, b;
	if (!conectar(&a, "A") || !conectar(&b, "B")) {
		fprintf(stderr, "sem Wayland\n");
		return 1;
	}
	if (!a.exp1 || !b.imp1 || !a.exp2 || !b.imp2) {
		printf("faltou global (exporter_v1=%p importer_v1=%p exporter_v2=%p importer_v2=%p)\n", (void *)a.exp1, (void *)b.imp1, (void *)a.exp2, (void *)b.imp2);
		return 2;
	}

	/* v2: o app exporta, o portal importa e faz do diálogo dele filho */
	struct janela *pai2 = abrir(&a, "pai v2");
	struct zxdg_exported_v2 *ex2 = zxdg_exporter_v2_export_toplevel(a.exp2, pai2->surface);
	zxdg_exported_v2_add_listener(ex2, &exp2_ouvinte, NULL);
	roundtrip(&a);
	printf(handle2[0] ? "v2 handle ok\n" : "v2 handle VAZIO\n");
	struct janela *dialogo2 = abrir(&b, "dialogo v2");
	struct zxdg_imported_v2 *im2 = zxdg_importer_v2_import_toplevel(b.imp2, handle2);
	zxdg_imported_v2_add_listener(im2, &imp2_ouvinte, &destruido2);
	zxdg_imported_v2_set_parent_of(im2, dialogo2->surface);
	roundtrip(&b);
	roundtrip(&a);
	roundtrip(&b);
	printf(destruido2 ? "v2 import MORTO\n" : "v2 import vivo\n");
	struct zxdg_imported_v2 *ruim2 = zxdg_importer_v2_import_toplevel(b.imp2, "este-handle-nao-existe");
	zxdg_imported_v2_add_listener(ruim2, &imp2_ouvinte, &destruido_ruim2);
	roundtrip(&b);
	printf(destruido_ruim2 ? "v2 handle ruim destruido\n" : "v2 handle ruim VIVO\n");
	zxdg_exported_v2_destroy(ex2); /* o app deixa de exportar: o portal é avisado */
	roundtrip(&a);
	roundtrip(&b);
	printf(destruido2 ? "v2 fim da exportacao destruido\n" : "v2 fim da exportacao SEM AVISO\n");

	/* v1: o mesmo */
	struct janela *pai1 = abrir(&a, "pai v1");
	struct zxdg_exported_v1 *ex1 = zxdg_exporter_v1_export(a.exp1, pai1->surface);
	zxdg_exported_v1_add_listener(ex1, &exp1_ouvinte, NULL);
	roundtrip(&a);
	printf(handle1[0] ? "v1 handle ok\n" : "v1 handle VAZIO\n");
	struct janela *dialogo1 = abrir(&b, "dialogo v1");
	struct zxdg_imported_v1 *im1 = zxdg_importer_v1_import(b.imp1, handle1);
	zxdg_imported_v1_add_listener(im1, &imp1_ouvinte, &destruido1);
	zxdg_imported_v1_set_parent_of(im1, dialogo1->surface);
	roundtrip(&b);
	roundtrip(&a);
	roundtrip(&b);
	printf(destruido1 ? "v1 import MORTO\n" : "v1 import vivo\n");
	struct zxdg_imported_v1 *ruim1 = zxdg_importer_v1_import(b.imp1, "este-handle-nao-existe");
	zxdg_imported_v1_add_listener(ruim1, &imp1_ouvinte, &destruido_ruim1);
	roundtrip(&b);
	printf(destruido_ruim1 ? "v1 handle ruim destruido\n" : "v1 handle ruim VIVO\n");
	/* o app fecha a janela exportada: o importador recebe "destroyed" */
	fechar(pai1);
	roundtrip(&b);
	printf(destruido1 ? "v1 fim da exportacao destruido\n" : "v1 fim da exportacao SEM AVISO\n");

	/* o registro é um só: um handle do v1 vale no v2 (e o contrário) */
	struct janela *pai3 = abrir(&a, "pai cruzado");
	handle1[0] = 0;
	struct zxdg_exported_v1 *ex3 = zxdg_exporter_v1_export(a.exp1, pai3->surface);
	zxdg_exported_v1_add_listener(ex3, &exp1_ouvinte, NULL);
	roundtrip(&a);
	static bool destruido_cruz;
	struct zxdg_imported_v2 *cruz = zxdg_importer_v2_import_toplevel(b.imp2, handle1);
	zxdg_imported_v2_add_listener(cruz, &imp2_ouvinte, &destruido_cruz);
	roundtrip(&b);
	printf(destruido_cruz ? "vcruzado v1->v2 destruido\n" : "vcruzado v1->v2 vivo\n");

	fechar(dialogo1);
	fechar(dialogo2);
	fechar(pai2);
	fechar(pai3);
	roundtrip(&a);
	roundtrip(&b);
	puts("fim");
	return 0;
}
