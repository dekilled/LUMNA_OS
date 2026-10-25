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
	bool travado;       /* uma rolagem ficou com o aperto: os de dentro não o tomam */
	bool direito;       /* o botão direito desceu neste quadro */
	bool segurou;       /* o dedo ficou meio segundo parado neste quadro (o "botão direito" do toque) */
	bool segurar_cancelado; /* este aperto já segurou, ou o dedo andou */
	double aperto_em;   /* quando e onde o último aperto desceu */
	float aperto_x, aperto_y;
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
int gv__app_escala(void); /* a escala do ecrã da app (1, 2...) */

/* ---- foco (gv_foco.c) ---- */
#define GV_POPUP_Z 100 /* o zIndex do primeiro popup; abaixo disto é a superfície principal */
/* Entra na fila do Tab (preferido: o item a focar quando o seu popup abre). Devolve se está focado. */
bool gv__focavel(Clay_ElementId id, bool preferido);
/* O anel de foco (só ao navegar pelo teclado), declarado dentro do elemento id. */
void gv__foco_anel(Clay_ElementId id, float raio);
/* Enter ou Espaço no focado (e consome a tecla). */
bool gv__foco_ativou(Clay_ElementId id);
/* Esta tecla, se id está focado (e consome-a). */
bool gv__foco_tecla(Clay_ElementId id, uint32_t keysym);
uint32_t gv__foco_id(void);
void gv__foco_definir(uint32_t id, bool anel);
void gv__foco_camada(int camada); /* os focáveis declarados a seguir são do popup camada (0: principal) */
void gv__foco_fim_do_quadro(void);

/* ---- janelas abertas (gv_janelas.c) ---- */
struct wl_registry;
struct wl_seat;
bool gv__janelas_global(struct wl_registry *registo, uint32_t nome, const char *iface, uint32_t versao);
void gv__janelas_seat(struct wl_seat *seat);
struct wl_output;
const char *gv__saida_nome(struct wl_output *saida); /* "" se não a conhece */

/* ---- imagens (gv_imagem.c) ---- */
void gv__imagens_iniciar(gv_app *app);
void gv__imagens_fim_do_quadro(void);

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
void gv__rolagens_fim_do_quadro(void);

/* ---- medidas de texto para os campos (pango) ---- */
float gv__texto_x(const char *texto, float tamanho, int indice);
int gv__texto_indice(const char *texto, float tamanho, float x);
/* o mesmo, num texto quebrado em várias linhas na largura dada (o campo de várias linhas) */
void gv__texto_pos(const char *texto, float tamanho, float largura, int indice, float *x, float *y, float *altura);
int gv__texto_indice_xy(const char *texto, float tamanho, float largura, float x, float y);
float gv__texto_altura(const char *texto, float tamanho, float largura);
void gv__texto_linha_de(const char *texto, float tamanho, float largura, int indice, int *ini, int *fim);

/* ---- o que vai pendurado nos elementos, para o desenho ---- */
enum gv_extra_tipo {
	GV_EXTRA_SOMBRA = 1,
	GV_EXTRA_ICONE,
	GV_EXTRA_DESLIZANTE,
	GV_EXTRA_INTERRUPTOR,
	GV_EXTRA_CAMPO,
	GV_EXTRA_ROLAGEM,
	GV_EXTRA_IMAGEM,
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
			float largura;         /* > 0: várias linhas, quebradas nesta largura */
			float rolagem_y;
			int cursor;            /* índices em bytes no texto mostrado */
			int sel_ini, sel_fim;
			int comp_ini, comp_fim; /* a palavra a compor, sublinhada */
			float cursor_alfa;
			Clay_Color cor, cor_dica, cor_sel, cor_cursor;
		} campo;
		struct {
			bool tem[2];     /* barra na horizontal [0] e na vertical [1] */
			float ini[2], tam[2]; /* onde a barra começa e o tamanho, ao longo do eixo */
			float alfa;
			bool grossa;     /* o rato está perto ou a arrastar */
			Clay_Color cor;
		} rolagem;
		struct {
			cairo_surface_t *sup; /* NULL: ainda não chegou (desenha só o fundo) */
			enum gv_ajuste ajuste;
			float alfa;           /* o esmaecer da chegada */
			Clay_Color fundo;     /* o lugar da imagem enquanto não chega */
			Clay_Color atras;     /* a cor do elemento, por baixo (as sobras do Ajustar, a transparência) */
		} imagem;
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
/* Desenha os comandos com zIndex entre zmin e zmax (os popups têm superfície própria). */
void gv__desenhar(cairo_t *cr, Clay_RenderCommandArray comandos, int16_t zmin, int16_t zmax);

/* ---- animações ---- */
void gv__anim_quadro(void);

#endif
