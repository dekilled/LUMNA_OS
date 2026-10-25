#include "orbit.h"

#include <getopt.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* -S: o comando acabou? então o Orbit também (os outros filhos que acabam não contam) */
static pid_t comando_sessao;

static int filho_acabou(int sinal, void *dados) {
	int estado;
	pid_t p;
	while ((p = waitpid(-1, &estado, WNOHANG)) > 0) {
		if (p == comando_sessao) {
			wlr_log(WLR_INFO, "o comando da sessão acabou: o Orbit fecha");
			wl_display_terminate(dados);
		}
	}
	return 0;
}

int main(int argc, char *argv[]) {
	char *startup = NULL;
	bool sessao = false;
	int opt;
	while ((opt = getopt(argc, argv, "s:S:h")) != -1) {
		if (opt == 's' || opt == 'S') {
			startup = optarg;
			/* -S: o Orbit fecha quando o comando acaba (a tela de login: entrou, sai para o greetd
			 * abrir a sessão) */
			sessao = opt == 'S';
		} else {
			fprintf(stderr, "uso: %s [-s comando | -S comando]\n", argv[0]);
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
	orbit_touch_bind(&server);
	/* ponteiro e teclado desde o início: no telemóvel não há teclado físico, e sem isto as
	 * aplicações nem escutam o teclado virtual */
	orbit_seat_capabilities(&server);
	/* ORBIT_TOQUE_VIRTUAL=1: anuncia o toque sem ecrã táctil, para testar com o orbit-msg */
	const char *virtual_touch = getenv("ORBIT_TOQUE_VIRTUAL");
	if (virtual_touch && strcmp(virtual_touch, "1") == 0) {
		server.virtual_touch = true;
		orbit_seat_capabilities(&server);
	}
	orbit_xkb_prepare();
	if (!orbit_im_bind(&server)) {
		return fail(&server, "falha ao preparar a escrita (teclado virtual, método de entrada)");
	}

	const char *socket = wl_display_add_socket_auto(server.display);
	if (!socket) {
		return fail(&server, "falha ao abrir o socket Wayland");
	}
	if (!wlr_backend_start(server.backend)) {
		return fail(&server, "falha ao arrancar o backend");
	}
	setenv("WAYLAND_DISPLAY", socket, 1);
	orbit_ipc_bind(&server, socket);
	wlr_log(WLR_INFO, "WAYLAND_DISPLAY=%s intervalo %d mestre %d%% escala %.2f arranjo %s",
		socket, server.gap, server.master_pct, server.phone_scale, layout_name(server.arrangement));
	pid_t filho = startup ? fork() : -1;
	if (filho == 0) {
		execl("/bin/sh", "/bin/sh", "-c", startup, (char *)NULL);
		_exit(1);
	}
	struct wl_event_source *sinal = NULL;
	if (sessao && filho > 0) {
		comando_sessao = filho;
		sinal = wl_event_loop_add_signal(wl_display_get_event_loop(server.display), SIGCHLD, filho_acabou, server.display);
	}
	wl_display_run(server.display);
	if (sinal) {
		wl_event_source_remove(sinal);
	}
	orbit_ipc_finish();
	wl_display_destroy_clients(server.display);
	/* o wlroots confere que ninguém mais ouve estes sinais quando o display acaba */
	wl_list_remove(&server.new_lock.link);
	wl_list_remove(&server.new_inhibitor.link);
	wl_display_destroy(server.display);
	orbit_xkb_finish();
	return 0;
}
