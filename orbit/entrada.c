/*
 * A entrada que o Painel de controle mexe, em ~/.config/lumna/entrada.ini (o Orbit vigia o arquivo):
 *
 *   [teclado]   layout=br,us  variant=abnt2,  options=grp:alt_shift_toggle,caps:escape  model=
 *               repeticao_taxa=25  repeticao_atraso=600
 *   [mouse]     botao_primario=direito|esquerdo  velocidade=-1..1  perfil=adaptativo|plano  rolagem_natural=0|1
 *   [touchpad]  tocar_para_clicar=0|1  rolagem_natural=0|1  botao_primario=...  velocidade=-1..1  perfil=...
 *               digitar_desativa=0|1
 *
 * O que não está no arquivo fica como o libinput traz de fábrica (o teclado, como em XKB_DEFAULT_*).
 * Os ajustes de ponteiro só valem em dispositivos de verdade (libinput); aninhado, no Wayland de fora,
 * só o teclado muda.
 */
#include "orbit.h"

#include <libinput.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/log.h>

static struct xkb_context *shared_context;
static struct xkb_keymap *shared_keymap;

bool orbit_lumna_dir(char *dest, size_t n) {
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xdg && *xdg) {
		snprintf(dest, n, "%s/lumna", xdg);
	} else if (home && *home) {
		snprintf(dest, n, "%s/.config/lumna", home);
	} else {
		dest[0] = '\0';
		return false;
	}
	return true;
}

static void defaults(struct orbit_input_config *c) {
	memset(c, 0, sizeof(*c));
	c->repeat_rate = 25;
	c->repeat_delay = 600;
	c->numlock = 1;
	c->mouse.left_handed = -1;
	c->mouse.speed = 2.0; /* fora de -1..1: não mexer */
	c->mouse.profile = -1;
	c->mouse.natural_scroll = -1;
	c->touchpad.tap = c->touchpad.natural_scroll = c->touchpad.left_handed = c->touchpad.disable_while_typing = -1;
	c->touchpad.speed = 2.0;
	c->touchpad.profile = -1;
}

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

static int boolean(const char *v) {
	return (!strcmp(v, "1") || !strcasecmp(v, "sim") || !strcasecmp(v, "true")) ? 1 : 0;
}

static int hand(const char *v) {
	return !strcasecmp(v, "direito") ? 1 : 0;
}

static int profile(const char *v) {
	return !strcasecmp(v, "plano") ? 1 : 0;
}

static double clamp_speed(const char *v) {
	double d = atof(v);
	return d < -1 ? -1 : d > 1 ? 1 : d;
}

