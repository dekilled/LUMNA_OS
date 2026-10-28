#include "nested.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"

static struct zwp_keyboard_shortcuts_inhibit_manager_v1 *gerente;
static struct wl_seat *seat;
static struct wl_display *conhecido;
static struct wl_data_device_manager *dd_gerente;

static void seat_capacidades(void *d, struct wl_seat *s, uint32_t caps);
static void seat_nome(void *d, struct wl_seat *s, const char *nome) {
}
static const struct wl_seat_listener seat_ouvinte = { seat_capacidades, seat_nome };

static void global(void *dados, struct wl_registry *reg, uint32_t nome, const char *interface, uint32_t versao) {
	if (!strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name)) {
		gerente = wl_registry_bind(reg, nome, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
	} else if (!strcmp(interface, wl_seat_interface.name) && !seat) {
		seat = wl_registry_bind(reg, nome, &wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_ouvinte, NULL);
	} else if (!strcmp(interface, wl_data_device_manager_interface.name) && !dd_gerente) {
		dd_gerente = wl_registry_bind(reg, nome, &wl_data_device_manager_interface, 1);
	}
}

static void global_some(void *dados, struct wl_registry *reg, uint32_t nome) {
}

static const struct wl_registry_listener ouvinte = { global, global_some };

/* o que o compositor de fora tem: numa fila à parte, para não mexer na do backend do wlroots */
static bool descobrir(struct wl_display *remote) {
	if (conhecido == remote) {
		return seat != NULL;
	}
	conhecido = remote;
	gerente = NULL;
	seat = NULL;
	dd_gerente = NULL;
	struct wl_event_queue *fila = wl_display_create_queue(remote);
	struct wl_display *embrulho = wl_proxy_create_wrapper(remote);
	wl_proxy_set_queue((struct wl_proxy *)embrulho, fila);
	struct wl_registry *reg = wl_display_get_registry(embrulho);
	wl_proxy_wrapper_destroy(embrulho);
	wl_registry_add_listener(reg, &ouvinte, NULL);
	wl_display_roundtrip_queue(remote, fila);
	/* o que se ligou volta para a fila de sempre: os eventos deles não têm quem ouça e não se acumulam */
	if (gerente) {
		wl_proxy_set_queue((struct wl_proxy *)gerente, NULL);
	}
	if (seat) {
		wl_proxy_set_queue((struct wl_proxy *)seat, NULL);
	}
	if (dd_gerente) {
		wl_proxy_set_queue((struct wl_proxy *)dd_gerente, NULL);
	}
	wl_proxy_set_queue((struct wl_proxy *)reg, NULL);
	wl_display_roundtrip_queue(remote, fila);
	wl_event_queue_destroy(fila);
	return seat != NULL;
}

bool orbit_nested_inhibit(struct wl_display *remote, struct wl_surface *surface) {
	if (!remote || !surface || !descobrir(remote) || !gerente) {
		return false;
	}
	zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(gerente, surface, seat);
	return wl_display_flush(remote) >= 0;
}

/* ---- a área de transferência (texto) ---- */

#define MARCA "application/x-orbit-ponte" /* quem vem do Orbit leva esta marca: não volta para dentro dele */

struct oferta {
	struct wl_data_offer *p;
	bool texto, nossa, viva;
	int prioridade;
	char mime[96];
};

static const struct orbit_nested_clip_ops *clip_ops;
static void *clip_dados;
static struct wl_display *clip_remote;
static struct wl_data_device *dd;
static struct wl_keyboard *teclado;
static struct wl_pointer *ponteiro;
static uint32_t serial_ultimo;
static struct oferta *atual;
static struct wl_data_source *fonte_fora;
static void (*pub_enviar)(const char *mime, int fd, void *dados);
static void *pub_dados;
/* uma cópia que chegou antes de haver um serial: sai no primeiro toque de tecla ou botão */
static char pend_mimes[8][96];
static int pend_n;
static void (*pend_enviar)(const char *mime, int fd, void *dados);
static void *pend_dados;
static void publicar_agora(void);
static void serial_novo(uint32_t s) {
	serial_ultimo = s;
	if (pend_n > 0) {
		publicar_agora();
	}
}

/* o tipo de texto que mais serve (0: não é texto) */
static int prioridade_do_tipo(const char *mime) {
	if (!strcmp(mime, "text/plain;charset=utf-8")) {
		return 4;
	}
	if (!strcmp(mime, "text/plain")) {
		return 3;
	}
	if (!strcmp(mime, "UTF8_STRING")) {
		return 2;
	}
	if (!strcmp(mime, "STRING") || !strcmp(mime, "TEXT")) {
		return 1;
	}
	return 0;
}

