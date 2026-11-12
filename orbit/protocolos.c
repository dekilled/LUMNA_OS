#include "orbit.h"

#include <stdlib.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/version.h>

#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8))
#include <wlr/types/wlr_ext_data_control_v1.h>
#endif

/*
 * Os protocolos que os apps de fora esperam de um compositor, além dos que o Orbit já falava (o layer-shell, o session-lock, o
 * input-method...). Cada um é pequeno; a trava do ponteiro é a que pesa (jogos em primeira pessoa, o Blender girando a vista).
 *
 *   primary-selection        a seleção do botão do meio (selecionar e colar com o meio)
 *   wlr-data-control         wl-clipboard e os gerenciadores de área de transferência, sem precisar de uma janela com foco
 *   cursor-shape             o app pede "a mãozinha" por nome, sem mandar uma imagem
 *   xdg-activation           um aviso (ou outro app) que pede para focar uma janela: traz a janela e o espaço dela
 *   single-pixel-buffer      uma cor sólida sem alocar imagem (o Firefox, o GTK4 usam)
 *   alpha-modifier           a transparência de uma superfície
 *   relative-pointer         o movimento cru do mouse, sem o limite da tela
 *   pointer-constraints      travar (lock) o ponteiro numa janela, ou confiná-lo (confine) dentro dela
 */

static struct {
	struct orbit_server *server;
	struct wl_listener request_primary;
	struct wl_listener request_shape;
	struct wl_listener request_activate;
	struct wlr_pointer_constraints_v1 *constraints;
	struct wlr_relative_pointer_manager_v1 *relative;
	struct wlr_pointer_constraint_v1 *active;
	double origin_x, origin_y; /* onde a superfície com o foco do ponteiro começa, no layout */
	double last_x, last_y;     /* onde o cursor estava antes do movimento de agora */
	struct wl_listener new_constraint;
	struct wl_listener pointer_focus;
	struct wl_listener keyboard_focus;
} protocols;

/* ---- as pequenas ---- */

static void request_set_primary_selection(struct wl_listener *listener, void *data) {
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(protocols.server->seat, event->source, event->serial);
	(void)listener;
}

static void request_set_shape(struct wl_listener *listener, void *data) {
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	struct orbit_server *server = protocols.server;
	/* só o cliente com o foco do ponteiro troca o desenho do cursor */
	if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER || server->seat->pointer_state.focused_client != event->seat_client) {
		return;
	}
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, wlr_cursor_shape_v1_name(event->shape));
	(void)listener;
}

static void request_activate(struct wl_listener *listener, void *data) {
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	struct orbit_server *server = protocols.server;
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->mapped && orbit_view_surface(view) == wlr_surface_get_root_surface(event->surface)) {
			if (!orbit_view_may_take_focus(view)) {
				return; /* no Gaming, com um jogo rodando, outra janela não o tira da frente sozinha */
			}
			if (!orbit_activation_may_switch(view)) {
				/* a janela é de outro modo (do Desktop, numa tela em Mídia ou Gaming; ou da Mídia, numa tela no Desktop): um app não troca o modo
				 * da tela sozinho. Fica como está, como no X11 (a pessoa a traz pela dock, pelo seletor ou pelo modo) */
				wlr_log(WLR_INFO, "ativação da janela %u ignorada: trocaria o modo da tela %s", view->id, view->output->wlr->name);
				return;
			}
			if (view->minimized) {
				orbit_view_set_minimized(view, false);
			}
			if (view->output) {
				view->output->tag = view->tag; /* o espaço onde a janela está passa para a frente */
			}
			orbit_arrange(server);
			orbit_focus(view);
			orbit_arrange(server);
			return;
		}
	}
	(void)listener;
}

/* ---- a trava do ponteiro ---- */

struct orbit_constraint {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
	struct wl_listener set_region;
};

/* um jogo que trava o ponteiro sugere onde ele fica ao soltar (cursor_hint): o cursor vai para lá em vez de aparecer onde parou */
static void warp_to_hint(struct wlr_pointer_constraint_v1 *constraint) {
	if (constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(protocols.server->cursor, NULL, protocols.origin_x + constraint->current.cursor_hint.x,
			protocols.origin_y + constraint->current.cursor_hint.y);
	}
}

