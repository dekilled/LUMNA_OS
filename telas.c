/*
 * Os monitores que o Painel de controle mexe, em ~/.config/lumna/telas.ini (o Orbit vigia o arquivo).
 * Uma seção por saída, pelo nome que o Orbit mostra em `orbit-msg saidas`:
 *
 *   [DP-1]
 *   ligada=1
 *   modo=1920x1080@60
 *   escala=1.25
 *   rotacao=0|90|180|270
 *   x=0
 *   y=0
 *
 * O que faltar fica como está (o modo preferido, a escala 1, a posição automática). Nunca desliga a
 * última saída ligada: sem tela ninguém desfaz nada.
 */
#include "orbit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/backend/wayland.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

static char *trim(char *s) {
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	char *e = s + strlen(s);
	while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) {
		*--e = '\0';
	}
	return s;
}

void orbit_screens_config_load(struct orbit_server *server) {
	server->nscreens = 0;
	char dir[400], path[460];
	if (!orbit_lumna_dir(dir, sizeof(dir))) {
		return;
	}
	snprintf(path, sizeof(path), "%s/telas.ini", dir);
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	char line[256];
	struct orbit_screen_config *c = NULL;
	while (fgets(line, sizeof(line), f)) {
		char *p = trim(line);
		if (!*p || *p == '#' || *p == ';') {
			continue;
		}
		if (*p == '[') {
			char *end = strchr(p, ']');
			c = NULL;
			if (end && server->nscreens < ORBIT_MAX_SCREEN_CFG) {
				*end = '\0';
				c = &server->screens[server->nscreens++];
				memset(c, 0, sizeof(*c));
				snprintf(c->name, sizeof(c->name), "%s", p + 1);
				c->enabled = -1;
			}
			continue;
		}
		char *eq = strchr(p, '=');
		if (!c || !eq) {
			continue;
		}
		*eq = '\0';
		char *k = trim(p), *v = trim(eq + 1);
		if (!strcmp(k, "ligada")) {
			c->enabled = atoi(v) ? 1 : 0;
		} else if (!strcmp(k, "modo")) {
			int w = 0, h = 0, hz = 0;
			if (sscanf(v, "%dx%d@%d", &w, &h, &hz) >= 2 || sscanf(v, "%dx%d", &w, &h) == 2) {
				c->width = w;
				c->height = h;
				c->hz = hz;
			}
		} else if (!strcmp(k, "escala")) {
			double s = atof(v);
			c->scale = s >= 0.5 && s <= 4.0 ? s : 0;
		} else if (!strcmp(k, "rotacao")) {
			int r = atoi(v);
			c->rotation = (r == 90 || r == 180 || r == 270) ? r : 0;
		} else if (!strcmp(k, "x")) {
			c->x = atoi(v);
			c->has_pos = true;
		} else if (!strcmp(k, "y")) {
			c->y = atoi(v);
			c->has_pos = true;
		}
	}
	fclose(f);
}

struct orbit_output *orbit_output_find_any(struct orbit_server *server, const char *name) {
	struct orbit_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr->name && !strcmp(o->wlr->name, name)) {
			return o;
		}
	}
	wl_list_for_each(o, &server->off_outputs, link) {
		if (o->wlr->name && !strcmp(o->wlr->name, name)) {
			return o;
		}
	}
	return NULL;
}

static enum wl_output_transform transform_of(int rotation) {
	switch (rotation) {
	case 90: return WL_OUTPUT_TRANSFORM_90;
	case 180: return WL_OUTPUT_TRANSFORM_180;
	case 270: return WL_OUTPUT_TRANSFORM_270;
	default: return WL_OUTPUT_TRANSFORM_NORMAL;
	}
}

/* o modo mais perto do pedido (taxa em mHz; sem taxa pedida, a maior daquele tamanho) */
static struct wlr_output_mode *pick_mode(struct wlr_output *wlr, int w, int h, int hz) {
	struct wlr_output_mode *best = NULL, *mode;
	wl_list_for_each(mode, &wlr->modes, link) {
		if (mode->width != w || mode->height != h) {
			continue;
		}
		if (!best) {
			best = mode;
			continue;
		}
		if (hz > 0) {
			if (abs(mode->refresh - hz * 1000) < abs(best->refresh - hz * 1000)) {
				best = mode;
			}
		} else if (mode->refresh > best->refresh) {
			best = mode;
		}
	}
	return best;
}

