/*
 * Escrever: a área de transferência (copiar e colar entre apps) e o text-input, por onde o
 * teclado no ecrã do LUMNA sabe que há um campo de texto focado (e se é senha, número, e-mail)
 * e manda o que se escreve nele.
 *
 * O campo focado chama gv__campo_pede() a cada quadro com o texto, o cursor e onde está o
 * cursor na tela; no fim do quadro isto liga, actualiza ou desliga o text-input conforme mudou.
 */
#define _GNU_SOURCE
#include "gv_interno.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#include "text-input-unstable-v3-client-protocol.h"

#define MIME_TEXTO "text/plain;charset=utf-8"
#define GV_MAX_RODEIO 3800 /* o protocolo aceita menos de 4000 bytes de texto à volta */

static struct {
	struct wl_display *display;
	struct wl_seat *seat;
	struct wl_surface *superficie;

	/* área de transferência */
	struct wl_data_device_manager *gerente;
	struct wl_data_device *dispositivo;
	struct wl_data_offer *oferta;     /* o que está copiado agora (de outra app) */
	bool oferta_texto;
	struct wl_data_source *fonte;     /* o que nós copiámos */
	char *copiado;

	/* text-input */
	struct zwp_text_input_manager_v3 *ti_gerente;
	struct zwp_text_input_v3 *ti;
	bool ti_dentro;   /* o compositor pôs o text-input na nossa superfície */
	bool ti_ligado;
	/* o que o campo pediu neste quadro */
	bool quer;
	char texto[GV_MAX_RODEIO + 1];
	int cursor, ancora;
	Clay_BoundingBox caixa;
	uint32_t dica, proposito;
	/* o que foi mandado da última vez */
	char enviado[GV_MAX_RODEIO + 1];
	int enviado_cursor, enviado_ancora;
	Clay_BoundingBox enviada_caixa;
	/* o que chega até ao done */
	char escrito[512];
	char compondo[256];
	int compondo_ini, compondo_fim;
	uint32_t apagar_antes, apagar_depois;
} e;

/* ---- área de transferência ---- */

static void oferta_tipo(void *dados, struct wl_data_offer *oferta, const char *tipo) {
	if (!strcmp(tipo, MIME_TEXTO) || !strcmp(tipo, "text/plain") || !strcmp(tipo, "UTF8_STRING")) {
		/* guarda na própria oferta que ela tem texto */
		wl_data_offer_set_user_data(oferta, (void *)1);
	}
}

static void oferta_acoes(void *d, struct wl_data_offer *o, uint32_t a) {}
static void oferta_acao(void *d, struct wl_data_offer *o, uint32_t a) {}

static const struct wl_data_offer_listener oferta_ouvinte = {
	.offer = oferta_tipo,
	.source_actions = oferta_acoes,
	.action = oferta_acao,
};

static void disp_oferta(void *d, struct wl_data_device *dev, struct wl_data_offer *oferta) {
	wl_data_offer_add_listener(oferta, &oferta_ouvinte, NULL);
}

static void disp_entrou(void *d, struct wl_data_device *dev, uint32_t s, struct wl_surface *sup, wl_fixed_t x,
		wl_fixed_t y, struct wl_data_offer *o) {}
static void disp_saiu(void *d, struct wl_data_device *dev) {}
static void disp_moveu(void *d, struct wl_data_device *dev, uint32_t t, wl_fixed_t x, wl_fixed_t y) {}
static void disp_largou(void *d, struct wl_data_device *dev) {}

static void disp_selecao(void *d, struct wl_data_device *dev, struct wl_data_offer *oferta) {
	if (e.oferta && e.oferta != oferta) {
		wl_data_offer_destroy(e.oferta);
	}
	e.oferta = oferta;
	e.oferta_texto = oferta && wl_data_offer_get_user_data(oferta) != NULL;
}

static const struct wl_data_device_listener disp_ouvinte = {
	.data_offer = disp_oferta,
	.enter = disp_entrou,
	.leave = disp_saiu,
	.motion = disp_moveu,
	.drop = disp_largou,
	.selection = disp_selecao,
};

static void fonte_alvo(void *d, struct wl_data_source *f, const char *tipo) {}

static void fonte_enviar(void *d, struct wl_data_source *f, const char *tipo, int32_t fd) {
	const char *t = e.copiado ? e.copiado : "";
	size_t n = strlen(t), feito = 0;
	while (feito < n) {
		ssize_t w = write(fd, t + feito, n - feito);
		if (w < 0 && errno == EINTR) {
			continue;
		}
		if (w <= 0) {
			break;
		}
		feito += w;
	}
	close(fd);
}

