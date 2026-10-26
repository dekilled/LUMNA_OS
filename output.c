#include "orbit.h"
#include "nested.h"

#include <wlr/backend/wayland.h>
#include <wlr/backend.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

bool output_is_phone(struct wlr_output *output) {
	const char *forced = getenv("ORBIT_PHONE");
	if (forced && output->name && strcmp(forced, output->name) == 0) {
		return true;
	}
	return output->name && strstr(output->name, "DSI") != NULL;
}

struct orbit_output *output_from_wlr(struct orbit_server *server, struct wlr_output *wlr) {
	if (!wlr) {
		return NULL;
	}
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr == wlr) {
			return output;
		}
	}
	return NULL;
}

struct orbit_output *output_at(struct orbit_server *server, double lx, double ly) {
	return output_from_wlr(server, wlr_output_layout_output_at(server->layout, lx, ly));
}

struct orbit_output *cursor_output(struct orbit_server *server) {
	if (wl_list_empty(&server->outputs)) {
		return NULL;
	}
	struct orbit_output *output = output_at(server, server->cursor->x, server->cursor->y);
	if (output) {
		return output;
	}
	return wl_container_of(server->outputs.next, output, link);
}

struct orbit_output *adjacent_output(struct orbit_server *server, struct orbit_output *from, int dir, bool saltar_ilhas) {
	struct wlr_box from_box;
	wlr_output_layout_get_box(server->layout, from->wlr, &from_box);
	int from_cx = from_box.x + from_box.width / 2;
	struct orbit_output *best = NULL;
	int best_dx = 0;
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output == from || (saltar_ilhas && orbit_output_isolated(output))) {
			continue;
		}
		struct wlr_box box;
		wlr_output_layout_get_box(server->layout, output->wlr, &box);
		int dx = (box.x + box.width / 2) - from_cx;
		if ((dir < 0 && dx >= 0) || (dir > 0 && dx <= 0)) {
			continue;
		}
		int adx = dx < 0 ? -dx : dx;
		int abd = best_dx < 0 ? -best_dx : best_dx;
		if (!best || adx < abd) {
			best = output;
			best_dx = dx;
		}
	}
	return best;
}

static void pin_touch(struct orbit_server *server) {
	struct orbit_output *phone = NULL;
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output_is_phone(output->wlr)) {
			phone = output;
			break;
		}
	}
	if (!phone) {
		return;
	}
	struct orbit_touch *touch;
	wl_list_for_each(touch, &server->touches, link) {
		wlr_cursor_map_input_to_output(server->cursor, touch->device, phone->wlr);
	}
}

static void output_frame(struct wl_listener *listener, void *data) {
	struct orbit_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(output->server->scene, output->wlr);
	if (!scene_output) {
		return;
	}
	wlr_scene_output_commit(scene_output, NULL);
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	struct orbit_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr, event->state);
	orbit_arrange(output->server);
}

static bool configure_output(struct orbit_output *output) {
	struct wlr_output *wlr = output->wlr;
	if (!wlr_output_init_render(wlr, output->server->allocator, output->server->renderer)) {
		wlr_log(WLR_ERROR, "saída %s: render falhou", wlr->name);
		return false;
	}
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}
	if (output_is_phone(wlr)) {
		wlr_output_state_set_scale(&state, output->server->phone_scale);
	}
	bool ok = wlr_output_commit_state(wlr, &state);
	wlr_output_state_finish(&state);
	if (!ok) {
		wlr_log(WLR_ERROR, "saída %s: commit do modo falhou", wlr->name);
	}
	return ok;
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct orbit_output *output = wl_container_of(listener, output, destroy);
	struct orbit_server *server = output->server;
	struct orbit_output *fallback = NULL;
	struct orbit_output *other;
	wl_list_for_each(other, &server->outputs, link) {
		if (other != output) {
			fallback = other;
			break;
		}
	}
	if (output_is_phone(output->wlr)) {
		server->phone_split = false;
		server->split_top = NULL;
		server->split_bottom = NULL;
		struct orbit_touch *touch;
		wl_list_for_each(touch, &server->touches, link) {
			wlr_cursor_map_input_to_output(server->cursor, touch->device, NULL);
		}
	}
	struct orbit_view *view;
	orbit_layers_close_output(output);
	wl_list_for_each(view, &server->views, link) {
		if (view->output == output) {
			orbit_split_release(server, view);
			view->output = fallback;
			view->fullscreen = false;
			if (fallback) {
				view->tag = fallback->tag;
			}
		}
	}
	wl_list_remove(&output->link);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wlr_log(WLR_INFO, "saída %s desligada", output->wlr->name);
	wlr_output_layout_remove(server->layout, output->wlr);
	if (output->bg) {
		wlr_scene_node_destroy(&output->bg->node);
	}
	free(output);
	orbit_outputs_publish(server);
	orbit_arrange(server);
	pin_touch(server);
}

static void server_new_output(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr = data;
	struct orbit_output *output = calloc(1, sizeof(*output));
	if (!output) {
		wlr_log(WLR_ERROR, "sem memória para a saída %s", wlr->name);
		return;
	}
	output->server = server;
	output->wlr = wlr;
	orbit_mode_init_output(server, output);
	if (!configure_output(output)) {
		free(output);
		return;
	}
	output->frame.notify = output_frame;
	wl_signal_add(&wlr->events.frame, &output->frame);
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr->events.request_state, &output->request_state);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr->events.destroy, &output->destroy);
	wl_list_insert(&server->outputs, &output->link);

	output->bg = wlr_scene_rect_create(server->layer_tree[ORBIT_LAYER_BACKGROUND], 1, 1, server->background);
	if (!output->bg) {
		wlr_log(WLR_ERROR, "saída %s: fundo da cena falhou", wlr->name);
	}
	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->layout, wlr);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
	/* aninhado: o Wayland de fora deixa os atalhos (o Super) para nós enquanto a janela do Orbit tem o foco */
	if (wlr_output_is_wl(wlr)) {
		/* um nome que dê para casar numa regra de janela do compositor de fora (KDE: Regras de janela) */
		char titulo[96];
		snprintf(titulo, sizeof(titulo), "LUMNA (Orbit) - %s", wlr->name);
		wlr_wl_output_set_title(wlr, titulo);
		wlr_wl_output_set_app_id(wlr, "lumna-orbit");
	}
	if (server->inhibit_host_shortcuts && wlr_output_is_wl(wlr)) {
		if (orbit_nested_inhibit(wlr_wl_backend_get_remote_display(wlr->backend), wlr_wl_output_get_surface(wlr))) {
			wlr_log(WLR_INFO, "%s: pedi ao compositor de fora que deixe os atalhos (o Super) comigo enquanto a janela tem o foco", wlr->name);
		} else {
			wlr_log(WLR_INFO, "%s: o compositor de fora não sabe inibir atalhos: o Super vai ser dele (use outro modificador em [atalho])", wlr->name);
		}
	}
	orbit_outputs_publish(server);
	wlr_log(WLR_INFO, "saída %s %dx%d scale %.2f%s", wlr->name, wlr->width, wlr->height, wlr->scale,
		output_is_phone(wlr) ? " telemóvel" : "");
	pin_touch(server);
	orbit_arrange(server);
}

void orbit_outputs_bind(struct orbit_server *server) {
	server->new_output.notify = server_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);
}
