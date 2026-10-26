#include "orbit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wlr/util/log.h>

/*
 * Os modos do desktop: Desktop, Gaming e Mídia. Cada saída tem um modo, e cada modo tem o seu
 * espaço de trabalho (Desktop usa os numerados 1-4; Gaming e Mídia, um espaço próprio cada), por
 * isso trocar de modo nunca fecha nem move janela: as de cada modo ficam onde estavam. O modo de
 * uma saída é só o espaço em que ela está (output->tag), então trocar de espaço por fora (o
 * foreign-toplevel da dock, Alt+1) mantém o modo certo, e o evento "modo" sai de qualquer jeito.
 *
 * Layout: "tela" (um modo por tela: só a saída em foco troca) ou "estender" (todas trocam juntas).
 * Vem de ~/.config/lumna/modos.ini ([geral] layout e ao_ligar) e muda em voo com `orbit-msg modo_layout`.
 * No telemóvel não há modos: fica sempre no Desktop.
 */

static const char *const nomes[ORBIT_MODES] = { "desktop", "gaming", "midia" };

const char *orbit_mode_name(int mode) {
	return mode >= 0 && mode < ORBIT_MODES ? nomes[mode] : "desktop";
}

int orbit_mode_parse(const char *text) {
	if (!text) {
		return -1;
	}
	for (int i = 0; i < ORBIT_MODES; i++) {
		if (!strcasecmp(text, nomes[i])) {
			return i;
		}
	}
	if (!strcasecmp(text, "media") || !strcasecmp(text, "mídia")) {
		return ORBIT_MODE_MEDIA;
	}
	return -1;
}

static int mode_of_tag(int tag) {
	return tag < ORBIT_TAGS ? ORBIT_MODE_DESKTOP : tag - ORBIT_TAGS + 1;
}

/* o espaço de um modo nesta saída (o Desktop volta ao numerado em que estava) */
static int tag_of_mode(struct orbit_output *output, int mode) {
	return mode == ORBIT_MODE_DESKTOP ? output->desktop_tag : ORBIT_TAGS + mode - 1;
}

/* o modo de uma saída acompanha o espaço dela (o arranjo precisa dele já: Gaming e Mídia são de tela cheia) */
void orbit_mode_sync_output(struct orbit_output *output) {
	if (output->tag < ORBIT_TAGS) {
		output->desktop_tag = output->tag;
	}
	output->mode = mode_of_tag(output->tag);
}

/* e o evento "modo" sai depois de cada arranjo, pelo orbit_ipc_sync */
void orbit_mode_follow(struct orbit_server *server) {
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		orbit_mode_sync_output(output);
	}
}

static void write_mode_file(int mode) {
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char dir[400], path[460];
	if (config && *config) {
		snprintf(dir, sizeof(dir), "%s/lumna", config);
	} else if (home) {
		snprintf(dir, sizeof(dir), "%s/.config/lumna", home);
	} else {
		return;
	}
	snprintf(path, sizeof(path), "%s/modo", dir);
	char tmp[480];
	snprintf(tmp, sizeof(tmp), "%s.novo", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	fprintf(f, "%s\n", orbit_mode_name(mode));
	fclose(f);
	rename(tmp, path);
}

static void spawn(const char *command) {
	if (!command || !command[0]) {
		return;
	}
	if (fork() == 0) {
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", command, (char *)NULL);
		_exit(1);
	}
}

bool orbit_output_isolated(const struct orbit_output *output) {
	return output && output->mode != ORBIT_MODE_DESKTOP && !output_is_phone(output->wlr);
}

bool orbit_view_isolated(const struct orbit_view *view) {
	return view && orbit_output_isolated(view->output);
}

static bool is_mode_output(struct orbit_output *output) {
	return output && !output_is_phone(output->wlr);
}

/* troca o modo: da saída dada (a em foco, se NULL) ou de todas, no layout "estender" */
void orbit_mode_set(struct orbit_server *server, struct orbit_output *from, int mode) {
	if (mode < 0 || mode >= ORBIT_MODES) {
		return;
	}
	if (!from) {
		from = cursor_output(server);
	}
	if (!is_mode_output(from)) {
		return;
	}
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (!is_mode_output(output) || (server->mode_extend ? false : output != from)) {
			continue;
		}
		int tag = tag_of_mode(output, mode);
		if (output->tag != tag) {
			if (output->tag < ORBIT_TAGS) {
				output->desktop_tag = output->tag;
			}
			output->tag = tag;
		}
	}
	orbit_arrange(server);
	wl_list_for_each(output, &server->outputs, link) {
		if (is_mode_output(output) && (server->mode_extend || output == from)) {
			focus_top(server, output);
		}
	}
	write_mode_file(from->mode);
	/* o seletor aparece mesmo se o modo já era esse (Super+Tab de novo), na saída que trocou */
	setenv("LUMNA_SAIDA", from->wlr->name, 1);
	setenv("LUMNA_MODO", orbit_mode_name(from->mode), 1);
	spawn(server->mode_osd);
}

