/*
 * O Gravity no Wayland: a superfície (janela ou camada), os buffers em memória partilhada, a
 * escala do ecrã, o ponteiro, o toque e o teclado, e o ciclo dos quadros.
 *
 * Um quadro só é desenhado quando algo mudou (entrada, tamanho, uma animação a correr) e ao
 * ritmo do ecrã: depois de cada commit o próximo espera o frame callback do compositor.
 */
#define _GNU_SOURCE
#include "gv_interno.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define GV_MAX_SAIDAS 8
#define GV_MAX_VIGIAS 8
#define GV_MAX_POPUPS 4
#define GV_POPUP_MARGEM 28 /* à volta do popup, para a sombra */

struct gv_buffer {
	struct wl_buffer *wl;
	void *dados;
	size_t tamanho;
	int largura, altura; /* em pixels do ecrã */
	bool ocupado;
	cairo_surface_t *superficie;
};

struct gv_saida {
	struct wl_output *wl;
	uint32_t global;
	int escala;
	bool dentro; /* a nossa superfície está nela */
	char nome[64]; /* "HDMI-A-1", "eDP-1"... (wl_output v4) */
};

/* Um popup: o conteúdo é declarado no layout da app (GV_POPUP), num elemento flutuante com o
 * seu zIndex; esses comandos de desenho vão para esta superfície em vez da principal. As
 * coordenadas são as da superfície principal: o popup fica onde o compositor o pôs (x, y). */
struct gv_popup {
	uint32_t id;           /* 0: livre */
	bool aberto;           /* a app quer-o aberto */
	bool declarado;        /* apareceu no layout deste quadro */
	bool configurado;
	struct wl_surface *sup;
	struct xdg_surface *xdg_sup;
	struct xdg_popup *xdg_popup;
	struct gv_buffer buffers[2];
	int x, y;              /* onde está, em coordenadas da superfície principal */
	int w, h;              /* o tamanho pedido ao compositor */
	Clay_BoundingBox ancora;
	gv_popup_opcoes opcoes;
	double desde;          /* quando apareceu (para o esmaecer) */
	uint32_t token;
};

struct gv_vigia {
	int fd;
	void (*pronto)(gv_app *app, int fd, void *dados);
	void *dados;
};

struct gv_app {
	gv_opcoes opcoes;
	struct wl_display *display;
	struct wl_registry *registo;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct xdg_wm_base *xdg;
	struct zwlr_layer_shell_v1 *camadas;
	struct gv_saida saidas[GV_MAX_SAIDAS];
	int nsaidas;

	struct wl_surface *superficie;
	struct xdg_surface *xdg_superficie;
	struct xdg_toplevel *janela;
	struct zwlr_layer_surface_v1 *camada;
	struct wl_callback *quadro_cb;

	struct wl_pointer *ponteiro;
	struct wl_touch *toque;
	struct wl_keyboard *teclado;
	struct wl_cursor_theme *cursores;
	struct wl_surface *cursor_sup;
	uint32_t ponteiro_serial;
	uint32_t aperto_serial; /* o último aperto (ponteiro ou dedo), para mover a janela */
	bool maximizada;
	int32_t dedo; /* o dedo que manda (-1: nenhum) */

	struct xkb_context *xkb;
	struct xkb_keymap *mapa;
	struct xkb_state *estado;
	struct xkb_compose_state *compor; /* teclas mortas: ´ + a = á */
	int repete_fd;                    /* timerfd da repetição de tecla */
	uint32_t repete_tecla;            /* a tecla segura (0: nenhuma) */
	int repete_taxa, repete_atraso;   /* por segundo / ms */
	double acordar;                   /* um quadro pedido para esta hora (0: nenhum) */
	/* teclas que esperam o quadro seguinte para manter a ordem (ver entregar) */
	struct tecla_pendente {
		uint32_t sym;
		char utf8[16];
		uint32_t mods;
	} pendentes[64];
	int npendentes;
	bool quadro_com_acao; /* já houve uma tecla de ação (Enter, setas...) neste quadro */
	uint32_t mods_reais;  /* os modificadores agora; gv__entrada.mods são os das teclas do quadro */

	struct gv_buffer buffers[2];
	int largura, altura; /* lógicos */
	int escala;
	bool configurado;
	bool precisa;   /* há algo novo para desenhar */
	bool a_correr;
	int codigo;

	struct gv_vigia vigias[GV_MAX_VIGIAS];
	int nvigias;

	struct gv_popup popups[GV_MAX_POPUPS];
	float ponteiro_dx, ponteiro_dy; /* da superfície onde está o ponteiro à principal */
	bool ponteiro_no_popup;
	float dedo_dx, dedo_dy;
	bool dedo_no_popup;
	uint32_t xdg_versao;
	uint32_t entrada_id;          /* gv_app_entrada_em: o elemento que recebe a entrada */
	bool saidas_mudaram;          /* uma tela entrou ou saiu desde o último gv_saidas_mudaram */
	Clay_BoundingBox entrada_caixa;

	Clay_Arena arena;
	double ultimo_quadro;
};

struct gv_entrada gv__entrada = { .x = -1, .y = -1 };
static gv_app *atual;

double gv_agora(void) {
	static struct timespec inicio;
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	if (!inicio.tv_sec && !inicio.tv_nsec) {
		inicio = t;
	}
	return (t.tv_sec - inicio.tv_sec) + (t.tv_nsec - inicio.tv_nsec) / 1e9;
}

void gv_app_redesenhar(gv_app *app) {
	if (!app) {
		app = atual;
	}
	if (app) {
		app->precisa = true;
	}
}

void gv__app_acordar_em(double segundos) {
	if (!atual) {
		return;
	}
	double quando = gv_agora() + segundos;
	if (atual->acordar <= 0 || quando < atual->acordar) {
		atual->acordar = quando;
	}
}

void gv_acordar_em(double segundos) {
	gv__app_acordar_em(segundos);
}

int gv__app_escala(void) {
	return atual && atual->escala > 0 ? atual->escala : 1;
}

void gv__app_pedir(void) {
	if (atual) {
		atual->precisa = true;
	}
}

float gv_app_largura(gv_app *app) {
	return app->largura;
}

float gv_app_altura(gv_app *app) {
	return app->altura;
}

void gv_app_minimizar(gv_app *app) {
	if (app->janela) {
		xdg_toplevel_set_minimized(app->janela);
	}
}

void gv_app_maximizar(gv_app *app) {
	if (!app->janela) {
		return;
	}
	if (app->maximizada) {
		xdg_toplevel_unset_maximized(app->janela);
	} else {
		xdg_toplevel_set_maximized(app->janela);
	}
}

void gv_app_mover(gv_app *app) {
	if (!app->janela || !app->seat) {
		return;
	}
	xdg_toplevel_move(app->janela, app->seat, app->aperto_serial);
	/* o compositor fica com o arrasto: o soltar não chega cá */
	gv__entrada.baixo = false;
	gv__entrada.ativo = 0;
}

const char *gv__saida_nome(struct wl_output *saida) {
	for (int i = 0; atual && i < atual->nsaidas; i++) {
		if (atual->saidas[i].wl == saida) {
			return atual->saidas[i].nome;
		}
	}
	return "";
}

int gv_saidas(gv_app *app, const char **nomes, int max) {
	int n = 0;
	for (int i = 0; i < app->nsaidas && n < max; i++) {
		if (app->saidas[i].nome[0]) {
			nomes[n++] = app->saidas[i].nome;
		}
	}
	return n;
}

bool gv_saidas_mudaram(gv_app *app) {
	bool m = app->saidas_mudaram;
	app->saidas_mudaram = false;
	return m;
}

void gv_app_camada(gv_app *app, int largura, int altura, int zona_exclusiva) {
	if (!app->camada) {
		return;
	}
	zwlr_layer_surface_v1_set_size(app->camada, largura > 0 ? largura : 0, altura > 0 ? altura : 0);
	zwlr_layer_surface_v1_set_exclusive_zone(app->camada, zona_exclusiva);
	app->opcoes.zona_exclusiva = zona_exclusiva;
	/* vale no próximo commit; o configure traz o tamanho novo */
	wl_surface_commit(app->superficie);
	app->precisa = true;
}

