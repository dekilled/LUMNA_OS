#include "orbit.h"

#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/version.h>

/*
 * As janelas livres do desktop (arranjo = livre, o de fábrica): abrem no meio da área útil, com
 * o tamanho que pedem, e movem-se pela barra de título (xdg move), com Alt+arrastar, e mudam de
 * tamanho pelas bordas (xdg resize) ou com Alt+botão direito. O encaixe só vem de um gatilho:
 * largar a janela na borda esquerda/direita da tela (metade) ou em cima (maximizar), ou
 * Super+setas. Uma janela encaixada ou maximizada volta ao tamanho livre de antes ao arrastá-la.
 * O telemóvel continua em mosaico.
 */

#define ORBIT_MIN_W 240
#define ORBIT_MIN_H 160
#define ORBIT_SNAP_ZONE 56 /* px, nas laterais de cada monitor: a zona de encaixe. Só vale se SOLTAR a janela com o cursor nela; passar por ela atravessa */
#define ORBIT_SNAP_WINDOW 12 /* px: a janela arrastada "chegou" à lateral quando a borda dela fica a menos disto da do monitor (ou passa dela) */
#define ORBIT_SNAP_TOP 4   /* em cima (maximizar): fina, porque quem arrasta pela barra de título anda sempre perto do topo */

/* o tamanho da janela que o cliente diz (no 0.19 passou a um campo) */
static struct wlr_box client_geometry(struct orbit_view *view) {
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR < 19
	struct wlr_box box;
	wlr_xdg_surface_get_geometry(view->toplevel->base, &box);
	return box;
#else
	return view->toplevel->base->geometry;
#endif
}

static struct wlr_box geometry(struct orbit_view *view) {
	struct wlr_box box = client_geometry(view);
	if (box.width < 1 || box.height < 1) {
		box.width = view->fw > 0 ? view->fw : 640;
		box.height = view->fh > 0 ? view->fh : 480;
	}
	return box;
}

/*
 * A janela muda de tela. A janela livre leva a posição junto: o mesmo lugar relativo na tela nova, e dentro da área útil dela
 * (sem isto ela continuava desenhada onde estava, na tela de antes, e atrás do preto de uma tela apagada). As janelas do mosaico,
 * maximizadas ou encaixadas são arrumadas pela tela nova. `carry` é falso quando a janela já está no lugar certo (largada na outra
 * tela com o mouse). Quem chama cuida do espaço (tag) e da tela cheia.
 */
void orbit_view_set_output(struct orbit_view *view, struct orbit_output *dest, bool carry) {
	struct orbit_server *server = view->server;
	struct orbit_output *old = view->output;
	view->output = dest;
	if (!dest || !old || old == dest || !view->floating || !carry) {
		return;
	}
	struct wlr_box ob = { 0 }, nb = { 0 };
	wlr_output_layout_get_box(server->layout, old->wlr, &ob);
	wlr_output_layout_get_box(server->layout, dest->wlr, &nb);
	if (nb.width <= 0 || nb.height <= 0) {
		return;
	}
	if (ob.width <= 0 || ob.height <= 0) {
		orbit_float_place_new(view); /* a tela de antes já não existe: abre no meio da nova */
		return;
	}
	int dx = nb.x - ob.x, dy = nb.y - ob.y;
	view->x += dx;
	view->y += dy;
	view->fx += dx;
	view->fy += dy;
	struct wlr_box usable = orbit_layer_usable(dest, &nb);
	struct wlr_box g = geometry(view);
	int w = g.width > 0 ? g.width : view->fw, h = g.height > 0 ? g.height : view->fh;
	int maxx = usable.x + usable.width - w, maxy = usable.y + usable.height - h;
	view->x = view->x > maxx ? maxx : view->x;
	view->y = view->y > maxy ? maxy : view->y;
	view->x = view->x < usable.x ? usable.x : view->x;
	view->y = view->y < usable.y ? usable.y : view->y;
	view->fx = view->x;
	view->fy = view->y;
	view->placed = true;
	view->sliding = false;
	view->slide_x = view->x;
	view->slide_y = view->y;
}

bool orbit_free_desktop(struct orbit_server *server, struct orbit_output *output) {
	return server->arrangement == ORBIT_LAYOUT_FREE && output && !output_is_phone(output->wlr);
}

