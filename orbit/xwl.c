#include "orbit.h"

#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/version.h>

/*
 * XWayland: as janelas X11 (a Steam, os jogos do Proton, apps antigos) entram como janelas normais do Orbit: a mesma struct orbit_view,
 * com o xsurface no lugar do toplevel do xdg. Tudo o que muda entre os dois mundos está nas funções orbit_view_* de view.c; aqui fica o
 * ciclo de vida da janela X11 e os menus/dicas dela (override-redirect), que se posicionam sozinhos e não passam pelo arranjo.
 * O servidor X sobe sob demanda (lazy): só quando o primeiro cliente X11 se conecta; o DISPLAY vai no ambiente para os apps que o Orbit abre.
 * Sem o XWayland na wlroots (WLR_HAS_XWAYLAND = 0) este arquivo não faz nada e o Orbit segue só com o Wayland.
 */

#if WLR_HAS_XWAYLAND

#include <wlr/xwayland.h>
#include <stdio.h>

/* o diagnóstico das janelas X11 (journalctl -b | grep "xwl:"): o que o servidor X cria, onde e de que tamanho */
static void xwl_log(const char *evento, const struct wlr_xwayland_surface *xs, const char *extra) {
	wlr_log(WLR_INFO, "xwl: %s id=0x%x classe=\"%s\" titulo=\"%s\" sem-moldura=%d pos=%d,%d tam=%ux%u pid=%d %s", evento, xs->window_id, xs->class ? xs->class : "",
		xs->title ? xs->title : "", xs->override_redirect, xs->x, xs->y, xs->width, xs->height, (int)xs->pid, extra ? extra : "");
}

/* ---- um jogo da Steam? ---- */

/* A Steam marca todo jogo que abre (o nativo e o do Proton) com SteamGameId/SteamAppId no ambiente do processo: o jogo nativo tem uma classe
 * qualquer (csgo_linux64, hl2_linux), então a classe não basta. Só o Linux deixa ler o ambiente do processo, e só o dono dele. */
bool orbit_xwl_steam_game(const struct orbit_view *view) {
	return view->xsurface && orbit_view_detect_game(view); /* jogo.c: a mesma conta das janelas Wayland */
}

/* ---- janelas gerenciadas ---- */

static void xwl_surface_map(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, map);
	orbit_view_map(view);
	char extra[160];
	snprintf(extra, sizeof(extra), "-> janela do Orbit %u na saida %s, espaco %d, %dx%d em %d,%d, jogo=%d", view->id, view->output ? view->output->wlr->name : "?", view->tag + 1,
		view->w, view->h, view->x, view->y, view->game);
	xwl_log("mapeou", view->xsurface, extra);
}

static void xwl_surface_unmap(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, unmap);
	orbit_view_unmap(view);
}

static void xwl_associate(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, xwl_associate);
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (!xs || !xs->surface || view->xwl_assoc) {
		return;
	}
	view->xwl_assoc = true;
	view->map.notify = xwl_surface_map;
	wl_signal_add(&xs->surface->events.map, &view->map);
	view->unmap.notify = xwl_surface_unmap;
	wl_signal_add(&xs->surface->events.unmap, &view->unmap);
	wlr_scene_surface_create(view->scene_tree, xs->surface);
}

static void xwl_dissociate(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, xwl_dissociate);
	if (!view->xwl_assoc) {
		return;
	}
	view->xwl_assoc = false;
	if (view->mapped) {
		orbit_view_unmap(view);
	}
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
}

