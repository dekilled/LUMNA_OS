#include "orbit.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "nested.h"

/*
 * Orbit aninhado (rodando numa janela de outro Wayland): a área de transferência de lá e a daqui são duas. Esta ponte copia o texto
 * nos dois sentidos. O lado cliente (falar com o compositor de fora) está em nested.c; aqui fica o lado do wlroots.
 *
 *   fora -> aqui: o que se copia lá fora vira uma "fonte" do nosso seat (o texto só é pedido lá fora quando alguém aqui cola).
 *   aqui -> fora: quando o seat daqui passa a ter um texto copiado que não veio de fora, ele é oferecido ao compositor de fora.
 */

static const char *const TIPOS[] = { "text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING", "TEXT" };
#define NTIPOS ((int)(sizeof(TIPOS) / sizeof(TIPOS[0])))

struct fonte_de_fora {
	struct wlr_data_source base;
	void *oferta; /* do lado cliente (nested.c) */
};

static struct {
	struct orbit_server *server;
	struct wl_listener set_selection;
	struct wlr_data_source *publicada; /* a fonte daqui que foi oferecida lá fora */
	bool ligada;
} ponte;

static void fora_send(struct wlr_data_source *fonte, const char *mime, int32_t fd) {
	struct fonte_de_fora *f = wl_container_of(fonte, f, base);
	orbit_nested_clip_receber(f->oferta, fd);
}

static void fora_destroy(struct wlr_data_source *fonte) {
	struct fonte_de_fora *f = wl_container_of(fonte, f, base);
	orbit_nested_clip_soltar(f->oferta);
	free(f);
}

static const struct wlr_data_source_impl fora_impl = { .send = fora_send, .destroy = fora_destroy };

static bool e_de_fora(const struct wlr_data_source *fonte) {
	return fonte && fonte->impl == &fora_impl;
}

static void selecao_de_fora(void *oferta, void *dados) {
	struct orbit_server *server = ponte.server;
	if (!oferta) {
		/* lá fora não há mais nada copiado: o que daqui veio de lá também some */
		if (e_de_fora(server->seat->selection_source)) {
			wlr_seat_set_selection(server->seat, NULL, wl_display_next_serial(server->display));
		}
		return;
	}
	struct fonte_de_fora *f = calloc(1, sizeof(*f));
	if (!f) {
		orbit_nested_clip_soltar(oferta);
		return;
	}
	f->oferta = oferta;
	wlr_data_source_init(&f->base, &fora_impl);
	for (int i = 0; i < NTIPOS; i++) {
		char **p = wl_array_add(&f->base.mime_types, sizeof(*p));
		if (p) {
			*p = strdup(TIPOS[i]);
		}
	}
	wlr_seat_set_selection(server->seat, &f->base, wl_display_next_serial(server->display));
}

/* a fonte daqui sumiu antes de a de fora pedir o texto */
static void enviar_para_fora(const char *mime, int fd, void *dados) {
	struct wlr_data_source *fonte = ponte.server->seat->selection_source;
	if (fonte && fonte == ponte.publicada && !e_de_fora(fonte)) {
		wlr_data_source_send(fonte, mime, fd); /* o wlroots fecha o fd depois de mandar a quem tem o texto */
	} else {
		close(fd);
	}
}

static void seat_selecao_mudou(struct wl_listener *listener, void *data) {
	struct wlr_data_source *fonte = ponte.server->seat->selection_source;
	if (e_de_fora(fonte)) {
		return; /* fomos nós que a pusemos, vinda de fora: não volta */
	}
	if (!fonte) {
		ponte.publicada = NULL;
		orbit_nested_clip_limpar();
		return;
	}
	/* só texto: o que tiver algum tipo de texto sobe */
	const char *tipos[8];
	int n = 0;
	char **p;
	wl_array_for_each(p, &fonte->mime_types) {
		for (int i = 0; i < NTIPOS && n < 8; i++) {
			if (!strcmp(*p, TIPOS[i])) {
				tipos[n++] = TIPOS[i];
			}
		}
	}
	if (n == 0) {
		return;
	}
	ponte.publicada = fonte;
	orbit_nested_clip_publicar(tipos, n, enviar_para_fora, NULL);
}

static const struct orbit_nested_clip_ops ops = { .selecao_de_fora = selecao_de_fora };

void orbit_clip_ponte_iniciar(struct orbit_server *server, struct wl_display *remote) {
	if (ponte.ligada) {
		return;
	}
	ponte.server = server;
	if (!orbit_nested_clip_iniciar(remote, &ops, NULL)) {
		wlr_log(WLR_INFO, "o compositor de fora não tem área de transferência: copiar e colar fica só aqui dentro");
		return;
	}
	ponte.ligada = true;
	ponte.set_selection.notify = seat_selecao_mudou;
	wl_signal_add(&server->seat->events.set_selection, &ponte.set_selection);
	wlr_log(WLR_INFO, "área de transferência (texto) ligada ao compositor de fora");
}
