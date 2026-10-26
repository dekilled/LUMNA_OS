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
#define ORBIT_SNAP_EDGE 2 /* px da borda da saída que fazem o gatilho */

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
	int w = g.width > 0 ? g.width : usable.width * 3 / 5;
	int h = g.height > 0 ? g.height : usable.height * 7 / 10;
	bool resize = g.width < 1 || g.height < 1;
	if (w > usable.width - 2 * server->gap) {
		w = usable.width - 2 * server->gap;
		resize = true;
	}
	if (h > usable.height - 2 * server->gap) {
		h = usable.height - 2 * server->gap;
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
	view->y = usable.y + (usable.height - h) / 2 + step;
	if (view->x + w > usable.x + usable.width) {
		view->x = usable.x + server->gap;
	}
	if (view->y + h > usable.y + usable.height) {
		view->y = usable.y + server->gap;
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

static void preview(struct orbit_server *server, struct wlr_box *box) {
	if (!box) {
		if (server->snap_preview) {
			wlr_scene_node_set_enabled(&server->snap_preview->node, false);
		}
		return;
	}
	if (!server->snap_preview) {
		float color[4] = { 0.30f * 0.3f, 0.22f * 0.3f, 0.55f * 0.3f, 0.3f };
		server->snap_preview = wlr_scene_rect_create(server->tile_tree, box->width, box->height, color);
		if (!server->snap_preview) {
			return;
		}
	}
	wlr_scene_rect_set_size(server->snap_preview, box->width, box->height);
	wlr_scene_node_set_position(&server->snap_preview->node, box->x, box->y);
	wlr_scene_node_set_enabled(&server->snap_preview->node, true);
	wlr_scene_node_raise_to_top(&server->snap_preview->node);
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
		view->x = (int)(cx - rel * w);
		view->y = (int)cy - 16;
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
		preview(server, NULL);
		return;
	}
	struct wlr_box full;
	wlr_output_layout_get_box(server->layout, output->wlr, &full);
	double x = server->cursor->x, y = server->cursor->y;
	if (x <= full.x + ORBIT_SNAP_EDGE) {
		server->snap_pending = 1;
	} else if (x >= full.x + full.width - 1 - ORBIT_SNAP_EDGE) {
		server->snap_pending = 2;
	} else if (y <= full.y + ORBIT_SNAP_EDGE) {
		server->snap_pending = 3;
	}
	if (!server->snap_pending) {
		preview(server, NULL);
		return;
	}
	struct orbit_output *old = view->output;
	view->output = output;
	struct wlr_box box;
	if (server->snap_pending == 3) {
		box = orbit_layer_usable(output, &full);
	} else {
		box = orbit_snap_box(view, server->snap_pending);
	}
	view->output = old;
	preview(server, &box);
}

/* largou: muda de tela se for o caso, e encaixa se estava numa borda */
void orbit_move_end(struct orbit_server *server) {
	struct orbit_view *view = server->grabbed;
	int snap = server->snap_pending;
	server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
	server->grabbed = NULL;
	server->snap_pending = 0;
	preview(server, NULL);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
	if (!view || !view->mapped) {
		return;
	}
	struct orbit_output *dest = output_at(server, server->cursor->x, server->cursor->y);
	if (dest && dest != view->output && !orbit_output_isolated(dest) && !orbit_view_isolated(view)) {
		orbit_split_release(server, view);
		view->output = dest;
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
	double x = lx - view->x, y = ly - view->y;
	uint32_t edges = 0;
	if (x < ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_LEFT;
	} else if (x > g.width - ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_RIGHT;
	}
	if (y < ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_TOP;
	} else if (y > g.height - ORBIT_EDGE_GRAB) {
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
