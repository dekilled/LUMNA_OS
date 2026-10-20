#include "orbit.h"

#include <stdlib.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

/*
 * Camadas, de baixo para cima: fundo, baixo, janelas, cima, overlay.
 * Uma exclusive zone positiva tira essa faixa da caixa das janelas.
 * Zero encosta na zona exclusiva sem a reservar (notificação).
 * -1 estica por baixo dos painéis (papel de parede).
 * O teclado no ecrã pede exclusive e encolhe o mosaico; a barra de cima
 * faz o mesmo. O overlay sem zona fica por cima, sem empurrar nada.
 */

static void layer_keyboard_enter(struct orbit_layer *layer) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(layer->server->seat);
	if (!layer->surface->surface) {
		return;
	}
	wlr_seat_keyboard_notify_enter(layer->server->seat, layer->surface->surface,
		keyboard ? keyboard->keycodes : NULL, keyboard ? keyboard->num_keycodes : 0,
		keyboard ? &keyboard->modifiers : NULL);
}

static void layer_sync_keyboard(struct orbit_layer *layer) {
	bool exclusive = layer->surface->surface->mapped &&
		layer->surface->current.keyboard_interactive ==
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
	if (exclusive) {
		layer->server->exclusive_layer = layer;
		layer_keyboard_enter(layer);
		return;
	}
	if (layer->server->exclusive_layer == layer) {
		layer->server->exclusive_layer = NULL;
		if (layer->server->focused) {
			orbit_focus(layer->server->focused);
		}
	}
}

static void layer_reparent(struct orbit_layer *layer) {
	int index = layer->surface->current.layer;
	if (index < 0 || index >= ORBIT_LAYER_COUNT) {
		index = ORBIT_LAYER_TOP;
	}
	wlr_scene_node_reparent(&layer->scene->tree->node, layer->server->layer_tree[index]);
}

static void layer_map(struct wl_listener *listener, void *data) {
	struct orbit_layer *layer = wl_container_of(listener, layer, map);
	const char *ns = layer->surface->namespace ? layer->surface->namespace : "?";
	const char *output = layer->surface->output ? layer->surface->output->name : "?";
	wlr_log(WLR_INFO, "camada %s na saída %s", ns, output);
	layer_sync_keyboard(layer);
	orbit_arrange(layer->server);
}

static void layer_unmap(struct wl_listener *listener, void *data) {
	struct orbit_layer *layer = wl_container_of(listener, layer, unmap);
	layer_sync_keyboard(layer);
	orbit_arrange(layer->server);
}

static void layer_commit(struct wl_listener *listener, void *data) {
	struct orbit_layer *layer = wl_container_of(listener, layer, commit);
	if (!layer->surface->output) {
		struct orbit_output *output = cursor_output(layer->server);
		if (output) {
			layer->surface->output = output->wlr;
		}
	}
	layer_reparent(layer);
	layer_sync_keyboard(layer);
	orbit_arrange(layer->server);
}

static void layer_destroy(struct wl_listener *listener, void *data) {
	struct orbit_layer *layer = wl_container_of(listener, layer, destroy);
	struct orbit_server *server = layer->server;
	if (layer->scene && layer->scene->tree) {
		layer->scene->tree->node.data = NULL;
	}
	wl_list_remove(&layer->map.link);
	wl_list_remove(&layer->unmap.link);
	wl_list_remove(&layer->commit.link);
	wl_list_remove(&layer->destroy.link);
	wl_list_remove(&layer->new_popup.link);
	wl_list_remove(&layer->link);
	if (server->exclusive_layer == layer) {
		server->exclusive_layer = NULL;
		if (server->focused) {
			orbit_focus(server->focused);
		}
	}
	free(layer);
	orbit_arrange(server);
}