static void xwl_request_configure(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_configure_x);
	struct wlr_xwayland_surface_configure_event *e = data;
	char pedido[80];
	snprintf(pedido, sizeof(pedido), "pediu %d,%d %ux%u mapeada=%d", e->x, e->y, e->width, e->height, view->mapped);
	xwl_log("configure", view->xsurface, pedido);
	if (!view->mapped) {
		/* ainda não está na tela: vale o que pediu (o tamanho dela na hora de abrir) */
		wlr_xwayland_surface_configure(view->xsurface, e->x, e->y, e->width, e->height);
		return;
	}
	/*
	 * Já gerenciada. Onde o Orbit controla o tamanho (tela cheia, maximizada, no mosaico, ou numa tela de Gaming/Mídia, que é um kiosk), o cliente recebe de volta
	 * o tamanho que o Orbit quer, e não o que ele pediu: um jogo troca a resolução por conta própria (XMoveResizeWindow) e, se a resposta fosse o tamanho
	 * da superfície de agora (ainda o de antes), a janela ficava pequena para sempre enquanto o jogo já desenhava na tela inteira. Janela livre do
	 * Desktop vale o que pediu (o app que cresce a própria janela).
	 */
	bool orbit_manda = view->fullscreen || view->maximized || !view->floating || (view->output && orbit_output_isolated(view->output));
	uint16_t w = e->width, h = e->height;
	if (orbit_manda && view->cfg_valid && view->cfg_w > 0 && view->cfg_h > 0) {
		w = (uint16_t)view->cfg_w;
		h = (uint16_t)view->cfg_h;
	} else if (!orbit_manda) {
		view->fw = w;
		view->fh = h;
		view->w = w;
		view->h = h;
	}
	wlr_xwayland_surface_configure(view->xsurface, (int16_t)view->scene_tree->node.x, (int16_t)view->scene_tree->node.y, w, h);
}

static void xwl_request_activate(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_activate_x);
	/* o app pediu o foco (a Steam abrindo uma janela): só vale se ela está na tela que a pessoa vê, e não tira um jogo da frente no Gaming */
	if (view->mapped && view->output && view->tag == view->output->tag && orbit_view_may_take_focus(view)) {
		orbit_focus(view);
	}
}


static void xwl_request_fullscreen(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_fullscreen);
	orbit_view_set_fullscreen(view, view->xsurface->fullscreen);
}

static void xwl_request_maximize(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_maximize);
	orbit_view_set_maximized(view, view->xsurface->maximized_horz && view->xsurface->maximized_vert);
}

static void xwl_request_minimize(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_minimize);
	struct wlr_xwayland_minimize_event *e = data;
	orbit_view_set_minimized(view, e->minimize);
}

static void xwl_request_move(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_move);
	if (view->server->cursor_mode == ORBIT_CURSOR_PASSTHROUGH) {
		orbit_move_begin(view->server, view);
	}
}

static void xwl_request_resize(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xwayland_resize_event *e = data;
	if (view->server->cursor_mode == ORBIT_CURSOR_PASSTHROUGH) {
		orbit_resize_begin(view->server, view, e->edges);
	}
}

static void xwl_set_title(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, xwl_set_title);
	if (view->mapped) {
		orbit_foreign_sync(view);
	}
}

static void xwl_destroy(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, destroy);
	if (view->xwl_assoc) {
		xwl_dissociate(&view->xwl_dissociate, NULL);
	}
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->xwl_associate.link);
	wl_list_remove(&view->xwl_dissociate.link);
	wl_list_remove(&view->request_configure_x.link);
	wl_list_remove(&view->request_activate_x.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->xwl_set_title.link);
	wl_list_remove(&view->xwl_set_class.link);
	orbit_view_release(view);
}