static void fonte_cancelada(void *d, struct wl_data_source *f) {
	/* outra app copiou por cima */
	wl_data_source_destroy(f);
	if (e.fonte == f) {
		e.fonte = NULL;
	}
}

static void fonte_largada(void *d, struct wl_data_source *f) {}
static void fonte_terminada(void *d, struct wl_data_source *f) {}
static void fonte_acao(void *d, struct wl_data_source *f, uint32_t a) {}

static const struct wl_data_source_listener fonte_ouvinte = {
	.target = fonte_alvo,
	.send = fonte_enviar,
	.cancelled = fonte_cancelada,
	.dnd_drop_performed = fonte_largada,
	.dnd_finished = fonte_terminada,
	.action = fonte_acao,
};

void gv_copiar(const char *texto) {
	if (!e.gerente || !e.dispositivo || !texto) {
		return;
	}
	free(e.copiado);
	e.copiado = strdup(texto);
	struct wl_data_source *fonte = wl_data_device_manager_create_data_source(e.gerente);
	wl_data_source_add_listener(fonte, &fonte_ouvinte, NULL);
	wl_data_source_offer(fonte, MIME_TEXTO);
	wl_data_source_offer(fonte, "text/plain");
	wl_data_source_offer(fonte, "UTF8_STRING");
	wl_data_device_set_selection(e.dispositivo, fonte, gv__entrada.serial_tecla);
	if (e.fonte) {
		wl_data_source_destroy(e.fonte);
	}
	e.fonte = fonte;
}

const char *gv_colar(void) {
	if (e.fonte && e.copiado) {
		/* fomos nós que copiámos: não precisa de pedir ao compositor */
		Clay_String s = gv_str(e.copiado);
		return s.chars;
	}
	if (!e.oferta || !e.oferta_texto) {
		return NULL;
	}
	int canos[2];
	if (pipe2(canos, O_CLOEXEC) < 0) {
		return NULL;
	}
	wl_data_offer_receive(e.oferta, MIME_TEXTO, canos[1]);
	close(canos[1]);
	wl_display_flush(e.display);
	size_t cap = 4096, n = 0;
	char *buf = malloc(cap);
	struct pollfd p = { .fd = canos[0], .events = POLLIN };
	/* quem copiou escreve no cano; espera no máximo meio segundo */
	while (buf && poll(&p, 1, 500) > 0) {
		if (n + 1024 > cap) {
			char *maior = realloc(buf, cap * 2);
			if (!maior) {
				break;
			}
			buf = maior;
			cap *= 2;
		}
		ssize_t r = read(canos[0], buf + n, cap - n - 1);
		if (r <= 0) {
			break;
		}
		n += r;
	}
	close(canos[0]);
	if (!buf) {
		return NULL;
	}
	buf[n] = '\0';
	Clay_String s = gv_str(buf);
	free(buf);
	return s.chars;
}

/* ---- text-input ---- */

static void ti_entrou(void *d, struct zwp_text_input_v3 *ti, struct wl_surface *sup) {
	e.ti_dentro = sup == e.superficie;
	e.ti_ligado = false;
	gv__app_pedir();
}

static void ti_saiu(void *d, struct zwp_text_input_v3 *ti, struct wl_surface *sup) {
	e.ti_dentro = false;
	e.ti_ligado = false;
}

static void ti_compondo(void *d, struct zwp_text_input_v3 *ti, const char *texto, int32_t ini, int32_t fim) {
	snprintf(e.compondo, sizeof(e.compondo), "%s", texto ? texto : "");
	e.compondo_ini = ini;
	e.compondo_fim = fim;
}

static void ti_escrito(void *d, struct zwp_text_input_v3 *ti, const char *texto) {
	if (texto && strlen(e.escrito) + strlen(texto) < sizeof(e.escrito)) {
		strcat(e.escrito, texto);
	}
}

static void ti_apagar(void *d, struct zwp_text_input_v3 *ti, uint32_t antes, uint32_t depois) {
	e.apagar_antes = antes;
	e.apagar_depois = depois;
}

static void ti_feito(void *d, struct zwp_text_input_v3 *ti, uint32_t serial) {
	/* tudo o que veio desde o último done vale junto, no próximo quadro */
	gv__entrada.ime.chegou = true;
	snprintf(gv__entrada.ime.escrito + strlen(gv__entrada.ime.escrito),
		sizeof(gv__entrada.ime.escrito) - strlen(gv__entrada.ime.escrito), "%s", e.escrito);
	snprintf(gv__entrada.ime.compondo, sizeof(gv__entrada.ime.compondo), "%s", e.compondo);
	gv__entrada.ime.compondo_ini = e.compondo_ini;
	gv__entrada.ime.compondo_fim = e.compondo_fim;
	gv__entrada.ime.apagar_antes += e.apagar_antes;
	gv__entrada.ime.apagar_depois += e.apagar_depois;
	e.escrito[0] = '\0';
	e.compondo[0] = '\0';
	e.apagar_antes = e.apagar_depois = 0;
	gv__app_pedir();
}

