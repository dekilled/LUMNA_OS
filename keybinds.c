#include "orbit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

static void spawn(const char *command) {
	if (!command || !command[0]) {
		return;
	}
	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", command, (char *)NULL);
		_exit(1);
	}
}

static void move_output(struct orbit_server *server, struct orbit_view *focused, int dir) {
	if (!focused || !focused->output) {
		return;
	}
	struct orbit_output *dest = adjacent_output(server, focused->output, dir);
	if (!dest) {
		return;
	}
	orbit_split_release(server, focused);
	focused->output = dest;
	focused->tag = dest->tag;
	focused->fullscreen = false;
	orbit_arrange(server);
	orbit_focus(focused);
}

static void go_tag(struct orbit_server *server, struct orbit_view *focused, int tag, bool move) {
	struct orbit_output *output = cursor_output(server);
	if (!output) {
		return;
	}
	if (move && focused) {
		focused->tag = tag;
		focused->fullscreen = false;
	}
	output->tag = tag;
	orbit_arrange(server);
	focus_top(server, output);
}

static void run_action(struct orbit_server *server, const struct orbit_bind *bind) {
	struct orbit_view *focused = server->focused;
	if (focused && !focused->mapped) {
		focused = NULL;
	}
	switch (bind->action) {
	case ORBIT_QUIT:
		wl_display_terminate(server->display);
		break;
	case ORBIT_TERMINAL:
		spawn(server->terminal);
		break;
	case ORBIT_CLOSE:
		if (focused) {
			wlr_xdg_toplevel_send_close(focused->toplevel);
		}
		break;
	case ORBIT_FLOAT:
		if (focused) {
			orbit_split_release(server, focused);
			focused->floating = !focused->floating;
			focused->fullscreen = false;
			orbit_arrange(server);
		}
		break;
	case ORBIT_FULLSCREEN:
		if (focused) {
			focused->fullscreen = !focused->fullscreen;
			if (focused->fullscreen) {
				focused->floating = false;
			}
			orbit_arrange(server);
		}
		break;
	case ORBIT_SPLIT:
		server->phone_split = !server->phone_split;
		server->split_top = NULL;
		server->split_bottom = NULL;
		orbit_arrange(server);
		break;
	case ORBIT_FOCUS_NEXT:
		orbit_focus_step(server, 1);
		break;
	case ORBIT_FOCUS_PREV:
		orbit_focus_step(server, -1);
		break;
	case ORBIT_MASTER_SHRINK:
		if (server->master_pct > ORBIT_MASTER_MIN) {
			server->master_pct -= 2;
		}
		orbit_arrange(server);
		break;
	case ORBIT_MASTER_GROW:
		if (server->master_pct < ORBIT_MASTER_MAX) {
			server->master_pct += 2;
		}
		orbit_arrange(server);
		break;
	case ORBIT_OUTPUT_LEFT:
		move_output(server, focused, -1);
		break;
	case ORBIT_OUTPUT_RIGHT:
		move_output(server, focused, 1);
		break;
	case ORBIT_TAG:
		go_tag(server, focused, bind->arg, false);
		break;
	case ORBIT_MOVE_TAG:
		go_tag(server, focused, bind->arg, true);
		break;
	case ORBIT_SCREENSHOT:
		spawn(server->screenshot);
		break;
	case ORBIT_VOLUME_UP:
		spawn(server->volume_up);
		break;
	case ORBIT_VOLUME_DOWN:
		spawn(server->volume_down);
		break;
	case ORBIT_BRIGHTNESS_UP:
		spawn(server->brightness_up);
		break;
	case ORBIT_BRIGHTNESS_DOWN:
		spawn(server->brightness_down);
		break;
	}
}

