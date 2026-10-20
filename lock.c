#include "orbit.h"

#include <stdlib.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/log.h>

struct orbit_lock {
	struct orbit_server *server;
	struct wlr_session_lock_v1 *lock;
	bool unlocked;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
};

struct orbit_lock_surface {
	struct orbit_lock *lock;
	struct wlr_session_lock_surface_v1 *surface;
	struct wlr_scene_tree *tree;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct orbit_inhibitor {
	struct wl_list link;
	struct orbit_server *server;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	struct wl_listener destroy;
};

static void blank_session(struct orbit_server *server, bool locked) {
	server->session_locked = locked;
	wlr_scene_node_set_enabled(&server->tile_tree->node, !locked);
	for (int i = 0; i < ORBIT_LAYER_COUNT; i++) {
		wlr_scene_node_set_enabled(&server->layer_tree[i]->node, !locked);
	}
}

static void lock_surface_place(struct orbit_lock_surface *surface) {
	if (!surface->surface->output || !surface->tree) {
		return;
	}
	struct wlr_box box;
	wlr_output_layout_get_box(surface->lock->server->layout, surface->surface->output, &box);
	if (box.width < 1 || box.height < 1) {
		return;
	}
	wlr_session_lock_surface_v1_configure(surface->surface, (uint32_t)box.width, (uint32_t)box.height);
	wlr_scene_node_set_position(&surface->tree->node, box.x, box.y);
}

static void lock_surface_commit(struct wl_listener *listener, void *data) {
	struct orbit_lock_surface *surface = wl_container_of(listener, surface, commit);
	lock_surface_place(surface);
}

static void lock_surface_destroy(struct wl_listener *listener, void *data) {
	struct orbit_lock_surface *surface = wl_container_of(listener, surface, destroy);
	wl_list_remove(&surface->commit.link);
	wl_list_remove(&surface->destroy.link);
	if (surface->tree) {
		wlr_scene_node_destroy(&surface->tree->node);
	}
	if (surface->surface) {
		surface->surface->data = NULL;
	}
	free(surface);
}

static void lock_new_surface(struct wl_listener *listener, void *data) {
	struct orbit_lock *lock = wl_container_of(listener, lock, new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	struct orbit_lock_surface *surface = calloc(1, sizeof(*surface));
	if (!surface) {
		wlr_log(WLR_ERROR, "sem memória para a superfície de bloqueio");
		return;
	}
	surface->lock = lock;
	surface->surface = lock_surface;
	lock_surface->data = surface;
	surface->tree = wlr_scene_subsurface_tree_create(lock->server->lock_tree, lock_surface->surface);
	if (!surface->tree) {
		wlr_log(WLR_ERROR, "cena do bloqueio falhou");
	}
	surface->commit.notify = lock_surface_commit;
	wl_signal_add(&lock_surface->surface->events.commit, &surface->commit);
	surface->destroy.notify = lock_surface_destroy;
	wl_signal_add(&lock_surface->events.destroy, &surface->destroy);
	lock_surface_place(surface);
}

static void lock_unlock(struct wl_listener *listener, void *data) {
	struct orbit_lock *lock = wl_container_of(listener, lock, unlock);
	lock->unlocked = true;
	blank_session(lock->server, false);
	orbit_arrange(lock->server);
	wlr_log(WLR_INFO, "sessão desbloqueada");
}

static void lock_destroy(struct wl_listener *listener, void *data) {
	struct orbit_lock *lock = wl_container_of(listener, lock, destroy);
	struct orbit_server *server = lock->server;
	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);
	if (server->session_lock == lock) {
		server->session_lock = NULL;
	}
	if (!lock->unlocked) {
		blank_session(server, true);
		wlr_log(WLR_INFO, "o cliente de bloqueio saiu; a sessão continua fechada");
	}
	free(lock);
}

static void server_new_lock(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, new_lock);
	struct wlr_session_lock_v1 *session = data;
	if (server->session_lock) {
		wlr_log(WLR_ERROR, "já há um bloqueio de sessão");
		wlr_session_lock_v1_destroy(session);
		return;
	}
	struct orbit_lock *lock = calloc(1, sizeof(*lock));
	if (!lock) {
		wlr_session_lock_v1_destroy(session);
		return;
	}
	lock->server = server;
	lock->lock = session;
	session->data = lock;
	server->session_lock = lock;
	lock->new_surface.notify = lock_new_surface;
	wl_signal_add(&session->events.new_surface, &lock->new_surface);
	lock->unlock.notify = lock_unlock;
	wl_signal_add(&session->events.unlock, &lock->unlock);
	lock->destroy.notify = lock_destroy;
	wl_signal_add(&session->events.destroy, &lock->destroy);
	blank_session(server, true);
	wlr_session_lock_v1_send_locked(session);
	wlr_log(WLR_INFO, "sessão bloqueada");
}