static const struct zwp_text_input_v3_listener ti_ouvinte = {
	.enter = ti_entrou,
	.leave = ti_saiu,
	.preedit_string = ti_compondo,
	.commit_string = ti_escrito,
	.delete_surrounding_text = ti_apagar,
	.done = ti_feito,
};

bool gv__escrita_global(struct wl_registry *registo, uint32_t nome, const char *iface, uint32_t versao) {
	if (!strcmp(iface, wl_data_device_manager_interface.name)) {
		e.gerente = wl_registry_bind(registo, nome, &wl_data_device_manager_interface, versao < 3 ? versao : 3);
		return true;
	}
	if (!strcmp(iface, zwp_text_input_manager_v3_interface.name)) {
		e.ti_gerente = wl_registry_bind(registo, nome, &zwp_text_input_manager_v3_interface, 1);
		return true;
	}
	return false;
}

void gv__escrita_iniciar(struct wl_display *display, struct wl_seat *seat, struct wl_surface *superficie) {
	e.display = display;
	e.seat = seat;
	e.superficie = superficie;
	if (!seat) {
		return;
	}
	if (e.gerente) {
		e.dispositivo = wl_data_device_manager_get_data_device(e.gerente, seat);
		wl_data_device_add_listener(e.dispositivo, &disp_ouvinte, NULL);
	}
	if (e.ti_gerente) {
		e.ti = zwp_text_input_manager_v3_get_text_input(e.ti_gerente, seat);
		zwp_text_input_v3_add_listener(e.ti, &ti_ouvinte, NULL);
	}
}

void gv__campo_pede(const char *texto, int cursor, int ancora, Clay_BoundingBox caixa, uint32_t dica,
		uint32_t proposito) {
	e.quer = true;
	/* um texto longo vai só à volta do cursor (o limite do protocolo) */
	int n = (int)strlen(texto);
	int ini = 0;
	if (n > GV_MAX_RODEIO) {
		ini = cursor - GV_MAX_RODEIO / 2;
		if (ini < 0) {
			ini = 0;
		}
		if (ini + GV_MAX_RODEIO > n) {
			ini = n - GV_MAX_RODEIO;
		}
		/* sem cortar um caractere ao meio */
		while (ini > 0 && (texto[ini] & 0xc0) == 0x80) {
			ini--;
		}
	}
	snprintf(e.texto, sizeof(e.texto), "%.*s", GV_MAX_RODEIO, texto + ini);
	e.cursor = cursor - ini;
	e.ancora = ancora - ini;
	e.caixa = caixa;
	e.dica = dica;
	e.proposito = proposito;
}

void gv__escrita_fim_do_quadro(void) {
	if (!e.ti) {
		e.quer = false;
		return;
	}
	if (e.quer && e.ti_dentro) {
		bool novo = !e.ti_ligado;
		bool mudou = novo || strcmp(e.texto, e.enviado) || e.cursor != e.enviado_cursor ||
			e.ancora != e.enviado_ancora || memcmp(&e.caixa, &e.enviada_caixa, sizeof(e.caixa));
		if (mudou) {
			if (novo) {
				zwp_text_input_v3_enable(e.ti);
				zwp_text_input_v3_set_content_type(e.ti, e.dica, e.proposito);
			}
			zwp_text_input_v3_set_surrounding_text(e.ti, e.texto, e.cursor, e.ancora);
			zwp_text_input_v3_set_cursor_rectangle(e.ti, (int)e.caixa.x, (int)e.caixa.y, (int)e.caixa.width,
				(int)e.caixa.height);
			zwp_text_input_v3_commit(e.ti);
			e.ti_ligado = true;
			memcpy(e.enviado, e.texto, sizeof(e.enviado));
			e.enviado_cursor = e.cursor;
			e.enviado_ancora = e.ancora;
			e.enviada_caixa = e.caixa;
		}
	} else if (!e.quer && e.ti_ligado) {
		zwp_text_input_v3_disable(e.ti);
		zwp_text_input_v3_commit(e.ti);
		e.ti_ligado = false;
		e.enviado[0] = '\0';
	}
	e.quer = false;
}