static void deactivate(void) {
	struct wlr_pointer_constraint_v1 *constraint = protocols.active;
	if (!constraint) {
		return;
	}
	protocols.active = NULL; /* antes do send_deactivated: numa trava de uma vez só ele destrói a restrição */
	warp_to_hint(constraint);
	wlr_pointer_constraint_v1_send_deactivated(constraint);
}

/* Qual restrição vale agora: a da superfície que tem o foco do ponteiro E do teclado (um jogo em segundo plano não prende o mouse). */
static void reevaluate(void) {
	struct wlr_seat *seat = protocols.server->seat;
	struct wlr_surface *pointer = seat->pointer_state.focused_surface;
	struct wlr_surface *keyboard = seat->keyboard_state.focused_surface;
	struct wlr_pointer_constraint_v1 *wanted = NULL;
	if (pointer && keyboard && !protocols.server->session_locked && wlr_surface_get_root_surface(pointer) == wlr_surface_get_root_surface(keyboard)) {
		wanted = wlr_pointer_constraints_v1_constraint_for_surface(protocols.constraints, pointer, seat);
		if (!wanted) {
			wanted = wlr_pointer_constraints_v1_constraint_for_surface(protocols.constraints, wlr_surface_get_root_surface(pointer), seat);
		}
	}
	if (wanted == protocols.active) {
		return;
	}
	deactivate();
	if (wanted) {
		protocols.active = wanted;
		wlr_pointer_constraint_v1_send_activated(wanted);
	}
}

static void handle_pointer_focus(struct wl_listener *listener, void *data) {
	struct wlr_seat_pointer_focus_change_event *event = data;
	if (event->new_surface) {
		protocols.origin_x = protocols.server->cursor->x - event->sx;
		protocols.origin_y = protocols.server->cursor->y - event->sy;
	}
	reevaluate();
	(void)listener;
}

static void handle_keyboard_focus(struct wl_listener *listener, void *data) {
	reevaluate();
	(void)listener;
	(void)data;
}

static void constraint_destroy(struct wl_listener *listener, void *data) {
	struct orbit_constraint *c = wl_container_of(listener, c, destroy);
	if (protocols.active == c->constraint) {
		protocols.active = NULL;
		warp_to_hint(c->constraint);
	}
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_region.link);
	free(c);
	(void)data;
}

static void constraint_set_region(struct wl_listener *listener, void *data) {
	(void)listener;
	(void)data; /* o confinamento lê a região na hora de usar */
}

static void handle_new_constraint(struct wl_listener *listener, void *data) {
	struct wlr_pointer_constraint_v1 *constraint = data;
	struct orbit_constraint *c = calloc(1, sizeof(*c));
	if (!c) {
		return;
	}
	c->constraint = constraint;
	c->destroy.notify = constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &c->destroy);
	c->set_region.notify = constraint_set_region;
	wl_signal_add(&constraint->events.set_region, &c->set_region);
	reevaluate(); /* a janela que pede já pode estar com o foco */
	(void)listener;
}

/*
 * Chamado a cada movimento relativo do mouse, antes de o cursor andar. Manda o movimento cru aos apps (relative-pointer) e, com o ponteiro travado, diz que o
 * cursor NÃO deve andar (devolve true).
 */
bool orbit_pointer_motion(struct orbit_server *server, struct wlr_pointer_motion_event *event) {
	protocols.last_x = server->cursor->x;
	protocols.last_y = server->cursor->y;
	if (protocols.relative) {
		wlr_relative_pointer_manager_v1_send_relative_motion(protocols.relative, server->seat, (uint64_t)event->time_msec * 1000, event->delta_x,
			event->delta_y, event->unaccel_dx, event->unaccel_dy);
	}
	/* só vale com o mouse solto: durante um arrasto de janela (Alt+arrastar, redimensionar) o cursor anda normal */
	return protocols.active && protocols.active->type == WLR_POINTER_CONSTRAINT_V1_LOCKED && server->cursor_mode == ORBIT_CURSOR_PASSTHROUGH;
}

