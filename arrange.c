#include "orbit.h"

#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

/*
 * Invariantes do par no telemóvel (phone_split):
 *
 * phone_split só fica verdadeiro com split_top e split_bottom distintos,
 * ambos mapeados. Alt+S liga o modo e zera os ponteiros; o arrange seguinte
 * escolhe o par com bind_or_release_split. Se não houver duas candidatas
 * na tag visível, o modo desliga-se.
 *
 * O par pode estacionar (as duas janelas continuam nesta saída, noutra tag).
 * Enquanto estiver estacionado, a tag visível volta ao monóculo e o par
 * não é refeito com outras janelas. Mudar de tag não apaga o par: quando
 * as duas regressam à tag activa, o sítio de cada uma mantém-se.
 *
 * Unmap de um lado promove a próxima candidata da mesma tag. Sem segunda
 * janela, phone_split passa a falso. Fechar a saída DSI limpa o par.
 * Uma janela flutuante ou em ecrã inteiro não entra no par.
 */

static void place(struct orbit_view *view, int x, int y, int w, int h, bool tiled) {
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
	int b = view->server->border_px;
	/* maximizada ou em ecrã inteiro, a janela vai até à borda: sem moldura */
	bool inset = !view->fullscreen && !view->maximized && view->border && b > 0 && w > b * 2 && h > b * 2;
	int cw = inset ? w - b * 2 : w;
	int ch = inset ? h - b * 2 : h;
	view->x = x;
	view->y = y;
	view->w = w;
	view->h = h;
	wlr_scene_node_set_enabled(&view->scene_tree->node, true);
	if (view->border) {
		wlr_scene_node_set_enabled(&view->border->node, inset);
		if (inset) {
			wlr_scene_rect_set_size(view->border, w, h);
			wlr_scene_node_place_below(&view->border->node, &view->scene_tree->node);
		}
	}
	/* em ecrã inteiro a janela passa para cima da barra e da dock (e volta ao sair) */
	struct wlr_scene_tree *tree = view->fullscreen ? view->server->full_tree : view->server->tile_tree;
	if (view->scene_tree->node.parent != tree) {
		wlr_scene_node_reparent(&view->scene_tree->node, tree);
		if (view->border) {
			wlr_scene_node_reparent(&view->border->node, tree);
			wlr_scene_node_place_below(&view->border->node, &view->scene_tree->node);
		}
	}
	view->leaving = false;
	orbit_slide(view, x, y);
	bool max = view->maximized && !view->fullscreen;
	uint32_t edges = tiled ? WLR_EDGE_LEFT | WLR_EDGE_RIGHT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM : WLR_EDGE_NONE;
	if (!view->cfg_valid || view->cfg_w != cw || view->cfg_h != ch || view->cfg_full != view->fullscreen ||
		view->cfg_max != max || view->cfg_tiled != edges) {
		wlr_xdg_toplevel_set_size(view->toplevel, (uint32_t)cw, (uint32_t)ch);
		wlr_xdg_toplevel_set_fullscreen(view->toplevel, view->fullscreen);
		wlr_xdg_toplevel_set_maximized(view->toplevel, max);
		wlr_xdg_toplevel_set_tiled(view->toplevel, edges);
		view->cfg_valid = true;
		view->cfg_w = cw;
		view->cfg_h = ch;
		view->cfg_full = view->fullscreen;
		view->cfg_max = max;
		view->cfg_tiled = edges;
	}
	orbit_foreign_sync(view);
}

static void hide_view(struct orbit_view *view) {
	if (view->leaving) {
		return; /* ainda deslizando para fora: some sozinha ao chegar */
	}
	orbit_slide_stop(view);
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);
	if (view->border) {
		wlr_scene_node_set_enabled(&view->border->node, false);
	}
	orbit_foreign_sync(view); /* deixou de se ver nesta saída */
}

static bool split_candidate(struct orbit_view *view, struct orbit_output *output) {
	return view->mapped && !view->minimized && view->output == output && view->tag == output->tag && !view->floating &&
		!view->fullscreen;
}

static struct orbit_view *next_phone_view(struct orbit_server *server, struct orbit_view *after,
		struct orbit_view *skip) {
	if (!after || !after->output) {
		return NULL;
	}
	struct orbit_output *output = after->output;
	struct orbit_view *view;
	bool seen = false;
	wl_list_for_each(view, &server->views, link) {
		if (!seen) {
			if (view == after) {
				seen = true;
			}
			continue;
		}
		if (view != skip && split_candidate(view, output)) {
			return view;
		}
	}
	wl_list_for_each(view, &server->views, link) {
		if (view == after) {
			break;
		}
		if (view != skip && split_candidate(view, output)) {
			return view;
		}
	}
	return NULL;
}