bool orbit_keybinding(struct orbit_server *server, xkb_keysym_t sym, uint32_t mods) {
	uint32_t clean = mods & ~(WLR_MODIFIER_CAPS | WLR_MODIFIER_MOD2);
	for (int i = 0; i < server->nbinds; i++) {
		const struct orbit_bind *bind = &server->binds[i];
		if (bind->sym != XKB_KEY_NoSymbol && bind->sym == sym && bind->mods == clean) {
			run_action(server, bind);
			return true;
		}
	}
	return false;
}

static void bind_put(struct orbit_server *server, uint32_t mods, xkb_keysym_t sym,
		enum orbit_action action, int arg) {
	for (int i = 0; i < server->nbinds; i++) {
		if (server->binds[i].action == action && server->binds[i].arg == arg) {
			server->binds[i].mods = mods;
			server->binds[i].sym = sym;
			return;
		}
	}
	if (server->nbinds >= ORBIT_MAX_BINDS) {
		wlr_log(WLR_ERROR, "limite de %d atalhos", ORBIT_MAX_BINDS);
		return;
	}
	server->binds[server->nbinds++] = (struct orbit_bind){
		.mods = mods,
		.sym = sym,
		.action = action,
		.arg = arg,
	};
}

void orbit_binds_reset(struct orbit_server *server) {
	server->nbinds = 0;
	uint32_t alt = WLR_MODIFIER_ALT;
	uint32_t alt_shift = WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT;
	bind_put(server, alt, XKB_KEY_Escape, ORBIT_QUIT, 0);
	bind_put(server, alt, XKB_KEY_Return, ORBIT_TERMINAL, 0);
	bind_put(server, alt, XKB_KEY_q, ORBIT_CLOSE, 0);
	bind_put(server, alt, XKB_KEY_space, ORBIT_FLOAT, 0);
	bind_put(server, alt, XKB_KEY_f, ORBIT_FULLSCREEN, 0);
	bind_put(server, alt, XKB_KEY_s, ORBIT_SPLIT, 0);
	bind_put(server, alt, XKB_KEY_j, ORBIT_FOCUS_NEXT, 0);
	bind_put(server, alt, XKB_KEY_k, ORBIT_FOCUS_PREV, 0);
	bind_put(server, alt, XKB_KEY_h, ORBIT_MASTER_SHRINK, 0);
	bind_put(server, alt, XKB_KEY_l, ORBIT_MASTER_GROW, 0);
	bind_put(server, alt, XKB_KEY_Left, ORBIT_OUTPUT_LEFT, 0);
	bind_put(server, alt, XKB_KEY_Right, ORBIT_OUTPUT_RIGHT, 0);
	for (int tag = 0; tag < 4; tag++) {
		bind_put(server, alt, XKB_KEY_1 + tag, ORBIT_TAG, tag);
		bind_put(server, alt_shift, XKB_KEY_1 + tag, ORBIT_MOVE_TAG, tag);
	}
}

struct orbit_action_name {
	const char *name;
	enum orbit_action action;
	int arg;
};

static const struct orbit_action_name action_names[] = {
	{ "terminar", ORBIT_QUIT, 0 },
	{ "terminal", ORBIT_TERMINAL, 0 },
	{ "fechar", ORBIT_CLOSE, 0 },
	{ "flutuar", ORBIT_FLOAT, 0 },
	{ "ecra", ORBIT_FULLSCREEN, 0 },
	{ "dividir", ORBIT_SPLIT, 0 },
	{ "foco_seguinte", ORBIT_FOCUS_NEXT, 0 },
	{ "foco_anterior", ORBIT_FOCUS_PREV, 0 },
	{ "mestre_menos", ORBIT_MASTER_SHRINK, 0 },
	{ "mestre_mais", ORBIT_MASTER_GROW, 0 },
	{ "saida_esquerda", ORBIT_OUTPUT_LEFT, 0 },
	{ "saida_direita", ORBIT_OUTPUT_RIGHT, 0 },
	{ "espaco_1", ORBIT_TAG, 0 },
	{ "espaco_2", ORBIT_TAG, 1 },
	{ "espaco_3", ORBIT_TAG, 2 },
	{ "espaco_4", ORBIT_TAG, 3 },
	{ "mover_1", ORBIT_MOVE_TAG, 0 },
	{ "mover_2", ORBIT_MOVE_TAG, 1 },
	{ "mover_3", ORBIT_MOVE_TAG, 2 },
	{ "mover_4", ORBIT_MOVE_TAG, 3 },
	{ "captura", ORBIT_SCREENSHOT, 0 },
	{ "volume_subir", ORBIT_VOLUME_UP, 0 },
	{ "volume_descer", ORBIT_VOLUME_DOWN, 0 },
	{ "brilho_subir", ORBIT_BRIGHTNESS_UP, 0 },
	{ "brilho_descer", ORBIT_BRIGHTNESS_DOWN, 0 },
};

