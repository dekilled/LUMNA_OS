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
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
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
	const char *oferta_imagem;        /* o tipo da imagem copiada ("image/png"...), ou NULL */
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

/* o que cada oferta tem fica na própria oferta (user_data): em bits 1 texto, 2 png, 4 jpeg, 8 webp, e a lista dos tipos (o arrastar escolhe um) */
#define GV_MAX_TIPOS 12
struct oferta_info {
	uintptr_t bits;
	int n;
	char tipos[GV_MAX_TIPOS][96];
};

static void oferta_destruir(struct wl_data_offer *oferta) {
	free(wl_data_offer_get_user_data(oferta));
	wl_data_offer_destroy(oferta);
}

static void oferta_tipo(void *dados, struct wl_data_offer *oferta, const char *tipo) {
	struct oferta_info *info = wl_data_offer_get_user_data(oferta);
	if (!info) {
		return;
	}
	if (info->n < GV_MAX_TIPOS) {
		snprintf(info->tipos[info->n++], sizeof(info->tipos[0]), "%s", tipo);
	}
	uintptr_t bits = info->bits;
	if (!strcmp(tipo, MIME_TEXTO) || !strcmp(tipo, "text/plain") || !strcmp(tipo, "UTF8_STRING")) {
		bits |= 1;
	} else if (!strcmp(tipo, "image/png")) {
		bits |= 2;
	} else if (!strcmp(tipo, "image/jpeg")) {
		bits |= 4;
	} else if (!strcmp(tipo, "image/webp")) {
		bits |= 8;
	}
	info->bits = bits;
}

static void oferta_acoes(void *d, struct wl_data_offer *o, uint32_t a) {}
static void oferta_acao(void *d, struct wl_data_offer *o, uint32_t a) {}

static const struct wl_data_offer_listener oferta_ouvinte = {
	.offer = oferta_tipo,
	.source_actions = oferta_acoes,
	.action = oferta_acao,
};

static void disp_oferta(void *d, struct wl_data_device *dev, struct wl_data_offer *oferta) {
	wl_data_offer_add_listener(oferta, &oferta_ouvinte, NULL); /* (isto põe o user_data a NULL: o nosso vem depois) */
	wl_data_offer_set_user_data(oferta, calloc(1, sizeof(struct oferta_info)));
}

/* ---- arrastar e soltar: o que esta app recebe ---- */

#define GV_MAX_ACEITOS 4
static char aceitos[GV_MAX_ACEITOS][96];
static int n_aceitos;
static struct {
	struct wl_data_offer *oferta;
	char mime[96];     /* o tipo aceito para esta oferta ("" nenhum) */
	bool sobre;
	double x, y;
	char *dados;       /* o que chegou no soltar (uma vez) */
	char dados_mime[96];
} dnd;

void gv_arraste_aceitar(const char *mime) {
	for (int i = 0; i < n_aceitos; i++) {
		if (!strcmp(aceitos[i], mime)) {
			return;
		}
	}
	if (n_aceitos < GV_MAX_ACEITOS) {
		snprintf(aceitos[n_aceitos++], sizeof(aceitos[0]), "%.95s", mime);
	}
}

static void dnd_largar_oferta(void) {
	if (dnd.oferta) {
		oferta_destruir(dnd.oferta);
		dnd.oferta = NULL;
	}
	dnd.sobre = false;
	dnd.mime[0] = '\0';
}

