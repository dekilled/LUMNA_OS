#include "orbit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/log.h>

static void copy_cmd(char *dest, const char *src) {
	snprintf(dest, ORBIT_CMD_LEN, "%s", src ? src : "");
}

static bool parse_int(const char *text, int min, int max, int *out) {
	char *end = NULL;
	long value = strtol(text, &end, 10);
	if (end == text || *end != '\0' || value < min || value > max) {
		return false;
	}
	*out = (int)value;
	return true;
}

static bool parse_float(const char *text, float min, float max, float *out) {
	char *end = NULL;
	float value = strtof(text, &end);
	if (end == text || *end != '\0' || value < min || value > max) {
		return false;
	}
	*out = value;
	return true;
}

static int hex_byte(char hi, char lo) {
	int value = 0;
	for (int i = 0; i < 2; i++) {
		char c = i == 0 ? hi : lo;
		value *= 16;
		if (c >= '0' && c <= '9') {
			value += c - '0';
		} else if (c >= 'a' && c <= 'f') {
			value += c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			value += c - 'A' + 10;
		} else {
			return -1;
		}
	}
	return value;
}

static bool parse_color(const char *text, float out[4]) {
	if (text[0] != '#') {
		return false;
	}
	size_t n = strlen(text + 1);
	if (n != 6 && n != 8) {
		return false;
	}
	int r = hex_byte(text[1], text[2]);
	int g = hex_byte(text[3], text[4]);
	int b = hex_byte(text[5], text[6]);
	int a = n == 8 ? hex_byte(text[7], text[8]) : 255;
	if (r < 0 || g < 0 || b < 0 || a < 0) {
		return false;
	}
	out[0] = r / 255.0f;
	out[1] = g / 255.0f;
	out[2] = b / 255.0f;
	out[3] = a / 255.0f;
	return true;
}

static bool parse_layout(const char *text, enum orbit_layout *out) {
	if (strcasecmp(text, "mestre") == 0 || strcasecmp(text, "master") == 0) {
		*out = ORBIT_LAYOUT_MASTER;
		return true;
	}
	if (strcasecmp(text, "monoculo") == 0 || strcasecmp(text, "monocle") == 0) {
		*out = ORBIT_LAYOUT_MONOCLE;
		return true;
	}
	if (strcasecmp(text, "espiral") == 0 || strcasecmp(text, "spiral") == 0) {
		*out = ORBIT_LAYOUT_SPIRAL;
		return true;
	}
	if (strcasecmp(text, "dwindle") == 0) {
		*out = ORBIT_LAYOUT_DWINDLE;
		return true;
	}
	return false;
}

static void trim(char *text) {
	char *start = text;
	while (*start == ' ' || *start == '\t' || *start == '\r') {
		start++;
	}
	if (start != text) {
		memmove(text, start, strlen(start) + 1);
	}
	size_t n = strlen(text);
	while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) {
		text[--n] = '\0';
	}
}

