/*
 * As janelas abertas no compositor (wlr-foreign-toplevel-management): título, app id, se está
 * ativa ou minimizada, e os pedidos de ativar e fechar. É o que a dock e o alternador usam.
 * Só existe se o compositor tiver o protocolo (o Orbit tem).
 */
#include "gv_interno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#define GV_MAX_JANELAS 64

struct janela {
	struct zwlr_foreign_toplevel_handle_v1 *alca;
	gv_janela publica;
	char titulo[256], app_id[128];
	/* o que chega até ao "done" fica à espera, para mudar tudo de uma vez */
	char titulo_novo[256], app_id_novo[128];
	bool ativa_nova, minimizada_nova, maximizada_nova, tela_cheia_nova;
	struct wl_output *saida; /* a última onde entrou (NULL: em nenhuma) */
};

static struct zwlr_foreign_toplevel_manager_v1 *gestor;
static struct wl_seat *seat;
static struct janela janelas[GV_MAX_JANELAS];
static int njanelas;
static uint32_t proximo_id = 1;

static struct janela *por_alca(struct zwlr_foreign_toplevel_handle_v1 *alca) {
	for (int i = 0; i < njanelas; i++) {
		if (janelas[i].alca == alca) {
			return &janelas[i];
		}
	}
	return NULL;
}

static void titulo(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *t) {
	struct janela *j = por_alca(h);
	if (j) {
		snprintf(j->titulo_novo, sizeof(j->titulo_novo), "%s", t);
	}
}

static void app_id(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *a) {
	struct janela *j = por_alca(h);
	if (j) {
		snprintf(j->app_id_novo, sizeof(j->app_id_novo), "%s", a);
	}
}

static void saida_entrou(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) {
	struct janela *j = por_alca(h);
	if (j) {
		j->saida = o;
	}
}

static void saida_saiu(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) {
	struct janela *j = por_alca(h);
	if (j && j->saida == o) {
		j->saida = NULL;
	}
}

static void estado(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *estados) {
	struct janela *j = por_alca(h);
	if (!j) {
		return;
	}
	j->ativa_nova = j->minimizada_nova = j->maximizada_nova = j->tela_cheia_nova = false;
	uint32_t *e;
	wl_array_for_each(e, estados) {
		if (*e == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
			j->ativa_nova = true;
		} else if (*e == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
			j->minimizada_nova = true;
		} else if (*e == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) {
			j->maximizada_nova = true;
		} else if (*e == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) {
			j->tela_cheia_nova = true;
		}
	}
}

static void feito(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {
	struct janela *j = por_alca(h);
	if (!j) {
		return;
	}
	memcpy(j->titulo, j->titulo_novo, sizeof(j->titulo));
	memcpy(j->app_id, j->app_id_novo, sizeof(j->app_id));
	j->publica.titulo = j->titulo;
	j->publica.app_id = j->app_id;
	j->publica.ativa = j->ativa_nova;
	j->publica.minimizada = j->minimizada_nova;
	j->publica.maximizada = j->maximizada_nova;
	j->publica.tela_cheia = j->tela_cheia_nova;
	j->publica.saida = j->saida ? gv__saida_nome(j->saida) : "";
	j->publica.pronta = true;
	gv_app_redesenhar(NULL);
}

static void fechada(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {
	for (int i = 0; i < njanelas; i++) {
		if (janelas[i].alca == h) {
			zwlr_foreign_toplevel_handle_v1_destroy(h);
			memmove(&janelas[i], &janelas[i + 1], (njanelas - i - 1) * sizeof(janelas[0]));
			njanelas--;
			/* os ponteiros de texto apontam para dentro de cada entrada: refazê-los */
			for (int k = 0; k < njanelas; k++) {
				janelas[k].publica.titulo = janelas[k].titulo;
				janelas[k].publica.app_id = janelas[k].app_id;
			}
			break;
		}
	}
	gv_app_redesenhar(NULL);
}

static void pai(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct zwlr_foreign_toplevel_handle_v1 *p) {}

static const struct zwlr_foreign_toplevel_handle_v1_listener alca_ouvinte = {
	.title = titulo,
	.app_id = app_id,
	.output_enter = saida_entrou,
	.output_leave = saida_saiu,
	.state = estado,
	.done = feito,
	.closed = fechada,
	.parent = pai,
};

static void nova(void *d, struct zwlr_foreign_toplevel_manager_v1 *g, struct zwlr_foreign_toplevel_handle_v1 *alca) {
	if (njanelas >= GV_MAX_JANELAS) {
		zwlr_foreign_toplevel_handle_v1_destroy(alca);
		return;
	}
	struct janela *j = &janelas[njanelas++];
	memset(j, 0, sizeof(*j));
	j->alca = alca;
	j->publica.id = proximo_id++;
	j->publica.titulo = j->titulo;
	j->publica.app_id = j->app_id;
	zwlr_foreign_toplevel_handle_v1_add_listener(alca, &alca_ouvinte, NULL);
}

static void acabou(void *d, struct zwlr_foreign_toplevel_manager_v1 *g) {
	zwlr_foreign_toplevel_manager_v1_destroy(g);
	gestor = NULL;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener gestor_ouvinte = { .toplevel = nova, .finished = acabou };

bool gv__janelas_global(struct wl_registry *registo, uint32_t nome, const char *iface, uint32_t versao) {
	if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
		return false;
	}
	gestor = wl_registry_bind(registo, nome, &zwlr_foreign_toplevel_manager_v1_interface, versao < 3 ? versao : 3);
	zwlr_foreign_toplevel_manager_v1_add_listener(gestor, &gestor_ouvinte, NULL);
	return true;
}

void gv__janelas_seat(struct wl_seat *s) {
	seat = s;
}

int gv_janelas(const gv_janela **lista, int max) {
	int n = 0;
	for (int i = 0; i < njanelas && n < max; i++) {
		if (janelas[i].publica.pronta) {
			lista[n++] = &janelas[i].publica;
		}
	}
	return n;
}

bool gv_janelas_disponivel(void) {
	return gestor != NULL;
}

static struct janela *por_id(uint32_t id) {
	for (int i = 0; i < njanelas; i++) {
		if (janelas[i].publica.id == id) {
			return &janelas[i];
		}
	}
	return NULL;
}

void gv_janela_ativar(uint32_t id) {
	struct janela *j = por_id(id);
	if (j && seat) {
		zwlr_foreign_toplevel_handle_v1_activate(j->alca, seat);
	}
}

void gv_janela_maximizar(uint32_t id, bool sim) {
	struct janela *j = por_id(id);
	if (j) {
		if (sim) {
			zwlr_foreign_toplevel_handle_v1_set_maximized(j->alca);
		} else {
			zwlr_foreign_toplevel_handle_v1_unset_maximized(j->alca);
		}
	}
}

void gv_janela_tela_cheia(uint32_t id, bool sim) {
	struct janela *j = por_id(id);
	if (j && zwlr_foreign_toplevel_handle_v1_get_version(j->alca) >= 2) {
		if (sim) {
			zwlr_foreign_toplevel_handle_v1_set_fullscreen(j->alca, NULL);
		} else {
			zwlr_foreign_toplevel_handle_v1_unset_fullscreen(j->alca);
		}
	}
}

void gv_janela_fechar(uint32_t id) {
	struct janela *j = por_id(id);
	if (j) {
		zwlr_foreign_toplevel_handle_v1_close(j->alca);
	}
}