static void disp_entrou(void *d, struct wl_data_device *dev, uint32_t serial, struct wl_surface *sup, wl_fixed_t x,
		wl_fixed_t y, struct wl_data_offer *oferta) {
	dnd_largar_oferta();
	if (getenv("GV_DEBUG_DND")) {
		fprintf(stderr, "gravity: arrastar: entrou (oferta %p) em %g,%g\n", (void *)oferta, wl_fixed_to_double(x), wl_fixed_to_double(y));
	}
	if (!oferta) {
		return;
	}
	dnd.oferta = oferta;
	dnd.x = wl_fixed_to_double(x);
	dnd.y = wl_fixed_to_double(y);
	struct oferta_info *info = wl_data_offer_get_user_data(oferta);
	if (getenv("GV_DEBUG_DND")) {
		fprintf(stderr, "gravity: arrastar: aceitos=%d, tipos da oferta=%d (%s)\n", n_aceitos, info ? info->n : -1, info && info->n ? info->tipos[0] : "-");
	}
	for (int i = 0; info && i < info->n && !dnd.mime[0]; i++) {
		for (int k = 0; k < n_aceitos; k++) {
			if (!strcmp(info->tipos[i], aceitos[k])) {
				snprintf(dnd.mime, sizeof(dnd.mime), "%.95s", aceitos[k]);
				break;
			}
		}
	}
	wl_data_offer_accept(oferta, serial, dnd.mime[0] ? dnd.mime : NULL);
	if (dnd.mime[0] && wl_data_offer_get_version(oferta) >= 3) {
		wl_data_offer_set_actions(oferta, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
	}
	dnd.sobre = dnd.mime[0] != '\0';
	gv__app_pedir();
}

static void disp_saiu(void *d, struct wl_data_device *dev) {
	dnd_largar_oferta();
	gv__app_pedir();
}

static void disp_moveu(void *d, struct wl_data_device *dev, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
	dnd.x = wl_fixed_to_double(x);
	dnd.y = wl_fixed_to_double(y);
	gv__app_pedir();
}

/* soltou em cima de nós: pede os dados à origem (que escreve no cano) e termina o arraste */
static const char *arr_dados_se(const char *mime); /* o que estamos arrastando, se for deste tipo */

static void disp_largou(void *d, struct wl_data_device *dev) {
	struct wl_data_offer *oferta = dnd.oferta;
	if (getenv("GV_DEBUG_DND")) {
		fprintf(stderr, "gravity: arrastar: largou (oferta %p, mime \"%s\")\n", (void *)oferta, dnd.mime);
	}
	if (!oferta || !dnd.mime[0]) {
		dnd_largar_oferta();
		return;
	}
	const char *nossos = arr_dados_se(dnd.mime);
	if (nossos) {
		/* a fonte somos nós: pedir pelo cano travava (o envio só é atendido depois deste retorno) */
		free(dnd.dados);
		dnd.dados = strdup(nossos);
		snprintf(dnd.dados_mime, sizeof(dnd.dados_mime), "%s", dnd.mime);
		if (wl_data_offer_get_version(oferta) >= 3) {
			wl_data_offer_finish(oferta);
		}
		dnd_largar_oferta();
		gv__app_pedir();
		return;
	}
	int canos[2];
	if (pipe2(canos, O_CLOEXEC) < 0) {
		dnd_largar_oferta();
		return;
	}
	wl_data_offer_receive(oferta, dnd.mime, canos[1]);
	close(canos[1]);
	wl_display_flush(e.display);
	size_t cap = 4096, n = 0;
	char *buf = malloc(cap);
	struct pollfd p = { .fd = canos[0], .events = POLLIN };
	while (buf && n < (1u << 20) && poll(&p, 1, 1500) > 0) {
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
		n += (size_t)r;
	}
	close(canos[0]);
	if (buf) {
		buf[n] = '\0';
	}
	if (wl_data_offer_get_version(oferta) >= 3) {
		wl_data_offer_finish(oferta);
	}
	free(dnd.dados);
	dnd.dados = buf;
	snprintf(dnd.dados_mime, sizeof(dnd.dados_mime), "%s", dnd.mime);
	dnd_largar_oferta();
	gv__app_pedir();
}

bool gv_arraste_sobre(Clay_Vector2 *pos) {
	if (dnd.sobre && pos) {
		pos->x = (float)dnd.x;
		pos->y = (float)dnd.y;
	}
	return dnd.sobre;
}

/* chegou um soltar do tipo mime: devolve os dados (g_free/free) uma vez */
bool gv_arraste_soltou(const char *mime, char **dados) {
	if (!dnd.dados || strcmp(dnd.dados_mime, mime)) {
		return false;
	}
	*dados = dnd.dados;
	dnd.dados = NULL;
	return true;
}

static void disp_selecao(void *d, struct wl_data_device *dev, struct wl_data_offer *oferta) {
	if (e.oferta && e.oferta != oferta) {
		oferta_destruir(e.oferta);
	}
	e.oferta = oferta;
	struct oferta_info *info = oferta ? wl_data_offer_get_user_data(oferta) : NULL;
	uintptr_t bits = info ? info->bits : 0;
	e.oferta_texto = bits & 1;
	e.oferta_imagem = bits & 2 ? "image/png" : bits & 4 ? "image/jpeg" : bits & 8 ? "image/webp" : NULL;
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

/* ---- arrastar e soltar: o que esta app arrasta ---- */

static struct {
	struct wl_data_source *fonte;
	char *mime;
	char *dados;
	struct wl_surface *icone;
	bool em_curso;
} arr;

static const char *arr_dados_se(const char *mime) {
	return arr.em_curso && arr.dados && arr.mime && !strcmp(arr.mime, mime) ? arr.dados : NULL;
}

static void arr_acabou(void) {
	if (arr.fonte) {
		wl_data_source_destroy(arr.fonte);
	}
	if (arr.icone) {
		wl_surface_destroy(arr.icone);
		gv__arraste_icone_soltar();
	}
	free(arr.mime);
	free(arr.dados);
	memset(&arr, 0, sizeof(arr));
	gv__app_pedir();
}

static void arr_enviar(void *d, struct wl_data_source *f, const char *tipo, int32_t fd) {
	const char *t = arr.dados ? arr.dados : "";
	size_t n = strlen(t), feito = 0;
	while (feito < n) {
		ssize_t w = write(fd, t + feito, n - feito);
		if (w < 0 && errno == EINTR) {
			continue;
		}
		if (w <= 0) {
			break;
		}
		feito += (size_t)w;
	}
	close(fd);
}

static bool arr_ultimo_ok;

static void arr_cancelado(void *d, struct wl_data_source *f) {
	if (getenv("GV_DEBUG_DND")) {
		fprintf(stderr, "gravity: arrastar: cancelado\n");
	}
	arr_acabou(); /* ninguém aceitou, ou o compositor cancelou (Esc) */
	arr_ultimo_ok = false;
}

static void arr_terminado(void *d, struct wl_data_source *f) {
	if (getenv("GV_DEBUG_DND")) {
		fprintf(stderr, "gravity: arrastar: terminado\n");
	}
	arr_acabou(); /* o destino leu os dados */
	arr_ultimo_ok = true;
}

bool gv_arraste_ultimo_ok(void) {
	return arr_ultimo_ok;
}

static void arr_nada_alvo(void *d, struct wl_data_source *f, const char *tipo) {}
static void arr_nada_largou(void *d, struct wl_data_source *f) {}
static void arr_nada_acao(void *d, struct wl_data_source *f, uint32_t a) {}

static const struct wl_data_source_listener arr_ouvinte = {
	.target = arr_nada_alvo,
	.send = arr_enviar,
	.cancelled = arr_cancelado,
	.dnd_drop_performed = arr_nada_largou,
	.dnd_finished = arr_terminado,
	.action = arr_nada_acao,
};

bool gv_arrastar_comecar(const char *mime, const char *dados, const char *icone) {
	if (arr.em_curso || !e.gerente || !e.dispositivo || !e.superficie || !mime || !dados) {
		return false;
	}
	uint32_t serial = gv__serial_aperto();
	if (getenv("GV_DEBUG_DND")) {
		fprintf(stderr, "gravity: arrastar: serial %u, gerente %p, dispositivo %p\n", serial, (void *)e.gerente, (void *)e.dispositivo);
	}
	if (!serial) {
		return false;
	}
	arr.fonte = wl_data_device_manager_create_data_source(e.gerente);
	wl_data_source_add_listener(arr.fonte, &arr_ouvinte, NULL);
	wl_data_source_offer(arr.fonte, mime);
	if (wl_data_source_get_version(arr.fonte) >= 3) {
		wl_data_source_set_actions(arr.fonte, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
	}
	arr.mime = strdup(mime);
	arr.dados = strdup(dados);
	arr.icone = gv__arraste_icone(icone, 56);
	arr.em_curso = true;
	wl_data_device_start_drag(e.dispositivo, arr.fonte, e.superficie, arr.icone, serial);
	wl_display_flush(e.display);
	gv__app_pedir();
	return true;
}

bool gv_arrastando_daqui(void) {
	return arr.em_curso;
}

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

/* há uma imagem copiada (de outro programa) e nenhum texto? É o caso de um print ou "copiar imagem" */
bool gv_area_tem_imagem(void) {
	return !(e.fonte && e.copiado) && e.oferta && e.oferta_imagem && !e.oferta_texto;
}

/* grava a imagem copiada em pasta/colado-AAAAMMDD-HHMMSS.ext e devolve o caminho em saida; false se não deu */
bool gv_colar_imagem(const char *pasta, char *saida, size_t tam) {
	if (!e.oferta || !e.oferta_imagem) {
		return false;
	}
	int canos[2];
	if (pipe2(canos, O_CLOEXEC) < 0) {
		return false;
	}
	const char *tipo = e.oferta_imagem;
	wl_data_offer_receive(e.oferta, tipo, canos[1]);
	close(canos[1]);
	wl_display_flush(e.display);
	size_t cap = 1 << 20, n = 0;
	const size_t LIMITE = 20u << 20; /* o servidor também limita: nada além de 20 MB */
	char *buf = malloc(cap);
	struct pollfd p = { .fd = canos[0], .events = POLLIN };
	while (buf && n < LIMITE && poll(&p, 1, 1500) > 0) {
		if (n + 65536 > cap) {
			char *maior = realloc(buf, cap * 2);
			if (!maior) {
				break;
			}
			buf = maior;
			cap *= 2;
		}
		ssize_t r = read(canos[0], buf + n, cap - n);
		if (r <= 0) {
			break;
		}
		n += (size_t)r;
	}
	close(canos[0]);
	bool ok = false;
	if (buf && n > 0 && n < LIMITE) {
		mkdir(pasta, 0700);
		time_t agora = time(NULL);
		struct tm t;
		localtime_r(&agora, &t);
		char hora[32];
		strftime(hora, sizeof(hora), "%Y%m%d-%H%M%S", &t);
		snprintf(saida, tam, "%s/colado-%s.%s", pasta, hora, !strcmp(tipo, "image/png") ? "png" : !strcmp(tipo, "image/jpeg") ? "jpg" : "webp");
		FILE *f = fopen(saida, "wb");
		if (f) {
			ok = fwrite(buf, 1, n, f) == n;
			fclose(f);
		}
	}
	free(buf);
	return ok;
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
	signal(SIGPIPE, SIG_IGN); /* quem lê o que enviamos (copiar, arrastar) pode fechar o cano: é uma falha da escrita, não o fim do app */
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

void gv__escrita_superficie(struct wl_surface *superficie) {
	e.superficie = superficie;
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