/* Depois de o cursor andar: com o ponteiro confinado, ele não sai da região (o resto do movimento vira só movimento relativo). */
void orbit_pointer_confine(struct orbit_server *server) {
	struct wlr_pointer_constraint_v1 *constraint = protocols.active;
	if (!constraint || constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED || server->cursor_mode != ORBIT_CURSOR_PASSTHROUGH) {
		return;
	}
	double from_x = protocols.last_x - protocols.origin_x, from_y = protocols.last_y - protocols.origin_y;
	double to_x = server->cursor->x - protocols.origin_x, to_y = server->cursor->y - protocols.origin_y;
	double x, y;
	/* sem uma região pedida a restrição vale para a janela inteira: corta pelo tamanho da superfície (a região "infinita" não serve para a conta); um pixel a menos, porque a borda de uma região já é fora da superfície */
	pixman_region32_t region;
	pixman_region32_init(&region);
	int margin = 0;
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->mapped && orbit_view_surface(view) == wlr_surface_get_root_surface(constraint->surface)) {
			margin = orbit_float_edge_margin(view); /* a faixa de mudar o tamanho de uma janela livre: a janela não vê o cursor ali */
			break;
		}
	}
	int w = constraint->surface->current.width, h = constraint->surface->current.height;
	pixman_region32_intersect_rect(&region, &constraint->region, margin, margin, w > 2 * margin + 1 ? w - 2 * margin - 1 : 1, h > 2 * margin + 1 ? h - 2 * margin - 1 : 1);
	bool ok = wlr_region_confine(&region, from_x, from_y, to_x, to_y, &x, &y);
	pixman_region32_fini(&region);
	if (!ok) {
		return;
	}
	if (x != to_x || y != to_y) {
		wlr_cursor_warp(server->cursor, NULL, protocols.origin_x + x, protocols.origin_y + y);
	}
}

bool orbit_protocols_bind(struct orbit_server *server) {
	protocols.server = server;
	struct wl_display *display = server->display;

	wlr_primary_selection_v1_device_manager_create(display);
	protocols.request_primary.notify = request_set_primary_selection;
	wl_signal_add(&server->seat->events.request_set_primary_selection, &protocols.request_primary);

	wlr_data_control_manager_v1_create(display);
#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8))
	wlr_ext_data_control_manager_v1_create(display, 1);
#endif

	struct wlr_cursor_shape_manager_v1 *shape = wlr_cursor_shape_manager_v1_create(display, 1);
	if (shape) {
		protocols.request_shape.notify = request_set_shape;
		wl_signal_add(&shape->events.request_set_shape, &protocols.request_shape);
	}

	struct wlr_xdg_activation_v1 *activation = wlr_xdg_activation_v1_create(display);
	if (activation) {
		protocols.request_activate.notify = request_activate;
		wl_signal_add(&activation->events.request_activate, &protocols.request_activate);
	}

	wlr_single_pixel_buffer_manager_v1_create(display);
	wlr_alpha_modifier_v1_create(display);

	protocols.relative = wlr_relative_pointer_manager_v1_create(display);
	protocols.constraints = wlr_pointer_constraints_v1_create(display);
	if (!protocols.relative || !protocols.constraints) {
		wlr_log(WLR_ERROR, "falha ao criar o ponteiro relativo e as travas do ponteiro");
		return false;
	}
	protocols.new_constraint.notify = handle_new_constraint;
	wl_signal_add(&protocols.constraints->events.new_constraint, &protocols.new_constraint);
	protocols.pointer_focus.notify = handle_pointer_focus;
	wl_signal_add(&server->seat->pointer_state.events.focus_change, &protocols.pointer_focus);
	protocols.keyboard_focus.notify = handle_keyboard_focus;
	wl_signal_add(&server->seat->keyboard_state.events.focus_change, &protocols.keyboard_focus);
	return true;
}
