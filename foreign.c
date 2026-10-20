#include "orbit.h"

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/util/log.h>

/*
 * Espelho para uma barra cliente. Publica título, app id, saída,
 * foco e ecrã inteiro. Os pedidos (activar, fechar, maximizar)
 * não mudam a política: quem manda continua a ser o Orbit.
 */

bool orbit_foreign_bind(struct orbit_server *server) {
	server->foreign_manager = wlr_foreign_toplevel_manager_v1_create(server->display);
	if (!server->foreign_manager) {
		wlr_log(WLR_ERROR, "falha ao criar o foreign toplevel");
		return false;
	}
	return true;
}

void orbit_foreign_map(struct orbit_view *view) {
	if (!view->server->foreign_manager || view->foreign) {
		return;
	}
	view->foreign = wlr_foreign_toplevel_handle_v1_create(view->server->foreign_manager);
	if (!view->foreign) {
		wlr_log(WLR_ERROR, "sem handle de toplevel para o cliente");
		return;
	}
	view->foreign_output = NULL;
	orbit_foreign_sync(view);
}

void orbit_foreign_sync(struct orbit_view *view) {
	if (!view->foreign || !view->mapped) {
		return;
	}
	const char *title = view->toplevel->title ? view->toplevel->title : "";
	const char *app = view->toplevel->app_id ? view->toplevel->app_id : "";
	wlr_foreign_toplevel_handle_v1_set_title(view->foreign, title);
	wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign, app);
	wlr_foreign_toplevel_handle_v1_set_activated(view->foreign, view->server->focused == view);
	wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign, view->fullscreen);
	wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign, false);
	struct wlr_output *output = view->output ? view->output->wlr : NULL;
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

void orbit_foreign_destroy(struct orbit_view *view) {
	if (!view->foreign) {
		return;
	}
	wlr_foreign_toplevel_handle_v1_destroy(view->foreign);
	view->foreign = NULL;
	view->foreign_output = NULL;
}