/* guarda onde e de que tamanho a janela estava livre (antes de encaixar ou maximizar) */
void orbit_float_save(struct orbit_view *view) {
	if (!view->floating || view->snap || view->maximized || view->fullscreen) {
		return;
	}
	struct wlr_box g = geometry(view);
	view->fx = view->x;
	view->fy = view->y;
	view->fw = g.width;
	view->fh = g.height;
}

/* ao abrir: no meio da área útil (umas por cima das outras, um pouco de lado), do tamanho que pediu */
void orbit_float_place_new(struct orbit_view *view) {
	struct orbit_server *server = view->server;
	struct orbit_output *output = view->output;
	struct wlr_box full = { 0, 0, 1280, 720 };
	if (output) {
		wlr_output_layout_get_box(server->layout, output->wlr, &full);
	}
	struct wlr_box usable = output ? orbit_layer_usable(output, &full) : full;
	struct wlr_box g = client_geometry(view);
	int top = orbit_deco_top(view); /* a barra de título fica por cima da janela e também tem de caber */
	int w = g.width > 0 ? g.width : usable.width * 3 / 5;
	int h = g.height > 0 ? g.height : (usable.height - top) * 7 / 10;
	bool resize = g.width < 1 || g.height < 1;
	if (w > usable.width - 2 * server->gap) {
		w = usable.width - 2 * server->gap;
		resize = true;
	}
	if (h > usable.height - 2 * server->gap - top) {
		h = usable.height - 2 * server->gap - top;
		resize = true;
	}
	int others = 0;
	struct orbit_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (v != view && v->mapped && v->output == output && v->tag == view->tag && v->floating && !v->minimized) {
			others++;
		}
	}
	int step = (others % 6) * 32;
	view->x = usable.x + (usable.width - w) / 2 + step;
	view->y = usable.y + top + (usable.height - top - h) / 2 + step;
	if (view->x + w > usable.x + usable.width) {
		view->x = usable.x + server->gap;
	}
	if (view->y + h > usable.y + usable.height) {
		view->y = usable.y + server->gap + top;
	}
	view->fx = view->x;
	view->fy = view->y;
	view->fw = w;
	view->fh = h;
	view->placed = true;
	view->sliding = false;
	view->slide_x = view->x;
	view->slide_y = view->y;
	if (resize) {
		wlr_xdg_toplevel_set_size(view->toplevel, (uint32_t)w, (uint32_t)h);
	}
}

/* mostra uma janela livre onde ela está; só manda configure quando sai de maximizada/encaixada */
void orbit_float_show(struct orbit_view *view) {
	view->sliding = false;
	view->placed = true;
	view->slide_x = view->x;
	view->slide_y = view->y;
	wlr_scene_node_set_enabled(&view->scene_tree->node, true);
	if (view->border) {
		wlr_scene_node_set_enabled(&view->border->node, false);
	}
	if (view->scene_tree->node.parent != view->server->tile_tree) {
		wlr_scene_node_reparent(&view->scene_tree->node, view->server->tile_tree);
	}
	wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
	if (view->fw > 0 && view->fh > 0) {
		view->w = view->fw;
		view->h = view->fh;
	}
	if (!view->cfg_valid || view->cfg_full || view->cfg_max || view->cfg_tiled) {
		wlr_xdg_toplevel_set_fullscreen(view->toplevel, false);
		wlr_xdg_toplevel_set_maximized(view->toplevel, false);
		wlr_xdg_toplevel_set_tiled(view->toplevel, WLR_EDGE_NONE);
		if (view->cfg_valid && view->fw > 0 && view->fh > 0) {
			/* voltou de maximizada ou encaixada: o tamanho livre de antes */
			wlr_xdg_toplevel_set_size(view->toplevel, (uint32_t)view->fw, (uint32_t)view->fh);
		}
		view->cfg_valid = true;
		view->cfg_full = false;
		view->cfg_max = false;
		view->cfg_tiled = WLR_EDGE_NONE;
		view->cfg_w = view->fw;
		view->cfg_h = view->fh;
	}
	orbit_deco_update(view);
}

