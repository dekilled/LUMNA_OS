#define _GNU_SOURCE
#include "orbit.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

/*
 * O histórico de copiados: os últimos 20 textos que passaram pela área de transferência do seat, do mais novo para o mais antigo.
 * Cópia nova entra na frente (repetida, só sobe) e a vigésima primeira sai. Fica só na memória do Orbit (sai ao reiniciar):
 * senhas copiadas não vão parar num arquivo. Quem copia com a marca de gerenciador de senhas (x-kde-passwordManagerHint) não entra.
 *   orbit-msg copiados          a lista (uma prévia de cada)
 *   orbit-msg copiar N          o N-ésimo volta a ser o copiado (e sobe para o primeiro)
 *   orbit-msg limpar_copiados   esvazia
 */

#define MAX_ITENS 20
#define MAX_BYTES (256 * 1024)
#define PREVIA 240
#define SEGREDO "x-kde-passwordManagerHint"

static const char *const TIPOS[] = { "text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING", "TEXT" };
#define NTIPOS ((int)(sizeof(TIPOS) / sizeof(TIPOS[0])))

struct item {
	char *texto;
	size_t tam;
	time_t em;
};

static struct {
	struct orbit_server *server;
	struct wl_listener set_selection;
	struct item itens[MAX_ITENS];
	int n;
} h;

static struct wl_event_loop *laco(void) {
	return wl_display_get_event_loop(h.server->display);
}

/* ---- a lista ---- */

static bool utf8_valido(const char *s, size_t n) {
	size_t i = 0;
	while (i < n) {
		unsigned char c = (unsigned char)s[i];
		size_t k = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
		if (!k || i + k > n) {
			return false;
		}
		for (size_t j = 1; j < k; j++) {
			if (((unsigned char)s[i + j] >> 6) != 2) {
				return false;
			}
		}
		i += k;
	}
	return true;
}

static void subir(int i) {
	struct item t = h.itens[i];
	memmove(&h.itens[1], &h.itens[0], (size_t)i * sizeof(struct item));
	h.itens[0] = t;
}

static void adicionar(char *texto, size_t tam) {
	size_t a = 0;
	while (a < tam && (texto[a] == ' ' || texto[a] == '\t' || texto[a] == '\n' || texto[a] == '\r')) {
		a++;
	}
	if (a == tam || !utf8_valido(texto, tam)) {
		free(texto); /* vazio ou não é texto */
		return;
	}
	for (int i = 0; i < h.n; i++) {
		if (h.itens[i].tam == tam && !memcmp(h.itens[i].texto, texto, tam)) {
			free(texto);
			h.itens[i].em = time(NULL);
			subir(i);
			return;
		}
	}
	if (h.n == MAX_ITENS) {
		free(h.itens[MAX_ITENS - 1].texto);
		h.n--;
	}
	memmove(&h.itens[1], &h.itens[0], (size_t)h.n * sizeof(struct item));
	h.itens[0] = (struct item){ .texto = texto, .tam = tam, .em = time(NULL) };
	h.n++;
}

/* ---- ler o que foi copiado (do programa que copiou, por um cano) ---- */

struct leitura {
	int fd;
	struct wl_event_source *fonte, *prazo;
	char *dados;
	size_t tam;
};

static void leitura_fim(struct leitura *l, bool guardar) {
	wl_event_source_remove(l->fonte);
	wl_event_source_remove(l->prazo);
	close(l->fd);
	if (guardar && l->tam > 0) {
		l->dados = realloc(l->dados, l->tam + 1);
		l->dados[l->tam] = '\0';
		adicionar(l->dados, l->tam);
	} else {
		free(l->dados);
	}
	free(l);
}

static int leitura_pronta(int fd, uint32_t mascara, void *dados) {
	struct leitura *l = dados;
	char tmp[8192];
	for (;;) {
		ssize_t n = read(fd, tmp, sizeof(tmp));
		if (n > 0) {
			if (l->tam + (size_t)n > MAX_BYTES) {
				leitura_fim(l, false); /* grande demais: não guarda */
				return 0;
			}
			l->dados = realloc(l->dados, l->tam + (size_t)n + 1);
			memcpy(l->dados + l->tam, tmp, (size_t)n);
			l->tam += (size_t)n;
		} else if (n == 0) {
			leitura_fim(l, true);
			return 0;
		} else if (errno == EAGAIN || errno == EINTR) {
			return 0;
		} else {
			leitura_fim(l, false);
			return 0;
		}
	}
}

static int leitura_prazo(void *dados) {
	leitura_fim(dados, false); /* quem copiou não respondeu a tempo */
	return 0;
}

/* ---- oferecer um item de volta (a fonte que o Orbit mesmo cria) ---- */

struct fonte_item {
	struct wlr_data_source base;
	char *texto;
	size_t tam;
};

struct escrita {
	int fd;
	struct wl_event_source *fonte;
	char *dados;
	size_t tam, feito;
};

static void escrita_fim(struct escrita *e) {
	wl_event_source_remove(e->fonte);
	close(e->fd);
	free(e->dados);
	free(e);
}

static int escrita_pronta(int fd, uint32_t mascara, void *dados) {
	struct escrita *e = dados;
	while (e->feito < e->tam) {
		ssize_t n = write(fd, e->dados + e->feito, e->tam - e->feito);
		if (n > 0) {
			e->feito += (size_t)n;
		} else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			return 0;
		} else {
			break;
		}
	}
	escrita_fim(e);
	return 0;
}