static bool parse_chord(const char *chord, uint32_t *mods, xkb_keysym_t *sym) {
	char buf[128];
	snprintf(buf, sizeof(buf), "%s", chord);
	*mods = 0;
	*sym = XKB_KEY_NoSymbol;
	char *save = NULL;
	char *key = NULL;
	for (char *part = strtok_r(buf, "+", &save); part; part = strtok_r(NULL, "+", &save)) {
		while (*part == ' ' || *part == '\t') {
			part++;
		}
		char *end = part + strlen(part);
		while (end > part && (end[-1] == ' ' || end[-1] == '\t')) {
			*--end = '\0';
		}
		if (part[0] == '\0') {
			continue;
		}
		if (strcasecmp(part, "alt") == 0 || strcasecmp(part, "mod1") == 0) {
			*mods |= WLR_MODIFIER_ALT;
		} else if (strcasecmp(part, "shift") == 0) {
			*mods |= WLR_MODIFIER_SHIFT;
		} else if (strcasecmp(part, "ctrl") == 0 || strcasecmp(part, "control") == 0) {
			*mods |= WLR_MODIFIER_CTRL;
		} else if (strcasecmp(part, "logo") == 0 || strcasecmp(part, "super") == 0 ||
			strcasecmp(part, "mod4") == 0) {
			*mods |= WLR_MODIFIER_LOGO;
		} else if (key) {
			return false;
		} else {
			key = part;
		}
	}
	if (!key) {
		return false;
	}
	*sym = xkb_keysym_from_name(key, XKB_KEYSYM_CASE_INSENSITIVE);
	return *sym != XKB_KEY_NoSymbol;
}

/* Uma acção pelo nome do orbit.ini (o orbit-msg usa: acao terminal, acao mestre_mais...). */
bool orbit_action_named(struct orbit_server *server, const char *name) {
	for (size_t i = 0; i < sizeof(action_names) / sizeof(action_names[0]); i++) {
		if (strcasecmp(action_names[i].name, name) == 0) {
			struct orbit_bind bind = { .action = action_names[i].action, .arg = action_names[i].arg };
			run_action(server, &bind);
			return true;
		}
	}
	return false;
}

bool orbit_bind_set(struct orbit_server *server, const char *action, const char *chord) {
	const struct orbit_action_name *found = NULL;
	for (size_t i = 0; i < sizeof(action_names) / sizeof(action_names[0]); i++) {
		if (strcasecmp(action_names[i].name, action) == 0) {
			found = &action_names[i];
			break;
		}
	}
	if (!found) {
		return false;
	}
	if (!chord || !chord[0] || strcmp(chord, "-") == 0 || strcasecmp(chord, "nenhum") == 0) {
		bind_put(server, 0, XKB_KEY_NoSymbol, found->action, found->arg);
		return true;
	}
	uint32_t mods = 0;
	xkb_keysym_t sym = XKB_KEY_NoSymbol;
	if (!parse_chord(chord, &mods, &sym)) {
		wlr_log(WLR_ERROR, "atalho %s=%s inválido", action, chord);
		return false;
	}
	bind_put(server, mods, sym, found->action, found->arg);
	return true;
}