/* a metade da área útil de um encaixe (1 esquerda, 2 direita) */
struct wlr_box orbit_snap_box(struct orbit_view *view, int snap) {
	struct orbit_server *server = view->server;
	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->layout, view->output->wlr, &full);
	struct wlr_box u = orbit_layer_usable(view->output, &full);
	int gap = server->gap;
	int half = (u.width - 3 * gap) / 2;
	struct wlr_box box = { snap == 1 ? u.x + gap : u.x + 2 * gap + half, u.y + gap, half, u.height - 2 * gap };
	return box;
}

void orbit_view_snap(struct orbit_view *view, int snap) {
	if (!view->mapped || !view->floating || view->fullscreen) {
		return;
	}
	if (snap) {
		orbit_float_save(view);
	}
	if (view->maximized) {
		view->maximized = false;
	}
	view->snap = snap;
	if (!snap) {
		view->x = view->fx;
		view->y = view->fy;
	}
	orbit_arrange(view->server);
	orbit_focus(view);
}

/* Super+setas: ←/→ encaixa na metade (de novo no mesmo lado solta), ↑ maximiza, ↓ desfaz um
 * encaixe ou a maximização (e numa janela livre, minimiza) */
void orbit_snap_key(struct orbit_server *server, int dir) {
	struct orbit_view *view = server->focused;
	if (!view || !view->mapped || !view->floating) {
		return;
	}
	if (dir == 1 || dir == 2) {
		orbit_view_snap(view, view->snap == dir ? 0 : dir);
	} else if (dir == 3) {
		orbit_view_set_maximized(view, true); /* guarda o tamanho livre (o de antes do encaixe, se encaixada) */
	} else if (view->maximized) {
		orbit_view_set_maximized(view, false);
	} else if (view->snap) {
		orbit_view_snap(view, 0);
	} else {
		orbit_view_set_minimized(view, true);
	}
}

/* ---- arrastar ---- */

/* um retângulo vazado (contorno de largura w) feito de 4 retângulos, a partir do índice i de snap_rects */
static void zone_outline(struct orbit_server *server, int i, const struct wlr_box *b, int w, const float color[4]) {
	struct wlr_box part[4] = { { b->x, b->y, b->width, w }, { b->x, b->y + b->height - w, b->width, w },
		{ b->x, b->y + w, w, b->height - 2 * w }, { b->x + b->width - w, b->y + w, w, b->height - 2 * w } };
	for (int k = 0; k < 4; k++) {
		struct wlr_scene_rect **r = &server->snap_rects[i + k];
		if (!*r) {
			*r = wlr_scene_rect_create(server->tile_tree, part[k].width, part[k].height, color);
			if (!*r) {
				return;
			}
		}
		wlr_scene_rect_set_size(*r, part[k].width, part[k].height);
		wlr_scene_rect_set_color(*r, color);
		wlr_scene_node_set_position(&(*r)->node, part[k].x, part[k].y);
		wlr_scene_node_set_enabled(&(*r)->node, true);
		wlr_scene_node_raise_to_top(&(*r)->node);
	}
}

static void zone_fill(struct orbit_server *server, int i, const struct wlr_box *b, const float color[4]) {
	struct wlr_scene_rect **r = &server->snap_rects[i];
	if (!*r) {
		*r = wlr_scene_rect_create(server->tile_tree, b->width, b->height, color);
		if (!*r) {
			return;
		}
	}
	wlr_scene_rect_set_size(*r, b->width, b->height);
	wlr_scene_rect_set_color(*r, color);
	wlr_scene_node_set_position(&(*r)->node, b->x, b->y);
	wlr_scene_node_set_enabled(&(*r)->node, true);
	wlr_scene_node_raise_to_top(&(*r)->node);
}

/* a prévia do encaixe: o alvo (cheio) e, nas laterais, a outra metade só no contorno, como no desenho "duas metades" */
static void preview(struct orbit_server *server, struct wlr_box *box, struct wlr_box *other) {
	for (int i = 0; i < 10; i++) {
		if (server->snap_rects[i]) {
			wlr_scene_node_set_enabled(&server->snap_rects[i]->node, false);
		}
	}
	if (!box) {
		return;
	}
	static const float fill[4] = { 0.50f * 0.26f, 0.36f * 0.26f, 0.95f * 0.26f, 0.26f };
	static const float line[4] = { 0.66f, 0.52f, 1.0f, 1.0f };
	static const float faint_fill[4] = { 0.50f * 0.07f, 0.36f * 0.07f, 0.95f * 0.07f, 0.07f };
	static const float faint_line[4] = { 0.66f * 0.5f, 0.52f * 0.5f, 1.0f * 0.5f, 0.5f };
	if (other) {
		zone_fill(server, 0, other, faint_fill);
		zone_outline(server, 1, other, 2, faint_line);
	}
	zone_fill(server, 5, box, fill);
	zone_outline(server, 6, box, 3, line);
}

