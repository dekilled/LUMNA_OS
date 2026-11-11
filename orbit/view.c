#include "orbit.h"

#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#if WLR_HAS_XWAYLAND
#include <wlr/xwayland.h>
#endif

/* ---- o que muda entre uma janela Wayland (xdg) e uma X11 (XWayland) ---- */

const char *orbit_view_app_id(const struct orbit_view *view) {
	if (view->toplevel) {
		return view->toplevel->app_id ? view->toplevel->app_id : "";
	}
#if WLR_HAS_XWAYLAND
	if (view->xsurface && view->xsurface->class) {
		return view->xsurface->class;
	}
#endif
	return "";
}

const char *orbit_view_title(const struct orbit_view *view) {
	if (view->toplevel) {
		return view->toplevel->title ? view->toplevel->title : "";
	}
#if WLR_HAS_XWAYLAND
	if (view->xsurface && view->xsurface->title) {
		return view->xsurface->title;
	}
#endif
	return "";
}

struct wlr_surface *orbit_view_surface(const struct orbit_view *view) {
	if (view->toplevel) {
		return view->toplevel->base->surface;
	}
#if WLR_HAS_XWAYLAND
	if (view->xsurface) {
		return view->xsurface->surface;
	}
#endif
	return NULL;
}

bool orbit_view_ready(const struct orbit_view *view) {
	if (view->toplevel) {
		return view->toplevel->base->initialized;
	}
	return view->xsurface != NULL;
}

void orbit_view_close(struct orbit_view *view) {
	if (view->toplevel) {
		wlr_xdg_toplevel_send_close(view->toplevel);
	}
#if WLR_HAS_XWAYLAND
	else if (view->xsurface) {
		wlr_xwayland_surface_close(view->xsurface);
	}
#endif
}

#if WLR_HAS_XWAYLAND
/* onde a janela X11 está na tela (o nó da cena, com a borda do Orbit) ou vai ficar quando a animação chegar */
static void xwl_pos(const struct orbit_view *view, int *x, int *y) {
	int b = view->server->border_px;
	bool inset = view->border && view->border->node.enabled && b > 0;
	int px = view->placed ? view->slide_to_x : view->x, py = view->placed ? view->slide_to_y : view->y;
	*x = px + (inset ? b : 0);
	*y = py + (inset ? b : 0);
}

void orbit_xwl_sync_pos(struct orbit_view *view, int x, int y) {
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (xs && (xs->x != x || xs->y != y)) {
		wlr_xwayland_surface_configure(xs, (int16_t)x, (int16_t)y, xs->width, xs->height);
	}
}
#endif

void orbit_view_set_activated(struct orbit_view *view, bool on) {
	if (view->toplevel) {
		wlr_xdg_toplevel_set_activated(view->toplevel, on);
	}
#if WLR_HAS_XWAYLAND
	else if (view->xsurface) {
		wlr_xwayland_surface_activate(view->xsurface, on);
		if (on) {
			wlr_xwayland_surface_restack(view->xsurface, NULL, XCB_STACK_MODE_ABOVE);
		}
	}
#endif
}

void orbit_view_set_size(struct orbit_view *view, int w, int h) {
	if (view->toplevel) {
		wlr_xdg_toplevel_set_size(view->toplevel, (uint32_t)w, (uint32_t)h);
	}
#if WLR_HAS_XWAYLAND
	else if (view->xsurface) {
		int x, y;
		xwl_pos(view, &x, &y);
		wlr_xwayland_surface_configure(view->xsurface, (int16_t)x, (int16_t)y, (uint16_t)(w > 0 ? w : 1), (uint16_t)(h > 0 ? h : 1));
	}
#endif
}

