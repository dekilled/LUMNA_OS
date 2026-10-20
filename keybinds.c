#include "orbit.h"

#include <unistd.h>
#include <wlr/types/wlr_keyboard.h>

bool orbit_keybinding(struct orbit_server *server, xkb_keysym_t sym, uint32_t mods) {
	bool shift = mods & WLR_MODIFIER_SHIFT;
	struct orbit_view *focused = server->focused;
	if (focused && !focused->mapped) {
		focused = NULL;
	}
	struct orbit_output *output = cursor_output(server);
	switch (sym) {
	case XKB_KEY_Escape:
		wl_display_terminate(server->display);
		return true;
	case XKB_KEY_Return:
		if (fork() == 0) {
			const char *term = server->term ? server->term : "foot";
			execl("/bin/sh", "/bin/sh", "-c", term, (char *)NULL);
			_exit(1);
		}
		return true;
	case XKB_KEY_q:
		if (focused) {
			wlr_xdg_toplevel_send_close(focused->toplevel);
		}
		return true;
	case XKB_KEY_space:
		if (focused) {
			orbit_split_release(server, focused);
			focused->floating = !focused->floating;
			focused->fullscreen = false;
			orbit_arrange(server);
		}
		return true;
	case XKB_KEY_f:
		if (focused) {
			focused->fullscreen = !focused->fullscreen;
			if (focused->fullscreen) {
				focused->floating = false;
			}
			orbit_arrange(server);
		}
		return true;
	case XKB_KEY_s:
		server->phone_split = !server->phone_split;
		server->split_top = NULL;
		server->split_bottom = NULL;
		orbit_arrange(server);
		return true;
	case XKB_KEY_j:
	case XKB_KEY_k:
		orbit_focus_step(server, sym == XKB_KEY_j ? 1 : -1);
		return true;
	case XKB_KEY_h:
		if (server->master_pct > ORBIT_MASTER_MIN) {
			server->master_pct -= 2;
		}
		orbit_arrange(server);
		return true;
	case XKB_KEY_l:
		if (server->master_pct < ORBIT_MASTER_MAX) {
			server->master_pct += 2;
		}
		orbit_arrange(server);
		return true;
	case XKB_KEY_Left:
	case XKB_KEY_Right:
		if (focused && focused->output) {
			struct orbit_output *dest = adjacent_output(server, focused->output, sym == XKB_KEY_Left ? -1 : 1);
			if (dest) {
				orbit_split_release(server, focused);
				focused->output = dest;
				focused->tag = dest->tag;
				focused->fullscreen = false;
				orbit_arrange(server);
				orbit_focus(focused);
			}
		}
		return true;
	case XKB_KEY_1:
	case XKB_KEY_2:
	case XKB_KEY_3:
	case XKB_KEY_4: {
		int tag = sym - XKB_KEY_1;
		if (!output) {
			return true;
		}
		if (shift && focused) {
			focused->tag = tag;
			focused->fullscreen = false;
		}
		output->tag = tag;
		orbit_arrange(server);
		focus_top(server, output);
		return true;
	}
	default:
		return false;
	}
}
