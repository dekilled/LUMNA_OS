/*
 * A barra de título que o Orbit desenha (decoração do lado do servidor, xdg-decoration).
 *
 * Quem fala o protocolo (o foot, o Blender pelo libdecor, o Firefox com a barra do sistema...) pergunta quem desenha a barra: o Orbit
 * responde "eu" e desenha, a não ser que o cliente peça para desenhar a dele. Quem não fala o protocolo (os apps do LUMNA em Gravity e os
 * GTK, que têm a própria barra) fica como estava: o Orbit não desenha nada neles.
 *
 * O visual é o da barra dos apps do LUMNA (gv_barra_janela): 54 px, o título à esquerda, minimizar, maximizar e fechar à direita, e o
 * fundo da superfície, com uma linha embaixo. A barra é uma imagem (cairo + pango) filha da árvore da janela, acima da superfície:
 * anda junto com ela (arrastar, trocar de espaço, animações) sem conta nenhuma. A convenção das posições não muda: view->y é o topo da
 * ÁREA DO CLIENTE, e a barra fica por cima dela (view->y - altura).
 */
#define _DEFAULT_SOURCE 1 /* M_PI */
#include "orbit.h"

#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

/* as medidas dos botões, como no gv_barra_janela (px lógicos) */
#define PAD_ESQ 20
#define PAD_DIR 10
#define BOTAO_L 44
#define BOTAO_A 36
#define BOTAO_FOLGA 14
#define DUPLO_CLIQUE_MS 400

/* ---- a imagem da barra: um wlr_buffer em cima de uma superfície do cairo ---- */

struct bar_buffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
};

static void bar_buffer_destroy(struct wlr_buffer *buffer) {
	struct bar_buffer *bb = wl_container_of(buffer, bb, base);
	cairo_surface_destroy(bb->surface);
	free(bb);
}

static bool bar_buffer_begin(struct wlr_buffer *buffer, uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct bar_buffer *bb = wl_container_of(buffer, bb, base);
	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) {
		return false;
	}
	cairo_surface_flush(bb->surface);
	*data = cairo_image_surface_get_data(bb->surface);
	*format = DRM_FORMAT_ARGB8888; /* o ARGB32 do cairo, com o alfa já multiplicado */
	*stride = (size_t)cairo_image_surface_get_stride(bb->surface);
	return true;
}

static void bar_buffer_end(struct wlr_buffer *buffer) {
	(void)buffer;
}

static const struct wlr_buffer_impl bar_buffer_impl = {
	.destroy = bar_buffer_destroy,
	.begin_data_ptr_access = bar_buffer_begin,
	.end_data_ptr_access = bar_buffer_end,
};

/* ---- o tamanho do cliente (o campo mudou de lugar no wlroots 0.19) ---- */

static int client_width(struct orbit_view *view) {
	return orbit_view_client_box(view).width;
}

/* ---- as medidas e as zonas ---- */

int orbit_deco_top(struct orbit_view *view) {
	if (!view->ssd || view->fullscreen || view->server->bar_h <= 0) {
		return 0;
	}
	if (view->output && output_is_phone(view->output->wlr)) {
		return 0; /* no telemóvel as janelas ocupam a tela: sem barra */
	}
	return view->server->bar_h;
}

static void botao_caixa(int largura, enum orbit_deco_zone botao, int *x) {
	int fecha = largura - PAD_DIR - BOTAO_L;
	int maxim = fecha - BOTAO_FOLGA - BOTAO_L;
	int minim = maxim - BOTAO_FOLGA - BOTAO_L;
	*x = botao == ORBIT_DECO_CLOSE ? fecha : botao == ORBIT_DECO_MAX ? maxim : minim;
}

static enum orbit_deco_zone zona_em(struct orbit_view *view, int x, int y, int altura) {
	int w = view->bar_w;
	int y0 = (altura - BOTAO_A) / 2;
	if (y >= y0 && y < y0 + BOTAO_A) {
		for (int b = ORBIT_DECO_MIN; b <= ORBIT_DECO_CLOSE; b++) {
			int bx;
			botao_caixa(w, (enum orbit_deco_zone)b, &bx);
			if (x >= bx && x < bx + BOTAO_L) {
				return (enum orbit_deco_zone)b;
			}
		}
	}
	return ORBIT_DECO_BAR;
}

/* ---- o desenho ---- */

static void caminho_topo_redondo(cairo_t *cr, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, r, r, r, M_PI, 1.5 * M_PI);
	cairo_arc(cr, w - r, r, r, 1.5 * M_PI, 2 * M_PI);
	cairo_line_to(cr, w, h);
	cairo_line_to(cr, 0, h);
	cairo_close_path(cr);
}