static void xwl_view_create(struct orbit_server *server, struct wlr_xwayland_surface *xs) {
	struct orbit_view *view = calloc(1, sizeof(*view));
	if (!view) {
		return;
	}
	view->server = server;
	view->id = ++server->next_view_id;
	view->xsurface = xs;
	view->scene_tree = wlr_scene_tree_create(server->tile_tree);
	if (!view->scene_tree) {
		free(view);
		return;
	}
	view->scene_tree->node.data = view;
	view->border = wlr_scene_rect_create(server->tile_tree, 1, 1, server->border_color);
	if (view->border) {
		wlr_scene_node_set_enabled(&view->border->node, false);
	}
	view->destroy.notify = xwl_destroy;
	wl_signal_add(&xs->events.destroy, &view->destroy);
	view->xwl_associate.notify = xwl_associate;
	wl_signal_add(&xs->events.associate, &view->xwl_associate);
	view->xwl_dissociate.notify = xwl_dissociate;
	wl_signal_add(&xs->events.dissociate, &view->xwl_dissociate);
	view->request_configure_x.notify = xwl_request_configure;
	wl_signal_add(&xs->events.request_configure, &view->request_configure_x);
	view->request_activate_x.notify = xwl_request_activate;
	wl_signal_add(&xs->events.request_activate, &view->request_activate_x);
	view->request_maximize.notify = xwl_request_maximize;
	wl_signal_add(&xs->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = xwl_request_fullscreen;
	wl_signal_add(&xs->events.request_fullscreen, &view->request_fullscreen);
	view->request_minimize.notify = xwl_request_minimize;
	wl_signal_add(&xs->events.request_minimize, &view->request_minimize);
	view->request_move.notify = xwl_request_move;
	wl_signal_add(&xs->events.request_move, &view->request_move);
	view->request_resize.notify = xwl_request_resize;
	wl_signal_add(&xs->events.request_resize, &view->request_resize);
	view->xwl_set_title.notify = xwl_set_title;
	wl_signal_add(&xs->events.set_title, &view->xwl_set_title);
	view->xwl_set_class.notify = xwl_set_title;
	wl_signal_add(&xs->events.set_class, &view->xwl_set_class);
	if (xs->surface) {
		xwl_associate(&view->xwl_associate, NULL); /* a superfície já existe: o evento passou antes de ouvirmos */
	}
}

/* ---- menus, dicas e janelas sem moldura (override-redirect): ficam onde o app mandou ---- */

struct xwl_or {
	enum orbit_node_kind kind; /* ORBIT_NODE_XOR: o view_at não as trata como janela (não têm foco nem borda) */
	struct orbit_server *server;
	struct wlr_xwayland_surface *xs;
	struct wlr_scene_tree *tree;
	bool assoc;
	struct wl_listener associate, dissociate, map, unmap, destroy, set_geometry;
};

static void or_place(struct xwl_or *o) {
	wlr_scene_node_set_position(&o->tree->node, o->xs->x, o->xs->y);
}

static void or_map(struct wl_listener *listener, void *data) {
	struct xwl_or *o = wl_container_of(listener, o, map);
	or_place(o);
	xwl_log("mapeou sem moldura", o->xs, NULL);
	wlr_scene_node_set_enabled(&o->tree->node, true);
	wlr_scene_node_raise_to_top(&o->tree->node);
}

static void or_unmap(struct wl_listener *listener, void *data) {
	struct xwl_or *o = wl_container_of(listener, o, unmap);
	wlr_scene_node_set_enabled(&o->tree->node, false);
}

static void or_associate(struct wl_listener *listener, void *data) {
	struct xwl_or *o = wl_container_of(listener, o, associate);
	if (!o->xs->surface || o->assoc) {
		return;
	}
	o->assoc = true;
	o->map.notify = or_map;
	wl_signal_add(&o->xs->surface->events.map, &o->map);
	o->unmap.notify = or_unmap;
	wl_signal_add(&o->xs->surface->events.unmap, &o->unmap);
	wlr_scene_surface_create(o->tree, o->xs->surface);
}

static void or_dissociate(struct wl_listener *listener, void *data) {
	struct xwl_or *o = wl_container_of(listener, o, dissociate);
	if (!o->assoc) {
		return;
	}
	o->assoc = false;
	wl_list_remove(&o->map.link);
	wl_list_remove(&o->unmap.link);
}

static void or_set_geometry(struct wl_listener *listener, void *data) {
	struct xwl_or *o = wl_container_of(listener, o, set_geometry);
	or_place(o);
}

static void or_destroy(struct wl_listener *listener, void *data) {
	struct xwl_or *o = wl_container_of(listener, o, destroy);
	if (o->assoc) {
		or_dissociate(&o->dissociate, NULL);
	}
	wl_list_remove(&o->destroy.link);
	wl_list_remove(&o->associate.link);
	wl_list_remove(&o->dissociate.link);
	wl_list_remove(&o->set_geometry.link);
	wlr_scene_node_destroy(&o->tree->node);
	free(o);
}

static void or_create(struct orbit_server *server, struct wlr_xwayland_surface *xs) {
	struct xwl_or *o = calloc(1, sizeof(*o));
	if (!o) {
		return;
	}
	o->kind = ORBIT_NODE_XOR;
	o->server = server;
	o->xs = xs;
	o->tree = wlr_scene_tree_create(server->full_tree);
	if (!o->tree) {
		free(o);
		return;
	}
	o->tree->node.data = o;
	wlr_scene_node_set_enabled(&o->tree->node, false);
	o->destroy.notify = or_destroy;
	wl_signal_add(&xs->events.destroy, &o->destroy);
	o->associate.notify = or_associate;
	wl_signal_add(&xs->events.associate, &o->associate);
	o->dissociate.notify = or_dissociate;
	wl_signal_add(&xs->events.dissociate, &o->dissociate);
	o->set_geometry.notify = or_set_geometry;
	wl_signal_add(&xs->events.set_geometry, &o->set_geometry);
	if (xs->surface) {
		or_associate(&o->associate, NULL);
	}
}

/* ---- o servidor X ---- */

static void xwl_new_surface(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, xwl_new_surface);
	struct wlr_xwayland_surface *xs = data;
	xwl_log("nova", xs, NULL);
	if (xs->override_redirect) {
		or_create(server, xs);
	} else {
		xwl_view_create(server, xs);
	}
}

