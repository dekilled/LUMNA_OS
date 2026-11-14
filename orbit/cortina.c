/*
 * A cortina preta do login. A sessão chama a animação de abertura primeiro, mas ela leva um instante para aparecer (o processo, o libmpv, o primeiro
 * quadro) e nesse instante o desktop já se mostrava por meio segundo. Com ORBIT_CORTINA=1 o Orbit cobre cada saída de preto, por cima de tudo, desde o
 * primeiro quadro; a cortina de uma saída sai quando a camada "lumna-abertura" daquela saída aparece (o primeiro quadro da abertura também é preto,
 * então não há emenda), ou em ORBIT_CORTINA_MS (3000 por omissão) se a abertura não vier, para nunca prender a tela.
 */
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "orbit.h"

static int cortina_prazo(void *data) {
	struct orbit_server *server = data;
	wlr_log(WLR_INFO, "cortina: o prazo acabou, descobrindo as telas");
	orbit_cortina_tirar(server, NULL);
	return 0;
}

void orbit_cortina_iniciar(struct orbit_server *server) {
	const char *on = getenv("ORBIT_CORTINA");
	if (!on || strcmp(on, "1") != 0 || !server->cortina_tree) {
		return;
	}
	server->cortina_ativa = true;
	int ms = 3000;
	const char *s = getenv("ORBIT_CORTINA_MS");
	if (s && atoi(s) > 0) {
		ms = atoi(s);
	}
	server->cortina_timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->display), cortina_prazo, server);
	if (server->cortina_timer) {
		wl_event_source_timer_update(server->cortina_timer, ms);
	}
	wlr_log(WLR_INFO, "cortina: ligada por até %d ms", ms);
}

void orbit_cortina_saida(struct orbit_server *server, struct orbit_output *output) {
	if (!server->cortina_ativa || output->cortina) {
		return;
	}
	struct wlr_box box = { 0 };
	wlr_output_layout_get_box(server->layout, output->wlr, &box);
	if (box.width <= 0 || box.height <= 0) {
		return;
	}
	float preto[4] = { 0, 0, 0, 1 };
	output->cortina = wlr_scene_rect_create(server->cortina_tree, box.width, box.height, preto);
	if (output->cortina) {
		wlr_scene_node_set_position(&output->cortina->node, box.x, box.y);
	}
}

void orbit_cortina_tirar(struct orbit_server *server, struct orbit_output *only) {
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if ((!only || only == output) && output->cortina) {
			wlr_scene_node_destroy(&output->cortina->node);
			output->cortina = NULL;
			wlr_log(WLR_INFO, "cortina: saída %s descoberta", output->wlr->name);
		}
	}
	if (!only) {
		server->cortina_ativa = false;
		if (server->cortina_timer) {
			wl_event_source_remove(server->cortina_timer);
			server->cortina_timer = NULL;
		}
	}
}

/* alguma saída ainda está coberta? */
bool orbit_cortina_ligada(struct orbit_server *server) {
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->cortina) {
			return true;
		}
	}
	return false;
}