void orbit_lock_configure(struct orbit_output *output) {
	struct orbit_lock *lock = output->server->session_lock;
	if (!lock) {
		return;
	}
	struct wlr_session_lock_surface_v1 *surface;
	wl_list_for_each(surface, &lock->lock->surfaces, link) {
		if (surface->output == output->wlr && surface->data) {
			lock_surface_place(surface->data);
		}
	}
}

static void inhibitor_destroy(struct wl_listener *listener, void *data) {
	struct orbit_inhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
	struct orbit_server *server = inhibitor->server;
	wl_list_remove(&inhibitor->destroy.link);
	wl_list_remove(&inhibitor->link);
	free(inhibitor);
	orbit_idle_update(server);
}

static void server_new_inhibitor(struct wl_listener *listener, void *data) {
	struct orbit_server *server = wl_container_of(listener, server, new_inhibitor);
	struct wlr_idle_inhibitor_v1 *idle = data;
	struct orbit_inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
	if (!inhibitor) {
		return;
	}
	inhibitor->server = server;
	inhibitor->inhibitor = idle;
	inhibitor->destroy.notify = inhibitor_destroy;
	wl_signal_add(&idle->events.destroy, &inhibitor->destroy);
	wl_list_insert(&server->inhibitors, &inhibitor->link);
	orbit_idle_update(server);
}

void orbit_idle_activity(struct orbit_server *server) {
	if (server->idle_notifier && server->seat) {
		wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
	}
}

void orbit_idle_update(struct orbit_server *server) {
	if (!server->idle_notifier) {
		return;
	}
	bool inhibited = false;
	struct orbit_inhibitor *inhibitor;
	wl_list_for_each(inhibitor, &server->inhibitors, link) {
		struct wlr_surface *surface = inhibitor->inhibitor->surface;
		if (surface && surface->mapped) {
			inhibited = true;
			break;
		}
	}
	wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, inhibited);
}

bool orbit_session_bind(struct orbit_server *server) {
	server->lock_tree = wlr_scene_tree_create(&server->scene->tree);
	if (!server->lock_tree) {
		wlr_log(WLR_ERROR, "falha ao criar a árvore de bloqueio");
		return false;
	}
	server->session_manager = wlr_session_lock_manager_v1_create(server->display);
	if (!server->session_manager) {
		wlr_log(WLR_ERROR, "falha ao criar o session lock");
		return false;
	}
	server->new_lock.notify = server_new_lock;
	wl_signal_add(&server->session_manager->events.new_lock, &server->new_lock);
	wl_list_init(&server->inhibitors);
	server->idle_inhibit = wlr_idle_inhibit_v1_create(server->display);
	server->idle_notifier = wlr_idle_notifier_v1_create(server->display);
	if (!server->idle_inhibit || !server->idle_notifier) {
		wlr_log(WLR_ERROR, "falha ao criar o idle");
		return false;
	}
	server->new_inhibitor.notify = server_new_inhibitor;
	wl_signal_add(&server->idle_inhibit->events.new_inhibitor, &server->new_inhibitor);
	return true;
}