void orbit_mode_step(struct orbit_server *server, int dir) {
	struct orbit_output *from = cursor_output(server);
	if (!is_mode_output(from)) {
		return;
	}
	orbit_mode_set(server, from, (from->mode + dir + ORBIT_MODES) % ORBIT_MODES);
}

void orbit_mode_set_extend(struct orbit_server *server, bool extend) {
	server->mode_extend = extend;
	if (extend) {
		/* juntar: todas seguem a saída em foco */
		struct orbit_output *from = cursor_output(server);
		if (is_mode_output(from)) {
			orbit_mode_set(server, from, from->mode);
		}
	}
}

/*
 * Liga e desliga espaços do Desktop (1 a ORBIT_TAGS). O que estava num espaço desligado vai para o
 * último que ficou ligado: nenhuma janela se perde.
 */
void orbit_areas_set(struct orbit_server *server, int count) {
	count = count < 1 ? 1 : count > ORBIT_TAGS ? ORBIT_TAGS : count;
	server->areas = count;
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->tag >= count && view->tag < ORBIT_TAGS) {
			orbit_split_release(server, view);
			view->tag = count - 1;
			view->fullscreen = false;
		}
	}
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->desktop_tag >= count) {
			output->desktop_tag = count - 1;
		}
		if (output->tag >= count && output->tag < ORBIT_TAGS) {
			output->tag = count - 1;
		}
	}
	orbit_arrange(server);
}

/* ~/.config/lumna/modos.ini: [geral] layout = tela|estender, ao_ligar = desktop|gaming|midia, areas = 1-3 */
void orbit_mode_config_load(struct orbit_server *server) {
	server->mode_extend = false;
	server->mode_boot = ORBIT_MODE_DESKTOP;
	server->areas = ORBIT_TAGS;
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char path[460];
	if (config && *config) {
		snprintf(path, sizeof(path), "%s/lumna/modos.ini", config);
	} else if (home) {
		snprintf(path, sizeof(path), "%s/.config/lumna/modos.ini", home);
	} else {
		return;
	}
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	char line[256];
	bool general = true; /* o que vier antes de qualquer [secção] também vale */
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (*p == '[') {
			general = !strncmp(p, "[geral]", 7);
			continue;
		}
		char *eq = strchr(p, '=');
		if (!general || !eq || *p == '#' || *p == ';') {
			continue;
		}
		*eq = '\0';
		char *key = p, *value = eq + 1;
		for (char *e = key + strlen(key); e > key && (e[-1] == ' ' || e[-1] == '\t'); e--) {
			e[-1] = '\0';
		}
		while (*value == ' ' || *value == '\t') {
			value++;
		}
		value[strcspn(value, " \t\r\n")] = '\0';
		if (!strcmp(key, "layout")) {
			server->mode_extend = !strcmp(value, "estender");
		} else if (!strcmp(key, "ao_ligar")) {
			int m = orbit_mode_parse(value);
			server->mode_boot = m >= 0 ? m : ORBIT_MODE_DESKTOP;
		}
	}
	fclose(f);
}