void orbit_split_release(struct orbit_server *server, struct orbit_view *view) {
	if (!server->phone_split || !view) {
		return;
	}
	if (view != server->split_top && view != server->split_bottom) {
		return;
	}
	struct orbit_view *other = view == server->split_top ? server->split_bottom : server->split_top;
	struct orbit_view *next = next_phone_view(server, view, other);
	if (view == server->split_top) {
		server->split_top = next;
	} else {
		server->split_bottom = next;
	}
	if (!server->split_top || !server->split_bottom || server->split_top == server->split_bottom) {
		server->phone_split = false;
		server->split_top = NULL;
		server->split_bottom = NULL;
	}
}

static int tiled_index(struct orbit_view **tiled, int n, struct orbit_view *view) {
	for (int i = 0; i < n; i++) {
		if (tiled[i] == view) {
			return i;
		}
	}
	return -1;
}

/* Liga o par à tag visível, ou larga-o se não houver duas candidatas. */
static void bind_or_release_split(struct orbit_server *server, struct orbit_view **tiled, int n) {
	if (tiled_index(tiled, n, server->split_top) < 0) {
		server->split_top = NULL;
	}
	if (tiled_index(tiled, n, server->split_bottom) < 0 || server->split_bottom == server->split_top) {
		server->split_bottom = NULL;
	}
	if (!server->split_top) {
		int focused = tiled_index(tiled, n, server->focused);
		server->split_top = focused >= 0 ? tiled[focused] : (n > 0 ? tiled[0] : NULL);
	}
	if (server->split_top && !server->split_bottom) {
		int top = tiled_index(tiled, n, server->split_top);
		for (int step = 1; step < n; step++) {
			struct orbit_view *candidate = tiled[(top + step) % n];
			if (candidate != server->split_top) {
				server->split_bottom = candidate;
				break;
			}
		}
	}
	if (!server->split_top || !server->split_bottom) {
		server->phone_split = false;
		server->split_top = NULL;
		server->split_bottom = NULL;
	}
}

static bool view_on_other_tag(struct orbit_view *view, struct orbit_output *output) {
	return view && view->mapped && view->output == output && view->tag != output->tag;
}

static bool pair_is_parked(struct orbit_server *server, struct orbit_output *output) {
	return server->phone_split && view_on_other_tag(server->split_top, output) &&
		view_on_other_tag(server->split_bottom, output);
}

void phone_on_map(struct orbit_server *server, struct orbit_view *view, struct orbit_view *prev) {
	struct orbit_output *output = view->output;
	if (!output || !output_is_phone(output->wlr) || !server->phone_split || !prev || prev == view) {
		return;
	}
	if (prev == server->split_top && server->split_bottom != view) {
		server->split_bottom = view;
	} else if (prev == server->split_bottom && server->split_top != view) {
		server->split_top = view;
	} else if (server->split_top && server->split_top != view) {
		server->split_bottom = view;
	}
}

static void raise_focused(struct orbit_server *server, struct orbit_view **tiled, int n) {
	if (!server->focused) {
		return;
	}
	for (int i = 0; i < n; i++) {
		if (tiled[i] == server->focused) {
			struct orbit_view *swap = tiled[0];
			tiled[0] = tiled[i];
			tiled[i] = swap;
			return;
		}
	}
}

static void phone_arrange(struct orbit_server *server, const struct wlr_box *box,
		struct orbit_view **tiled, int n, bool parked) {
	int gap = server->gap;
	if (!parked && server->phone_split && server->split_top && server->split_bottom) {
		int half = (box->height - gap) / 2;
		for (int i = 0; i < n; i++) {
			if (tiled[i] == server->split_top) {
				place(tiled[i], box->x, box->y, box->width, half, true);
			} else if (tiled[i] == server->split_bottom) {
				place(tiled[i], box->x, box->y + half + gap, box->width, box->height - half - gap, true);
			} else {
				hide_view(tiled[i]);
			}
		}
		return;
	}
	if (n > 0) {
		place(tiled[0], box->x, box->y, box->width, box->height, true);
		for (int i = 1; i < n; i++) {
			hide_view(tiled[i]);
		}
	}
}