static void caminho_caixa_redonda(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -0.5 * M_PI, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * M_PI);
	cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_close_path(cr);
}

/* o ícone de um botão, no quadrado de 18 px do centro dele (os desenhos são os do Gravity, numa grade de 24) */
static void desenha_icone(cairo_t *cr, enum orbit_deco_zone botao, double bx, double by, bool maximizada) {
	cairo_save(cr);
	cairo_translate(cr, bx + (BOTAO_L - 18) / 2.0, by + (BOTAO_A - 18) / 2.0);
	cairo_scale(cr, 18.0 / 24.0, 18.0 / 24.0);
	cairo_set_line_width(cr, 1.6);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	switch (botao) {
	case ORBIT_DECO_MIN:
		cairo_move_to(cr, 6, 12);
		cairo_line_to(cr, 18, 12);
		break;
	case ORBIT_DECO_MAX:
		if (maximizada) { /* dois quadrados: o de trás só aparece o canto */
			cairo_rectangle(cr, 5.5, 9.5, 9, 9);
			cairo_move_to(cr, 9.5, 9.5);
			cairo_line_to(cr, 9.5, 5.5);
			cairo_line_to(cr, 18.5, 5.5);
			cairo_line_to(cr, 18.5, 14.5);
			cairo_line_to(cr, 14.5, 14.5);
		} else {
			cairo_rectangle(cr, 6, 6, 12, 12);
		}
		break;
	default:
		cairo_move_to(cr, 6, 6);
		cairo_line_to(cr, 18, 18);
		cairo_move_to(cr, 18, 6);
		cairo_line_to(cr, 6, 18);
		break;
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

static struct bar_buffer *desenha_barra(int w, int h, float scale, bool foco, bool maximizada, int sob_cursor,
		const char *titulo) {
	int pw = (int)ceilf(w * scale), ph = (int)ceilf(h * scale);
	struct bar_buffer *bb = calloc(1, sizeof(*bb));
	if (!bb) {
		return NULL;
	}
	bb->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
	if (cairo_surface_status(bb->surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(bb->surface);
		free(bb);
		return NULL;
	}
	wlr_buffer_init(&bb->base, &bar_buffer_impl, pw, ph);
	cairo_t *cr = cairo_create(bb->surface);
	cairo_scale(cr, (double)pw / w, (double)ph / h);

	/* o fundo (a superfície dos apps do LUMNA) com os cantos de cima redondos, e a linha de baixo */
	caminho_topo_redondo(cr, w, h, 12);
	cairo_set_source_rgb(cr, 0x1d / 255.0, 0x1e / 255.0, 0x22 / 255.0);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0x30 / 255.0, 0x32 / 255.0, 0x38 / 255.0);
	cairo_rectangle(cr, 0, h - 1, w, 1);
	cairo_fill(cr);

	/* os botões: o fundo do que está sob o cursor, e o ícone */
	for (int b = ORBIT_DECO_MIN; b <= ORBIT_DECO_CLOSE; b++) {
		int bx;
		botao_caixa(w, (enum orbit_deco_zone)b, &bx);
		double by = (h - BOTAO_A) / 2.0;
		if (sob_cursor == b) {
			if (b == ORBIT_DECO_CLOSE) {
				cairo_set_source_rgba(cr, 0xe5 / 255.0, 0x8b / 255.0, 0x98 / 255.0, 0.25);
			} else {
				cairo_set_source_rgb(cr, 0x2d / 255.0, 0x2e / 255.0, 0x33 / 255.0);
			}
			caminho_caixa_redonda(cr, bx, by, BOTAO_L, BOTAO_A, 6);
			cairo_fill(cr);
		}
		cairo_set_source_rgb(cr, 0xbc / 255.0, 0xb5 / 255.0, 0xce / 255.0);
		desenha_icone(cr, (enum orbit_deco_zone)b, bx, by, maximizada);
	}

	/* o título: à esquerda, cortado com reticências antes dos botões; mais apagado quando a janela não tem o foco */
	int fim_texto;
	botao_caixa(w, ORBIT_DECO_MIN, &fim_texto);
	int disponivel = fim_texto - BOTAO_FOLGA - PAD_ESQ;
	if (disponivel > 20) {
		PangoLayout *layout = pango_cairo_create_layout(cr);
		PangoFontDescription *fonte = pango_font_description_from_string("Inter, Noto Sans, sans-serif");
		pango_font_description_set_absolute_size(fonte, 17 * PANGO_SCALE);
		pango_layout_set_font_description(layout, fonte);
		pango_font_description_free(fonte);
		pango_layout_set_text(layout, titulo, -1);
		pango_layout_set_width(layout, disponivel * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		pango_layout_set_single_paragraph_mode(layout, TRUE);
		int tw, th;
		pango_layout_get_pixel_size(layout, &tw, &th);
		cairo_set_source_rgba(cr, 0xf4 / 255.0, 0xf1 / 255.0, 0xfa / 255.0, foco ? 1.0 : 0.6);
		cairo_move_to(cr, PAD_ESQ, (h - th) / 2.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}
	cairo_destroy(cr);
	return bb;
}

void orbit_deco_update(struct orbit_view *view) {
	int top = orbit_deco_top(view);
	if (top == 0) {
		if (view->bar) {
			wlr_scene_node_set_enabled(&view->bar->node, false);
		}
		return;
	}
	int w = client_width(view);
	if (w < 1) {
		w = view->w;
	}
	if (w < 2 * (PAD_ESQ + PAD_DIR)) {
		return; /* ainda sem tamanho (a janela não deu o primeiro commit) */
	}
	struct orbit_server *server = view->server;
	const char *titulo = *orbit_view_title(view) ? orbit_view_title(view) : orbit_view_app_id(view);
	float scale = view->output && view->output->wlr->scale > 0 ? view->output->wlr->scale : 1.0f;
	bool foco = server->focused == view, maxim = view->maximized;
	if (view->bar && view->bar->node.enabled && view->bar_w == w && view->bar_scale == scale && view->bar_focus == foco && view->bar_max == maxim &&
		!strcmp(view->bar_title, titulo)) {
		return; /* nada mudou desde o último desenho */
	}
	if (!view->bar) {
		view->bar = wlr_scene_buffer_create(view->scene_tree, NULL);
		if (!view->bar) {
			return;
		}
		view->bar->node.data = view; /* o orbit_deco_at reconhece a barra pelo nó (view->bar) */
	}
	struct bar_buffer *bb = desenha_barra(w, top, scale, foco, maxim, view->bar_hover, titulo);
	if (!bb) {
		return;
	}
	wlr_scene_buffer_set_buffer(view->bar, &bb->base);
	wlr_buffer_drop(&bb->base);
	wlr_scene_buffer_set_dest_size(view->bar, w, top);
	wlr_scene_node_set_position(&view->bar->node, 0, -top);
	wlr_scene_node_set_enabled(&view->bar->node, true);
	view->bar_w = w;
	view->bar_scale = scale;
	view->bar_focus = foco;
	view->bar_max = maxim;
	snprintf(view->bar_title, sizeof(view->bar_title), "%s", titulo);
}

/* o botão sob o cursor mudou: só a barra é redesenhada */
static void redesenha_hover(struct orbit_view *view, enum orbit_deco_zone zona) {
	int hover = zona >= ORBIT_DECO_MIN ? zona : 0;
	if (view->bar_hover == hover) {
		return;
	}
	view->bar_hover = hover;
	view->bar_w = 0; /* força o desenho */
	orbit_deco_update(view);
}

static struct orbit_view *hovered;

void orbit_deco_hover(struct orbit_server *server, struct orbit_view *view, enum orbit_deco_zone zone) {
	(void)server;
	if (hovered && hovered != view) {
		redesenha_hover(hovered, ORBIT_DECO_NONE);
		hovered = NULL;
	}
	if (view) {
		hovered = view;
		redesenha_hover(view, zone);
	}
}

void orbit_deco_destroy(struct orbit_view *view) {
	if (hovered == view) {
		hovered = NULL;
	}
	if (view->server->bar_click_view == view) {
		view->server->bar_click_view = NULL;
	}
	if (view->server->bar_pending == view) {
		view->server->bar_pending = NULL;
	}
	if (view->xdg_deco) {
		wl_list_remove(&view->deco_request_mode.link);
		wl_list_remove(&view->deco_destroy.link);
		view->xdg_deco = NULL;
	}
	/* a barra (um nó da árvore da janela) some com a árvore */
	view->bar = NULL;
}

/* ---- quem está sob o cursor ---- */

struct orbit_view *orbit_deco_at(struct orbit_server *server, double lx, double ly, enum orbit_deco_zone *zone) {
	double nx, ny;
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, &nx, &ny);
	*zone = ORBIT_DECO_NONE;
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	/* o wlroots 0.19+ larga o buffer da cena assim que sobe a textura (sb->buffer fica NULL): a barra se reconhece pelo nó, nunca pelo buffer */
	struct orbit_view *view = NULL, *candidate;
	wl_list_for_each(candidate, &server->views, link) {
		if (candidate->bar && &candidate->bar->node == node) {
			view = candidate;
			break;
		}
	}
	if (!view || !view->mapped) {
		return NULL;
	}
	*zone = zona_em(view, (int)nx, (int)ny, orbit_deco_top(view));
	return view;
}

/* ---- o aperto na barra ---- */

void orbit_deco_press(struct orbit_server *server, struct orbit_view *view, enum orbit_deco_zone zone, uint32_t time) {
	orbit_focus(view);
	switch (zone) {
	case ORBIT_DECO_CLOSE:
		orbit_view_close(view);
		return;
	case ORBIT_DECO_MIN:
		orbit_view_set_minimized(view, true);
		return;
	case ORBIT_DECO_MAX:
		orbit_view_set_maximized(view, !view->maximized);
		return;
	default:
		break;
	}
	/* o corpo da barra: duplo clique maximiza e volta; um aperto só começa a arrastar */
	if (server->bar_click_view == view && time - server->bar_click_time < DUPLO_CLIQUE_MS) {
		server->bar_click_view = NULL;
		orbit_view_set_maximized(view, !view->maximized);
		return;
	}
	server->bar_click_view = view;
	server->bar_click_time = time;
	if (view->maximized || view->snap) {
		/* arrastar uma maximizada a solta, mas um clique (ou o primeiro de um duplo) não pode: espera o mouse andar */
		server->bar_pending = view;
		server->bar_pending_x = server->cursor->x;
		server->bar_pending_y = server->cursor->y;
		return;
	}
	orbit_move_begin(server, view);
}

/* o mouse andou com a barra apertada: agora sim começa a arrastar */
void orbit_deco_pending_motion(struct orbit_server *server) {
	struct orbit_view *view = server->bar_pending;
	if (!view) {
		return;
	}
	double dx = server->cursor->x - server->bar_pending_x, dy = server->cursor->y - server->bar_pending_y;
	if (dx * dx + dy * dy < 8 * 8) {
		return;
	}
	server->bar_pending = NULL;
	if (view->mapped) {
		server->bar_click_view = NULL; /* arrastou: não é um clique */
		orbit_move_begin(server, view);
	}
}

/* ---- o protocolo ---- */

/* manda o modo ao cliente. Só depois de a superfície ter dado o primeiro commit (o wlroots não aceita configure antes): até lá o
 * modo fica anotado e o xdg_commit (view.c) chama isto de novo quando o primeiro commit chega */
void orbit_deco_send_mode(struct orbit_view *view) {
	if (!view->xdg_deco || !view->toplevel->base->initialized) {
		return;
	}
	wlr_xdg_toplevel_decoration_v1_set_mode(view->xdg_deco,
		view->ssd ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
}

static void aplica_modo(struct orbit_view *view, enum wlr_xdg_toplevel_decoration_v1_mode pedido) {
	bool servidor = view->server->bar_h > 0 && pedido != WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	bool antes = view->ssd;
	view->ssd = servidor;
	orbit_deco_send_mode(view);
	if (antes != servidor && view->mapped) {
		view->cfg_valid = false; /* a área do cliente mudou de tamanho */
		orbit_arrange(view->server);
		orbit_deco_update(view);
	}
}

static void deco_request_mode(struct wl_listener *listener, void *data) {
	(void)data;
	struct orbit_view *view = wl_container_of(listener, view, deco_request_mode);
	aplica_modo(view, view->xdg_deco->requested_mode);
}

static void deco_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct orbit_view *view = wl_container_of(listener, view, deco_destroy);
	wl_list_remove(&view->deco_request_mode.link);
	wl_list_remove(&view->deco_destroy.link);
	view->xdg_deco = NULL;
	bool antes = view->ssd;
	view->ssd = false; /* sem o protocolo, o cliente cuida da barra dele */
	if (antes && view->mapped) {
		view->cfg_valid = false;
		orbit_deco_update(view);
		orbit_arrange(view->server);
	}
}

static void server_new_deco(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, new_deco);
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	struct wlr_scene_tree *tree = deco->toplevel->base->data;
	struct orbit_view *view = tree ? tree->node.data : NULL;
	if (!view || view->kind != ORBIT_NODE_VIEW || view->server != server) {
		return;
	}
	view->xdg_deco = deco;
	view->deco_request_mode.notify = deco_request_mode;
	wl_signal_add(&deco->events.request_mode, &view->deco_request_mode);
	view->deco_destroy.notify = deco_destroy;
	wl_signal_add(&deco->events.destroy, &view->deco_destroy);
	aplica_modo(view, deco->requested_mode); /* sem pedido ainda (NONE): o Orbit desenha */
}

void orbit_deco_bind(struct orbit_server *server) {
	server->deco_mgr = wlr_xdg_decoration_manager_v1_create(server->display);
	if (!server->deco_mgr) {
		wlr_log(WLR_ERROR, "xdg-decoration falhou: as janelas ficam sem barra do Orbit");
		return;
	}
	server->new_deco.notify = server_new_deco;
	wl_signal_add(&server->deco_mgr->events.new_toplevel_decoration, &server->new_deco);
}