/* uma saída nova começa no modo de "ao ligar" (e as que já havia não mexem) */
void orbit_mode_init_output(struct orbit_server *server, struct orbit_output *output) {
	output->desktop_tag = 0;
	output->tag = 0;
	if (is_mode_output(output) && server->mode_boot != ORBIT_MODE_DESKTOP && !server->mode_booted) {
		output->tag = tag_of_mode(output, server->mode_boot);
	}
	output->shown_tag = output->tag; /* a saída nova aparece no lugar, sem deslizar */
	server->mode_booted = true;
	output->mode = mode_of_tag(output->tag);
}

/* as janelas que pertencem a um modo pelo app_id (começa por): a Mídia do LUMNA, e os jogos e lançadores do Gaming */
static int family_mode(const char *app_id) {
	static const char *const midia[] = { "lumna-midia", NULL };
	static const char *const gaming[] = { "lumna-jogos", "lumna-gaming", "steam", "gamescope", "heroic", "lutris", "net.lutris", "com.heroicgameslauncher", NULL };
	if (!app_id || !*app_id) {
		return -1;
	}
	for (int i = 0; midia[i]; i++) {
		if (!strncasecmp(app_id, midia[i], strlen(midia[i]))) {
			return ORBIT_MODE_MEDIA;
		}
	}
	for (int i = 0; gaming[i]; i++) {
		if (!strncasecmp(app_id, gaming[i], strlen(gaming[i]))) {
			return ORBIT_MODE_GAMING;
		}
	}
	return -1;
}

struct orbit_output *orbit_output_for_new_view(struct orbit_server *server, const char *app_id) {
	struct orbit_output *cursor = cursor_output(server);
	int mode = family_mode(app_id);
	struct orbit_output *output;
	if (mode >= 0) {
		if (cursor && cursor->mode == mode && !output_is_phone(cursor->wlr)) {
			return cursor;
		}
		wl_list_for_each(output, &server->outputs, link) {
			if (output->mode == mode && !output_is_phone(output->wlr)) {
				return output;
			}
		}
		return cursor;
	}
	if (!cursor || !orbit_output_isolated(cursor)) {
		return cursor;
	}
	wl_list_for_each(output, &server->outputs, link) {
		if (!orbit_output_isolated(output) && !output_is_phone(output->wlr)) {
			return output;
		}
	}
	return cursor;
}

/*
 * Trocar as telas: o espaço de trabalho de cada uma (com o modo e as janelas) passa para a outra.
 * É a única ponte entre uma tela em Gaming ou Mídia e o resto: a pessoa pede, e o filme que passava
 * na TV passa para o monitor, e o que estava no monitor vai para a TV.
 */
bool orbit_outputs_swap(struct orbit_server *server, struct orbit_output *a, struct orbit_output *b) {
	if (!a || !b || a == b || output_is_phone(a->wlr) || output_is_phone(b->wlr)) {
		return false;
	}
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->output == a || view->output == b) {
			orbit_split_release(server, view);
			view->output = view->output == a ? b : a; /* o espaço (tag) vai junto: é o da janela */
		}
	}
	int tag = a->tag, desktop = a->desktop_tag;
	a->tag = b->tag;
	a->desktop_tag = b->desktop_tag;
	b->tag = tag;
	b->desktop_tag = desktop;
	a->shown_tag = a->tag; /* sem deslizar: o que muda é a tela, não o espaço */
	b->shown_tag = b->tag;
	orbit_arrange(server);
	focus_top(server, a);
	focus_top(server, b);
	return true;
}

bool orbit_outputs_swap_from(struct orbit_server *server, struct orbit_output *from) {
	if (!from) {
		return false;
	}
	struct orbit_output *other = adjacent_output(server, from, 1, false);
	if (!other) {
		other = adjacent_output(server, from, -1, false);
	}
	return orbit_outputs_swap(server, from, other);
}
