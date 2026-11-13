/*
 * As animações das janelas (Painel > Aparência > Animações > "Abrir e fechar janelas"; animacoes.ini [geral] janelas = sim|nao):
 *  - ABRIR: a janela nasce um pouco maior (6%) e transparente e assenta no tamanho final já opaca, como um cartão jogado na mesa;
 *  - FECHAR: o contrário. Na hora em que a janela some, a última imagem dela fica num "fantasma" que cresce um pouco e esmaece.
 * Dura 180 ms. Não anima jogos nem janelas em tela cheia, nem as telas de Gaming e Mídia (ali o que manda é o desempenho), nem as telas do celular.
 *
 * Como: o wlroots não escala uma árvore, então cada imagem (scene buffer) da janela é escalada em volta do centro da janela (tamanho de destino
 * e posição) e ganha opacidade. As medidas de partida ficam guardadas e tudo volta ao exato no fim.
 */
#include "orbit.h"

#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#define WINANIM_MS_PADRAO 180
#define WINANIM_GROW 0.06f
#define WINANIM_MAX 48
#define GHOST_MAX 8

struct winanim_item {
	struct wlr_scene_buffer *buffer;
	int rel_x, rel_y; /* a posição de partida, relativa ao pai */
	int abs_x, abs_y; /* e na janela (a origem da árvore dela) */
	int w, h;         /* o tamanho de destino de partida */
};

struct orbit_winanim {
	uint32_t start;
	int n;
	float cx, cy; /* o centro da janela, na árvore dela */
	struct winanim_item item[WINANIM_MAX];
};

struct orbit_ghost {
	uint32_t start;
	struct wlr_scene_tree *tree;
	float cx, cy;
	int n;
	struct winanim_item item[WINANIM_MAX]; /* buffer: os nós do fantasma (nossos) */
};

/* a duração (ms); LUMNA_TESTE_JANELA_MS só existe para os testes poderem ver os quadros do meio */
static float duration_ms(void) {
	static int ms;
	if (!ms) {
		const char *e = getenv("LUMNA_TESTE_JANELA_MS");
		ms = e && atoi(e) > 0 ? atoi(e) : WINANIM_MS_PADRAO;
	}
	return (float)ms;
}

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static float ease_out(float t) {
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

/* ---- quem anima ---- */

static bool wanted(struct orbit_view *view) {
	struct orbit_server *server = view->server;
	struct orbit_output *output = view->output;
	return server->anim_windows && view->mapped && !view->game && !view->fullscreen && !view->minimized && output && !output->off &&
		view->tag == output->tag && !orbit_output_isolated(output) && !output_is_phone(output->wlr) && view->scene_tree &&
		view->scene_tree->node.enabled;
}

/* o centro do que se vê (a caixa das imagens): é em volta dele que escala; a origem da árvore nem sempre é o canto da geometria */
static void center_of(const struct winanim_item *item, int n, float *cx, float *cy) {
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (int i = 0; i < n; i++) {
		x0 = item[i].abs_x < x0 ? (float)item[i].abs_x : x0;
		y0 = item[i].abs_y < y0 ? (float)item[i].abs_y : y0;
		x1 = item[i].abs_x + item[i].w > x1 ? (float)(item[i].abs_x + item[i].w) : x1;
		y1 = item[i].abs_y + item[i].h > y1 ? (float)(item[i].abs_y + item[i].h) : y1;
	}
	*cx = (x0 + x1) / 2.0f;
	*cy = (y0 + y1) / 2.0f;
}

/* ---- escalar as imagens de uma janela ---- */

struct collect {
	struct orbit_winanim *wa;
	bool ghost;
};

static void collect_cb(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
	struct orbit_winanim *wa = data;
	if (wa->n >= WINANIM_MAX || !buffer->buffer || buffer->dst_width <= 0 || buffer->dst_height <= 0) {
		return;
	}
	struct winanim_item *it = &wa->item[wa->n++];
	it->buffer = buffer;
	it->rel_x = buffer->node.x;
	it->rel_y = buffer->node.y;
	it->abs_x = sx;
	it->abs_y = sy;
	it->w = buffer->dst_width;
	it->h = buffer->dst_height;
}

static struct winanim_item *find(struct orbit_winanim *wa, struct wlr_scene_buffer *buffer) {
	for (int i = 0; i < wa->n; i++) {
		if (wa->item[i].buffer == buffer) {
			return &wa->item[i];
		}
	}
	return NULL;
}

static void place(struct winanim_item *it, float cx, float cy, float scale, float alpha) {
	float ax = (it->abs_x - cx) * scale + cx, ay = (it->abs_y - cy) * scale + cy;
	int px = it->abs_x - it->rel_x, py = it->abs_y - it->rel_y; /* o pai não se mexe */
	wlr_scene_node_set_position(&it->buffer->node, (int)(ax + (ax < 0 ? -0.5f : 0.5f)) - px, (int)(ay + (ay < 0 ? -0.5f : 0.5f)) - py);
	wlr_scene_buffer_set_dest_size(it->buffer, (int)(it->w * scale + 0.5f), (int)(it->h * scale + 0.5f));
	wlr_scene_buffer_set_opacity(it->buffer, alpha);
}

struct apply {
	struct orbit_winanim *wa;
	float scale, alpha;
};

static void apply_cb(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
	struct apply *ap = data;
	struct winanim_item *it = find(ap->wa, buffer);
	if (!it) {
		return;
	}
	/* o app pode ter mudado o tamanho da superfície enquanto anima: vale o de agora */
	struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
	if (ss && ss->surface->current.width > 0 && ss->surface->current.height > 0) {
		it->w = ss->surface->current.width;
		it->h = ss->surface->current.height;
	}
	place(it, ap->wa->cx, ap->wa->cy, ap->scale, ap->alpha);
}

static void apply(struct orbit_view *view, float scale, float alpha) {
	struct apply ap = { view->wanim, scale, alpha };
	wlr_scene_node_for_each_buffer(&view->scene_tree->node, apply_cb, &ap);
}

/* ---- abrir ---- */

void orbit_win_anim_forget(struct orbit_view *view) {
	view->wanim_pending = 0;
	free(view->wanim);
	view->wanim = NULL;
}

static int win_tick(void *data);

static void ensure_timer(struct orbit_server *server) {
	if (!server->win_timer && server->display) {
		server->win_timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->display), win_tick, server);
	}
	if (server->win_timer) {
		wl_event_source_timer_update(server->win_timer, 16);
	}
}

