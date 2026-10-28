#include "orbit.h"

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/util/log.h>

/*
 * Espelho para uma barra cliente. Publica título, app id, foco,
 * maximizada, ecrã inteiro e a saída onde se vê (só quando está no
 * espaço visível dela: a dock esconde-se pela janela maximizada que
 * está à vista, não pela de outro espaço). Atende activar (como o
 * "foco" do orbit-msg: vai para o espaço da janela), fechar,
 * maximizar, minimizar e ecrã inteiro.
 */

static void handle_activate(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, foreign_activate);
	if (!view->mapped) {
		return;
	}
	if (view->output) {
		view->output->tag = view->tag;
	}
	orbit_arrange(view->server);
	orbit_focus(view);
	orbit_arrange(view->server);
}

static void handle_maximize(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, foreign_maximize);
	struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
	orbit_view_set_maximized(view, event->maximized);
}

static void handle_fullscreen(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, foreign_fullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
	orbit_view_set_fullscreen(view, event->fullscreen);
}

static void handle_minimize(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, foreign_minimize);
	struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
	orbit_view_set_minimized(view, event->minimized);
}

static void handle_close(struct wl_listener *listener, void *data) {
	struct orbit_view *view = wl_container_of(listener, view, foreign_close);
	wlr_xdg_toplevel_send_close(view->toplevel);
}

bool orbit_foreign_bind(struct orbit_server *server) {
	server->foreign_manager = wlr_foreign_toplevel_manager_v1_create(server->display);
	if (!server->foreign_manager) {
		wlr_log(WLR_ERROR, "falha ao criar o foreign toplevel");
		return false;
	}
	return true;
}

void orbit_foreign_map(struct orbit_view *view) {
	if (!view->server->foreign_manager || view->foreign || orbit_view_isolated(view)) {
		return;
	}
	view->foreign = wlr_foreign_toplevel_handle_v1_create(view->server->foreign_manager);
	if (!view->foreign) {
		wlr_log(WLR_ERROR, "sem handle de toplevel para o cliente");
		return;
	}
	view->foreign_output = NULL;
	view->foreign_activate.notify = handle_activate;
	wl_signal_add(&view->foreign->events.request_activate, &view->foreign_activate);
	view->foreign_close.notify = handle_close;
	wl_signal_add(&view->foreign->events.request_close, &view->foreign_close);
	view->foreign_maximize.notify = handle_maximize;
	wl_signal_add(&view->foreign->events.request_maximize, &view->foreign_maximize);
	view->foreign_fullscreen.notify = handle_fullscreen;
	wl_signal_add(&view->foreign->events.request_fullscreen, &view->foreign_fullscreen);
	view->foreign_minimize.notify = handle_minimize;
	wl_signal_add(&view->foreign->events.request_minimize, &view->foreign_minimize);
	orbit_foreign_sync(view);
}

void orbit_foreign_sync(struct orbit_view *view) {
	if (!view->mapped) {
		return;
	}
	/* as janelas de uma tela em Gaming ou Mídia não aparecem na dock nem no alternador; ao voltar, reaparecem */
	if (orbit_view_isolated(view) != (view->foreign == NULL)) {
		if (view->foreign) {
			orbit_foreign_destroy(view);
		} else {
			orbit_foreign_map(view);
		}
		return;
	}
	if (!view->foreign) {
		return;
	}
	const char *title = view->toplevel->title ? view->toplevel->title : "";
	const char *app = view->toplevel->app_id ? view->toplevel->app_id : "";
	wlr_foreign_toplevel_handle_v1_set_title(view->foreign, title);
	wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign, app);
	wlr_foreign_toplevel_handle_v1_set_activated(view->foreign, view->server->focused == view);
	wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign, view->fullscreen);
	wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign, view->maximized && !view->fullscreen);
	wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign, view->minimized);
	/* a saída só enquanto se vê nela (o espaço dela é o visível) */
	struct wlr_output *output = view->output && view->tag == view->output->tag ? view->output->wlr : NULL;
	if (view->foreign_output != output) {
		if (view->foreign_output) {
			wlr_foreign_toplevel_handle_v1_output_leave(view->foreign, view->foreign_output);
		}
		view->foreign_output = output;
		if (output) {
			wlr_foreign_toplevel_handle_v1_output_enter(view->foreign, output);
		}
	}
}

/* O wlroots 0.20 não deixa destruir o handle dentro de um pedido dele (ativar, fechar...): enquanto percorre os ouvintes de um sinal ele
 * põe marcadores na lista, e o handle confere que ela está vazia (assert). Um pedido da dock que mexe nas janelas chega aqui no meio
 * disso. Os nossos ouvintes saem já (eles apontam para a janela, que pode acabar); o handle em si sai no próximo ciclo do laço. */
static void foreign_destroy_depois(void *handle) {
	wlr_foreign_toplevel_handle_v1_destroy(handle);
}

void orbit_foreign_destroy(struct orbit_view *view) {
	if (!view->foreign) {
		return;
	}
	wl_list_remove(&view->foreign_activate.link);
	wl_list_remove(&view->foreign_close.link);
	wl_list_remove(&view->foreign_maximize.link);
	wl_list_remove(&view->foreign_fullscreen.link);
	wl_list_remove(&view->foreign_minimize.link);
	struct wlr_foreign_toplevel_handle_v1 *handle = view->foreign;
	view->foreign = NULL;
	view->foreign_output = NULL;
	wl_event_loop_add_idle(wl_display_get_event_loop(view->server->display), foreign_destroy_depois, handle);
}
