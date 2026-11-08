#define _GNU_SOURCE
#include "teclado_im.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

struct ti_cliente {
	struct wl_display *display;
	struct wl_registry *registro;
	struct wl_seat *seat;
	struct zwp_input_method_manager_v2 *gerente;
	struct zwp_input_method_v2 *metodo;
	struct zwp_virtual_keyboard_manager_v1 *gerente_vk;
	struct zwp_virtual_keyboard_v1 *vk;
	struct xkb_context *xkb;
	struct xkb_keymap *mapa;
	ti_ouvinte ouvinte;
	void *dados;
	bool indisponivel;
	uint32_t serial; /* quantos done chegaram */
	/* o estado que chega aos poucos e vale no done */
	bool pendente_ativo;
	uint32_t pendente_proposito, pendente_dica;
	char pendente_texto[512];
	int pendente_cursor;
	ti_campo campo;
};

/* ---- método de entrada ---- */

static void m_ativar(void *d, struct zwp_input_method_v2 *m) {
	ti_cliente *c = d;
	c->pendente_ativo = true;
}
static void m_desativar(void *d, struct zwp_input_method_v2 *m) {
	ti_cliente *c = d;
	c->pendente_ativo = false;
}
static void m_em_volta(void *d, struct zwp_input_method_v2 *m, const char *texto, uint32_t cursor, uint32_t ancora) {
	ti_cliente *c = d;
	snprintf(c->pendente_texto, sizeof(c->pendente_texto), "%s", texto ? texto : "");
	c->pendente_cursor = (int)cursor;
}
static void m_causa(void *d, struct zwp_input_method_v2 *m, uint32_t causa) {}
static void m_conteudo(void *d, struct zwp_input_method_v2 *m, uint32_t dica, uint32_t proposito) {
	ti_cliente *c = d;
	c->pendente_dica = dica;
	c->pendente_proposito = proposito;
}
static void m_feito(void *d, struct zwp_input_method_v2 *m) {
	ti_cliente *c = d;
	c->serial++;
	c->campo.ativo = c->pendente_ativo;
	c->campo.proposito = c->pendente_proposito;
	c->campo.dica = c->pendente_dica;
	/* só o fim do que vem antes do cursor interessa (a maiúscula depende de como a frase acaba) */
	int cursor = c->pendente_cursor;
	int len = (int)strlen(c->pendente_texto);
	if (cursor < 0 || cursor > len) {
		cursor = len;
	}
	int ini = cursor > (int)sizeof(c->campo.antes) - 1 ? cursor - ((int)sizeof(c->campo.antes) - 1) : 0;
	while (ini < cursor && (c->pendente_texto[ini] & 0xC0) == 0x80) {
		ini++; /* não corta um caractere ao meio */
	}
	snprintf(c->campo.antes, sizeof(c->campo.antes), "%.*s", cursor - ini, c->pendente_texto + ini);
	c->campo.cursor = cursor;
	if (!c->campo.ativo) {
		c->campo.antes[0] = '\0';
	}
	if (c->ouvinte.campo) {
		c->ouvinte.campo(c->dados, &c->campo);
	}
}
static void m_indisponivel(void *d, struct zwp_input_method_v2 *m) {
	ti_cliente *c = d;
	c->indisponivel = true;
	if (c->ouvinte.indisponivel) {
		c->ouvinte.indisponivel(c->dados);
	}
}
static const struct zwp_input_method_v2_listener metodo_ouvinte = {
	.activate = m_ativar, .deactivate = m_desativar, .surrounding_text = m_em_volta, .text_change_cause = m_causa,
	.content_type = m_conteudo, .done = m_feito, .unavailable = m_indisponivel,
};

/* ---- registro ---- */

static void reg_global(void *d, struct wl_registry *r, uint32_t nome, const char *iface, uint32_t versao) {
	ti_cliente *c = d;
	if (!strcmp(iface, "wl_seat") && !c->seat) {
		c->seat = wl_registry_bind(r, nome, &wl_seat_interface, 1);
	} else if (!strcmp(iface, zwp_input_method_manager_v2_interface.name)) {
		c->gerente = wl_registry_bind(r, nome, &zwp_input_method_manager_v2_interface, 1);
	} else if (!strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name)) {
		c->gerente_vk = wl_registry_bind(r, nome, &zwp_virtual_keyboard_manager_v1_interface, 1);
	}
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t nome) {}
static const struct wl_registry_listener reg_ouvinte = { reg_global, reg_remove };

