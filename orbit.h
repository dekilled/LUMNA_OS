#ifndef ORBIT_H
#define ORBIT_H

#define _POSIX_C_SOURCE 200112L

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <xkbcommon/xkbcommon.h>

/* Tecto das listas em stack. O tamanho é de compilação; o aviso é em runtime. */
#define ORBIT_MAX_VIEWS 64
#define ORBIT_GAP_DEFAULT 8
#define ORBIT_GAP_MAX 64
#define ORBIT_MASTER_DEFAULT 56
#define ORBIT_MASTER_MIN 40
#define ORBIT_MASTER_MAX 70
#define ORBIT_PHONE_SCALE_DEFAULT 2.0f

/*
 * Registos do binário em português. O wlroots continua a escrever os dele
 * em inglês; os nossos wlr_log não misturam os dois.
 */

struct wlr_backend;
struct wlr_renderer;
struct wlr_allocator;
struct wlr_input_device;
struct wlr_surface;

struct orbit_view;
struct orbit_output;
struct orbit_server;

enum orbit_cursor_mode {
	ORBIT_CURSOR_PASSTHROUGH,
	ORBIT_CURSOR_MOVE,
};

struct orbit_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_output_layout *layout;
	struct wl_listener new_output;
	struct wl_list outputs;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list views;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	struct wl_list touches;

	enum orbit_cursor_mode cursor_mode;
	struct orbit_view *grabbed;
	struct orbit_view *focused;
	struct orbit_view *split_top;
	struct orbit_view *split_bottom;
	double grab_x, grab_y;
	int grab_gx, grab_gy;
	int master_pct;
	int gap;
	float phone_scale;
	bool phone_split;
	const char *term;
};

struct orbit_output {
	struct wl_list link;
	struct orbit_server *server;
	struct wlr_output *wlr;
	struct wlr_scene_rect *bg;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
	int tag;
	bool warned_near;
	bool warned_cap;
};

struct orbit_view {
	struct wl_list link;
	struct orbit_server *server;
	struct orbit_output *output;
	struct wlr_xdg_toplevel *toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	int x, y, w, h;
	int tag;
	bool floating;
	bool fullscreen;
	bool mapped;
};

struct orbit_keyboard {
	struct wl_list link;
	struct orbit_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct orbit_touch {
	struct wl_list link;
	struct wlr_input_device *device;
	struct wl_listener destroy;
};

struct orbit_popup {
	struct orbit_server *server;
	struct orbit_view *parent_view;
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

bool output_is_phone(struct wlr_output *output);
struct orbit_output *output_from_wlr(struct orbit_server *server, struct wlr_output *wlr);
struct orbit_output *output_at(struct orbit_server *server, double lx, double ly);
struct orbit_output *cursor_output(struct orbit_server *server);
struct orbit_output *adjacent_output(struct orbit_server *server, struct orbit_output *from, int dir);
void orbit_outputs_bind(struct orbit_server *server);

void orbit_views_bind(struct orbit_server *server);
void orbit_focus(struct orbit_view *view);
void orbit_unfocus(struct orbit_server *server);
void focus_top(struct orbit_server *server, struct orbit_output *output);
struct orbit_view *view_at(struct orbit_server *server, double lx, double ly,
	struct wlr_surface **surface, double *sx, double *sy);

void orbit_arrange(struct orbit_server *server);
void orbit_focus_step(struct orbit_server *server, int dir);
void orbit_split_release(struct orbit_server *server, struct orbit_view *view);
void phone_on_map(struct orbit_server *server, struct orbit_view *view, struct orbit_view *prev);

void orbit_input_bind(struct orbit_server *server);
void orbit_xkb_prepare(void);
void orbit_xkb_finish(void);

bool orbit_keybinding(struct orbit_server *server, xkb_keysym_t sym, uint32_t mods);

#endif