void orbit_view_set_state(struct orbit_view *view, bool fullscreen, bool maximized, uint32_t tiled) {
	if (view->toplevel) {
		wlr_xdg_toplevel_set_fullscreen(view->toplevel, fullscreen);
		wlr_xdg_toplevel_set_maximized(view->toplevel, maximized);
		wlr_xdg_toplevel_set_tiled(view->toplevel, tiled);
	}
#if WLR_HAS_XWAYLAND
	else if (view->xsurface) {
		wlr_xwayland_surface_set_fullscreen(view->xsurface, fullscreen);
#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8))
		wlr_xwayland_surface_set_maximized(view->xsurface, maximized, maximized);
#else
		wlr_xwayland_surface_set_maximized(view->xsurface, maximized);
#endif
	}
#endif
	(void)tiled;
}

void orbit_view_set_resizing(struct orbit_view *view, bool on) {
	if (view->toplevel) {
		wlr_xdg_toplevel_set_resizing(view->toplevel, on);
	}
	(void)on;
}

void orbit_view_reconfigure(struct orbit_view *view) {
	if (view->toplevel) {
		if (view->toplevel->base->initialized) {
			wlr_xdg_surface_schedule_configure(view->toplevel->base);
		}
		return;
	}
#if WLR_HAS_XWAYLAND
	if (view->xsurface) {
		wlr_xwayland_surface_set_fullscreen(view->xsurface, view->fullscreen);
	}
#endif
}

struct wlr_box orbit_view_client_box(struct orbit_view *view) {
	struct wlr_box box = { 0 };
	if (view->toplevel) {
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR < 19
		wlr_xdg_surface_get_geometry(view->toplevel->base, &box);
#else
		box = view->toplevel->base->geometry;
#endif
		return box;
	}
#if WLR_HAS_XWAYLAND
	if (view->xsurface) {
		struct wlr_surface *s = view->xsurface->surface;
		box.width = s && s->current.width > 0 ? s->current.width : view->xsurface->width;
		box.height = s && s->current.height > 0 ? s->current.height : view->xsurface->height;
	}
#endif
	return box;
}

void orbit_focus(struct orbit_view *view) {
	if (!view || !view->mapped || view->server->session_locked) {
		return; /* trancado, o teclado é só da tela de bloqueio */
	}
	struct orbit_server *server = view->server;
	if (view->minimized) {
		/* dar o foco a uma minimizada (a dock, o alt-tab, o orbit-msg) traz ela de volta */
		view->minimized = false;
		orbit_arrange(server);
	}
	struct orbit_view *previous_view = server->focused;
	struct wlr_surface *prev = server->seat->keyboard_state.focused_surface;
	if (prev && prev != orbit_view_surface(view)) {
#if WLR_HAS_XWAYLAND
		struct wlr_xwayland_surface *xprev = wlr_xwayland_surface_try_from_wlr_surface(prev);
		if (xprev) {
			wlr_xwayland_surface_activate(xprev, false);
		}
#endif
		struct wlr_xdg_surface *previous = wlr_xdg_surface_try_from_wlr_surface(prev);
		/* uma janela escondida (o app segue vivo, sem janela) está desmapeada e sem configure: pedir-lhe um é erro de protocolo (e, conforme o wlroots, aborta o compositor) */
		if (previous && previous->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && previous->toplevel && previous->initialized) {
			wlr_xdg_toplevel_set_activated(previous->toplevel, false);
		}
	}
	server->focused = view;
	view->focus_seq = ++server->focus_seq;
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	if (view->border && view->border->node.enabled) {
		wlr_scene_node_place_below(&view->border->node, &view->scene_tree->node);
	}
	orbit_view_set_activated(view, true);
	/* o foco entra mesmo sem teclado nenhum ainda (o telemóvel só tem o virtual, que chega
	 * depois): o wlroots aceita, e as teclas chegam assim que houver um */
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (!server->exclusive_layer) {
		wlr_seat_keyboard_notify_enter(server->seat, orbit_view_surface(view),
			keyboard ? keyboard->keycodes : NULL, keyboard ? keyboard->num_keycodes : 0,
			keyboard ? &keyboard->modifiers : NULL);
	}
	orbit_foreign_sync(view);
	if (previous_view && previous_view != view && previous_view->mapped) {
		orbit_foreign_sync(previous_view);
	}
	orbit_borders_sync(server);
	orbit_ipc_sync(server);
}