static void apply_general(struct orbit_server *server, const char *key, const char *value, int line) {
	int number = 0;
	float scale = 0;
	if (strcasecmp(key, "intervalo") == 0 || strcasecmp(key, "gap") == 0) {
		if (!parse_int(value, 0, ORBIT_GAP_MAX, &number)) {
			wlr_log(WLR_ERROR, "config linha %d: intervalo %s inválido", line, value);
			return;
		}
		server->gap = number;
	} else if (strcasecmp(key, "mestre") == 0 || strcasecmp(key, "master") == 0) {
		if (!parse_int(value, ORBIT_MASTER_MIN, ORBIT_MASTER_MAX, &number)) {
			wlr_log(WLR_ERROR, "config linha %d: mestre %s inválido", line, value);
			return;
		}
		server->master_pct = number;
	} else if (strcasecmp(key, "escala") == 0 || strcasecmp(key, "phone_scale") == 0) {
		if (!parse_float(value, 1.0f, 3.0f, &scale)) {
			wlr_log(WLR_ERROR, "config linha %d: escala %s inválida", line, value);
			return;
		}
		server->phone_scale = scale;
	} else if (strcasecmp(key, "fundo") == 0 || strcasecmp(key, "background") == 0) {
		if (!parse_color(value, server->background)) {
			wlr_log(WLR_ERROR, "config linha %d: fundo %s inválido", line, value);
		}
	} else if (strcasecmp(key, "borda") == 0 || strcasecmp(key, "border") == 0) {
		if (!parse_color(value, server->border_color)) {
			wlr_log(WLR_ERROR, "config linha %d: borda %s inválida", line, value);
		}
	} else if (strcasecmp(key, "borda_px") == 0 || strcasecmp(key, "border_px") == 0) {
		if (!parse_int(value, 0, ORBIT_BORDER_MAX, &number)) {
			wlr_log(WLR_ERROR, "config linha %d: borda_px %s inválido", line, value);
			return;
		}
		server->border_px = number;
	} else if (strcasecmp(key, "animacao") == 0 || strcasecmp(key, "anim") == 0) {
		if (!parse_int(value, 0, ORBIT_ANIM_MAX, &number)) {
			wlr_log(WLR_ERROR, "config linha %d: animacao %s inválida", line, value);
			return;
		}
		server->anim_ms = number;
	} else if (strcasecmp(key, "arranjo") == 0 || strcasecmp(key, "layout") == 0) {
		if (!parse_layout(value, &server->layout)) {
			wlr_log(WLR_ERROR, "config linha %d: arranjo %s inválido", line, value);
		}
	} else if (strcasecmp(key, "terminal") == 0) {
		copy_cmd(server->terminal, value);
	} else if (strcasecmp(key, "captura") == 0 || strcasecmp(key, "screenshot") == 0) {
		copy_cmd(server->screenshot, value);
	} else if (strcasecmp(key, "volume_subir") == 0 || strcasecmp(key, "volume_up") == 0) {
		copy_cmd(server->volume_up, value);
	} else if (strcasecmp(key, "volume_descer") == 0 || strcasecmp(key, "volume_down") == 0) {
		copy_cmd(server->volume_down, value);
	} else if (strcasecmp(key, "brilho_subir") == 0 || strcasecmp(key, "brightness_up") == 0) {
		copy_cmd(server->brightness_up, value);
	} else if (strcasecmp(key, "brilho_descer") == 0 || strcasecmp(key, "brightness_down") == 0) {
		copy_cmd(server->brightness_down, value);
	} else {
		wlr_log(WLR_ERROR, "config linha %d: chave %s desconhecida", line, key);
	}
}

static void apply_env(struct orbit_server *server) {
	const char *gap = getenv("ORBIT_GAP");
	const char *master = getenv("ORBIT_MASTER");
	const char *scale = getenv("ORBIT_PHONE_SCALE");
	const char *term = getenv("ORBIT_TERMINAL");
	int number = 0;
	float value = 0;
	if (gap && gap[0]) {
		if (parse_int(gap, 0, ORBIT_GAP_MAX, &number)) {
			server->gap = number;
		} else {
			wlr_log(WLR_ERROR, "ORBIT_GAP=%s inválido; fica o valor do ficheiro", gap);
		}
	}
	if (master && master[0]) {
		if (parse_int(master, ORBIT_MASTER_MIN, ORBIT_MASTER_MAX, &number)) {
			server->master_pct = number;
		} else {
			wlr_log(WLR_ERROR, "ORBIT_MASTER=%s inválido; fica o valor do ficheiro", master);
		}
	}
	if (scale && scale[0]) {
		if (parse_float(scale, 1.0f, 3.0f, &value)) {
			server->phone_scale = value;
		} else {
			wlr_log(WLR_ERROR, "ORBIT_PHONE_SCALE=%s inválido; fica o valor do ficheiro", scale);
		}
	}
	if (term && term[0]) {
		copy_cmd(server->terminal, term);
	}
}