static int enabled_count(struct orbit_server *server) {
	int n = 0;
	struct orbit_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		n++;
	}
	return n;
}

static void turn_off(struct orbit_server *server, struct orbit_output *output) {
	if (wlr_output_is_wl(output->wlr)) {
		wlr_log(WLR_INFO, "saída %s: é uma janela do Wayland de fora, não dá para desligar", output->wlr->name);
		return;
	}
	if (output->off || enabled_count(server) < 2) {
		if (!output->off) {
			wlr_log(WLR_INFO, "saída %s: é a única ligada, fica ligada", output->wlr->name);
		}
		return;
	}
	orbit_output_release(server, output, false);
	wl_list_remove(&output->link);
	wl_list_insert(&server->off_outputs, &output->link);
	output->off = true;
	wlr_output_layout_remove(server->layout, output->wlr);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, false);
	wlr_output_commit_state(output->wlr, &state);
	wlr_output_state_finish(&state);
	wlr_log(WLR_INFO, "saída %s desligada pelo Painel", output->wlr->name);
}

static void turn_on(struct orbit_server *server, struct orbit_output *output, const struct orbit_screen_config *c) {
	if (!output->off) {
		return;
	}
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(output->wlr);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_commit_state(output->wlr, &state);
	wlr_output_state_finish(&state);
	wl_list_remove(&output->link);
	wl_list_insert(&server->outputs, &output->link);
	output->off = false;
	struct wlr_output_layout_output *l = c && c->has_pos ? wlr_output_layout_add(server->layout, output->wlr, c->x, c->y)
	                                                    : wlr_output_layout_add_auto(server->layout, output->wlr);
	struct wlr_scene_output *so = wlr_scene_get_scene_output(server->scene, output->wlr);
	if (l && so) {
		wlr_scene_output_layout_add_output(server->scene_layout, l, so);
	}
	wlr_log(WLR_INFO, "saída %s ligada pelo Painel", output->wlr->name);
}

void orbit_screen_apply_one(struct orbit_server *server, struct orbit_output *output) {
	const struct orbit_screen_config *c = NULL;
	for (int i = 0; i < server->nscreens; i++) {
		if (output->wlr->name && !strcmp(server->screens[i].name, output->wlr->name)) {
			c = &server->screens[i];
			break;
		}
	}
	if (!c) {
		return;
	}
	if (c->enabled == 0) {
		turn_off(server, output);
		return;
	}
	if (c->enabled == 1) {
		turn_on(server, output, c);
	}
	if (output->off) {
		return;
	}
	struct wlr_output *wlr = output->wlr;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	if (c->width > 0 && c->height > 0) {
		struct wlr_output_mode *mode = pick_mode(wlr, c->width, c->height, c->hz);
		if (mode) {
			wlr_output_state_set_mode(&state, mode);
		} else if (wl_list_empty(&wlr->modes)) {
			wlr_output_state_set_custom_mode(&state, c->width, c->height, c->hz > 0 ? c->hz * 1000 : 0);
		} else {
			wlr_log(WLR_INFO, "saída %s: o modo %dx%d não existe, fica o que está", wlr->name, c->width, c->height);
		}
	}
	/* a escala do telemóvel é do orbit.ini, a não ser que o Painel diga outra */
	if (c->scale > 0) {
		wlr_output_state_set_scale(&state, (float)c->scale);
	}
	wlr_output_state_set_transform(&state, transform_of(c->rotation));
	if (!wlr_output_commit_state(wlr, &state)) {
		wlr_log(WLR_ERROR, "saída %s: o Painel pediu algo que a tela não aceitou", wlr->name);
	}
	wlr_output_state_finish(&state);
	if (c->has_pos) {
		wlr_output_layout_add(server->layout, wlr, c->x, c->y);
	}
}

void orbit_screens_apply(struct orbit_server *server) {
	/* ligar primeiro (uma saída desligada que volta), depois desligar: nunca zero ligadas no meio */
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < server->nscreens; i++) {
			struct orbit_output *o = orbit_output_find_any(server, server->screens[i].name);
			if (!o) {
				continue;
			}
			bool wants_off = server->screens[i].enabled == 0;
			if ((pass == 0) == wants_off) {
				continue;
			}
			orbit_screen_apply_one(server, o);
		}
	}
	orbit_outputs_publish(server);
	orbit_arrange(server);
}