struct orbit_layer_popup {
	struct orbit_layer *layer;
	struct wlr_xdg_popup *popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

static void layer_popup_unconstrain(struct orbit_layer_popup *popup) {
	struct orbit_layer *layer = popup->layer;
	if (!layer->surface->output || !layer->scene) {
		return;
	}
	struct wlr_box out;
	wlr_output_layout_get_box(layer->server->layout, layer->surface->output, &out);
	int lx = layer->scene->tree->node.x;
	int ly = layer->scene->tree->node.y;
	struct wlr_box box = {
		.x = out.x - lx,
		.y = out.y - ly,
		.width = out.width,
		.height = out.height,
	};
	wlr_xdg_popup_unconstrain_from_box(popup->popup, &box);
}

static void layer_popup_commit(struct wl_listener *listener, void *data) {
	struct orbit_layer_popup *popup = wl_container_of(listener, popup, commit);
	if (popup->popup->base->initial_commit) {
		layer_popup_unconstrain(popup);
	}
}

static void layer_popup_destroy(struct wl_listener *listener, void *data) {
	struct orbit_layer_popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void layer_new_popup(struct wl_listener *listener, void *data) {
	struct orbit_layer *layer = wl_container_of(listener, layer, new_popup);
	struct wlr_xdg_popup *xdg_popup = data;
	struct orbit_layer_popup *popup = calloc(1, sizeof(*popup));
	if (!popup) {
		wlr_log(WLR_ERROR, "sem memória para um popup de camada");
		return;
	}
	popup->layer = layer;
	popup->popup = xdg_popup;
	popup->commit.notify = layer_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
	popup->destroy.notify = layer_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
	if (layer->scene) {
		struct wlr_scene_tree *tree = wlr_scene_xdg_surface_create(layer->scene->tree, xdg_popup->base);
		if (!tree) {
			wlr_log(WLR_ERROR, "cena do popup de camada falhou");
		} else {
			xdg_popup->base->data = tree;
		}
	}
}

static void arrange_tree(struct orbit_output *output, struct wlr_scene_tree *tree,
		const struct wlr_box *full, struct wlr_box *usable, bool exclusive) {
	struct wlr_scene_node *node;
	wl_list_for_each(node, &tree->children, link) {
		struct orbit_layer *layer = node->data;
		if (!layer || layer->kind != ORBIT_NODE_LAYER || !layer->surface) {
			continue;
		}
		if (layer->surface->output != output->wlr) {
			continue;
		}
		if ((layer->surface->current.exclusive_zone > 0) != exclusive) {
			continue;
		}
		wlr_scene_layer_surface_v1_configure(layer->scene, full, usable);
	}
}

struct wlr_box orbit_layer_usable(struct orbit_output *output, const struct wlr_box *full) {
	struct wlr_box usable = *full;
	for (int pass = 0; pass < 2; pass++) {
		bool exclusive = pass == 0;
		for (int index = ORBIT_LAYER_COUNT - 1; index >= 0; index--) {
			arrange_tree(output, output->server->layer_tree[index], full, &usable, exclusive);
		}
	}
	if (usable.width < 1) {
		usable.width = 1;
	}
	if (usable.height < 1) {
		usable.height = 1;
	}
	return usable;
}

void orbit_layers_close_output(struct orbit_output *output) {
	struct orbit_layer *layer;
	struct orbit_layer *tmp;
	wl_list_for_each_safe(layer, tmp, &output->server->layers, link) {
		if (layer->surface && layer->surface->output == output->wlr) {
			wlr_layer_surface_v1_destroy(layer->surface);
		}
	}
}

static struct orbit_layer *orbit_layer_for_surface(struct wlr_surface *surface) {
	struct wlr_surface *current = surface;
	while (current) {
		struct wlr_layer_surface_v1 *layer = wlr_layer_surface_v1_try_from_wlr_surface(current);
		if (layer && layer->data) {
			return layer->data;
		}
		struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(current);
		if (!xdg || !xdg->popup) {
			return NULL;
		}
		current = xdg->popup->parent;
	}
	return NULL;
}

void orbit_layer_offer_keyboard(struct wlr_surface *surface) {
	struct orbit_layer *layer = orbit_layer_for_surface(surface);
	if (!layer || !layer->surface->surface->mapped) {
		return;
	}
	if (layer->surface->current.keyboard_interactive ==
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
		return;
	}
	layer_keyboard_enter(layer);
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *surface = data;
	if (!surface->output) {
		struct orbit_output *output = cursor_output(server);
		if (!output) {
			wlr_log(WLR_ERROR, "camada sem saída");
			wlr_layer_surface_v1_destroy(surface);
			return;
		}
		surface->output = output->wlr;
	}
	struct orbit_layer *layer = calloc(1, sizeof(*layer));
	if (!layer) {
		wlr_log(WLR_ERROR, "sem memória para uma camada");
		wlr_layer_surface_v1_destroy(surface);
		return;
	}
	layer->kind = ORBIT_NODE_LAYER;
	layer->server = server;
	layer->surface = surface;
	surface->data = layer;
	int index = surface->pending.layer;
	if (index < 0 || index >= ORBIT_LAYER_COUNT) {
		index = ORBIT_LAYER_TOP;
	}
	layer->scene = wlr_scene_layer_surface_v1_create(server->layer_tree[index], surface);
	if (!layer->scene) {
		wlr_log(WLR_ERROR, "cena da camada falhou");
		surface->data = NULL;
		free(layer);
		return;
	}
	layer->scene->tree->node.data = layer;
	layer->map.notify = layer_map;
	wl_signal_add(&surface->surface->events.map, &layer->map);
	layer->unmap.notify = layer_unmap;
	wl_signal_add(&surface->surface->events.unmap, &layer->unmap);
	layer->commit.notify = layer_commit;
	wl_signal_add(&surface->surface->events.commit, &layer->commit);
	layer->destroy.notify = layer_destroy;
	wl_signal_add(&surface->events.destroy, &layer->destroy);
	layer->new_popup.notify = layer_new_popup;
	wl_signal_add(&surface->events.new_popup, &layer->new_popup);
	wl_list_insert(&server->layers, &layer->link);
}

bool orbit_layers_bind(struct orbit_server *server) {
	server->layer_tree[ORBIT_LAYER_BACKGROUND] = wlr_scene_tree_create(&server->scene->tree);
	server->layer_tree[ORBIT_LAYER_BOTTOM] = wlr_scene_tree_create(&server->scene->tree);
	server->tile_tree = wlr_scene_tree_create(&server->scene->tree);
	server->layer_tree[ORBIT_LAYER_TOP] = wlr_scene_tree_create(&server->scene->tree);
	server->layer_tree[ORBIT_LAYER_OVERLAY] = wlr_scene_tree_create(&server->scene->tree);
	if (!server->layer_tree[ORBIT_LAYER_BACKGROUND] || !server->layer_tree[ORBIT_LAYER_BOTTOM] ||
		!server->tile_tree || !server->layer_tree[ORBIT_LAYER_TOP] ||
		!server->layer_tree[ORBIT_LAYER_OVERLAY]) {
		wlr_log(WLR_ERROR, "falha ao criar as árvores de camada");
		return false;
	}
	wl_list_init(&server->layers);
	server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
	if (!server->layer_shell) {
		wlr_log(WLR_ERROR, "falha ao criar o layer shell");
		return false;
	}
	server->new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);
	return true;
}