static void oferta_tipo(void *d, struct wl_data_offer *p, const char *mime) {
	struct oferta *o = d;
	if (!strcmp(mime, MARCA)) {
		o->nossa = true;
		return;
	}
	int pr = prioridade_do_tipo(mime);
	if (pr > o->prioridade) {
		o->prioridade = pr;
		o->texto = true;
		snprintf(o->mime, sizeof(o->mime), "%s", mime);
	}
}
static const struct wl_data_offer_listener oferta_ouvinte = { oferta_tipo, NULL, NULL };

static void dd_oferta(void *d, struct wl_data_device *dev, struct wl_data_offer *p) {
	struct oferta *o = calloc(1, sizeof(*o));
	if (!o) {
		wl_data_offer_destroy(p);
		return;
	}
	o->p = p;
	o->viva = true;
	wl_data_offer_add_listener(p, &oferta_ouvinte, o);
}

static void oferta_largar(struct oferta *o) {
	if (o->p) {
		wl_data_offer_destroy(o->p);
		o->p = NULL;
	}
	o->viva = false;
}

static void dd_selecao(void *d, struct wl_data_device *dev, struct wl_data_offer *p) {
	if (!p) {
		if (atual) {
			oferta_largar(atual);
			atual = NULL;
		}
		clip_ops->selecao_de_fora(NULL, clip_dados);
		return;
	}
	struct oferta *o = wl_data_offer_get_user_data(p);
	if (!o || o->nossa || !o->texto) {
		/* o que o Orbit mesmo ofereceu, ou algo que não é texto (imagem...): fica como está */
		if (o) {
			oferta_largar(o);
			free(o);
		}
		return;
	}
	if (atual) {
		oferta_largar(atual); /* a memória dela sai em orbit_nested_clip_soltar, quando o Orbit trocar a fonte */
	}
	atual = o;
	clip_ops->selecao_de_fora(o, clip_dados);
}

static void dd_entrou(void *d, struct wl_data_device *dev, uint32_t s, struct wl_surface *sup, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *p) {
}
static void dd_saiu(void *d, struct wl_data_device *dev) {
}
static void dd_moveu(void *d, struct wl_data_device *dev, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
}
static void dd_largou(void *d, struct wl_data_device *dev) {
}
static const struct wl_data_device_listener dd_ouvinte = { dd_oferta, dd_entrou, dd_saiu, dd_moveu, dd_largou, dd_selecao };

/* o serial que o compositor de fora exige para aceitar uma cópia: o do último toque de tecla ou botão nesta janela */
static void tec_keymap(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t n) {
	close(fd);
}
static void tec_entrou(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sup, struct wl_array *teclas) {
	serial_novo(s);
}
static void tec_saiu(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sup) {
}
static void tec_tecla(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t tecla, uint32_t estado) {
	serial_novo(s);
}
static void tec_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t g) {
	serial_novo(s);
}
static void tec_repete(void *d, struct wl_keyboard *k, int32_t taxa, int32_t atraso) {
}
static const struct wl_keyboard_listener teclado_ouvinte = { tec_keymap, tec_entrou, tec_saiu, tec_tecla, tec_mods, tec_repete };

static void pt_entrou(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sup, wl_fixed_t x, wl_fixed_t y) {
	serial_novo(s);
}
static void pt_saiu(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sup) {
}
static void pt_moveu(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
}
static void pt_botao(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t botao, uint32_t estado) {
	serial_novo(s);
}
static void pt_eixo(void *d, struct wl_pointer *p, uint32_t t, uint32_t eixo, wl_fixed_t v) {
}
static const struct wl_pointer_listener ponteiro_ouvinte = { pt_entrou, pt_saiu, pt_moveu, pt_botao, pt_eixo };

static void seat_capacidades(void *d, struct wl_seat *s, uint32_t caps) {
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !teclado) {
		teclado = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(teclado, &teclado_ouvinte, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ponteiro) {
		ponteiro = wl_seat_get_pointer(s);
		wl_pointer_add_listener(ponteiro, &ponteiro_ouvinte, NULL);
	}
}

bool orbit_nested_clip_iniciar(struct wl_display *remote, const struct orbit_nested_clip_ops *ops, void *dados) {
	if (!remote || !ops || !descobrir(remote) || !dd_gerente) {
		return false;
	}
	if (dd) {
		return true;
	}
	clip_ops = ops;
	clip_dados = dados;
	clip_remote = remote;
	dd = wl_data_device_manager_get_data_device(dd_gerente, seat);
	wl_data_device_add_listener(dd, &dd_ouvinte, NULL);
	return wl_display_flush(remote) >= 0;
}