void orbit_unfocus(struct orbit_server *server) {
	struct orbit_view *view = server->focused;
	if (!view) {
		return;
	}
	server->focused = NULL;
	if (view->mapped) {
		orbit_view_set_activated(view, false);
	}
	if (!server->exclusive_layer) {
		wlr_seat_keyboard_notify_clear_focus(server->seat);
	}
	orbit_foreign_sync(view);
	orbit_borders_sync(server);
	orbit_ipc_sync(server);
}

void focus_top(struct orbit_server *server, struct orbit_output *output) {
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->mapped && !view->minimized && view->output == output && view->tag == output->tag) {
			orbit_focus(view);
			if (output_is_phone(output->wlr)) {
				orbit_arrange(server);
			}
			return;
		}
	}
	if (server->focused && server->focused->output == output) {
		orbit_unfocus(server);
	}
}

struct orbit_view *view_at(struct orbit_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}
	*surface = scene_surface->surface;
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	if (!tree) {
		return NULL;
	}
	struct orbit_view *view = tree->node.data;
	if (view->kind != ORBIT_NODE_VIEW) {
		return NULL;
	}
	return view;
}

/*
 * Maximizar e ecrã inteiro, pedidos pela própria janela (o botão dela, o F11 de um leitor de
 * vídeo), pela dock (foreign toplevel) ou pelo Orbit. Maximizada ocupa a área útil da saída e as
 * outras ficam no mosaico por baixo (voltam à frente com o foco); em ecrã inteiro vai para cima
 * da barra e da dock.
 */
void orbit_view_set_maximized(struct orbit_view *view, bool on) {
	if (view->maximized == on) {
		orbit_view_reconfigure(view);
		return;
	}
	if (on) {
		orbit_float_save(view);
		view->snap = 0;
	} else if (view->floating) {
		view->x = view->fx; /* volta para onde estava livre */
		view->y = view->fy;
	}
	view->maximized = on;
	if (on) {
		orbit_split_release(view->server, view);
	}
	if (!view->mapped) {
		if (view->toplevel && view->toplevel->base->initialized) {
			wlr_xdg_toplevel_set_maximized(view->toplevel, on);
			view->cfg_valid = false;
		}
		return;
	}
	orbit_arrange(view->server);
	if (on) {
		orbit_focus(view);
	}
}

void orbit_view_set_fullscreen(struct orbit_view *view, bool on) {
	if (view->fullscreen == on) {
		orbit_view_reconfigure(view);
		return;
	}
	view->fullscreen = on;
	if (on) {
		view->floating = false;
		orbit_split_release(view->server, view);
	}
	if (!view->mapped) {
		if (view->toplevel && view->toplevel->base->initialized) {
			wlr_xdg_toplevel_set_fullscreen(view->toplevel, on);
			view->cfg_valid = false;
		}
		return;
	}
	orbit_arrange(view->server);
	orbit_focus(view);
}

static void xdg_request_maximize(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_maximize);
	orbit_view_set_maximized(view, view->toplevel->requested.maximized);
}

void orbit_view_set_minimized(struct orbit_view *view, bool on) {
	if (!view->mapped || view->minimized == on) {
		return;
	}
	struct orbit_server *server = view->server;
	if (!on) {
		orbit_focus(view); /* desfaz o minimizar e traz para a frente */
		return;
	}
	view->minimized = true;
	orbit_split_release(server, view);
	if (server->grabbed == view) {
		server->grabbed = NULL;
		server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
	}
	bool was_focused = server->focused == view;
	if (was_focused) {
		orbit_unfocus(server);
	}
	orbit_arrange(server);
	if (was_focused && view->output) {
		focus_top(server, view->output);
	}
	orbit_foreign_sync(view);
}