static void xwl_ready(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, xwl_ready);
	wlr_xwayland_set_seat(server->xwayland, server->seat);
	wlr_log(WLR_INFO, "XWayland pronto em %s", server->xwayland->display_name);
}

bool orbit_xwayland_bind(struct orbit_server *server, struct wlr_compositor *compositor) {
	server->xwayland = wlr_xwayland_create(server->display, compositor, true);
	if (!server->xwayland) {
		wlr_log(WLR_ERROR, "XWayland indisponível: os apps X11 (a Steam) não abrem");
		return false;
	}
	server->xwl_new_surface.notify = xwl_new_surface;
	wl_signal_add(&server->xwayland->events.new_surface, &server->xwl_new_surface);
	server->xwl_ready.notify = xwl_ready;
	wl_signal_add(&server->xwayland->events.ready, &server->xwl_ready);
	setenv("DISPLAY", server->xwayland->display_name, 1);
	wlr_log(WLR_INFO, "XWayland (sob demanda) em DISPLAY=%s", server->xwayland->display_name);
	return true;
}

void orbit_xwayland_finish(struct orbit_server *server) {
	if (server->xwayland) {
		wl_list_remove(&server->xwl_new_surface.link);
		wl_list_remove(&server->xwl_ready.link);
		wlr_xwayland_destroy(server->xwayland);
		server->xwayland = NULL;
	}
}

#else /* sem XWayland na wlroots */

bool orbit_xwl_steam_game(const struct orbit_view *view) {
	(void)view;
	return false;
}

bool orbit_xwayland_bind(struct orbit_server *server, struct wlr_compositor *compositor) {
	(void)server;
	(void)compositor;
	wlr_log(WLR_INFO, "esta wlroots foi compilada sem XWayland: só apps Wayland");
	return false;
}

void orbit_xwayland_finish(struct orbit_server *server) {
	(void)server;
}

#endif
