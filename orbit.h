#ifndef ORBIT_H
#define ORBIT_H

#define _POSIX_C_SOURCE 200112L

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/version.h>
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
#define ORBIT_CMD_LEN 192
#define ORBIT_MAX_BINDS 48
#define ORBIT_BORDER_DEFAULT 1
#define ORBIT_BORDER_MAX 8
#define ORBIT_ANIM_DEFAULT 120
#define ORBIT_ANIM_MAX 400
#define ORBIT_TAGS 4

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
struct orbit_layer;
struct wlr_layer_shell_v1;
struct wlr_foreign_toplevel_manager_v1;
struct wlr_foreign_toplevel_handle_v1;
struct wlr_session_lock_manager_v1;
struct wlr_idle_inhibit_manager_v1;
struct wlr_idle_notifier_v1;
struct wlr_gamma_control_manager_v1;
struct wlr_output_manager_v1;
struct wlr_output;

enum orbit_node_kind {
	ORBIT_NODE_VIEW = 0,
	ORBIT_NODE_LAYER = 1,
};

/* Igual a zwlr_layer_shell_v1_layer. A ordem da cena segue estes índices. */
enum {
	ORBIT_LAYER_BACKGROUND = 0,
	ORBIT_LAYER_BOTTOM = 1,
	ORBIT_LAYER_TOP = 2,
	ORBIT_LAYER_OVERLAY = 3,
	ORBIT_LAYER_COUNT = 4,
};

enum orbit_cursor_mode {
	ORBIT_CURSOR_PASSTHROUGH,
	ORBIT_CURSOR_MOVE,
	ORBIT_CURSOR_GESTURE,
};

enum orbit_layout {
	ORBIT_LAYOUT_MASTER,
	ORBIT_LAYOUT_MONOCLE,
	ORBIT_LAYOUT_SPIRAL,
	ORBIT_LAYOUT_DWINDLE,
};

enum orbit_action {
	ORBIT_QUIT,
	ORBIT_TERMINAL,
	ORBIT_CLOSE,
	ORBIT_FLOAT,
	ORBIT_FULLSCREEN,
	ORBIT_SPLIT,
	ORBIT_FOCUS_NEXT,
	ORBIT_FOCUS_PREV,
	ORBIT_MASTER_SHRINK,
	ORBIT_MASTER_GROW,
	ORBIT_OUTPUT_LEFT,
	ORBIT_OUTPUT_RIGHT,
	ORBIT_TAG,
	ORBIT_MOVE_TAG,
	ORBIT_SCREENSHOT,
	ORBIT_VOLUME_UP,
	ORBIT_VOLUME_DOWN,
	ORBIT_BRIGHTNESS_UP,
	ORBIT_BRIGHTNESS_DOWN,
};

struct orbit_bind {
	uint32_t mods;
	xkb_keysym_t sym;
	enum orbit_action action;
	int arg;
};

struct orbit_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_output_layout *layout;
	struct wlr_scene_output_layout *scene_layout;
	struct wl_listener new_output;
	struct wl_list outputs;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list views;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct wl_list layers;
	struct orbit_layer *exclusive_layer;
	struct wlr_scene_tree *layer_tree[ORBIT_LAYER_COUNT];
	struct wlr_scene_tree *tile_tree;
	struct wlr_scene_tree *full_tree; /* janelas em ecrã inteiro: por cima da barra e da dock, por baixo do overlay */
	struct wlr_scene_tree *lock_tree;

	struct wlr_foreign_toplevel_manager_v1 *foreign_manager;
	struct wlr_session_lock_manager_v1 *session_manager;
	struct wl_listener new_lock;
	void *session_lock;
	bool session_locked;
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit;
	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wl_listener new_inhibitor;
	struct wl_list inhibitors;
	struct wlr_gamma_control_manager_v1 *gamma_manager;
	struct wl_listener gamma_set;
	struct wlr_output_manager_v1 *output_manager;
	struct wl_listener output_apply;
	struct wl_listener output_test;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wl_listener touch_down;
	struct wl_listener touch_motion;
	struct wl_listener touch_up;
	struct wl_listener touch_cancel;
	struct wl_listener touch_frame;

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
	enum orbit_layout arrangement;
	float background[4];
	char terminal[ORBIT_CMD_LEN];
	char screenshot[ORBIT_CMD_LEN];
	char volume_up[ORBIT_CMD_LEN];
	char volume_down[ORBIT_CMD_LEN];
	char brightness_up[ORBIT_CMD_LEN];
	char brightness_down[ORBIT_CMD_LEN];
	struct orbit_bind binds[ORBIT_MAX_BINDS];
	int nbinds;
	bool phone_split;
	int border_px;
	float border_color[4];
	int anim_ms;
	struct wl_event_source *slide_timer;
	int gesture;
	bool gesture_done;
	double gesture_x, gesture_y;
	struct orbit_output *gesture_output;
	uint32_t next_view_id;
	bool virtual_touch;
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
	enum orbit_node_kind kind;
	uint32_t id; /* o do orbit-msg, nunca reaproveitado */
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
	bool maximized;  /* ocupa a área útil da saída (sem a barra e a dock); as outras ficam por baixo */
	bool mapped;
	struct wlr_scene_rect *border;
	struct wlr_foreign_toplevel_handle_v1 *foreign;
	struct wlr_output *foreign_output;
	struct wl_listener foreign_activate;
	struct wl_listener foreign_close;
	struct wl_listener foreign_maximize;
	struct wl_listener foreign_fullscreen;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	bool placed;
	bool sliding;
	int slide_x, slide_y;
	int slide_from_x, slide_from_y;
	int slide_to_x, slide_to_y;
	uint32_t slide_start;
	/* o último configure mandado por place(): só se manda outro quando algo muda (cada configure
	 * faz o app redesenhar e responder; mandá-los sem mudança era um ciclo sem fim) */
	bool cfg_valid;
	int cfg_w, cfg_h;
	bool cfg_full, cfg_max;
	uint32_t cfg_tiled;
};