void orbit_input_config_load(struct orbit_server *server) {
	struct orbit_input_config *c = &server->input;
	defaults(c);
	/* sem arquivo: o ambiente (XKB_DEFAULT_*) e, sem ele, o teclado brasileiro (o LUMNA OS nasce em português) */
	snprintf(c->layout, sizeof(c->layout), "br");
	snprintf(c->variant, sizeof(c->variant), "abnt2");
	const char *e;
	if ((e = getenv("XKB_DEFAULT_LAYOUT"))) {
		snprintf(c->layout, sizeof(c->layout), "%s", e);
	}
	if ((e = getenv("XKB_DEFAULT_VARIANT"))) {
		snprintf(c->variant, sizeof(c->variant), "%s", e);
	}
	if ((e = getenv("XKB_DEFAULT_OPTIONS"))) {
		snprintf(c->options, sizeof(c->options), "%s", e);
	}
	if ((e = getenv("XKB_DEFAULT_MODEL"))) {
		snprintf(c->model, sizeof(c->model), "%s", e);
	}
	char dir[400], path[460];
	if (!orbit_lumna_dir(dir, sizeof(dir))) {
		return;
	}
	snprintf(path, sizeof(path), "%s/entrada.ini", dir);
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	char line[512];
	enum { NONE, TECLADO, MOUSE, TOUCHPAD } sec = NONE;
	bool file_layout = false;
	while (fgets(line, sizeof(line), f)) {
		char *p = trim(line);
		if (!*p || *p == '#' || *p == ';') {
			continue;
		}
		if (*p == '[') {
			sec = !strncmp(p, "[teclado]", 9) ? TECLADO : !strncmp(p, "[mouse]", 7) ? MOUSE : !strncmp(p, "[touchpad]", 10) ? TOUCHPAD : NONE;
			continue;
		}
		char *eq = strchr(p, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		char *k = trim(p), *v = trim(eq + 1);
		if (sec == TECLADO) {
			if (!strcmp(k, "layout")) {
				snprintf(c->layout, sizeof(c->layout), "%s", v);
				file_layout = true;
			} else if (!strcmp(k, "variant")) {
				snprintf(c->variant, sizeof(c->variant), "%s", v);
			} else if (!strcmp(k, "options")) {
				snprintf(c->options, sizeof(c->options), "%s", v);
			} else if (!strcmp(k, "numlock")) {
				c->numlock = strcmp(v, "0") && strcasecmp(v, "false") && strcasecmp(v, "nao") && strcasecmp(v, "não");
			} else if (!strcmp(k, "model")) {
				snprintf(c->model, sizeof(c->model), "%s", v);
			} else if (!strcmp(k, "repeticao_taxa")) {
				c->repeat_rate = atoi(v) < 1 ? 1 : atoi(v) > 100 ? 100 : atoi(v);
			} else if (!strcmp(k, "repeticao_atraso")) {
				c->repeat_delay = atoi(v) < 100 ? 100 : atoi(v) > 2000 ? 2000 : atoi(v);
			}
		} else if (sec == MOUSE) {
			if (!strcmp(k, "botao_primario")) {
				c->mouse.left_handed = hand(v);
			} else if (!strcmp(k, "velocidade")) {
				c->mouse.speed = clamp_speed(v);
			} else if (!strcmp(k, "perfil")) {
				c->mouse.profile = profile(v);
			} else if (!strcmp(k, "rolagem_natural")) {
				c->mouse.natural_scroll = boolean(v);
			}
		} else if (sec == TOUCHPAD) {
			if (!strcmp(k, "tocar_para_clicar")) {
				c->touchpad.tap = boolean(v);
			} else if (!strcmp(k, "rolagem_natural")) {
				c->touchpad.natural_scroll = boolean(v);
			} else if (!strcmp(k, "botao_primario")) {
				c->touchpad.left_handed = hand(v);
			} else if (!strcmp(k, "velocidade")) {
				c->touchpad.speed = clamp_speed(v);
			} else if (!strcmp(k, "perfil")) {
				c->touchpad.profile = profile(v);
			} else if (!strcmp(k, "digitar_desativa")) {
				c->touchpad.disable_while_typing = boolean(v);
			}
		}
	}
	(void)file_layout;
	fclose(f);
}

/* ---- o mapa do teclado ---- */

static bool keymap_build(struct orbit_server *server) {
	if (!shared_context) {
		shared_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (!shared_context) {
			wlr_log(WLR_ERROR, "contexto XKB falhou");
			return false;
		}
	}
	const struct orbit_input_config *c = &server->input;
	struct xkb_rule_names names = {
		.rules = getenv("XKB_DEFAULT_RULES"),
		.model = c->model[0] ? c->model : NULL,
		.layout = c->layout[0] ? c->layout : NULL,
		.variant = c->variant[0] ? c->variant : NULL,
		.options = c->options[0] ? c->options : NULL,
	};
	/* a mesma configuração: o mapa que já está em uso vale (refazê-lo, e depois pô-lo em todos os teclados, apagaria o NumLock e o CapsLock
	 * de quem os ligou, e isso aconteceria a cada teclado novo que aparece) */
	static char chave_atual[700];
	char chave[700];
	snprintf(chave, sizeof(chave), "%s|%s|%s|%s|%s", names.rules ? names.rules : "", names.model ? names.model : "", names.layout ? names.layout : "",
		names.variant ? names.variant : "", names.options ? names.options : "");
	if (shared_keymap && !strcmp(chave, chave_atual)) {
		return false;
	}
	struct xkb_keymap *novo = xkb_keymap_new_from_names(shared_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!novo) {
		wlr_log(WLR_ERROR, "keymap XKB falhou (layout \"%s\" variante \"%s\" opções \"%s\"): fico com o que tinha", c->layout, c->variant, c->options);
		return false;
	}
	if (shared_keymap) {
		xkb_keymap_unref(shared_keymap);
	}
	shared_keymap = novo;
	snprintf(chave_atual, sizeof(chave_atual), "%s", chave);
	return true;
}

void orbit_xkb_prepare(struct orbit_server *server) {
	if (!shared_keymap) {
		keymap_build(server);
	}
}

void orbit_xkb_finish(void) {
	if (shared_keymap) {
		xkb_keymap_unref(shared_keymap);
		shared_keymap = NULL;
	}
	if (shared_context) {
		xkb_context_unref(shared_context);
		shared_context = NULL;
	}
}

const char *orbit_input_layout_name(struct orbit_server *server) {
	(void)server;
	return shared_keymap && xkb_keymap_num_layouts(shared_keymap) > 0 ? xkb_keymap_layout_get_name(shared_keymap, 0) : NULL;
}

/* ---- o ponteiro (libinput) ---- */

static void apply_one(struct orbit_server *server, struct orbit_pointer *p) {
	if (!wlr_input_device_is_libinput(p->device)) {
		return;
	}
	struct libinput_device *dev = wlr_libinput_get_device_handle(p->device);
	if (!dev) {
		return;
	}
	const struct orbit_input_config *c = &server->input;
	p->touchpad = libinput_device_config_tap_get_finger_count(dev) > 0;
	int left = p->touchpad ? c->touchpad.left_handed : c->mouse.left_handed;
	double speed = p->touchpad ? c->touchpad.speed : c->mouse.speed;
	int prof = p->touchpad ? c->touchpad.profile : c->mouse.profile;
	int natural = p->touchpad ? c->touchpad.natural_scroll : c->mouse.natural_scroll;
	if (left >= 0 && libinput_device_config_left_handed_is_available(dev)) {
		libinput_device_config_left_handed_set(dev, left);
	}
	if (libinput_device_config_accel_is_available(dev)) {
		if (prof >= 0) {
			libinput_device_config_accel_set_profile(dev, prof ? LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT : LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
		}
		if (speed >= -1.0 && speed <= 1.0) {
			libinput_device_config_accel_set_speed(dev, speed);
		}
	}
	if (natural >= 0 && libinput_device_config_scroll_has_natural_scroll(dev)) {
		libinput_device_config_scroll_set_natural_scroll_enabled(dev, natural);
	}
	if (p->touchpad) {
		if (c->touchpad.tap >= 0) {
			libinput_device_config_tap_set_enabled(dev, c->touchpad.tap ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
		}
		if (c->touchpad.disable_while_typing >= 0 && libinput_device_config_dwt_is_available(dev)) {
			libinput_device_config_dwt_set_enabled(dev, c->touchpad.disable_while_typing ? LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);
		}
	}
}

/* os LEDs do teclado (NumLock, CapsLock, ScrollLock) como o estado diz */
static void leds_atualizar(struct wlr_keyboard *kb) {
	static const char *const nomes[] = { XKB_LED_NAME_NUM, XKB_LED_NAME_CAPS, XKB_LED_NAME_SCROLL };
	uint32_t leds = 0;
	for (int i = 0; i < 3 && i < WLR_LED_COUNT; i++) {
		if (xkb_state_led_name_is_active(kb->xkb_state, nomes[i]) > 0) {
			leds |= 1u << i;
		}
	}
	wlr_keyboard_led_update(kb, leds);
}

/* liga o NumLock uma vez por teclado: ao ligar o PC, no login e depois do logoff (cada um é um Orbit novo, com teclados novos) */
static void numlock_ligar(struct orbit_keyboard *k) {
	struct wlr_keyboard *kb = k->wlr_keyboard;
	if (k->numlock_done || !k->server->input.numlock || !kb->keymap || !kb->xkb_state) {
		return;
	}
	xkb_mod_index_t idx = xkb_keymap_mod_get_index(kb->keymap, XKB_MOD_NAME_NUM);
	if (idx == XKB_MOD_INVALID) {
		return;
	}
	k->numlock_done = true;
	uint32_t locked = kb->modifiers.locked | (1u << idx);
	wlr_keyboard_notify_modifiers(kb, kb->modifiers.depressed, kb->modifiers.latched, locked, kb->modifiers.group);
	leds_atualizar(kb);
	wlr_log(WLR_INFO, "NumLock ligado no teclado \"%s\"", k->wlr_keyboard->base.name ? k->wlr_keyboard->base.name : "?");
}

/*
 * Antes de cada tecla: se o NumLock devia estar ligado (padrão) e ninguém o desligou apertando a tecla, mas o estado está apagado,
 * algo o zerou (um mapa refeito, um teclado que reapareceu...): religa antes da tecla ser tratada, para o teclado numérico dar números.
 */
void orbit_numlock_guarda(struct orbit_keyboard *k, const xkb_keysym_t *syms, int n) {
	for (int i = 0; i < n; i++) {
		if (syms[i] == XKB_KEY_Num_Lock) {
			k->numlock_mexeu = true;
		}
	}
	struct wlr_keyboard *kb = k->wlr_keyboard;
	if (!k->shared || k->numlock_mexeu || !k->server->input.numlock || !kb->keymap || !kb->xkb_state) {
		return;
	}
	if (xkb_state_mod_name_is_active(kb->xkb_state, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED) > 0) {
		return;
	}
	wlr_log(WLR_ERROR, "NumLock apagou sozinho no teclado \"%s\" (ninguém apertou a tecla): religando", kb->base.name ? kb->base.name : "?");
	k->numlock_done = false;
	numlock_ligar(k);
}

/* só para os testes: zera o estado do N-ésimo teclado como um mapa novo faz */
void orbit_input_test_zerar(struct orbit_server *server, int idx) {
	int i = 0;
	struct orbit_keyboard *k;
	wl_list_for_each(k, &server->keyboards, link) {
		if (k->shared && i++ == idx && shared_keymap) {
			wlr_keyboard_set_keymap(k->wlr_keyboard, shared_keymap);
		}
	}
}

void orbit_input_apply(struct orbit_server *server) {
	/* o teclado: o mapa novo e a repetição */
	if (keymap_build(server) || shared_keymap) {
		struct orbit_keyboard *k;
		wl_list_for_each(k, &server->keyboards, link) {
			if (!k->shared) {
				continue;
			}
			struct wlr_keyboard *kb = k->wlr_keyboard;
			if (kb->keymap != shared_keymap) {
				/* trocar o mapa cria um estado novo, com o NumLock e o CapsLock destravados: guarda o que estava travado e devolve */
				uint32_t travadas = kb->keymap ? kb->modifiers.locked : 0;
				wlr_keyboard_set_keymap(kb, shared_keymap);
				if (travadas) {
					wlr_keyboard_notify_modifiers(kb, 0, 0, travadas, 0);
					leds_atualizar(kb);
				}
			}
			numlock_ligar(k);
			wlr_keyboard_set_repeat_info(k->wlr_keyboard, server->input.repeat_rate, server->input.repeat_delay);
		}
	}
	struct orbit_pointer *p;
	wl_list_for_each(p, &server->pointers, link) {
		apply_one(server, p);
	}
}

static void pointer_destroy(struct wl_listener *listener, void *data) {
	struct orbit_pointer *p = wl_container_of(listener, p, destroy);
	(void)data;
	wl_list_remove(&p->link);
	wl_list_remove(&p->destroy.link);
	free(p);
}

void orbit_pointer_add(struct orbit_server *server, struct wlr_input_device *device) {
	struct orbit_pointer *p = calloc(1, sizeof(*p));
	if (!p) {
		return;
	}
	p->server = server;
	p->device = device;
	p->destroy.notify = pointer_destroy;
	wl_signal_add(&device->events.destroy, &p->destroy);
	wl_list_insert(&server->pointers, &p->link);
	apply_one(server, p);
}

/* o que há ligado (o Painel mostra só o que existe): "teclado", "mouse" ou "touchpad" */
int orbit_input_describe(struct orbit_server *server, void (*cb)(void *ctx, const char *name, const char *kind, bool configurable), void *ctx) {
	int n = 0;
	struct orbit_keyboard *k;
	wl_list_for_each(k, &server->keyboards, link) {
		if (k->shared) {
			cb(ctx, k->wlr_keyboard->base.name ? k->wlr_keyboard->base.name : "teclado", "teclado", true);
			n++;
		}
	}
	struct orbit_pointer *p;
	wl_list_for_each(p, &server->pointers, link) {
		bool real = wlr_input_device_is_libinput(p->device);
		cb(ctx, p->device->name ? p->device->name : "ponteiro", p->touchpad ? "touchpad" : "mouse", real);
		n++;
	}
	return n;
}

/* Só para os testes (LUMNA_TESTE_TECLADOS=N): N teclados falsos, como os de um PC de verdade (o teclado, as teclas de mídia, o botão de ligar...).
 * O backend headless do wlroots não cria teclados. */
static const struct wlr_keyboard_impl teclado_falso_impl = { .name = "teclado-falso" };

void orbit_input_fake_keyboards(struct orbit_server *server, int n) {
	for (int i = 0; i < n; i++) {
		struct wlr_keyboard *kb = calloc(1, sizeof(*kb));
		if (!kb) {
			return;
		}
		char nome[32];
		snprintf(nome, sizeof(nome), "teclado-falso-%d", i);
		wlr_keyboard_init(kb, &teclado_falso_impl, nome);
		orbit_keyboard_add(server, kb, true);
	}
}