static void xdg_request_move(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_move);
	/* a barra de título da própria janela: só com o botão em baixo (o pedido vem de um aperto) */
	if (view->server->cursor_mode == ORBIT_CURSOR_PASSTHROUGH) {
		orbit_move_begin(view->server, view);
	}
}

static void xdg_request_resize(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xdg_toplevel_resize_event *event = data;
	if (view->server->cursor_mode == ORBIT_CURSOR_PASSTHROUGH) {
		orbit_resize_begin(view->server, view, event->edges);
	}
}

static void xdg_request_minimize(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_minimize);
	orbit_view_set_minimized(view, true);
}

static void xdg_request_fullscreen(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_fullscreen);
	orbit_view_set_fullscreen(view, view->toplevel->requested.fullscreen);
}

void orbit_view_map(struct orbit_view *view) {
	/* o jogo da Steam vai para a tela do Gaming (o nativo tem uma classe qualquer: quem diz é o ambiente do processo dele) */
	const char *familia = view->xsurface && orbit_xwl_steam_game(view) ? "steam_app" : orbit_view_app_id(view);
	struct orbit_output *output = orbit_output_for_new_view(view->server, familia);
	view->mapped = true;
	view->output = output;
	view->tag = output ? output->tag : 0;
	struct wlr_box box = { 0, 0, 800, 600 };
	if (output) {
		wlr_output_layout_get_box(view->server->layout, output->wlr, &box);
	}
	view->x = box.x + view->server->gap + 48;
	view->y = box.y + view->server->gap + 48;
	view->w = box.width / 2;
	view->h = box.height / 2;
	if (view->toplevel && orbit_app_is_gaming_shell(orbit_view_app_id(view))) {
		view->fullscreen = true; /* a biblioteca do Gaming ocupa a tela inteira do modo */
		view->floating = false;
		wlr_xdg_toplevel_set_fullscreen(view->toplevel, true);
		view->cfg_valid = false;
	}
#if WLR_HAS_XWAYLAND
	if (view->xsurface && view->xsurface->fullscreen) {
		view->fullscreen = true; /* o jogo X11 já abre pedindo tela cheia */
		view->floating = false;
	}
#endif
	if (orbit_free_desktop(view->server, output) && !view->fullscreen) {
		view->floating = true;
		orbit_float_place_new(view);
	}
	wl_list_insert(view->server->views.prev, &view->link);
	orbit_foreign_map(view);
	orbit_ipc_view_event(view, "janela_aberta");
	struct orbit_server *server = view->server;
	struct orbit_view *prev = server->focused;
	orbit_arrange(server);
	orbit_focus(view);
	phone_on_map(server, view, prev);
	if (output && (output_is_phone(output->wlr) || orbit_output_isolated(output))) {
		/* numa tela de Gaming ou Mídia só uma janela aparece (a que tem o foco): a arrumação de cima ainda via o foco na janela de antes (o lumna-jogos) e
		 * escondia esta; sem arrumar de novo ela só aparecia quando outro evento refazia o arranjo (o jogo abria "por trás" e depois vinha à frente) */
		orbit_arrange(server);
	}
}

void orbit_view_unmap(struct orbit_view *view) {
	if (!view->mapped) {
		return;
	}
	struct orbit_server *server = view->server;
	bool was_focused = server->focused == view;
	struct orbit_view *partner = view == server->split_top ? server->split_bottom
		: view == server->split_bottom ? server->split_top : NULL;
	orbit_split_release(server, view);
	orbit_ipc_view_event(view, "janela_fechada");
	view->mapped = false;
	orbit_foreign_destroy(view);
	if (view->server->grabbed == view) {
		view->server->grabbed = NULL;
		view->server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
	}
	if (was_focused) {
		server->focused = NULL;
	}
	struct orbit_output *output = view->output;
	wl_list_remove(&view->link);
	/* o teclado não pode ficar apontando para uma superfície que acabou de sumir */
	if (server->seat->keyboard_state.focused_surface && server->seat->keyboard_state.focused_surface == orbit_view_surface(view)) {
		wlr_seat_keyboard_notify_clear_focus(server->seat);
	}
	orbit_arrange(server);
	if (was_focused && partner && partner->mapped && partner->output == output) {
		orbit_focus(partner);
	} else if (output) {
		focus_top(server, output);
	}
}