static void item_send(struct wlr_data_source *fonte, const char *mime, int32_t fd) {
	struct fonte_item *f = wl_container_of(fonte, f, base);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	struct escrita *e = calloc(1, sizeof(*e));
	if (!e) {
		close(fd);
		return;
	}
	e->fd = fd;
	e->dados = malloc(f->tam);
	if (!e->dados) {
		free(e);
		close(fd);
		return;
	}
	memcpy(e->dados, f->texto, f->tam);
	e->tam = f->tam;
	e->fonte = wl_event_loop_add_fd(laco(), fd, WL_EVENT_WRITABLE, escrita_pronta, e);
	escrita_pronta(fd, WL_EVENT_WRITABLE, e);
}

static void item_destroy(struct wlr_data_source *fonte) {
	struct fonte_item *f = wl_container_of(fonte, f, base);
	free(f->texto);
	free(f);
}

static const struct wlr_data_source_impl item_impl = { .send = item_send, .destroy = item_destroy };

/* ---- a seleção mudou: guarda ---- */

static void selecao_mudou(struct wl_listener *listener, void *data) {
	struct wlr_data_source *fonte = h.server->seat->selection_source;
	if (!fonte || fonte->impl == &item_impl) {
		return;
	}
	const char *escolhido = NULL;
	int melhor = NTIPOS;
	char **p;
	wl_array_for_each(p, &fonte->mime_types) {
		if (!strcmp(*p, SEGREDO)) {
			return; /* um gerenciador de senhas: não guarda */
		}
		for (int i = 0; i < NTIPOS; i++) {
			if (!strcmp(*p, TIPOS[i]) && i < melhor) {
				melhor = i;
				escolhido = TIPOS[i];
			}
		}
	}
	if (!escolhido) {
		return; /* imagem, arquivos...: só texto */
	}
	int cano[2];
	if (pipe2(cano, O_CLOEXEC | O_NONBLOCK) < 0) {
		return;
	}
	struct leitura *l = calloc(1, sizeof(*l));
	if (!l) {
		close(cano[0]);
		close(cano[1]);
		return;
	}
	l->fd = cano[0];
	l->fonte = wl_event_loop_add_fd(laco(), cano[0], WL_EVENT_READABLE, leitura_pronta, l);
	l->prazo = wl_event_loop_add_timer(laco(), leitura_prazo, l);
	wl_event_source_timer_update(l->prazo, 3000);
	wlr_data_source_send(fonte, escolhido, cano[1]); /* quem tem o texto escreve e fecha a ponta de cá */
}

/* ---- o que o ipc usa ---- */

char *orbit_copiados_lista(void) {
	size_t cap = 4096, len = 0;
	char *s = malloc(cap);
	if (!s) {
		return NULL;
	}
	len += (size_t)snprintf(s, cap, "[");
	for (int i = 0; i < h.n; i++) {
		struct item *it = &h.itens[i];
		size_t pv = it->tam < PREVIA ? it->tam : PREVIA;
		while (pv > 0 && pv < it->tam && ((unsigned char)it->texto[pv] & 0xC0) == 0x80) {
			pv--; /* não corta no meio de um caractere */
		}
		/* uma prévia escapada: o dobro do tamanho basta, com a margem das aspas e dos números */
		size_t precisa = len + pv * 6 + 128;
		if (precisa > cap) {
			cap = precisa * 2;
			s = realloc(s, cap);
		}
		len += (size_t)snprintf(s + len, cap - len, "%s{\"n\":%d,\"tam\":%zu,\"em\":%ld,\"texto\":\"", i ? "," : "", i, it->tam, (long)it->em);
		for (size_t k = 0; k < pv; k++) {
			unsigned char c = (unsigned char)it->texto[k];
			if (c == '"' || c == '\\') {
				len += (size_t)snprintf(s + len, cap - len, "\\%c", c);
			} else if (c == '\n') {
				len += (size_t)snprintf(s + len, cap - len, "\\n");
			} else if (c < 0x20) {
				len += (size_t)snprintf(s + len, cap - len, "\\u%04x", c);
			} else {
				s[len++] = (char)c;
			}
		}
		len += (size_t)snprintf(s + len, cap - len, "%s\"}", pv < it->tam ? "…" : "");
	}
	len += (size_t)snprintf(s + len, cap - len, "]");
	return s;
}

bool orbit_copiados_usar(int n) {
	if (n < 0 || n >= h.n) {
		return false;
	}
	struct fonte_item *f = calloc(1, sizeof(*f));
	if (!f) {
		return false;
	}
	f->texto = malloc(h.itens[n].tam + 1);
	if (!f->texto) {
		free(f);
		return false;
	}
	memcpy(f->texto, h.itens[n].texto, h.itens[n].tam + 1);
	f->tam = h.itens[n].tam;
	wlr_data_source_init(&f->base, &item_impl);
	for (int i = 0; i < NTIPOS; i++) {
		char **p = wl_array_add(&f->base.mime_types, sizeof(*p));
		if (p) {
			*p = strdup(TIPOS[i]);
		}
	}
	h.itens[n].em = time(NULL);
	subir(n);
	wlr_seat_set_selection(h.server->seat, &f->base, wl_display_next_serial(h.server->display));
	return true;
}

void orbit_copiados_limpar(void) {
	for (int i = 0; i < h.n; i++) {
		free(h.itens[i].texto);
	}
	h.n = 0;
}

void orbit_copiados_iniciar(struct orbit_server *server) {
	h.server = server;
	h.set_selection.notify = selecao_mudou;
	wl_signal_add(&server->seat->events.set_selection, &h.set_selection);
	wlr_log(WLR_INFO, "histórico de copiados ligado (até %d)", MAX_ITENS);
}