struct wlr_layer_surface_v1;
struct wlr_scene_layer_surface_v1;

struct orbit_layer {
	enum orbit_node_kind kind;
	struct wl_list link;
	struct orbit_server *server;
	struct wlr_layer_surface_v1 *surface;
	struct wlr_scene_layer_surface_v1 *scene;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener scene_destroy; /* a cena pode ir antes da camada: esquecê-la */
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
	struct orbit_server *server;
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
void orbit_view_set_maximized(struct orbit_view *view, bool on);
void orbit_view_set_fullscreen(struct orbit_view *view, bool on);
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
void orbit_seat_capabilities(struct orbit_server *server);
void orbit_keyboard_add(struct orbit_server *server, struct wlr_keyboard *keyboard, bool shared);
bool orbit_gesture_begin(struct orbit_server *server, double lx, double ly);
void orbit_gesture_motion(struct orbit_server *server, double lx, double ly);
void orbit_gesture_end(struct orbit_server *server);

void orbit_touch_bind(struct orbit_server *server);
void orbit_touch_down(struct orbit_server *server, uint32_t time, int32_t id, double lx, double ly);
void orbit_touch_motion(struct orbit_server *server, uint32_t time, int32_t id, double lx, double ly);
void orbit_touch_up(struct orbit_server *server, uint32_t time, int32_t id);
void orbit_touch_frame(struct orbit_server *server);

bool orbit_im_bind(struct orbit_server *server);
bool orbit_im_forward_key(struct orbit_server *server, struct wlr_keyboard *keyboard,
	struct wlr_keyboard_key_event *event);
bool orbit_im_forward_modifiers(struct orbit_server *server, struct wlr_keyboard *keyboard);

bool orbit_ipc_bind(struct orbit_server *server, const char *wayland_display);
void orbit_ipc_finish(void);
void orbit_ipc_sync(struct orbit_server *server);
void orbit_ipc_view_event(struct orbit_view *view, const char *event);
void orbit_xkb_prepare(void);
void orbit_xkb_finish(void);

bool orbit_layers_bind(struct orbit_server *server);
struct wlr_box orbit_layer_usable(struct orbit_output *output, const struct wlr_box *full);
void orbit_layers_close_output(struct orbit_output *output);
void orbit_layer_offer_keyboard(struct wlr_surface *surface);

bool orbit_foreign_bind(struct orbit_server *server);
void orbit_foreign_map(struct orbit_view *view);
void orbit_foreign_sync(struct orbit_view *view);
void orbit_foreign_destroy(struct orbit_view *view);

bool orbit_session_bind(struct orbit_server *server);
void orbit_lock_configure(struct orbit_output *output);
void orbit_idle_activity(struct orbit_server *server);
void orbit_idle_update(struct orbit_server *server);

bool orbit_manage_bind(struct orbit_server *server);
void orbit_outputs_publish(struct orbit_server *server);
void orbit_borders_sync(struct orbit_server *server);
void orbit_slide(struct orbit_view *view, int x, int y);
void orbit_slide_stop(struct orbit_view *view);

bool orbit_keybinding(struct orbit_server *server, xkb_keysym_t sym, uint32_t mods);
bool orbit_action_named(struct orbit_server *server, const char *name);
void orbit_binds_reset(struct orbit_server *server);
bool orbit_bind_set(struct orbit_server *server, const char *action, const char *chord);

void orbit_config_load(struct orbit_server *server);

#endif