/* ---- teclado virtual ---- */

static void vk_mapa(ti_cliente *c, const char *layout, const char *variante) {
	c->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!c->xkb) {
		return;
	}
	struct xkb_rule_names nomes = { .rules = "evdev", .model = "pc105", .layout = layout && *layout ? layout : "us", .variant = variante ? variante : "" };
	c->mapa = xkb_keymap_new_from_names(c->xkb, &nomes, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!c->mapa) {
		struct xkb_rule_names us = { .rules = "evdev", .model = "pc105", .layout = "us" };
		c->mapa = xkb_keymap_new_from_names(c->xkb, &us, XKB_KEYMAP_COMPILE_NO_FLAGS);
	}
	if (!c->mapa || !c->vk) {
		return;
	}
	char *texto = xkb_keymap_get_as_string(c->mapa, XKB_KEYMAP_FORMAT_TEXT_V1);
	if (!texto) {
		return;
	}
	size_t tam = strlen(texto) + 1;
	int fd = memfd_create("teclado-mapa", MFD_CLOEXEC);
	if (fd >= 0 && ftruncate(fd, (off_t)tam) == 0) {
		void *m = mmap(NULL, tam, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (m != MAP_FAILED) {
			memcpy(m, texto, tam);
			munmap(m, tam);
			zwp_virtual_keyboard_v1_keymap(c->vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)tam);
		}
	}
	if (fd >= 0) {
		close(fd);
	}
	free(texto);
}

static uint32_t agora_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* ---- API ---- */

ti_cliente *ti_abrir(const ti_ouvinte *ouvinte, void *dados, const char *layout, const char *variante) {
	ti_cliente *c = calloc(1, sizeof(*c));
	if (!c) {
		return NULL;
	}
	if (ouvinte) {
		c->ouvinte = *ouvinte;
	}
	c->dados = dados;
	c->display = wl_display_connect(NULL);
	if (!c->display) {
		free(c);
		return NULL;
	}
	c->registro = wl_display_get_registry(c->display);
	wl_registry_add_listener(c->registro, &reg_ouvinte, c);
	wl_display_roundtrip(c->display);
	if (!c->seat || !c->gerente) {
		ti_fechar(c);
		return NULL;
	}
	c->metodo = zwp_input_method_manager_v2_get_input_method(c->gerente, c->seat);
	zwp_input_method_v2_add_listener(c->metodo, &metodo_ouvinte, c);
	if (c->gerente_vk) {
		c->vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(c->gerente_vk, c->seat);
	}
	vk_mapa(c, layout, variante);
	wl_display_roundtrip(c->display);
	if (c->indisponivel) {
		ti_fechar(c);
		return NULL;
	}
	return c;
}

void ti_fechar(ti_cliente *c) {
	if (!c) {
		return;
	}
	if (c->mapa) {
		xkb_keymap_unref(c->mapa);
	}
	if (c->xkb) {
		xkb_context_unref(c->xkb);
	}
	if (c->display) {
		wl_display_disconnect(c->display);
	}
	free(c);
}

int ti_fd(ti_cliente *c) { return c ? wl_display_get_fd(c->display) : -1; }
const ti_campo *ti_estado(ti_cliente *c) { return &c->campo; }
bool ti_metodo_disponivel(ti_cliente *c) { return c && !c->indisponivel; }

bool ti_despachar(ti_cliente *c) {
	if (wl_display_prepare_read(c->display) != 0) {
		return wl_display_dispatch_pending(c->display) >= 0;
	}
	wl_display_flush(c->display);
	struct pollfd p = { .fd = wl_display_get_fd(c->display), .events = POLLIN };
	if (poll(&p, 1, 0) > 0) {
		if (wl_display_read_events(c->display) < 0) {
			return false;
		}
	} else {
		wl_display_cancel_read(c->display);
	}
	if (wl_display_dispatch_pending(c->display) < 0) {
		return false;
	}
	wl_display_flush(c->display);
	return true;
}