static void xdg_map(struct wl_listener *listener, void *data) {
	orbit_view_map(wl_container_of(listener, (struct orbit_view *)NULL, map));
}

static void xdg_unmap(struct wl_listener *listener, void *data) {
	orbit_view_unmap(wl_container_of(listener, (struct orbit_view *)NULL, unmap));
}

static void xdg_destroy(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	orbit_view_release(view);
}

void orbit_view_release(struct orbit_view *view) {
	if (view->mapped) {
		orbit_split_release(view->server, view);
		wl_list_remove(&view->link);
	}
	if (view->server->split_top == view) {
		view->server->split_top = NULL;
	}
	if (view->server->split_bottom == view) {
		view->server->split_bottom = NULL;
	}
	if (view->server->focused == view) {
		view->server->focused = NULL;
	}
	if (view->server->grabbed == view) {
		view->server->grabbed = NULL;
		view->server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
	}
	orbit_foreign_destroy(view);
	orbit_deco_destroy(view);
	if (view->border) {
		wlr_scene_node_destroy(&view->border->node);
		view->border = NULL;
	}
	if (view->xsurface) {
		wlr_scene_node_destroy(&view->scene_tree->node); /* a do xdg morre com a superfície; a da janela X11 é nossa */
	}
	free(view);
}

static void xdg_commit(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, commit);
	if (view->toplevel->base->initial_commit) {
		const char *app_id = view->toplevel->app_id;
		struct orbit_output *alvo = app_id && orbit_app_is_mode_shell(app_id) ? orbit_output_for_new_view(view->server, app_id) : NULL;
		struct wlr_box caixa = { 0 };
		if (alvo) {
			wlr_output_layout_get_box(view->server->layout, alvo->wlr, &caixa);
		}
		if (alvo && caixa.width > 0 && caixa.height > 0) {
			/* o Gaming e a Mídia já nascem do tamanho da tela (e o Gaming em tela cheia): o primeiro quadro do app já é o certo, sem crescer aos saltos */
			if (orbit_app_is_gaming_shell(app_id)) {
				wlr_xdg_toplevel_set_fullscreen(view->toplevel, true);
			}
			wlr_xdg_toplevel_set_size(view->toplevel, caixa.width, caixa.height);
		} else {
			wlr_xdg_toplevel_set_size(view->toplevel, 0, 0);
		}
		orbit_deco_send_mode(view); /* o modo da barra vai junto com o primeiro configure */
		view->cfg_valid = false;
	} else if (view->mapped) {
		orbit_foreign_sync(view);
		orbit_deco_update(view); /* o cliente mudou de tamanho: a barra acompanha a largura */
	}
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *toplevel = data;
	struct orbit_view *view = calloc(1, sizeof(*view));
	if (!view) {
		wlr_log(WLR_ERROR, "sem memória para um toplevel");
		return;
	}
	view->server = server;
	view->id = ++server->next_view_id;
	view->toplevel = toplevel;
	view->scene_tree = wlr_scene_xdg_surface_create(server->tile_tree, toplevel->base);
	if (!view->scene_tree) {
		wlr_log(WLR_ERROR, "cena do toplevel falhou");
		free(view);
		return;
	}
	view->scene_tree->node.data = view;
	toplevel->base->data = view->scene_tree;
	view->border = wlr_scene_rect_create(server->tile_tree, 1, 1, server->border_color);
	if (view->border) {
		wlr_scene_node_set_enabled(&view->border->node, false);
	}
	view->map.notify = xdg_map;
	wl_signal_add(&toplevel->base->surface->events.map, &view->map);
	view->unmap.notify = xdg_unmap;
	wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
	view->commit.notify = xdg_commit;
	wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);
	view->destroy.notify = xdg_destroy;
	wl_signal_add(&toplevel->events.destroy, &view->destroy);
	view->request_maximize.notify = xdg_request_maximize;
	wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = xdg_request_fullscreen;
	wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
	view->request_minimize.notify = xdg_request_minimize;
	wl_signal_add(&toplevel->events.request_minimize, &view->request_minimize);
	view->request_move.notify = xdg_request_move;
	wl_signal_add(&toplevel->events.request_move, &view->request_move);
	view->request_resize.notify = xdg_request_resize;
	wl_signal_add(&toplevel->events.request_resize, &view->request_resize);
}