static void fibonacci(struct orbit_server *server, const struct wlr_box *box,
		struct orbit_view **tiled, int n, bool spiral) {
	int gap = server->gap;
	int x = box->x + gap;
	int y = box->y + gap;
	int w = box->width - 2 * gap;
	int h = box->height - 2 * gap;
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
	for (int i = 0; i < n; i++) {
		if (i == n - 1) {
			place(tiled[i], x, y, w, h, true);
			break;
		}
		bool vertical = i % 2 == 1;
		bool flip = !spiral && (i % 4 == 2 || i % 4 == 3);
		if (!vertical) {
			int part = w * (i == 0 ? server->master_pct : 50) / 100;
			if (part < 1) {
				part = 1;
			}
			if (part >= w) {
				part = w / 2;
			}
			if (flip) {
				place(tiled[i], x + (w - part) + gap, y, part - gap, h, true);
				w -= part;
			} else {
				place(tiled[i], x, y, part - gap, h, true);
				x += part;
				w -= part;
			}
		} else {
			int part = h / 2;
			if (part < 1) {
				part = 1;
			}
			if (flip) {
				place(tiled[i], x, y + (h - part) + gap, w, part - gap, true);
				h -= part;
			} else {
				place(tiled[i], x, y, w, part - gap, true);
				y += part;
				h -= part;
			}
		}
	}
}

static void desktop_arrange(struct orbit_server *server, const struct wlr_box *box,
		struct orbit_view **tiled, int n) {
	if (server->arrangement == ORBIT_LAYOUT_MONOCLE) {
		raise_focused(server, tiled, n);
		if (n > 0) {
			place(tiled[0], box->x, box->y, box->width, box->height, true);
			for (int i = 1; i < n; i++) {
				hide_view(tiled[i]);
			}
		}
		return;
	}
	if (server->arrangement == ORBIT_LAYOUT_SPIRAL || server->arrangement == ORBIT_LAYOUT_DWINDLE) {
		fibonacci(server, box, tiled, n, server->arrangement == ORBIT_LAYOUT_SPIRAL);
		return;
	}
	int gap = server->gap;
	if (n == 1) {
		place(tiled[0], box->x + gap, box->y + gap, box->width - 2 * gap, box->height - 2 * gap, true);
	} else if (n > 1) {
		int master = box->width * server->master_pct / 100;
		int rows = n - 1;
		int inner = box->height - gap * (rows + 1);
		int row_h = rows > 0 ? inner / rows : inner;
		place(tiled[0], box->x + gap, box->y + gap, master - gap, box->height - 2 * gap, true);
		for (int i = 1; i < n; i++) {
			int y = box->y + gap + (i - 1) * (row_h + gap);
			int h = (i == n - 1) ? (box->y + box->height - gap - y) : row_h;
			place(tiled[i], box->x + master + gap, y, box->width - master - 2 * gap, h, true);
		}
	}
}

static void note_tiled_pressure(struct orbit_output *output, int n, bool capped) {
	int near = (ORBIT_MAX_VIEWS * 3) / 4;
	if (capped) {
		if (!output->warned_cap) {
			wlr_log(WLR_ERROR, "saída %s cheia: limite de %d janelas em mosaico",
				output->wlr->name, ORBIT_MAX_VIEWS);
			output->warned_cap = true;
		}
		return;
	}
	output->warned_cap = false;
	if (n >= near) {
		if (!output->warned_near) {
			wlr_log(WLR_INFO, "saída %s: %d/%d janelas em mosaico",
				output->wlr->name, n, ORBIT_MAX_VIEWS);
			output->warned_near = true;
		}
	} else {
		output->warned_near = false;
	}
}

/* as janelas que entraram nesta troca de espaço começam de lado e deslizam até o lugar */
static void enter_views(struct orbit_server *server, struct orbit_output *output) {
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->enter_dx != 0 && view->output == output) {
			if (view->scene_tree->node.enabled) {
				orbit_slide_enter(view, view->enter_dx);
			}
		}
		view->enter_dx = 0;
	}
}

