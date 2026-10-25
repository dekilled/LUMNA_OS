/* O que as partes do Gravity dividem entre si (as apps usam só o gravity.h). */
#ifndef GV_INTERNO_H
#define GV_INTERNO_H

#include <cairo.h>
#include <stddef.h>

#include "gravity.h"

#define GV_MAX_TECLAS 16

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- memória do quadro: textos e dados dos elementos, apagada a cada quadro ---- */
void *gv__quadro_alloc(size_t tamanho);
void gv__quadro_limpar(void);

/* ---- entrada do quadro (gv_app.c enche, os componentes lêem) ---- */
struct gv_entrada {
	float x, y;         /* ponteiro ou dedo, em pixels lógicos */
	bool baixo;         /* botão ou dedo em baixo agora */
	bool apertou;       /* desceu desde o último quadro */
	bool soltou;        /* subiu desde o último quadro */
	bool toque;         /* o último a mexer foi um dedo */
	uint32_t ativo;     /* o id que recebeu o aperto (0: nenhum) */
	uint32_t teclas[GV_MAX_TECLAS]; /* keysyms carregados neste quadro (com as repetições) */
	int nteclas;
	char texto[128];    /* o texto escrito neste quadro (UTF-8, já com os acentos compostos) */
	uint32_t mods;      /* GV_MOD_* */
	uint32_t serial_tecla;
	float roda_x, roda_y;
	/* o que o teclado no ecrã (text-input) mandou neste quadro */
	struct {
		bool chegou;
		char escrito[512];
		char compondo[256]; /* a palavra a meio (sublinhada), ainda não escrita */
		int compondo_ini, compondo_fim;
		uint32_t apagar_antes, apagar_depois;
	} ime;
};
extern struct gv_entrada gv__entrada;
void gv__entrada_fim_do_quadro(void);
void gv__app_pedir(void); /* o quadro actual pede outro (animação a correr) */
void gv__app_acordar_em(double segundos); /* um quadro daqui a pouco (sem desenhar até lá) */

/* ---- escrita: área de transferência e text-input (gv_escrita.c) ---- */
struct wl_registry;
struct wl_display;
struct wl_seat;
struct wl_surface;
bool gv__escrita_global(struct wl_registry *registo, uint32_t nome, const char *iface, uint32_t versao);
void gv__escrita_iniciar(struct wl_display *display, struct wl_seat *seat, struct wl_surface *superficie);
void gv__escrita_fim_do_quadro(void);
/* O campo focado diz, a cada quadro, o que o teclado no ecrã deve saber dele. */
void gv__campo_pede(const char *texto, int cursor, int ancora, Clay_BoundingBox cursor_caixa, uint32_t dica, uint32_t proposito);
void gv__campos_fim_do_quadro(void);

/* ---- medidas de texto para os campos (pango) ---- */
float gv__texto_x(const char *texto, float tamanho, int indice);
int gv__texto_indice(const char *texto, float tamanho, float x);

/* ---- o que vai pendurado nos elementos, para o desenho ---- */
enum gv_extra_tipo {
	GV_EXTRA_SOMBRA = 1,
	GV_EXTRA_ICONE,
	GV_EXTRA_DESLIZANTE,
	GV_EXTRA_INTERRUPTOR,
	GV_EXTRA_CAMPO,
};

struct gv_extra {
	enum gv_extra_tipo tipo;
	union {
		struct {
			float desfoque, desloca_y, alfa;
		} sombra;
		struct {
			const char *caminho; /* o "d" do SVG, em 24 × 24 */
			Clay_Color cor;
			float traco;
		} icone;
		struct {
			float valor; /* 0..1 */
			float trilho, puxador;
			Clay_Color cheio, vazio, cor_puxador;
			float realce; /* 0..1: o puxador cresce um pouco ao arrastar */
		} deslizante;
		struct {
			float posicao; /* 0 desligado .. 1 ligado (animado) */
			Clay_Color fundo, bolinha;
		} interruptor;
		struct {
			const char *mostrado;  /* o texto como aparece (pontos numa senha, com a palavra a compor) */
			const char *dica;      /* o texto cinza do campo vazio (NULL: não mostra) */
			float tamanho, rolagem;
			int cursor;            /* índices em bytes no texto mostrado */
			int sel_ini, sel_fim;
			int comp_ini, comp_fim; /* a palavra a compor, sublinhada */
			float cursor_alfa;
			Clay_Color cor, cor_dica, cor_sel, cor_cursor;
		} campo;
	};
};

/* ---- texto (pango) ---- */
void gv__texto_iniciar(void);
Clay_Dimensions gv__texto_medir(Clay_StringSlice texto, Clay_TextElementConfig *config, void *dados);
void gv__texto_desenhar(cairo_t *cr, Clay_RenderCommand *cmd);
struct gv_extra;
void gv__campo_desenhar(cairo_t *cr, Clay_BoundingBox caixa, const struct gv_extra *e);

/* ---- ícones ---- */
const char *gv__icone_caminho(const char *nome);
/* Traça o caminho SVG (só o "d") no cairo; quem chama faz o stroke. */
bool gv__svg_tracar(cairo_t *cr, const char *d);

/* ---- desenho ---- */
void gv__desenhar(cairo_t *cr, Clay_RenderCommandArray comandos);

/* ---- animações ---- */
void gv__anim_quadro(void);

#endif