void orbit_move_begin(struct orbit_server *server, struct orbit_view *view) {
	if (orbit_view_isolated(view)) {
		return; /* a janela de uma tela em Gaming ou Mídia não se arrasta */
	}
	if (!view || !view->mapped || view->fullscreen || server->session_locked) {
		return;
	}
	if (!view->floating && orbit_free_desktop(server, view->output)) {
		view->floating = true;
	}
	if (!view->floating) {
		return; /* no mosaico (o telemóvel) não se arrasta */
	}
	double cx = server->cursor->x, cy = server->cursor->y;
	if (view->maximized || view->snap) {
		/* solta do encaixe: o tamanho livre de antes, debaixo do cursor no mesmo sítio relativo */
		struct wlr_box g = geometry(view);
		double rel = g.width > 0 ? (cx - view->x) / g.width : 0.5;
		int w = view->fw > 0 ? view->fw : g.width;
		view->maximized = false;
		view->snap = 0;
		int top = orbit_deco_top(view);
		view->x = (int)(cx - rel * w);
		view->y = top > 0 ? (int)cy + top / 2 : (int)cy - 16; /* com barra: o cursor no meio dela */
		view->fx = view->x;
		view->fy = view->y;
		orbit_arrange(server);
	}
	server->cursor_mode = ORBIT_CURSOR_MOVE;
	server->grabbed = view;
	server->grab_x = cx;
	server->grab_y = cy;
	server->grab_gx = view->x;
	server->grab_gy = view->y;
	server->snap_pending = 0;
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "fleur");
	orbit_focus(view);
}

/* o cursor mexeu a arrastar: a janela segue, e nas bordas aparece a prévia do encaixe */
/* há outra saída ligada logo depois desta borda (dx, dy: de que lado)? Olha dois pontos, perto dos cantos, para um monitor menor ao lado também contar */
void orbit_move_motion(struct orbit_server *server) {
	struct orbit_view *view = server->grabbed;
	struct orbit_output *sob = output_at(server, server->cursor->x, server->cursor->y);
	if (sob && sob != view->output && orbit_output_isolated(sob)) {
		return; /* não se passa a janela para uma tela em Gaming ou Mídia: ela espera na borda */
	}
	view->x = server->grab_gx + (int)(server->cursor->x - server->grab_x);
	view->y = server->grab_gy + (int)(server->cursor->y - server->grab_y);
	view->sliding = false;
	view->placed = true;
	view->slide_x = view->x;
	view->slide_y = view->y;
	wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
	struct orbit_output *output = output_at(server, server->cursor->x, server->cursor->y);
	server->snap_pending = 0;
	if (!orbit_free_desktop(server, output)) {
		preview(server, NULL, NULL);
		return;
	}
	struct wlr_box full;
	wlr_output_layout_get_box(server->layout, output->wlr, &full);
	double x = server->cursor->x, y = server->cursor->y;
	/* As laterais do monitor são uma zona (ORBIT_SNAP_ZONE): a janela passa por ela direto para o monitor do lado, e só entra no arranjo
	 * (metade da tela) se for SOLTA com o cursor dentro dela. Em cima, uma faixa fina maximiza. */
	/* A zona aparece também quando é a JANELA que chega à lateral (a borda dela encosta na do monitor), mesmo com o cursor mais para dentro:
	 * uma janela larga agarrada pelo meio chega à borda muito antes do cursor. Só conta se a janela está na maior parte neste monitor
	 * (senão é uma travessia: a janela passando para o outro lado) e o cursor está na metade daquela lateral. */
	struct wlr_box g = geometry(view);
	int gw = g.width > 0 ? g.width : view->w, gh = g.height > 0 ? g.height : view->h;
	struct wlr_box win = { view->x, view->y, gw > 0 ? gw : 1, gh > 0 ? gh : 1 }, inter;
	bool mostly_here = wlr_box_intersection(&inter, &win, &full) && (long)inter.width * inter.height * 2 >= (long)win.width * win.height;
	bool left_half = x < full.x + full.width / 2.0;
	if (x <= full.x + ORBIT_SNAP_ZONE || (mostly_here && left_half && view->x <= full.x + ORBIT_SNAP_WINDOW)) {
		server->snap_pending = 1;
	} else if (x >= full.x + full.width - 1 - ORBIT_SNAP_ZONE || (mostly_here && !left_half && view->x + win.width >= full.x + full.width - ORBIT_SNAP_WINDOW)) {
		server->snap_pending = 2;
	} else if (y <= full.y + ORBIT_SNAP_TOP) {
		server->snap_pending = 3;
	}
	if (!server->snap_pending) {
		preview(server, NULL, NULL);
		return;
	}
	struct orbit_output *old = view->output;
	view->output = output;
	struct wlr_box box, other;
	struct wlr_box *other_ptr = NULL;
	if (server->snap_pending == 3) {
		box = orbit_layer_usable(output, &full);
	} else {
		box = orbit_snap_box(view, server->snap_pending);
		other = orbit_snap_box(view, server->snap_pending == 1 ? 2 : 1);
		other_ptr = &other;
	}
	view->output = old;
	preview(server, &box, other_ptr);
}

