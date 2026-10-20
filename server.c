#include "orbit.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

static const char *layout_name(enum orbit_layout layout) {
	switch (layout) {
	case ORBIT_LAYOUT_MONOCLE:
		return "monóculo";
	case ORBIT_LAYOUT_SPIRAL:
		return "espiral";
	case ORBIT_LAYOUT_DWINDLE:
		return "dwindle";
	default:
		return "mestre";
	}
}

static int fail(struct orbit_server *server, const char *msg) {
	wlr_log(WLR_ERROR, "%s", msg);
	if (server->display) {
		wl_display_destroy(server->display);
		server->display = NULL;
	}
	orbit_xkb_finish();
	return 1;
}

int main(int argc, char *argv[]) {
	char *startup = NULL;
	int opt;
	while ((opt = getopt(argc, argv, "s:h")) != -1) {
		if (opt == 's') {
			startup = optarg;
		} else {
			fprintf(stderr, "uso: %s [-s comando]\n", argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}

	wlr_log_init(WLR_INFO, NULL);
	struct orbit_server server = { 0 };
	orbit_config_load(&server);
	server.display = wl_display_create();
	if (!server.display) {
		wlr_log(WLR_ERROR, "falha ao criar o display Wayland");
		return 1;
	}

	struct wlr_session *session = NULL;
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.display), &session);
	(void)session;
	if (!server.backend) {
		return fail(&server, "falha ao criar o backend");
	}
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (!server.renderer) {
		return fail(&server, "falha ao criar o renderer");
	}
	wlr_renderer_init_wl_display(server.renderer, server.display);
	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (!server.allocator) {
		return fail(&server, "falha ao criar o allocator");
	}

	if (!wlr_compositor_create(server.display, 5, server.renderer)) {
		return fail(&server, "falha ao criar o compositor");
	}
	wlr_subcompositor_create(server.display);
	wlr_data_device_manager_create(server.display);
	wlr_viewporter_create(server.display);
	wlr_fractional_scale_manager_v1_create(server.display, 1);

	server.layout = wlr_output_layout_create(server.display);
	wlr_xdg_output_manager_v1_create(server.display, server.layout);
	wlr_screencopy_manager_v1_create(server.display);
	if (!server.layout) {
		return fail(&server, "falha ao criar o layout de saídas");
	}
	wl_list_init(&server.outputs);
	orbit_outputs_bind(&server);

	server.scene = wlr_scene_create();
	if (!server.scene) {
		return fail(&server, "falha ao criar a cena");
	}
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.layout);
	if (!orbit_layers_bind(&server)) {
		return fail(&server, "falha ao preparar o layer shell");
	}
	if (!orbit_foreign_bind(&server) || !orbit_session_bind(&server) || !orbit_manage_bind(&server)) {
		return fail(&server, "falha ao preparar os protocolos de sessão");
	}

	wl_list_init(&server.views);
	server.xdg_shell = wlr_xdg_shell_create(server.display, 3);
	if (!server.xdg_shell) {
		return fail(&server, "falha ao criar o xdg_shell");
	}
	orbit_views_bind(&server);

	server.cursor = wlr_cursor_create();
	if (!server.cursor) {
		return fail(&server, "falha ao criar o cursor");
	}
	wlr_cursor_attach_output_layout(server.cursor, server.layout);
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	if (!server.cursor_mgr) {
		return fail(&server, "falha ao criar o cursor X");
	}
	wlr_xcursor_manager_load(server.cursor_mgr, 1);
	wlr_xcursor_manager_load(server.cursor_mgr, 2);

	wl_list_init(&server.keyboards);
	wl_list_init(&server.touches);
	server.seat = wlr_seat_create(server.display, "seat0");
	if (!server.seat) {
		return fail(&server, "falha ao criar o seat0");
	}
	orbit_input_bind(&server);
	orbit_xkb_prepare();

	const char *socket = wl_display_add_socket_auto(server.display);
	if (!socket) {
		return fail(&server, "falha ao abrir o socket Wayland");
	}
	if (!wlr_backend_start(server.backend)) {
		return fail(&server, "falha ao arrancar o backend");
	}
	setenv("WAYLAND_DISPLAY", socket, 1);
	wlr_log(WLR_INFO, "WAYLAND_DISPLAY=%s intervalo %d mestre %d%% escala %.2f arranjo %s",
		socket, server.gap, server.master_pct, server.phone_scale, layout_name(server.arrangement));
	if (startup && fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", startup, (char *)NULL);
		_exit(1);
	}
	wl_display_run(server.display);
	wl_display_destroy_clients(server.display);
	wl_display_destroy(server.display);
	orbit_xkb_finish();
	return 0;
}