static struct orbit_view *popup_root_view(struct wlr_xdg_popup *popup) {
	struct wlr_surface *surface = popup->parent;
	while (surface) {
		struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(surface);
		if (!xdg) {
			return NULL;
		}
		if (xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && xdg->data) {
			struct wlr_scene_tree *tree = xdg->data;
			return tree->node.data;
		}
		if (xdg->role == WLR_XDG_SURFACE_ROLE_POPUP && xdg->popup) {
			surface = xdg->popup->parent;
			continue;
		}
		return NULL;
	}
	return NULL;
}

/* A caixa é no espaço da superfície do toplevel, não do layout global. Um menu pode passar da janela: o limite é a área útil da tela
 * (sem a barra e a dock, que ficam por cima), não o tamanho da janela. */
static void popup_unconstrain(struct orbit_popup *popup) {
	struct orbit_view *view = popup->parent_view;
	struct wlr_box box = { 0, 0, 0, 0 };
	if (view && view->output) {
		struct wlr_box out;
		wlr_output_layout_get_box(popup->server->layout, view->output->wlr, &out);
		struct wlr_box usable = view->output->off ? out : orbit_layer_usable(view->output, &out);
		box.x = usable.x - view->x;
		box.y = usable.y - view->y;
		box.width = usable.width;
		box.height = usable.height;
	} else if (view && view->w > 0 && view->h > 0) {
		box.width = view->w;
		box.height = view->h;
	} else {
		wlr_output_layout_get_box(popup->server->layout, NULL, &box);
	}
	if (box.width > 0 && box.height > 0) {
		wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &box);
	}
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	struct orbit_popup *popup = wl_container_of(listener, popup, commit);
	if (!popup->xdg_popup->base->initial_commit) {
		return;
	}
	popup_unconstrain(popup);
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	struct orbit_popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, new_xdg_popup);
	struct wlr_xdg_popup *xdg_popup = data;
	/* um popup de camada nasce sem pai: a layer-shell dá-lho depois (zwlr_layer_surface_v1.get_popup)
	 * e quem cuida dele é o layer.c */
	if (!xdg_popup->parent) {
		return;
	}
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	if (parent == NULL || parent->data == NULL) {
		return;
	}
	struct wlr_scene_tree *parent_tree = parent->data;
	struct wlr_scene_tree *tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
	if (!tree) {
		wlr_log(WLR_ERROR, "cena do popup falhou");
		return;
	}
	xdg_popup->base->data = tree;
	struct orbit_popup *popup = calloc(1, sizeof(*popup));
	if (!popup) {
		wlr_log(WLR_ERROR, "sem memória para um popup");
		return;
	}
	popup->server = server;
	popup->parent_view = popup_root_view(xdg_popup);
	popup->xdg_popup = xdg_popup;
	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void orbit_views_bind(struct orbit_server *server) {
	server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
	server->new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);
	orbit_deco_bind(server);
}