/* largou: muda de tela se for o caso, e encaixa se estava numa borda */
void orbit_move_end(struct orbit_server *server) {
	struct orbit_view *view = server->grabbed;
	int snap = server->snap_pending;
	server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
	server->grabbed = NULL;
	server->snap_pending = 0;
	preview(server, NULL, NULL);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
	if (!view || !view->mapped) {
		return;
	}
	struct orbit_output *dest = output_at(server, server->cursor->x, server->cursor->y);
	/* o cursor pode ficar parado na borda do monitor (o compositor de fora segura o mouse na janela do clique) com a janela já quase toda
	 * do outro lado: vale a saída que cobre a maior parte da janela, e não o pixel em que o cursor ficou */
	if (dest && !snap) {
		struct wlr_box jan = { view->x, view->y, view->w > 0 ? view->w : 1, view->h > 0 ? view->h : 1 };
		struct orbit_output *o, *maior = dest;
		long area_maior = 0;
		wl_list_for_each(o, &server->outputs, link) {
			struct wlr_box ob, inter;
			wlr_output_layout_get_box(server->layout, o->wlr, &ob);
			long area = wlr_box_intersection(&inter, &jan, &ob) ? (long)inter.width * inter.height : 0;
			if (area > area_maior && (o == dest || !orbit_output_isolated(o))) {
				area_maior = area;
				maior = o;
			}
		}
		dest = maior;
	}
	if (dest && dest != view->output && !orbit_output_isolated(dest) && !orbit_view_isolated(view)) {
		orbit_split_release(server, view);
		orbit_view_set_output(view, dest, false); /* largada com o mouse: já está no lugar */
		view->tag = dest->tag;
		view->fullscreen = false;
	}
	/* a posição livre fica a de onde começou a arrastar (para voltar a ela ao soltar o encaixe) */
	if (snap) {
		view->x = server->grab_gx;
		view->y = server->grab_gy;
		orbit_float_save(view);
	} else {
		view->fx = view->x;
		view->fy = view->y;
	}
	if (snap == 3) {
		orbit_view_set_maximized(view, true);
		return;
	}
	if (snap) {
		view->snap = snap;
	}
	orbit_arrange(server);
	orbit_focus(view);
}

/* ---- mudar o tamanho ---- */

#define ORBIT_EDGE_GRAB 6 /* px por dentro da borda de uma janela livre que pegam para mudar o tamanho */

/* as bordas de uma janela livre debaixo deste ponto (0: no meio dela) */
uint32_t orbit_float_edges_at(struct orbit_view *view, double lx, double ly) {
	if (!view || !view->floating || view->maximized || view->snap || view->fullscreen) {
		return 0;
	}
	struct wlr_box g = geometry(view);
	int top = orbit_deco_top(view); /* a caixa de fora começa no topo da barra */
	double x = lx - view->x, y = ly - (view->y - top);
	int altura = g.height + top;
	uint32_t edges = 0;
	if (x < ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_LEFT;
	} else if (x > g.width - ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_RIGHT;
	}
	if (y < ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_TOP;
	} else if (y > altura - ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_BOTTOM;
	}
	return edges;
}