void orbit_nested_clip_receber(void *oferta, int fd) {
	struct oferta *o = oferta;
	if (o && o->viva && o->p && o->texto) {
		wl_data_offer_receive(o->p, o->mime, fd);
		wl_display_flush(clip_remote);
	}
	close(fd);
}

void orbit_nested_clip_soltar(void *oferta) {
	struct oferta *o = oferta;
	if (!o) {
		return;
	}
	if (atual == o) {
		atual = NULL;
	}
	oferta_largar(o);
	free(o);
}

static void fonte_alvo(void *d, struct wl_data_source *f, const char *mime) {
}
static void fonte_enviar(void *d, struct wl_data_source *f, const char *mime, int32_t fd) {
	if (pub_enviar && f == fonte_fora) {
		pub_enviar(mime, fd, pub_dados);
	} else {
		close(fd);
	}
}
static void fonte_cancelada(void *d, struct wl_data_source *f) {
	/* outro programa de fora copiou por cima */
	wl_data_source_destroy(f);
	if (fonte_fora == f) {
		fonte_fora = NULL;
	}
}
static const struct wl_data_source_listener fonte_ouvinte = { fonte_alvo, fonte_enviar, fonte_cancelada, NULL, NULL, NULL };

static void publicar_agora(void) {
	struct wl_data_source *nova = wl_data_device_manager_create_data_source(dd_gerente);
	wl_data_source_add_listener(nova, &fonte_ouvinte, NULL);
	for (int i = 0; i < pend_n; i++) {
		wl_data_source_offer(nova, pend_mimes[i]);
	}
	wl_data_source_offer(nova, MARCA);
	struct wl_data_source *velha = fonte_fora;
	fonte_fora = nova;
	pub_enviar = pend_enviar;
	pub_dados = pend_dados;
	pend_n = 0;
	wl_data_device_set_selection(dd, nova, serial_ultimo);
	if (velha) {
		wl_data_source_destroy(velha);
	}
	wl_display_flush(clip_remote);
}

void orbit_nested_clip_publicar(const char *const *mimes, int n, void (*enviar)(const char *mime, int fd, void *dados), void *dados) {
	if (!dd || !dd_gerente || n <= 0) {
		return;
	}
	pend_n = n > 8 ? 8 : n;
	for (int i = 0; i < pend_n; i++) {
		snprintf(pend_mimes[i], sizeof(pend_mimes[i]), "%s", mimes[i]);
	}
	pend_enviar = enviar;
	pend_dados = dados;
	if (serial_ultimo) {
		publicar_agora();
	}
}

void orbit_nested_clip_limpar(void) {
	pend_n = 0;
	if (!dd || !fonte_fora) {
		return;
	}
	wl_data_device_set_selection(dd, NULL, serial_ultimo);
	wl_data_source_destroy(fonte_fora);
	fonte_fora = NULL;
	wl_display_flush(clip_remote);
}

/* ---- tela cheia ---- */

bool orbit_nested_tela_cheia(const void *saida_wlr, unsigned long tam, struct wl_surface *surface, bool ligar) {
	static struct wl_proxy *toplevel;
	static const void *de;
	if (!saida_wlr || !surface) {
		return false;
	}
	if (de != saida_wlr) {
		toplevel = NULL;
		de = saida_wlr;
	}
	if (!toplevel) {
		/* depois do struct wlr_output vêm: backend, link (2 ponteiros), surface, own_surface, frame_callback, xdg_surface, xdg_toplevel */
		const uintptr_t *w = (const uintptr_t *)((const char *)saida_wlr + tam);
		int pos = -1;
		for (int i = 0; i < 24; i++) {
			if (w[i] == (uintptr_t)surface) {
				pos = i;
				break;
			}
		}
		if (pos < 0) {
			return false;
		}
		for (int i = pos + 1; i < pos + 10; i++) {
			uintptr_t v = w[i];
			if (v < 0x10000 || (v & 7)) {
				continue; /* um número ou nada: só os ponteiros de verdade se olham */
			}
			const char *classe = wl_proxy_get_class((struct wl_proxy *)v);
			if (classe && !strcmp(classe, "xdg_toplevel")) {
				toplevel = (struct wl_proxy *)v;
				break;
			}
		}
		if (!toplevel) {
			return false;
		}
	}
	/* xdg_toplevel: set_fullscreen (11, com a saída, NULL: a que for) e unset_fullscreen (12) */
	wl_proxy_marshal_flags(toplevel, ligar ? 11 : 12, NULL, wl_proxy_get_version(toplevel), 0, NULL);
	return true;
}