void gv_app_camada_ancoras(gv_app *app, uint32_t ancoras, bool teclado_exclusivo) {
	if (!app->camada) {
		return;
	}
	zwlr_layer_surface_v1_set_anchor(app->camada, ancoras);
	zwlr_layer_surface_v1_set_keyboard_interactivity(app->camada,
		teclado_exclusivo ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
		: app->opcoes.teclado ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
				      : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	app->opcoes.ancoras = ancoras;
	app->opcoes.teclado_exclusivo = teclado_exclusivo;
}

void gv_app_entrada_em(gv_app *app, Clay_ElementId id) {
	if (app->entrada_id != id.id) {
		app->entrada_caixa = (Clay_BoundingBox){ -1, -1, -1, -1 }; /* outro elemento: refazer a região */
	}
	app->entrada_id = id.id;
}

/* depois do layout: a região de entrada segue a caixa do elemento escolhido */
static void entrada_rever(gv_app *app) {
	if (!app->entrada_id) {
		return;
	}
	Clay_ElementData d = Clay_GetElementData((Clay_ElementId){ .id = app->entrada_id });
	if (!d.found) {
		return;
	}
	Clay_BoundingBox b = d.boundingBox;
	if (!memcmp(&b, &app->entrada_caixa, sizeof(b))) {
		return;
	}
	app->entrada_caixa = b;
	struct wl_region *regiao = wl_compositor_create_region(app->compositor);
	wl_region_add(regiao, (int)floorf(b.x), (int)floorf(b.y), (int)ceilf(b.width), (int)ceilf(b.height));
	wl_surface_set_input_region(app->superficie, regiao);
	wl_region_destroy(regiao);
}

void gv_app_sair(gv_app *app) {
	app->a_correr = false;
}

void gv_app_vigiar(gv_app *app, int fd, void (*pronto)(gv_app *, int, void *), void *dados) {
	if (app->nvigias >= GV_MAX_VIGIAS) {
		fprintf(stderr, "gravity: mais de %d descritores vigiados\n", GV_MAX_VIGIAS);
		return;
	}
	app->vigias[app->nvigias++] = (struct gv_vigia){ fd, pronto, dados };
}

/* ---- buffers ---- */

static void buffer_libertado(void *dados, struct wl_buffer *wl) {
	struct gv_buffer *buffer = dados;
	buffer->ocupado = false;
	(void)wl;
}

static const struct wl_buffer_listener buffer_ouvinte = { .release = buffer_libertado };

static void buffer_destruir(struct gv_buffer *buffer) {
	if (buffer->superficie) {
		cairo_surface_destroy(buffer->superficie);
	}
	if (buffer->wl) {
		wl_buffer_destroy(buffer->wl);
	}
	if (buffer->dados) {
		munmap(buffer->dados, buffer->tamanho);
	}
	*buffer = (struct gv_buffer){ 0 };
}

static bool buffer_criar(gv_app *app, struct gv_buffer *buffer, int largura, int altura) {
	int passo = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, largura);
	size_t tamanho = (size_t)passo * altura;
	int fd = memfd_create("gravity", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, tamanho) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		return false;
	}
	void *dados = mmap(NULL, tamanho, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (dados == MAP_FAILED) {
		close(fd);
		return false;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, fd, tamanho);
	buffer->wl = wl_shm_pool_create_buffer(pool, 0, largura, altura, passo, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(buffer->wl, &buffer_ouvinte, buffer);
	buffer->dados = dados;
	buffer->tamanho = tamanho;
	buffer->largura = largura;
	buffer->altura = altura;
	buffer->superficie = cairo_image_surface_create_for_data(dados, CAIRO_FORMAT_ARGB32, largura, altura, passo);
	return true;
}

/* Um dos dois buffers livre, no tamanho dado (pixels do ecrã). */
static struct gv_buffer *buffer_livre_em(gv_app *app, struct gv_buffer *buffers, int largura, int altura) {
	for (int i = 0; i < 2; i++) {
		struct gv_buffer *buffer = &buffers[i];
		if (buffer->ocupado) {
			continue;
		}
		if (buffer->wl && (buffer->largura != largura || buffer->altura != altura)) {
			buffer_destruir(buffer);
		}
		if (!buffer->wl && !buffer_criar(app, buffer, largura, altura)) {
			return NULL;
		}
		return buffer;
	}
	return NULL;
}

static struct gv_buffer *buffer_livre(gv_app *app) {
	return buffer_livre_em(app, app->buffers, app->largura * app->escala, app->altura * app->escala);
}

/* ---- escala: a maior das saídas onde a superfície está ---- */

static void escala_rever(gv_app *app) {
	int escala = 1;
	for (int i = 0; i < app->nsaidas; i++) {
		if (app->saidas[i].dentro && app->saidas[i].escala > escala) {
			escala = app->saidas[i].escala;
		}
	}
	if (escala != app->escala) {
		app->escala = escala;
		wl_surface_set_buffer_scale(app->superficie, escala);
		for (int i = 0; i < GV_MAX_POPUPS; i++) {
			if (app->popups[i].sup) {
				wl_surface_set_buffer_scale(app->popups[i].sup, escala);
			}
		}
		if (app->cursores) {
			wl_cursor_theme_destroy(app->cursores);
			app->cursores = NULL;
		}
		app->precisa = true;
	}
}

static void saida_geometria(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph,
		int32_t sub, const char *marca, const char *modelo, int32_t transf) {}
static void saida_modo(void *d, struct wl_output *o, uint32_t f, int32_t w, int32_t h, int32_t r) {}
static void saida_feito(void *d, struct wl_output *o) {}
static void saida_escala(void *dados, struct wl_output *o, int32_t escala) {
	struct gv_saida *saida = dados;
	saida->escala = escala;
	if (atual) {
		escala_rever(atual);
	}
}
static void saida_nome(void *dados, struct wl_output *o, const char *n) {
	struct gv_saida *saida = dados;
	snprintf(saida->nome, sizeof(saida->nome), "%s", n);
}
static void saida_descricao(void *d, struct wl_output *o, const char *n) {}

static const struct wl_output_listener saida_ouvinte = {
	.geometry = saida_geometria,
	.mode = saida_modo,
	.done = saida_feito,
	.scale = saida_escala,
	.name = saida_nome,
	.description = saida_descricao,
};

static void sup_entrou(void *dados, struct wl_surface *s, struct wl_output *wl) {
	gv_app *app = dados;
	for (int i = 0; i < app->nsaidas; i++) {
		if (app->saidas[i].wl == wl) {
			app->saidas[i].dentro = true;
		}
	}
	escala_rever(app);
}

static void sup_saiu(void *dados, struct wl_surface *s, struct wl_output *wl) {
	gv_app *app = dados;
	for (int i = 0; i < app->nsaidas; i++) {
		if (app->saidas[i].wl == wl) {
			app->saidas[i].dentro = false;
		}
	}
	escala_rever(app);
}

static const struct wl_surface_listener sup_ouvinte = { .enter = sup_entrou, .leave = sup_saiu };

/* ---- popups: a entrada que chega a um popup vem nas coordenadas da principal ---- */

static void popup_deslocamento(gv_app *app, struct wl_surface *s, float *dx, float *dy, bool *no_popup) {
	*dx = *dy = 0;
	*no_popup = false;
	for (int i = 0; i < GV_MAX_POPUPS; i++) {
		struct gv_popup *p = &app->popups[i];
		if (p->sup && p->sup == s) {
			*dx = p->x - GV_POPUP_MARGEM;
			*dy = p->y - GV_POPUP_MARGEM;
			*no_popup = true;
		}
	}
}

/* Um aperto na superfície principal com popups abertos fecha-os e não vale para mais nada
 * (como tocar fora de um menu). Fora da app quem fecha é o compositor (o grab). */
static void popups_fechar_fora(gv_app *app) {
	bool fechou = false;
	for (int i = 0; i < GV_MAX_POPUPS; i++) {
		if (app->popups[i].id && app->popups[i].aberto) {
			app->popups[i].aberto = false;
			fechou = true;
		}
	}
	if (fechou) {
		gv__entrada.travado = true;
		gv__entrada.ativo = 0xffffffffu; /* ninguém */
	}
}

/* ---- ponteiro ---- */

static void cursor_aplicar(gv_app *app) {
	if (!app->cursores) {
		app->cursores = wl_cursor_theme_load(getenv("XCURSOR_THEME"), 24 * app->escala, app->shm);
	}
	struct wl_cursor *cursor = app->cursores ? wl_cursor_theme_get_cursor(app->cursores, "default") : NULL;
	if (!cursor && app->cursores) {
		cursor = wl_cursor_theme_get_cursor(app->cursores, "left_ptr");
	}
	if (!cursor) {
		return;
	}
	struct wl_cursor_image *imagem = cursor->images[0];
	if (!app->cursor_sup) {
		app->cursor_sup = wl_compositor_create_surface(app->compositor);
	}
	wl_surface_set_buffer_scale(app->cursor_sup, app->escala);
	wl_surface_attach(app->cursor_sup, wl_cursor_image_get_buffer(imagem), 0, 0);
	wl_surface_damage_buffer(app->cursor_sup, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(app->cursor_sup);
	wl_pointer_set_cursor(app->ponteiro, app->ponteiro_serial, app->cursor_sup,
		imagem->hotspot_x / app->escala, imagem->hotspot_y / app->escala);
}

static void ponteiro_entrou(void *dados, struct wl_pointer *p, uint32_t serial, struct wl_surface *s,
		wl_fixed_t x, wl_fixed_t y) {
	gv_app *app = dados;
	app->ponteiro_serial = serial;
	popup_deslocamento(app, s, &app->ponteiro_dx, &app->ponteiro_dy, &app->ponteiro_no_popup);
	gv__entrada.x = wl_fixed_to_double(x) + app->ponteiro_dx;
	gv__entrada.y = wl_fixed_to_double(y) + app->ponteiro_dy;
	gv__entrada.toque = false;
	cursor_aplicar(app);
	app->precisa = true;
}

static void ponteiro_saiu(void *dados, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) {
	gv_app *app = dados;
	if (!gv__entrada.baixo) {
		gv__entrada.x = gv__entrada.y = -1;
	}
	app->precisa = true;
}

static void ponteiro_moveu(void *dados, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
	gv_app *app = dados;
	gv__entrada.x = wl_fixed_to_double(x) + app->ponteiro_dx;
	gv__entrada.y = wl_fixed_to_double(y) + app->ponteiro_dy;
	gv__entrada.toque = false;
	if (gv__entrada.baixo && fabsf(gv__entrada.x - gv__entrada.aperto_x) + fabsf(gv__entrada.y - gv__entrada.aperto_y) > 10) {
		gv__entrada.segurar_cancelado = true; /* arrastou: não é segurar */
	}
	app->precisa = true;
}

static void ponteiro_botao(void *dados, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t botao,
		uint32_t estado) {
	gv_app *app = dados;
	if (botao == BTN_RIGHT) {
		if (estado == WL_POINTER_BUTTON_STATE_PRESSED) {
			app->aperto_serial = serial; /* o popup do menu precisa do serial para o grab */
			if (!app->ponteiro_no_popup) {
				popups_fechar_fora(app);
			}
			gv__entrada.direito = true;
			app->precisa = true;
		}
		return;
	}
	if (botao != BTN_LEFT) {
		return;
	}
	if (estado == WL_POINTER_BUTTON_STATE_PRESSED) {
		app->aperto_serial = serial;
		gv__entrada.baixo = true;
		gv__entrada.apertou = true;
		gv__entrada.aperto_em = gv_agora();
		gv__entrada.aperto_x = gv__entrada.x;
		gv__entrada.aperto_y = gv__entrada.y;
		gv__entrada.segurar_cancelado = false;
		gv__app_acordar_em(0.5);
		if (!app->ponteiro_no_popup) {
			popups_fechar_fora(app);
		}
	} else {
		gv__entrada.baixo = false;
		gv__entrada.soltou = true;
	}
	app->precisa = true;
}

static void ponteiro_eixo(void *dados, struct wl_pointer *p, uint32_t t, uint32_t eixo, wl_fixed_t valor) {
	gv_app *app = dados;
	if (eixo == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		gv__entrada.roda_y -= wl_fixed_to_double(valor);
	} else {
		gv__entrada.roda_x -= wl_fixed_to_double(valor);
	}
	app->precisa = true;
}

static void ponteiro_quadro(void *d, struct wl_pointer *p) {}
static void ponteiro_fonte(void *d, struct wl_pointer *p, uint32_t f) {}
static void ponteiro_parou(void *d, struct wl_pointer *p, uint32_t t, uint32_t e) {}
static void ponteiro_discreto(void *d, struct wl_pointer *p, uint32_t e, int32_t v) {}
static void ponteiro_v120(void *d, struct wl_pointer *p, uint32_t e, int32_t v) {}
static void ponteiro_direcao(void *d, struct wl_pointer *p, uint32_t e, uint32_t dir) {}

static const struct wl_pointer_listener ponteiro_ouvinte = {
	.enter = ponteiro_entrou,
	.leave = ponteiro_saiu,
	.motion = ponteiro_moveu,
	.button = ponteiro_botao,
	.axis = ponteiro_eixo,
	.frame = ponteiro_quadro,
	.axis_source = ponteiro_fonte,
	.axis_stop = ponteiro_parou,
	.axis_discrete = ponteiro_discreto,
	.axis_value120 = ponteiro_v120,
	.axis_relative_direction = ponteiro_direcao,
};

/* ---- toque: o primeiro dedo faz de ponteiro (os gestos de vários dedos ficam para depois) ---- */

static void toque_baixo(void *dados, struct wl_touch *t, uint32_t serial, uint32_t tempo, struct wl_surface *s,
		int32_t id, wl_fixed_t x, wl_fixed_t y) {
	gv_app *app = dados;
	if (app->dedo >= 0) {
		return;
	}
	app->dedo = id;
	app->aperto_serial = serial;
	popup_deslocamento(app, s, &app->dedo_dx, &app->dedo_dy, &app->dedo_no_popup);
	gv__entrada.x = wl_fixed_to_double(x) + app->dedo_dx;
	gv__entrada.y = wl_fixed_to_double(y) + app->dedo_dy;
	gv__entrada.baixo = true;
	gv__entrada.apertou = true;
	gv__entrada.toque = true;
	/* segurar meio segundo é o menu de contexto: acorda para ver se o dedo ficou */
	gv__entrada.aperto_em = gv_agora();
	gv__entrada.aperto_x = gv__entrada.x;
	gv__entrada.aperto_y = gv__entrada.y;
	gv__entrada.segurar_cancelado = false;
	gv__app_acordar_em(0.5);
	if (!app->dedo_no_popup) {
		popups_fechar_fora(app);
	}
	app->precisa = true;
}

static void toque_cima(void *dados, struct wl_touch *t, uint32_t serial, uint32_t tempo, int32_t id) {
	gv_app *app = dados;
	if (id != app->dedo) {
		return;
	}
	app->dedo = -1;
	gv__entrada.baixo = false;
	gv__entrada.soltou = true;
	app->precisa = true;
}

static void toque_moveu(void *dados, struct wl_touch *t, uint32_t tempo, int32_t id, wl_fixed_t x, wl_fixed_t y) {
	gv_app *app = dados;
	if (id != app->dedo) {
		return;
	}
	gv__entrada.x = wl_fixed_to_double(x) + app->dedo_dx;
	gv__entrada.y = wl_fixed_to_double(y) + app->dedo_dy;
	if (fabsf(gv__entrada.x - gv__entrada.aperto_x) + fabsf(gv__entrada.y - gv__entrada.aperto_y) > 10) {
		gv__entrada.segurar_cancelado = true; /* andou: é arrasto, não é segurar */
	}
	app->precisa = true;
}

static void toque_quadro(void *d, struct wl_touch *t) {}

static void toque_cancelado(void *dados, struct wl_touch *t) {
	gv_app *app = dados;
	app->dedo = -1;
	gv__entrada.baixo = false;
	gv__entrada.ativo = 0;
	gv__entrada.x = gv__entrada.y = -1;
	app->precisa = true;
}

static void toque_forma(void *d, struct wl_touch *t, int32_t id, wl_fixed_t a, wl_fixed_t b) {}
static void toque_orientacao(void *d, struct wl_touch *t, int32_t id, wl_fixed_t o) {}

static const struct wl_touch_listener toque_ouvinte = {
	.down = toque_baixo,
	.up = toque_cima,
	.motion = toque_moveu,
	.frame = toque_quadro,
	.cancel = toque_cancelado,
	.shape = toque_forma,
	.orientation = toque_orientacao,
};

/* ---- teclado ---- */

static void teclado_mapa(void *dados, struct wl_keyboard *k, uint32_t formato, int32_t fd, uint32_t tamanho) {
	gv_app *app = dados;
	if (formato != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *texto = mmap(NULL, tamanho, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (texto == MAP_FAILED) {
		return;
	}
	struct xkb_keymap *mapa = xkb_keymap_new_from_string(app->xkb, texto, XKB_KEYMAP_FORMAT_TEXT_V1,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(texto, tamanho);
	if (!mapa) {
		return;
	}
	if (app->estado) {
		xkb_state_unref(app->estado);
	}
	if (app->mapa) {
		xkb_keymap_unref(app->mapa);
	}
	app->mapa = mapa;
	app->estado = xkb_state_new(mapa);
	if (!app->compor) {
		const char *local = getenv("LC_ALL");
		if (!local || !*local) {
			local = getenv("LC_CTYPE");
		}
		if (!local || !*local) {
			local = getenv("LANG");
		}
		if (!local || !*local) {
			local = "C.UTF-8";
		}
		struct xkb_compose_table *tabela = xkb_compose_table_new_from_locale(app->xkb, local, XKB_COMPOSE_COMPILE_NO_FLAGS);
		if (tabela) {
			app->compor = xkb_compose_state_new(tabela, XKB_COMPOSE_STATE_NO_FLAGS);
			xkb_compose_table_unref(tabela);
		}
	}
}

static void teclado_entrou(void *dados, struct wl_keyboard *k, uint32_t serial, struct wl_surface *sup,
		struct wl_array *teclas) {
	gv__entrada.serial_tecla = serial;
}

static void repeticao_parar(gv_app *app) {
	app->repete_tecla = 0;
	if (app->repete_fd >= 0) {
		struct itimerspec zero = { 0 };
		timerfd_settime(app->repete_fd, 0, &zero, NULL);
	}
}

static void teclado_saiu(void *dados, struct wl_keyboard *k, uint32_t s, struct wl_surface *sup) {
	gv_app *app = dados;
	repeticao_parar(app);
	gv__entrada.mods = 0;
}

/* Uma tecla (já com o texto que escreve) entra no quadro, ou espera pelo seguinte. Num quadro os
 * componentes leem primeiro as teclas de ação e depois o texto; para não trocar a ordem quando
 * várias chegam juntas (o teclado no ecrã, um colar por teclas), uma tecla de ação depois de
 * texto, um texto depois de uma ação, e tudo depois de um Tab (o foco anda no fim do quadro)
 * esperam o quadro seguinte. */
static void entregar(gv_app *app, uint32_t sym, const char *utf8, uint32_t mods) {
	bool escreve = utf8[0] != '\0';
	bool depois_de_tab = false;
	for (int i = 0; i < gv__entrada.nteclas; i++) {
		uint32_t s = gv__entrada.teclas[i];
		depois_de_tab |= s == XKB_KEY_Tab || s == XKB_KEY_ISO_Left_Tab;
	}
	/* uma tecla com outros modificadores que as do quadro também espera: Shift+↑ tem de ver o
	 * Shift mesmo que ele já tenha sido solto quando o quadro lê as teclas */
	bool outros_mods = gv__entrada.nteclas > 0 && mods != gv__entrada.mods;
	bool esperar = app->npendentes > 0 || depois_de_tab || outros_mods || (escreve && app->quadro_com_acao) ||
		(!escreve && gv__entrada.texto[0]);
	app->precisa = true;
	if (esperar) {
		if (app->npendentes < (int)(sizeof(app->pendentes) / sizeof(app->pendentes[0]))) {
			struct tecla_pendente *p = &app->pendentes[app->npendentes++];
			p->sym = sym;
			snprintf(p->utf8, sizeof(p->utf8), "%s", utf8);
			p->mods = mods;
		}
		return;
	}
	if (gv__entrada.nteclas == 0) {
		gv__entrada.mods = mods; /* o quadro vê os modificadores da sua primeira tecla */
	}
	if (gv__entrada.nteclas < GV_MAX_TECLAS) {
		gv__entrada.teclas[gv__entrada.nteclas++] = sym;
	}
	if (escreve) {
		size_t usado = strlen(gv__entrada.texto);
		if (usado + strlen(utf8) < sizeof(gv__entrada.texto)) {
			strcat(gv__entrada.texto, utf8);
		}
	} else if (!(sym >= XKB_KEY_Shift_L && sym <= XKB_KEY_Hyper_R)) {
		app->quadro_com_acao = true;
	}
}

/* Uma tecla carregada (ou repetida): o keysym para os atalhos e o texto para os campos. */
static void tecla_vale(gv_app *app, uint32_t codigo) {
	xkb_keycode_t k = codigo + 8;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(app->estado, k);
	/* Shift+Tab já como ISO_Left_Tab: o Shift pode ser solto antes de o quadro ler as teclas */
	if (sym == XKB_KEY_Tab && (app->mods_reais & GV_MOD_SHIFT)) {
		sym = XKB_KEY_ISO_Left_Tab;
	}
	char utf8[32] = "";
	if (!(app->mods_reais & (GV_MOD_CTRL | GV_MOD_ALT | GV_MOD_LOGO))) {
		if (app->compor && xkb_compose_state_feed(app->compor, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
			switch (xkb_compose_state_get_status(app->compor)) {
			case XKB_COMPOSE_COMPOSING:
				return; /* uma tecla morta: espera a próxima */
			case XKB_COMPOSE_COMPOSED:
				xkb_compose_state_get_utf8(app->compor, utf8, sizeof(utf8));
				xkb_compose_state_reset(app->compor);
				break;
			case XKB_COMPOSE_CANCELLED:
				xkb_compose_state_reset(app->compor);
				return;
			default:
				xkb_state_key_get_utf8(app->estado, k, utf8, sizeof(utf8));
				break;
			}
		} else {
			xkb_state_key_get_utf8(app->estado, k, utf8, sizeof(utf8));
		}
		/* só o que se escreve: Enter, Tab, Backspace e companhia vão pelos keysyms */
		if ((unsigned char)utf8[0] < 0x20 || utf8[0] == 0x7f) {
			utf8[0] = '\0';
		}
	}
	entregar(app, sym, utf8, app->mods_reais);
}

static void teclado_tecla(void *dados, struct wl_keyboard *k, uint32_t serial, uint32_t tempo, uint32_t tecla,
		uint32_t estado) {
	gv_app *app = dados;
	if (!app->estado) {
		return;
	}
	if (estado != WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (tecla == app->repete_tecla) {
			repeticao_parar(app);
		}
		return;
	}
	gv__entrada.serial_tecla = serial;
	tecla_vale(app, tecla);
	if (app->repete_fd >= 0 && app->repete_taxa > 0 && xkb_keymap_key_repeats(app->mapa, tecla + 8)) {
		app->repete_tecla = tecla;
		struct itimerspec t = {
			.it_value = { app->repete_atraso / 1000, (app->repete_atraso % 1000) * 1000000L },
			.it_interval = { 0, 1000000000L / app->repete_taxa },
		};
		timerfd_settime(app->repete_fd, 0, &t, NULL);
	}
}

static void repeticao_pronta(gv_app *app, int fd, void *dados) {
	uint64_t vezes = 0;
	if (read(fd, &vezes, sizeof(vezes)) <= 0 || !app->repete_tecla) {
		return;
	}
	for (uint64_t i = 0; i < vezes && i < 8; i++) {
		tecla_vale(app, app->repete_tecla);
	}
}

static void teclado_mods(void *dados, struct wl_keyboard *k, uint32_t serial, uint32_t baixo, uint32_t preso,
		uint32_t trancado, uint32_t grupo) {
	gv_app *app = dados;
	if (!app->estado) {
		return;
	}
	xkb_state_update_mask(app->estado, baixo, preso, trancado, 0, 0, grupo);
	uint32_t mods = 0;
	if (xkb_state_mod_name_is_active(app->estado, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) {
		mods |= GV_MOD_SHIFT;
	}
	if (xkb_state_mod_name_is_active(app->estado, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) {
		mods |= GV_MOD_CTRL;
	}
	if (xkb_state_mod_name_is_active(app->estado, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) {
		mods |= GV_MOD_ALT;
	}
	if (xkb_state_mod_name_is_active(app->estado, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) {
		mods |= GV_MOD_LOGO;
	}
	app->mods_reais = mods;
	/* com teclas já no quadro, os modificadores delas valem até ao fim dele */
	if (gv__entrada.nteclas == 0 && app->npendentes == 0) {
		gv__entrada.mods = mods;
	}
	app->precisa = true;
}

static void teclado_repete(void *dados, struct wl_keyboard *k, int32_t taxa, int32_t atraso) {
	gv_app *app = dados;
	app->repete_taxa = taxa;
	app->repete_atraso = atraso;
}

static const struct wl_keyboard_listener teclado_ouvinte = {
	.keymap = teclado_mapa,
	.enter = teclado_entrou,
	.leave = teclado_saiu,
	.key = teclado_tecla,
	.modifiers = teclado_mods,
	.repeat_info = teclado_repete,
};

static void seat_capacidades(void *dados, struct wl_seat *seat, uint32_t caps) {
	gv_app *app = dados;
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !app->ponteiro) {
		app->ponteiro = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(app->ponteiro, &ponteiro_ouvinte, app);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && app->ponteiro) {
		wl_pointer_release(app->ponteiro);
		app->ponteiro = NULL;
	}
	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !app->toque) {
		app->toque = wl_seat_get_touch(seat);
		wl_touch_add_listener(app->toque, &toque_ouvinte, app);
	} else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && app->toque) {
		wl_touch_release(app->toque);
		app->toque = NULL;
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !app->teclado) {
		app->teclado = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(app->teclado, &teclado_ouvinte, app);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && app->teclado) {
		wl_keyboard_release(app->teclado);
		app->teclado = NULL;
	}
}

static void seat_nome(void *d, struct wl_seat *s, const char *n) {}

static const struct wl_seat_listener seat_ouvinte = { .capabilities = seat_capacidades, .name = seat_nome };

/* ---- registo ---- */

static void xdg_ping(void *d, struct xdg_wm_base *xdg, uint32_t serial) {
	xdg_wm_base_pong(xdg, serial);
}

static const struct xdg_wm_base_listener xdg_ouvinte = { .ping = xdg_ping };

static void registo_global(void *dados, struct wl_registry *r, uint32_t nome, const char *iface, uint32_t versao) {
	gv_app *app = dados;
	if (!strcmp(iface, wl_compositor_interface.name)) {
		app->compositor = wl_registry_bind(r, nome, &wl_compositor_interface, versao < 4 ? versao : 4);
	} else if (!strcmp(iface, wl_shm_interface.name)) {
		app->shm = wl_registry_bind(r, nome, &wl_shm_interface, 1);
	} else if (!strcmp(iface, wl_seat_interface.name) && !app->seat) {
		app->seat = wl_registry_bind(r, nome, &wl_seat_interface, versao < 8 ? versao : 8);
		wl_seat_add_listener(app->seat, &seat_ouvinte, app);
	} else if (!strcmp(iface, xdg_wm_base_interface.name)) {
		/* v3: popups que mudam de tamanho (reposition) e seguem a janela (reactive) */
		app->xdg_versao = versao < 3 ? versao : 3;
		app->xdg = wl_registry_bind(r, nome, &xdg_wm_base_interface, app->xdg_versao);
		xdg_wm_base_add_listener(app->xdg, &xdg_ouvinte, app);
	} else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
		app->camadas = wl_registry_bind(r, nome, &zwlr_layer_shell_v1_interface, versao < 4 ? versao : 4);
	} else if (gv__escrita_global(r, nome, iface, versao)) {
		/* área de transferência e campo de texto (gv_escrita.c) */
	} else if (gv__janelas_global(r, nome, iface, versao)) {
		/* as janelas abertas (gv_janelas.c) */
	} else if (!strcmp(iface, wl_output_interface.name) && app->nsaidas < GV_MAX_SAIDAS) {
		struct gv_saida *saida = &app->saidas[app->nsaidas++];
		saida->escala = 1;
		saida->global = nome;
		if (app->a_correr) {
			app->saidas_mudaram = true; /* uma tela ligada com a app a correr */
		}
		saida->wl = wl_registry_bind(r, nome, &wl_output_interface, versao < 4 ? versao : 4);
		wl_output_add_listener(saida->wl, &saida_ouvinte, saida);
	}
}

static void registo_saiu(void *dados, struct wl_registry *r, uint32_t nome) {
	gv_app *app = dados;
	for (int i = 0; i < app->nsaidas; i++) {
		if (app->saidas[i].global == nome) {
			wl_output_release(app->saidas[i].wl);
			memmove(&app->saidas[i], &app->saidas[i + 1], (app->nsaidas - i - 1) * sizeof(app->saidas[0]));
			app->nsaidas--;
			app->saidas_mudaram = true;
			app->precisa = true;
			/* a lista é por valor: os ouvintes das que ficaram apontam para o lugar antigo */
			for (int k = i; k < app->nsaidas; k++) {
				wl_output_set_user_data(app->saidas[k].wl, &app->saidas[k]);
			}
			return;
		}
	}
}

static const struct wl_registry_listener registo_ouvinte = { .global = registo_global, .global_remove = registo_saiu };

/* ---- a superfície ---- */

static void tamanho_novo(gv_app *app, int largura, int altura) {
	if (largura > 0) {
		app->largura = largura;
	}
	if (altura > 0) {
		app->altura = altura;
	}
	app->configurado = true;
	app->precisa = true;
}

static void xdg_sup_configurar(void *dados, struct xdg_surface *s, uint32_t serial) {
	xdg_surface_ack_configure(s, serial);
	gv_app *app = dados;
	app->configurado = true;
	app->precisa = true;
}

static const struct xdg_surface_listener xdg_sup_ouvinte = { .configure = xdg_sup_configurar };

static void janela_configurar(void *dados, struct xdg_toplevel *j, int32_t largura, int32_t altura,
		struct wl_array *estados) {
	gv_app *app = dados;
	app->maximizada = false;
	uint32_t *e;
	wl_array_for_each(e, estados) {
		if (*e == XDG_TOPLEVEL_STATE_MAXIMIZED) {
			app->maximizada = true;
		}
	}
	tamanho_novo(dados, largura, altura);
}

static void janela_fechar(void *dados, struct xdg_toplevel *j) {
	gv_app_sair(dados);
}

static void janela_limites(void *d, struct xdg_toplevel *j, int32_t w, int32_t h) {}
static void janela_capacidades(void *d, struct xdg_toplevel *j, struct wl_array *c) {}

static const struct xdg_toplevel_listener janela_ouvinte = {
	.configure = janela_configurar,
	.close = janela_fechar,
	.configure_bounds = janela_limites,
	.wm_capabilities = janela_capacidades,
};

static void camada_configurar(void *dados, struct zwlr_layer_surface_v1 *c, uint32_t serial, uint32_t largura,
		uint32_t altura) {
	zwlr_layer_surface_v1_ack_configure(c, serial);
	tamanho_novo(dados, largura, altura);
}

static void camada_fechar(void *dados, struct zwlr_layer_surface_v1 *c) {
	gv_app_sair(dados);
}

static const struct zwlr_layer_surface_v1_listener camada_ouvinte = {
	.configure = camada_configurar,
	.closed = camada_fechar,
};

static void quadro_pronto(void *dados, struct wl_callback *cb, uint32_t tempo) {
	gv_app *app = dados;
	wl_callback_destroy(cb);
	app->quadro_cb = NULL;
}

static const struct wl_callback_listener quadro_ouvinte = { .done = quadro_pronto };

static void clay_erro(Clay_ErrorData erro) {
	fprintf(stderr, "gravity: layout: %.*s\n", erro.errorText.length, erro.errorText.chars);
}

gv_app *gv_app_criar(const gv_opcoes *opcoes) {
	gv_app *app = calloc(1, sizeof(*app));
	if (!app) {
		return NULL;
	}
	app->opcoes = *opcoes;
	app->escala = 1;
	app->dedo = -1;
	app->largura = opcoes->largura > 0 ? opcoes->largura : 480;
	app->altura = opcoes->altura > 0 ? opcoes->altura : 640;
	atual = app;

	gv_tema_padrao(&gv_t);
	gv_tema_carregar(NULL);
	gv__texto_iniciar();

	app->display = wl_display_connect(NULL);
	if (!app->display) {
		fprintf(stderr, "gravity: sem compositor Wayland (WAYLAND_DISPLAY)\n");
		free(app);
		return NULL;
	}
	app->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	app->registo = wl_display_get_registry(app->display);
	wl_registry_add_listener(app->registo, &registo_ouvinte, app);
	wl_display_roundtrip(app->display);
	wl_display_roundtrip(app->display);
	if (!app->compositor || !app->shm) {
		fprintf(stderr, "gravity: o compositor não tem wl_compositor/wl_shm\n");
		free(app);
		return NULL;
	}

	app->superficie = wl_compositor_create_surface(app->compositor);
	wl_surface_add_listener(app->superficie, &sup_ouvinte, app);
	if (opcoes->tipo == GV_CAMADA) {
		if (!app->camadas) {
			fprintf(stderr, "gravity: o compositor não tem layer-shell; abrindo como janela\n");
			app->opcoes.tipo = GV_JANELA;
		} else {
			/* numa tela escolhida pelo nome (sem nome, ou se não houver: a que o compositor quiser) */
			struct wl_output *saida = NULL;
			for (int i = 0; opcoes->saida && i < app->nsaidas; i++) {
				if (!strcmp(app->saidas[i].nome, opcoes->saida) || (!strcmp(opcoes->saida, "@principal") && i == 0)) {
					saida = app->saidas[i].wl;
				}
			}
			if (opcoes->saida && !saida) {
				fprintf(stderr, "gravity: não há a tela %s; abrindo na que o compositor escolher\n", opcoes->saida);
			}
			app->camada = zwlr_layer_shell_v1_get_layer_surface(app->camadas, app->superficie, saida,
				opcoes->nivel, opcoes->app_id ? opcoes->app_id : "gravity");
			zwlr_layer_surface_v1_add_listener(app->camada, &camada_ouvinte, app);
			zwlr_layer_surface_v1_set_size(app->camada, opcoes->largura > 0 ? opcoes->largura : 0,
				opcoes->altura > 0 ? opcoes->altura : 0);
			zwlr_layer_surface_v1_set_anchor(app->camada, opcoes->ancoras);
			zwlr_layer_surface_v1_set_exclusive_zone(app->camada, opcoes->zona_exclusiva);
			zwlr_layer_surface_v1_set_keyboard_interactivity(app->camada,
				opcoes->teclado_exclusivo ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
				: opcoes->teclado ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
						: ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
			zwlr_layer_surface_v1_set_margin(app->camada, opcoes->margens[0], opcoes->margens[1], opcoes->margens[2],
				opcoes->margens[3]);
		}
	}
	if (app->opcoes.tipo == GV_JANELA) {
		if (!app->xdg) {
			fprintf(stderr, "gravity: o compositor não tem xdg-shell\n");
			free(app);
			return NULL;
		}
		app->xdg_superficie = xdg_wm_base_get_xdg_surface(app->xdg, app->superficie);
		xdg_surface_add_listener(app->xdg_superficie, &xdg_sup_ouvinte, app);
		app->janela = xdg_surface_get_toplevel(app->xdg_superficie);
		xdg_toplevel_add_listener(app->janela, &janela_ouvinte, app);
		xdg_toplevel_set_title(app->janela, opcoes->titulo ? opcoes->titulo : "Gravity");
		xdg_toplevel_set_app_id(app->janela, opcoes->app_id ? opcoes->app_id : "gravity");
	}
	wl_surface_commit(app->superficie);
	gv__escrita_iniciar(app->display, app->seat, app->superficie);
	gv__imagens_iniciar(app);
	gv__janelas_seat(app->seat);

	app->repete_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	app->repete_taxa = 25;
	app->repete_atraso = 600;
	if (app->repete_fd >= 0) {
		gv_app_vigiar(app, app->repete_fd, repeticao_pronta, NULL);
	}

	uint32_t memoria = Clay_MinMemorySize();
	app->arena = Clay_CreateArenaWithCapacityAndMemory(memoria, malloc(memoria));
	Clay_Initialize(app->arena, (Clay_Dimensions){ app->largura, app->altura }, (Clay_ErrorHandler){ .errorHandlerFunction = clay_erro });
	Clay_SetMeasureTextFunction(gv__texto_medir, NULL);
	/* um popup pode ficar fora da superfície principal (o menu da topbar): nada se corta */
	Clay_SetCullingEnabled(false);
	app->precisa = true;
	return app;
}

/* ---- popups ---- */

static struct gv_popup *popup_de(gv_app *app, uint32_t id, bool criar) {
	struct gv_popup *livre = NULL;
	for (int i = 0; i < GV_MAX_POPUPS; i++) {
		if (app->popups[i].id == id) {
			return &app->popups[i];
		}
		if (!app->popups[i].id && !app->popups[i].sup && !livre) {
			livre = &app->popups[i];
		}
	}
	if (!criar) {
		return NULL;
	}
	if (!livre) {
		fprintf(stderr, "gravity: mais de %d popups abertos\n", GV_MAX_POPUPS);
		return NULL;
	}
	livre->id = id;
	return livre;
}

void gv_popup_abrir(Clay_ElementId id) {
	struct gv_popup *p = atual ? popup_de(atual, id.id, true) : NULL;
	if (p && !p->aberto) {
		p->aberto = true;
		atual->precisa = true;
	}
}

void gv_popup_fechar(Clay_ElementId id) {
	struct gv_popup *p = atual ? popup_de(atual, id.id, false) : NULL;
	if (p && p->aberto) {
		p->aberto = false;
		atual->precisa = true;
	}
}

bool gv_popup_aberto(Clay_ElementId id) {
	struct gv_popup *p = atual ? popup_de(atual, id.id, false) : NULL;
	return p && p->aberto;
}

void gv_popup_alternar(Clay_ElementId id) {
	if (gv_popup_aberto(id)) {
		gv_popup_fechar(id);
	} else {
		gv_popup_abrir(id);
	}
}

/* onde o popup deve ficar antes de o compositor dizer (e se ele não mexer) */
static void popup_palpite(struct gv_popup *p, float w, float h) {
	Clay_BoundingBox a = p->ancora;
	float d = p->opcoes.distancia > 0 ? p->opcoes.distancia : 6;
	float x, y;
	switch (p->opcoes.lado) {
	case GV_POPUP_ACIMA:
	case GV_POPUP_ABAIXO:
		y = p->opcoes.lado == GV_POPUP_ABAIXO ? a.y + a.height + d : a.y - d - h;
		x = p->opcoes.alinhar == GV_ALINHAR_INICIO ? a.x
			: p->opcoes.alinhar == GV_ALINHAR_FIM ? a.x + a.width - w : a.x + (a.width - w) / 2;
		break;
	default:
		x = p->opcoes.lado == GV_POPUP_DIREITA ? a.x + a.width + d : a.x - d - w;
		y = a.y;
		break;
	}
	p->x = (int)roundf(x);
	p->y = (int)roundf(y);
}

bool gv__popup_comecar(Clay_ElementId id, Clay_ElementId ancora, const gv_popup_opcoes *o) {
	struct gv_popup *p = atual ? popup_de(atual, id.id, false) : NULL;
	if (!p || !p->aberto) {
		return false;
	}
	/* Esc fecha o de cima (e não chega à app) */
	for (int i = 0; i < gv__entrada.nteclas; i++) {
		if (gv__entrada.teclas[i] == 0xff1b /* XKB_KEY_Escape */) {
			memmove(&gv__entrada.teclas[i], &gv__entrada.teclas[i + 1], (gv__entrada.nteclas - i - 1) * sizeof(uint32_t));
			gv__entrada.nteclas--;
			p->aberto = false;
			atual->precisa = true;
			return false;
		}
	}
	p->declarado = true;
	p->opcoes = *o;
	gv__foco_camada((int)(p - atual->popups) + 1);
	Clay_ElementData a = Clay_GetElementData(ancora);
	if (a.found) {
		p->ancora = a.boundingBox;
	}
	if (!p->configurado) {
		popup_palpite(p, p->w > 0 ? p->w : (o->largura > 0 ? o->largura : 200), p->h);
	}
	return true;
}

void gv__popup_fim(void) {
	gv__foco_camada(0);
}

Clay_ElementDeclaration gv__popup_declaracao(Clay_ElementId id) {
	struct gv_popup *p = popup_de(atual, id.id, false);
	int indice = p ? (int)(p - atual->popups) : 0;
	return (Clay_ElementDeclaration){
		.layout = {
			.layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { p && p->opcoes.largura > 0 ? CLAY_SIZING_FIXED(p->opcoes.largura) : CLAY_SIZING_FIT(0) },
			.padding = CLAY_PADDING_ALL(6),
			.childGap = 2,
		},
		.backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
		.userData = gv_sombra(GV_POPUP_MARGEM - 6, 8, 0.55f),
		.floating = {
			.attachTo = CLAY_ATTACH_TO_ROOT,
			.offset = { p ? p->x : 0, p ? p->y : 0 },
			.zIndex = (int16_t)(GV_POPUP_Z + indice),
			.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
		},
	};
}

static void popup_destruir(struct gv_popup *p) {
	if (p->xdg_popup) {
		xdg_popup_destroy(p->xdg_popup);
	}
	if (p->xdg_sup) {
		xdg_surface_destroy(p->xdg_sup);
	}
	if (p->sup) {
		wl_surface_destroy(p->sup);
	}
	for (int i = 0; i < 2; i++) {
		buffer_destruir(&p->buffers[i]);
	}
	*p = (struct gv_popup){ 0 };
}

static void popup_xdg_configurar(void *dados, struct xdg_surface *s, uint32_t serial) {
	struct gv_popup *p = dados;
	xdg_surface_ack_configure(s, serial);
	if (!p->configurado) {
		p->desde = gv_agora();
	}
	p->configurado = true;
	gv__app_pedir();
}

static const struct xdg_surface_listener popup_xdg_ouvinte = { .configure = popup_xdg_configurar };

static void popup_configurar(void *dados, struct xdg_popup *x, int32_t px, int32_t py, int32_t w, int32_t h) {
	struct gv_popup *p = dados;
	/* o compositor pode tê-lo virado ou empurrado para caber na tela */
	p->x = px;
	p->y = py;
}

static void popup_acabou(void *dados, struct xdg_popup *x) {
	struct gv_popup *p = dados;
	p->aberto = false; /* tocaram fora (o grab do compositor) */
	gv__app_pedir();
}

static void popup_reposto(void *dados, struct xdg_popup *x, uint32_t token) {}

static const struct xdg_popup_listener popup_ouvinte = {
	.configure = popup_configurar,
	.popup_done = popup_acabou,
	.repositioned = popup_reposto,
};

static struct xdg_positioner *popup_posicionador(gv_app *app, struct gv_popup *p) {
	struct xdg_positioner *pos = xdg_wm_base_create_positioner(app->xdg);
	Clay_BoundingBox a = p->ancora;
	xdg_positioner_set_size(pos, p->w, p->h);
	xdg_positioner_set_anchor_rect(pos, (int)a.x, (int)a.y, a.width >= 1 ? (int)a.width : 1, a.height >= 1 ? (int)a.height : 1);
	int d = p->opcoes.distancia > 0 ? (int)p->opcoes.distancia : 6;
	enum gv_alinhar al = p->opcoes.alinhar;
	switch (p->opcoes.lado) {
	case GV_POPUP_ABAIXO:
	case GV_POPUP_ACIMA: {
		bool baixo = p->opcoes.lado == GV_POPUP_ABAIXO;
		uint32_t anc = baixo ? (al == GV_ALINHAR_INICIO ? XDG_POSITIONER_ANCHOR_BOTTOM_LEFT
			: al == GV_ALINHAR_FIM ? XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT : XDG_POSITIONER_ANCHOR_BOTTOM)
			: (al == GV_ALINHAR_INICIO ? XDG_POSITIONER_ANCHOR_TOP_LEFT
			: al == GV_ALINHAR_FIM ? XDG_POSITIONER_ANCHOR_TOP_RIGHT : XDG_POSITIONER_ANCHOR_TOP);
		uint32_t grav = baixo ? (al == GV_ALINHAR_INICIO ? XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT
			: al == GV_ALINHAR_FIM ? XDG_POSITIONER_GRAVITY_BOTTOM_LEFT : XDG_POSITIONER_GRAVITY_BOTTOM)
			: (al == GV_ALINHAR_INICIO ? XDG_POSITIONER_GRAVITY_TOP_RIGHT
			: al == GV_ALINHAR_FIM ? XDG_POSITIONER_GRAVITY_TOP_LEFT : XDG_POSITIONER_GRAVITY_TOP);
		xdg_positioner_set_anchor(pos, anc);
		xdg_positioner_set_gravity(pos, grav);
		xdg_positioner_set_offset(pos, 0, baixo ? d : -d);
		break;
	}
	default: {
		bool dir = p->opcoes.lado == GV_POPUP_DIREITA;
		xdg_positioner_set_anchor(pos, dir ? XDG_POSITIONER_ANCHOR_TOP_RIGHT : XDG_POSITIONER_ANCHOR_TOP_LEFT);
		xdg_positioner_set_gravity(pos, dir ? XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT : XDG_POSITIONER_GRAVITY_BOTTOM_LEFT);
		xdg_positioner_set_offset(pos, dir ? d : -d, 0);
		break;
	}
	}
	xdg_positioner_set_constraint_adjustment(pos, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
	if (app->xdg_versao >= 3) {
		xdg_positioner_set_reactive(pos);
	}
	return pos;
}

static void popup_criar(gv_app *app, struct gv_popup *p) {
	struct xdg_positioner *pos = popup_posicionador(app, p);
	p->sup = wl_compositor_create_surface(app->compositor);
	wl_surface_set_buffer_scale(p->sup, app->escala);
	p->xdg_sup = xdg_wm_base_get_xdg_surface(app->xdg, p->sup);
	xdg_surface_add_listener(p->xdg_sup, &popup_xdg_ouvinte, p);
	p->xdg_popup = xdg_surface_get_popup(p->xdg_sup, app->xdg_superficie, pos);
	xdg_popup_add_listener(p->xdg_popup, &popup_ouvinte, p);
	/* a geometria é o conteúdo; a margem à volta é da sombra (depois do papel de popup) */
	xdg_surface_set_window_geometry(p->xdg_sup, GV_POPUP_MARGEM, GV_POPUP_MARGEM, p->w, p->h);
	if (app->camada) {
		zwlr_layer_surface_v1_get_popup(app->camada, p->xdg_popup);
	}
	/* o grab: tocar fora da app fecha (o compositor manda popup_done) */
	if (app->seat && app->aperto_serial && !p->opcoes.sem_grab) {
		xdg_popup_grab(p->xdg_popup, app->seat, app->aperto_serial);
	}
	xdg_positioner_destroy(pos);
	wl_surface_commit(p->sup);
}

/* Depois do layout: cria, muda de tamanho ou fecha as superfícies dos popups. */
static void popups_rever(gv_app *app) {
	for (int i = 0; i < GV_MAX_POPUPS; i++) {
		struct gv_popup *p = &app->popups[i];
		if (!p->id) {
			continue;
		}
		if (!p->aberto || !p->declarado) {
			if (p->sup) {
				popup_destruir(p);
				app->precisa = true;
			} else if (!p->aberto) {
				p->id = 0;
			}
			if (p->id) {
				p->declarado = false;
			}
			continue;
		}
		p->declarado = false;
		Clay_ElementData d = Clay_GetElementData((Clay_ElementId){ .id = p->id });
		if (!d.found) {
			continue;
		}
		int w = (int)ceilf(d.boundingBox.width), h = (int)ceilf(d.boundingBox.height);
		if (w < 1 || h < 1) {
			continue;
		}
		if (!p->sup) {
			p->w = w;
			p->h = h;
			popup_criar(app, p);
		} else if ((w != p->w || h != p->h) && app->xdg_versao >= 3) {
			p->w = w;
			p->h = h;
			struct xdg_positioner *pos = popup_posicionador(app, p);
			xdg_surface_set_window_geometry(p->xdg_sup, GV_POPUP_MARGEM, GV_POPUP_MARGEM, w, h);
			xdg_popup_reposition(p->xdg_popup, pos, ++p->token);
			xdg_positioner_destroy(pos);
		}
	}
}

static void popup_desenhar(gv_app *app, struct gv_popup *p, Clay_RenderCommandArray comandos, int16_t z) {
	int m = GV_POPUP_MARGEM;
	struct gv_buffer *buffer = buffer_livre_em(app, p->buffers, (p->w + 2 * m) * app->escala, (p->h + 2 * m) * app->escala);
	if (!buffer) {
		return;
	}
	double t = gv_t.animacoes ? fmin(1, (gv_agora() - p->desde) / 0.14) : 1;
	if (t < 1) {
		app->precisa = true;
	}
	double e = 1 - (1 - t) * (1 - t);
	cairo_t *cr = cairo_create(buffer->superficie);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_scale(cr, app->escala, app->escala);
	/* chega descendo um pouco e esmaecendo (ou subindo, se abre para cima) */
	double desliza = (1 - e) * (p->opcoes.lado == GV_POPUP_ACIMA ? 6 : -6);
	cairo_translate(cr, m - p->x, m - p->y + desliza);
	cairo_push_group(cr);
	gv__desenhar(cr, comandos, z, z);
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, e);
	cairo_destroy(cr);
	cairo_surface_flush(buffer->superficie);
	wl_surface_attach(p->sup, buffer->wl, 0, 0);
	wl_surface_damage_buffer(p->sup, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(p->sup);
	buffer->ocupado = true;
}

/* ---- o quadro ---- */

static void desenhar_quadro(gv_app *app, gv_desenho desenhar, void *dados) {
	struct gv_buffer *buffer = buffer_livre(app);
	if (!buffer) {
		return; /* os dois ainda no compositor: fica para o próximo */
	}
	double agora = gv_agora();
	float dt = app->ultimo_quadro > 0 ? (float)(agora - app->ultimo_quadro) : 0.016f;
	app->ultimo_quadro = agora;
	app->precisa = false;
	app->quadro_com_acao = false;
	if (gv__entrada.nteclas == 0 && !app->npendentes) {
		gv__entrada.mods = app->mods_reais;
	}
	if (app->npendentes) {
		/* as que esperaram, pela ordem: as que voltarem a esperar ficam para o próximo */
		struct tecla_pendente fila[64];
		int n = app->npendentes;
		memcpy(fila, app->pendentes, n * sizeof(fila[0]));
		app->npendentes = 0;
		for (int i = 0; i < n; i++) {
			entregar(app, fila[i].sym, fila[i].utf8, fila[i].mods);
		}
	}

	gv__anim_quadro();
	/* o dedo (ou o botão do rato) ficou meio segundo no mesmo sítio: segurou (uma vez por aperto) */
	if (gv__entrada.baixo && !gv__entrada.segurar_cancelado && agora - gv__entrada.aperto_em >= 0.5) {
		gv__entrada.segurou = true;
		gv__entrada.segurar_cancelado = true;
	}
	Clay_SetLayoutDimensions((Clay_Dimensions){ app->largura, app->altura });
	Clay_SetPointerState((Clay_Vector2){ gv__entrada.x, gv__entrada.y }, gv__entrada.baixo || gv__entrada.apertou);
	/* a rolagem é do gv_rolagem (inércia e elástico): o Clay só corta e desloca */
	Clay_UpdateScrollContainers(false, (Clay_Vector2){ 0 }, dt);
	Clay_BeginLayout();
	desenhar(app, dados);
	Clay_RenderCommandArray comandos = Clay_EndLayout(dt);
	popups_rever(app);
	entrada_rever(app);

	cairo_t *cr = cairo_create(buffer->superficie);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_scale(cr, app->escala, app->escala);
	gv__desenhar(cr, comandos, INT16_MIN, GV_POPUP_Z - 1);
	cairo_destroy(cr);
	cairo_surface_flush(buffer->superficie);
	for (int i = 0; i < GV_MAX_POPUPS; i++) {
		struct gv_popup *p = &app->popups[i];
		if (p->sup && p->configurado) {
			popup_desenhar(app, p, comandos, (int16_t)(GV_POPUP_Z + i));
		}
	}

	gv__escrita_fim_do_quadro();
	gv__entrada_fim_do_quadro();
	gv__quadro_limpar();

	wl_surface_attach(app->superficie, buffer->wl, 0, 0);
	wl_surface_damage_buffer(app->superficie, 0, 0, INT32_MAX, INT32_MAX);
	app->quadro_cb = wl_surface_frame(app->superficie);
	wl_callback_add_listener(app->quadro_cb, &quadro_ouvinte, app);
	wl_surface_commit(app->superficie);
	buffer->ocupado = true;
}

int gv_app_correr(gv_app *app, gv_desenho desenhar, void *dados) {
	app->a_correr = true;
	struct pollfd fds[1 + GV_MAX_VIGIAS];
	while (app->a_correr) {
		if (app->precisa && app->configurado && !app->quadro_cb) {
			desenhar_quadro(app, desenhar, dados);
		}
		if (!app->a_correr) {
			break; /* gv_app_sair durante o quadro: sai já, sem esperar o próximo despertar */
		}
		while (wl_display_prepare_read(app->display) != 0) {
			wl_display_dispatch_pending(app->display);
		}
		if (wl_display_flush(app->display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(app->display);
			break;
		}
		fds[0] = (struct pollfd){ .fd = wl_display_get_fd(app->display), .events = POLLIN };
		for (int i = 0; i < app->nvigias; i++) {
			fds[1 + i] = (struct pollfd){ .fd = app->vigias[i].fd, .events = POLLIN };
		}
		/* com um quadro por desenhar mas à espera do compositor, o poll acorda com o callback */
		int espera = (app->precisa && app->configurado && !app->quadro_cb) ? 0 : -1;
		if (espera < 0 && app->acordar > 0) {
			/* um quadro marcado (o cursor que pisca): dorme só até lá */
			double falta = app->acordar - gv_agora();
			espera = falta > 0 ? (int)(falta * 1000) + 1 : 0;
		}
		if (poll(fds, 1 + app->nvigias, espera) < 0 && errno != EINTR) {
			wl_display_cancel_read(app->display);
			break;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(app->display) < 0) {
				break;
			}
		} else {
			wl_display_cancel_read(app->display);
		}
		if (wl_display_dispatch_pending(app->display) < 0) {
			break;
		}
		if (app->acordar > 0 && gv_agora() >= app->acordar) {
			app->acordar = 0;
			app->precisa = true;
		}
		for (int i = 0; i < app->nvigias; i++) {
			if (fds[1 + i].revents & (POLLIN | POLLHUP)) {
				app->vigias[i].pronto(app, app->vigias[i].fd, app->vigias[i].dados);
				app->precisa = true;
			}
		}
	}
	for (int i = 0; i < 2; i++) {
		buffer_destruir(&app->buffers[i]);
	}
	wl_display_disconnect(app->display);
	return app->codigo;
}