const char *orbit_edges_cursor(uint32_t edges) {
	switch (edges) {
	case WLR_EDGE_TOP: return "n-resize";
	case WLR_EDGE_BOTTOM: return "s-resize";
	case WLR_EDGE_LEFT: return "w-resize";
	case WLR_EDGE_RIGHT: return "e-resize";
	case WLR_EDGE_TOP | WLR_EDGE_LEFT: return "nw-resize";
	case WLR_EDGE_TOP | WLR_EDGE_RIGHT: return "ne-resize";
	case WLR_EDGE_BOTTOM | WLR_EDGE_LEFT: return "sw-resize";
	case WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT: return "se-resize";
	default: return "left_ptr";
	}
}

void orbit_resize_begin(struct orbit_server *server, struct orbit_view *view, uint32_t edges) {
	if (!view || !view->mapped || view->fullscreen || server->session_locked) {
		return;
	}
	if (!view->floating && orbit_free_desktop(server, view->output)) {
		view->floating = true;
	}
	if (!view->floating) {
		return;
	}
	if (view->maximized || view->snap) {
		/* encaixada: começa do tamanho em que está, agora livre */
		struct wlr_box g = geometry(view);
		view->fw = g.width;
		view->fh = g.height;
		view->fx = view->x;
		view->fy = view->y;
		view->maximized = false;
		view->snap = 0;
		orbit_arrange(server);
	}
	if (!edges) {
		/* Alt+botão direito: a borda mais perto do cursor */
		struct wlr_box g = geometry(view);
		double cx = server->cursor->x - view->x, cy = server->cursor->y - view->y;
		edges = (cx < g.width / 2.0 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT) | (cy < g.height / 2.0 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM);
	}
	struct wlr_box g = geometry(view);
	server->cursor_mode = ORBIT_CURSOR_RESIZE;
	server->grabbed = view;
	server->resize_edges = edges;
	server->grab_x = server->cursor->x;
	server->grab_y = server->cursor->y;
	server->grab_gx = view->x;
	server->grab_gy = view->y;
	server->grab_w = g.width;
	server->grab_h = g.height;
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, orbit_edges_cursor(edges & 15));
	orbit_focus(view);
}

void orbit_resize_motion(struct orbit_server *server) {
	struct orbit_view *view = server->grabbed;
	int dx = (int)(server->cursor->x - server->grab_x);
	int dy = (int)(server->cursor->y - server->grab_y);
	int l = server->grab_gx, t = server->grab_gy;
	int r = l + server->grab_w, b = t + server->grab_h;
	uint32_t e = server->resize_edges;
	if (e & WLR_EDGE_LEFT) {
		l += dx;
		if (r - l < ORBIT_MIN_W) {
			l = r - ORBIT_MIN_W;
		}
	} else if (e & WLR_EDGE_RIGHT) {
		r += dx;
		if (r - l < ORBIT_MIN_W) {
			r = l + ORBIT_MIN_W;
		}
	}
	if (e & WLR_EDGE_TOP) {
		t += dy;
		if (b - t < ORBIT_MIN_H) {
			t = b - ORBIT_MIN_H;
		}
	} else if (e & WLR_EDGE_BOTTOM) {
		b += dy;
		if (b - t < ORBIT_MIN_H) {
			b = t + ORBIT_MIN_H;
		}
	}
	view->x = l;
	view->y = t;
	view->fx = l;
	view->fy = t;
	view->fw = view->w = r - l;
	view->fh = view->h = b - t;
	view->slide_x = l;
	view->slide_y = t;
	wlr_scene_node_set_position(&view->scene_tree->node, l, t);
	wlr_xdg_toplevel_set_resizing(view->toplevel, true);
	wlr_xdg_toplevel_set_size(view->toplevel, (uint32_t)(r - l), (uint32_t)(b - t));
	view->cfg_w = r - l;
	view->cfg_h = b - t;
}

void orbit_resize_end(struct orbit_server *server) {
	struct orbit_view *view = server->grabbed;
	server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
	server->grabbed = NULL;
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
	if (view && view->mapped) {
		wlr_xdg_toplevel_set_resizing(view->toplevel, false);
	}
}
