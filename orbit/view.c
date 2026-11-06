#include "orbit.h"

#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

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
	if (prev && prev != view->toplevel->base->surface) {
		struct wlr_xdg_surface *previous = wlr_xdg_surface_try_from_wlr_surface(prev);
		if (previous && previous->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && previous->toplevel) {
			wlr_xdg_toplevel_set_activated(previous->toplevel, false);
		}
	}
	server->focused = view;
	view->focus_seq = ++server->focus_seq;
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	if (view->border && view->border->node.enabled) {
		wlr_scene_node_place_below(&view->border->node, &view->scene_tree->node);
	}
	wlr_xdg_toplevel_set_activated(view->toplevel, true);
	/* o foco entra mesmo sem teclado nenhum ainda (o telemóvel só tem o virtual, que chega
	 * depois): o wlroots aceita, e as teclas chegam assim que houver um */
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (!server->exclusive_layer) {
		wlr_seat_keyboard_notify_enter(server->seat, view->toplevel->base->surface,
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
	if (view->mapped && view->toplevel) {
		wlr_xdg_toplevel_set_activated(view->toplevel, false);
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
		if (view->toplevel->base->initialized) {
			wlr_xdg_surface_schedule_configure(view->toplevel->base);
		}
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
		if (view->toplevel->base->initialized) {
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
		if (view->toplevel->base->initialized) {
			wlr_xdg_surface_schedule_configure(view->toplevel->base);
		}
		return;
	}
	view->fullscreen = on;
	if (on) {
		view->floating = false;
		orbit_split_release(view->server, view);
	}
	if (!view->mapped) {
		if (view->toplevel->base->initialized) {
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

static void xdg_map(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, map);
	struct orbit_output *output = orbit_output_for_new_view(view->server, view->toplevel->app_id);
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
	if (output && output_is_phone(output->wlr)) {
		orbit_arrange(server);
	}
}

static void xdg_unmap(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, unmap);
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
	orbit_arrange(server);
	if (was_focused && partner && partner->mapped && partner->output == output) {
		orbit_focus(partner);
	} else if (output) {
		focus_top(server, output);
	}
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
	if (view->border) {
		wlr_scene_node_destroy(&view->border->node);
		view->border = NULL;
	}
	free(view);
}

static void xdg_commit(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, commit);
	if (view->toplevel->base->initial_commit) {
		wlr_xdg_toplevel_set_size(view->toplevel, 0, 0);
		view->cfg_valid = false;
	} else if (view->mapped) {
		orbit_foreign_sync(view);
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

/* A caixa é no espaço da superfície do toplevel, não do layout global. */
static void popup_unconstrain(struct orbit_popup *popup) {
	struct orbit_view *view = popup->parent_view;
	struct wlr_box box = { 0, 0, 0, 0 };
	if (view && view->w > 0 && view->h > 0) {
		box.width = view->w;
		box.height = view->h;
	} else if (view && view->output) {
		struct wlr_box out;
		wlr_output_layout_get_box(popup->server->layout, view->output->wlr, &out);
		box.x = out.x - view->x;
		box.y = out.y - view->y;
		box.width = out.width;
		box.height = out.height;
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
}
