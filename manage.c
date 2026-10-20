#include "orbit.h"

#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/util/log.h>

static void gamma_set(struct wl_listener *listener, void *data) {
	struct wlr_gamma_control_v1 *control = data;
	struct wlr_output *output = control->output;
	if (!output) {
		return;
	}
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	bool applied = wlr_gamma_control_v1_apply(control, &state);
	if (!applied || !wlr_output_commit_state(output, &state)) {
		wlr_gamma_control_v1_send_failed_and_destroy(control);
		wlr_log(WLR_ERROR, "gamma da saída %s falhou", output->name);
	}
	wlr_output_state_finish(&state);
	(void)listener;
}

static bool head_state(struct wlr_output *output, struct wlr_output_configuration_head_v1 *head, bool test) {
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, head->state.enabled);
	if (head->state.enabled) {
		if (head->state.mode) {
			wlr_output_state_set_mode(&state, head->state.mode);
		} else if (head->state.custom_mode.width > 0 && head->state.custom_mode.height > 0) {
			wlr_output_state_set_custom_mode(&state, head->state.custom_mode.width,
				head->state.custom_mode.height, head->state.custom_mode.refresh);
		}
		wlr_output_state_set_scale(&state, head->state.scale > 0 ? head->state.scale : 1);
		wlr_output_state_set_transform(&state, head->state.transform);
	}
	bool ok = test ? wlr_output_test_state(output, &state) : wlr_output_commit_state(output, &state);
	wlr_output_state_finish(&state);
	return ok;
}

static bool apply_config(struct orbit_server *server, struct wlr_output_configuration_v1 *config, bool test) {
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		if (!head->state.output) {
			continue;
		}
		if (!head_state(head->state.output, head, test)) {
			return false;
		}
		if (test) {
			continue;
		}
		if (!head->state.enabled) {
			wlr_output_layout_remove(server->layout, head->state.output);
			continue;
		}
		struct wlr_box box;
		wlr_output_layout_get_box(server->layout, head->state.output, &box);
		if (box.x != head->state.x || box.y != head->state.y) {
			wlr_output_layout_remove(server->layout, head->state.output);
			wlr_output_layout_add(server->layout, head->state.output, head->state.x, head->state.y);
		}
	}
	return true;
}

static void output_apply(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, output_apply);
	struct wlr_output_configuration_v1 *config = data;
	if (!apply_config(server, config, false)) {
		wlr_output_configuration_v1_send_failed(config);
		wlr_log(WLR_ERROR, "configuração de saídas recusada");
		return;
	}
	wlr_output_configuration_v1_send_succeeded(config);
	orbit_outputs_publish(server);
	orbit_arrange(server);
}

static void output_test(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, output_test);
	struct wlr_output_configuration_v1 *config = data;
	if (apply_config(server, config, true)) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
}

void orbit_outputs_publish(struct orbit_server *server) {
	if (!server->output_manager) {
		return;
	}
	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	if (!config) {
		return;
	}
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(config, output->wlr);
		if (!head) {
			continue;
		}
		struct wlr_box box;
		wlr_output_layout_get_box(server->layout, output->wlr, &box);
		head->state.enabled = output->wlr->enabled;
		head->state.mode = output->wlr->current_mode;
		head->state.x = box.x;
		head->state.y = box.y;
		head->state.scale = output->wlr->scale;
		head->state.transform = output->wlr->transform;
	}
	wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

bool orbit_manage_bind(struct orbit_server *server) {
	server->gamma_manager = wlr_gamma_control_manager_v1_create(server->display);
	if (!server->gamma_manager) {
		wlr_log(WLR_ERROR, "falha ao criar o gamma");
		return false;
	}
	server->gamma_set.notify = gamma_set;
	wl_signal_add(&server->gamma_manager->events.set_gamma, &server->gamma_set);
	server->output_manager = wlr_output_manager_v1_create(server->display);
	if (!server->output_manager) {
		wlr_log(WLR_ERROR, "falha ao criar o output management");
		return false;
	}
	server->output_apply.notify = output_apply;
	wl_signal_add(&server->output_manager->events.apply, &server->output_apply);
	server->output_test.notify = output_test;
	wl_signal_add(&server->output_manager->events.test, &server->output_test);
	return true;
}