/* tenta começar: false se as imagens da janela ainda não estão na cena (o wlroots as põe no commit, depois do map) */
static bool start(struct orbit_view *view) {
	struct orbit_winanim *wa = calloc(1, sizeof(*wa));
	if (!wa) {
		return true;
	}
	wa->start = now_ms();
	wlr_scene_node_for_each_buffer(&view->scene_tree->node, collect_cb, wa);
	if (!wa->n) {
		free(wa);
		return false;
	}
	center_of(wa->item, wa->n, &wa->cx, &wa->cy);
	view->wanim = wa;
	view->server->wanim_opens++;
	apply(view, 1.0f + WINANIM_GROW, 0.0f); /* o primeiro quadro já nasce maior e transparente */
	ensure_timer(view->server);
	return true;
}

void orbit_win_anim_open(struct orbit_view *view) {
	if (!wanted(view) || view->wanim) {
		return;
	}
	if (!start(view)) {
		view->wanim_pending = now_ms(); /* as imagens chegam no commit (ou, numa janela X11, no próximo relógio) */
		ensure_timer(view->server);
	}
}

static void apply_now(struct orbit_view *view) {
	struct orbit_winanim *wa = view->wanim;
	float t = (float)(now_ms() - wa->start) / duration_ms();
	float e = ease_out(t);
	apply(view, 1.0f + WINANIM_GROW * (1.0f - e), e);
}

/* o commit da janela: as imagens já estão na cena. O wlroots refaz o tamanho da imagem a cada commit do app; sem reaplicar aqui, um app que
 * redesenha a cada quadro desfazia a escala entre dois relógios e a janela tremia */
void orbit_win_anim_commit(struct orbit_view *view) {
	if (view->wanim_pending) {
		view->wanim_pending = 0;
		if (wanted(view) && !view->wanim) {
			start(view);
		}
	} else if (view->wanim) {
		apply_now(view);
	}
}

/* ---- fechar ---- */

static void ghost_free(struct orbit_server *server, int i) {
	struct orbit_ghost *g = server->ghosts[i];
	wlr_scene_node_destroy(&g->tree->node);
	free(g);
	server->ghosts[i] = server->ghosts[--server->nghosts];
	server->ghosts[server->nghosts] = NULL;
}