static void cometer(ti_cliente *c) {
	zwp_input_method_v2_commit(c->metodo, c->serial);
	wl_display_flush(c->display);
}

void ti_tecla(ti_cliente *c, uint32_t evdev) {
	if (!c->vk) {
		return;
	}
	uint32_t t = agora_ms();
	zwp_virtual_keyboard_v1_key(c->vk, t, evdev, WL_KEYBOARD_KEY_STATE_PRESSED);
	zwp_virtual_keyboard_v1_key(c->vk, t + 1, evdev, WL_KEYBOARD_KEY_STATE_RELEASED);
	wl_display_flush(c->display);
}

/* Um caractere como tecla do mapa (com Shift se for o segundo nível). */
static bool caractere_como_tecla(ti_cliente *c, uint32_t cp) {
	if (!c->vk || !c->mapa) {
		return false;
	}
	xkb_keysym_t sym = xkb_utf32_to_keysym(cp);
	if (sym == XKB_KEY_NoSymbol) {
		return false;
	}
	xkb_keycode_t min = xkb_keymap_min_keycode(c->mapa), max = xkb_keymap_max_keycode(c->mapa);
	for (xkb_keycode_t k = min; k <= max; k++) {
		xkb_level_index_t niveis = xkb_keymap_num_levels_for_key(c->mapa, k, 0);
		for (xkb_level_index_t n = 0; n < niveis && n < 2; n++) {
			const xkb_keysym_t *syms;
			if (xkb_keymap_key_get_syms_by_level(c->mapa, k, 0, n, &syms) != 1 || syms[0] != sym) {
				continue;
			}
			uint32_t t = agora_ms();
			xkb_mod_index_t shift = xkb_keymap_mod_get_index(c->mapa, XKB_MOD_NAME_SHIFT);
			if (n == 1 && shift != XKB_MOD_INVALID) {
				zwp_virtual_keyboard_v1_modifiers(c->vk, 1u << shift, 0, 0, 0);
			}
			zwp_virtual_keyboard_v1_key(c->vk, t, k - 8, WL_KEYBOARD_KEY_STATE_PRESSED);
			zwp_virtual_keyboard_v1_key(c->vk, t + 1, k - 8, WL_KEYBOARD_KEY_STATE_RELEASED);
			if (n == 1 && shift != XKB_MOD_INVALID) {
				zwp_virtual_keyboard_v1_modifiers(c->vk, 0, 0, 0, 0);
			}
			return true;
		}
	}
	return false;
}

static uint32_t proximo_cp(const char **s) {
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t cp;
	int n;
	if (p[0] < 0x80) { cp = p[0]; n = 1; }
	else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; n = 2; }
	else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; n = 3; }
	else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; n = 4; }
	else { *s += 1; return 0xFFFD; }
	for (int i = 1; i < n; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*s += i;
			return 0xFFFD;
		}
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	*s += n;
	return cp;
}

bool ti_texto(ti_cliente *c, const char *utf8) {
	if (!utf8 || !*utf8) {
		return false;
	}
	if (c->campo.ativo && c->metodo && !c->indisponivel) {
		zwp_input_method_v2_commit_string(c->metodo, utf8);
		cometer(c);
		return true;
	}
	/* sem campo (um terminal, um app sem text-input): letra por letra, pelas teclas do mapa */
	bool ok = true;
	for (const char *s = utf8; *s;) {
		ok = caractere_como_tecla(c, proximo_cp(&s)) && ok;
	}
	wl_display_flush(c->display);
	return ok;
}

void ti_apagar(ti_cliente *c) {
	if (c->campo.ativo && c->metodo && !c->indisponivel && c->campo.antes[0]) {
		/* apaga o último caractere (em bytes UTF-8) antes do cursor */
		int len = (int)strlen(c->campo.antes), n = 1;
		while (len - n > 0 && (c->campo.antes[len - n] & 0xC0) == 0x80) {
			n++;
		}
		zwp_input_method_v2_delete_surrounding_text(c->metodo, (uint32_t)n, 0);
		cometer(c);
		return;
	}
	ti_tecla(c, TI_KEY_BACKSPACE);
}