static void config_path(char *dest, size_t n) {
	const char *given = getenv("ORBIT_CONFIG");
	if (given && given[0]) {
		snprintf(dest, n, "%s", given);
		return;
	}
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && xdg[0]) {
		snprintf(dest, n, "%s/orbit/orbit.ini", xdg);
		return;
	}
	const char *home = getenv("HOME");
	if (home && home[0]) {
		snprintf(dest, n, "%s/.config/orbit/orbit.ini", home);
		return;
	}
	dest[0] = '\0';
}

static void read_file(struct orbit_server *server, const char *path) {
	FILE *file = fopen(path, "r");
	if (!file) {
		if (getenv("ORBIT_CONFIG")) {
			wlr_log(WLR_ERROR, "não abri a configuração %s", path);
		}
		return;
	}
	wlr_log(WLR_INFO, "configuração %s", path);
	enum { SECTION_NONE, SECTION_GENERAL, SECTION_BIND } section = SECTION_NONE;
	char line_buf[512];
	int line = 0;
	while (fgets(line_buf, sizeof(line_buf), file)) {
		line++;
		trim(line_buf);
		if (line_buf[0] == '\0' || line_buf[0] == '#' || line_buf[0] == ';') {
			continue;
		}
		if (line_buf[0] == '[') {
			char *end = strchr(line_buf, ']');
			if (!end) {
				wlr_log(WLR_ERROR, "config linha %d: secção inválida", line);
				continue;
			}
			*end = '\0';
			if (strcasecmp(line_buf + 1, "geral") == 0) {
				section = SECTION_GENERAL;
			} else if (strcasecmp(line_buf + 1, "atalho") == 0) {
				section = SECTION_BIND;
			} else {
				section = SECTION_NONE;
				wlr_log(WLR_ERROR, "config linha %d: secção %s desconhecida", line, line_buf + 1);
			}
			continue;
		}
		char *eq = strchr(line_buf, '=');
		if (!eq) {
			wlr_log(WLR_ERROR, "config linha %d: falta '='", line);
			continue;
		}
		*eq = '\0';
		char *key = line_buf;
		char *value = eq + 1;
		trim(key);
		trim(value);
		if (section == SECTION_GENERAL) {
			apply_general(server, key, value, line);
		} else if (section == SECTION_BIND) {
			if (!orbit_bind_set(server, key, value)) {
				wlr_log(WLR_ERROR, "config linha %d: atalho %s desconhecido", line, key);
			}
		} else {
			wlr_log(WLR_ERROR, "config linha %d: chave fora de secção", line);
		}
	}
	fclose(file);
}

void orbit_config_load(struct orbit_server *server) {
	server->gap = ORBIT_GAP_DEFAULT;
	server->master_pct = ORBIT_MASTER_DEFAULT;
	server->phone_scale = ORBIT_PHONE_SCALE_DEFAULT;
	server->layout = ORBIT_LAYOUT_MASTER;
	server->background[0] = 0.063f;
	server->background[1] = 0.078f;
	server->background[2] = 0.071f;
	server->background[3] = 1.0f;
	server->border_px = ORBIT_BORDER_DEFAULT;
	server->border_color[0] = 0.545f;
	server->border_color[1] = 0.659f;
	server->border_color[2] = 0.600f;
	server->border_color[3] = 1.0f;
	server->anim_ms = ORBIT_ANIM_DEFAULT;
	copy_cmd(server->terminal, "foot");
	copy_cmd(server->screenshot, "grim");
	copy_cmd(server->volume_up, "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+");
	copy_cmd(server->volume_down, "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-");
	copy_cmd(server->brightness_up, "brightnessctl set 5%+");
	copy_cmd(server->brightness_down, "brightnessctl set 5%-");
	orbit_binds_reset(server);
	char path[512];
	config_path(path, sizeof(path));
	if (path[0]) {
		read_file(server, path);
	}
	apply_env(server);
}