static void ghost_collect_cb(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
	struct orbit_ghost *g = data;
	if (g->n >= WINANIM_MAX || !buffer->buffer || buffer->dst_width <= 0 || buffer->dst_height <= 0) {
		return;
	}
	/* uma cópia da última imagem (o wlroots a segura enquanto o fantasma existir) */
	struct wlr_scene_buffer *copy = wlr_scene_buffer_create(g->tree, buffer->buffer);
	if (!copy) {
		return;
	}
	wlr_scene_buffer_set_source_box(copy, &buffer->src_box);
	wlr_scene_buffer_set_transform(copy, buffer->transform);
	wlr_scene_buffer_set_dest_size(copy, buffer->dst_width, buffer->dst_height);
	wlr_scene_node_set_position(&copy->node, sx, sy);
	struct winanim_item *it = &g->item[g->n++];
	it->buffer = copy;
	it->rel_x = it->abs_x = sx;
	it->rel_y = it->abs_y = sy;
	it->w = buffer->dst_width;
	it->h = buffer->dst_height;
}

/* como wlr_scene_node_for_each_buffer, mas sem pular os nós desligados: no unmap o wlroots já desligou a árvore da superfície, e a última imagem
 * continua lá dentro, com o buffer */
static void walk_buffers(struct wlr_scene_node *node, int lx, int ly, wlr_scene_buffer_iterator_func_t cb, void *data) {
	lx += node->x;
	ly += node->y;
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		cb(wlr_scene_buffer_from_node(node), lx, ly, data);
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_node *child;
		wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link) {
			walk_buffers(child, lx, ly, cb, data);
		}
	}
}

/* chamada no unmap, com a imagem ainda na janela */
void orbit_win_anim_close(struct orbit_view *view) {
	struct orbit_server *server = view->server;
	if (!wanted(view) || view->sliding || !view->scene_tree->node.parent) {
		return;
	}
	if (server->nghosts >= GHOST_MAX) {
		ghost_free(server, 0);
	}
	struct orbit_ghost *g = calloc(1, sizeof(*g));
	if (!g) {
		return;
	}
	struct wlr_scene_tree *parent = view->scene_tree->node.parent; /* a mesma camada da janela: o fantasma aparece onde ela estava */
	g->tree = wlr_scene_tree_create(parent);
	if (!g->tree) {
		free(g);
		return;
	}
	int lx = 0, ly = 0;
	wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);
	int px = 0, py = 0;
	wlr_scene_node_coords(&parent->node, &px, &py);
	wlr_scene_node_set_position(&g->tree->node, lx - px, ly - py);
	g->start = now_ms();
	walk_buffers(&view->scene_tree->node, -view->scene_tree->node.x, -view->scene_tree->node.y, ghost_collect_cb, g); /* a origem é a da árvore da janela (o fantasma tem a mesma) */
	center_of(g->item, g->n, &g->cx, &g->cy);
	if (!g->n) {
		wlr_scene_node_destroy(&g->tree->node);
		free(g);
		return;
	}
	wlr_scene_node_raise_to_top(&g->tree->node);
	server->ghosts[server->nghosts++] = g;
	server->wanim_closes++;
	ensure_timer(server);
}

/* ---- o relógio ---- */

static int win_tick(void *data) {
	struct orbit_server *server = data;
	uint32_t now = now_ms();
	bool more = false;
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->wanim_pending) {
			if (now - view->wanim_pending > 150 || (wanted(view) && !view->wanim && start(view))) {
				view->wanim_pending = 0;
			} else {
				more = true;
			}
		}
		struct orbit_winanim *wa = view->wanim;
		if (!wa) {
			continue;
		}
		float t = (float)(now - wa->start) / duration_ms();
		if (t >= 1.0f || !view->scene_tree) {
			if (view->scene_tree) {
				apply(view, 1.0f, 1.0f); /* de volta ao exato */
			}
			orbit_win_anim_forget(view);
			continue;
		}
		float e = ease_out(t);
		apply(view, 1.0f + WINANIM_GROW * (1.0f - e), e);
		more = true;
	}
	for (int i = server->nghosts - 1; i >= 0; i--) {
		struct orbit_ghost *g = server->ghosts[i];
		float t = (float)(now - g->start) / duration_ms();
		if (t >= 1.0f) {
			ghost_free(server, i);
			continue;
		}
		float e = ease_out(t);
		for (int k = 0; k < g->n; k++) {
			place(&g->item[k], g->cx, g->cy, 1.0f + WINANIM_GROW * e, 1.0f - e);
		}
		more = true;
	}
	if (more && server->win_timer) {
		wl_event_source_timer_update(server->win_timer, 16);
	}
	return 0;
}