void orbit_arrange(struct orbit_server *server) {
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_box box;
		wlr_output_layout_get_box(server->layout, output->wlr, &box);
		if (output->bg) {
			int bw = box.width > 0 ? box.width : 1;
			int bh = box.height > 0 ? box.height : 1;
			wlr_scene_rect_set_size(output->bg, bw, bh);
			wlr_scene_node_set_position(&output->bg->node, box.x, box.y);
			wlr_scene_node_lower_to_bottom(&output->bg->node);
		}
		if (box.width <= 0 || box.height <= 0) {
			continue;
		}

		orbit_lock_configure(output);
		/* a troca de espaço (ou de modo) desliza: para a direita se o novo vem depois do que estava */
		int dir = output->tag == output->shown_tag ? 0 : output->tag > output->shown_tag ? 1 : -1;
		output->shown_tag = output->tag;
		bool animate = dir != 0 && server->anim_ms > 0 && !output_is_phone(output->wlr);
		struct wlr_box usable = orbit_layer_usable(output, &box);

		struct orbit_view *tiled[ORBIT_MAX_VIEWS];
		struct orbit_view *maxed[ORBIT_MAX_VIEWS];
		struct orbit_view *full = NULL;
		int n = 0, nmax = 0;
		bool capped = false;
		struct orbit_view *view;
		wl_list_for_each(view, &server->views, link) {
			if (!view->mapped || view->output != output) {
				continue;
			}
			if (view->tag != output->tag || view->minimized) {
				if (animate && view->tag != output->tag && !view->minimized && !view->leaving &&
					view->scene_tree->node.enabled) {
					orbit_slide_leave(view, -dir * box.width);
					orbit_foreign_sync(view);
				} else {
					hide_view(view);
				}
				continue;
			}
			if (animate && !view->scene_tree->node.enabled) {
				view->enter_dx = dir * box.width;
			}
			if (view->fullscreen) {
				full = view;
			}
			if (view->maximized && !view->fullscreen && nmax < ORBIT_MAX_VIEWS) {
				maxed[nmax++] = view; /* fora do mosaico: ocupa a área útil, por cima dele */
				continue;
			}
			if (view->floating && !view->fullscreen) {
				if (view->snap) {
					struct wlr_box s = orbit_snap_box(view, view->snap);
					place(view, s.x, s.y, s.width, s.height, true);
				} else {
					orbit_float_show(view);
				}
				continue;
			}
			if (n < ORBIT_MAX_VIEWS) {
				tiled[n++] = view;
			} else {
				hide_view(view);
				capped = true;
			}
		}
		assert(n <= ORBIT_MAX_VIEWS);
		note_tiled_pressure(output, n, capped);

		bool phone = output_is_phone(output->wlr);
		bool parked = phone && pair_is_parked(server, output);
		if (phone && server->phone_split && !parked) {
			bind_or_release_split(server, tiled, n);
		}
		if (phone && (!server->phone_split || parked)) {
			raise_focused(server, tiled, n);
		}

		if (full) {
			for (int i = 0; i < n; i++) {
				if (tiled[i] == full) {
					place(full, box.x, box.y, box.width, box.height, false);
				} else {
					hide_view(tiled[i]);
				}
			}
			for (int i = 0; i < nmax; i++) {
				hide_view(maxed[i]);
			}
			enter_views(server, output);
			continue;
		}
		if (phone) {
			phone_arrange(server, &usable, tiled, n, parked);
		} else {
			desktop_arrange(server, &usable, tiled, n);
		}
		for (int i = 0; i < nmax; i++) {
			place(maxed[i], usable.x, usable.y, usable.width, usable.height, false);
		}
		enter_views(server, output);
	}
	orbit_idle_update(server);
	orbit_borders_sync(server);
	orbit_ipc_sync(server);
}

void orbit_borders_sync(struct orbit_server *server) {
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->border || !view->border->node.enabled) {
			continue;
		}
		float color[4];
		bool on = server->focused == view;
		for (int i = 0; i < 3; i++) {
			color[i] = on ? server->border_color[i] : server->border_color[i] * 0.4f;
		}
		color[3] = server->border_color[3];
		wlr_scene_rect_set_color(view->border, color);
	}
}

void orbit_focus_step(struct orbit_server *server, int dir) {
	struct orbit_output *output = cursor_output(server);
	if (!output) {
		return;
	}
	struct orbit_view *list[ORBIT_MAX_VIEWS];
	int n = 0;
	bool capped = false;
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped || view->minimized || view->output != output || view->tag != output->tag) {
			continue;
		}
		if (n < ORBIT_MAX_VIEWS) {
			list[n++] = view;
		} else {
			capped = true;
		}
	}
	assert(n <= ORBIT_MAX_VIEWS);
	if (capped) {
		wlr_log(WLR_ERROR, "foco em %s cortado em %d janelas", output->wlr->name, ORBIT_MAX_VIEWS);
	}
	if (output_is_phone(output->wlr) && server->phone_split && server->split_top && server->split_bottom &&
		split_candidate(server->split_top, output) && split_candidate(server->split_bottom, output)) {
		struct orbit_view *pair[2] = { server->split_top, server->split_bottom };
		int index = server->focused == server->split_bottom ? 1 : 0;
		struct orbit_view *next = (server->focused == server->split_top || server->focused == server->split_bottom)
			? pair[(index + dir + 2) % 2]
			: pair[dir < 0 ? 1 : 0];
		orbit_focus(next);
		return;
	}
	if (n == 0) {
		return;
	}
	int index = 0;
	for (int i = 0; i < n; i++) {
		if (list[i] == server->focused) {
			index = i;
		}
	}
	orbit_focus(list[(index + dir + n) % n]);
	if (output_is_phone(output->wlr)) {
		orbit_arrange(server);
	}
}
