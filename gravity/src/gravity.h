/*
 * Gravity — a interface do LUMNA OS em C.
 *
 * Uma app Gravity desenha a tela inteira a cada quadro (modo imediato): a função de desenho
 * declara os elementos com o layout do Clay (CLAY(...) { ... }, como flexbox) e os componentes
 * gv_* (texto, ícone, botão, deslizante, interruptor...). O Gravity cuida do Wayland (janela
 * ou camada do LUMNA), do toque, do rato e do teclado, do texto (pango), dos ícones do LUMNA e
 * das animações; o visual sai do tema (gv_t), que um tema.ini muda sem recompilar.
 *
 *   static bool lanterna;
 *   static void desenhar(gv_app *app, void *dados) {
 *       CLAY(CLAY_ID("raiz"), { .layout = { .padding = CLAY_PADDING_ALL(16) } }) {
 *           if (gv_botao_redondo(CLAY_ID("lanterna"), "flashlight", 64, lanterna, false)) lanterna = !lanterna;
 *       }
 *   }
 *   int main(void) {
 *       gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Lanterna", .largura = 200, .altura = 200 });
 *       return gv_app_correr(app, desenhar, NULL);
 *   }
 *
 * Unidades: pixels lógicos (os do CSS). Num ecrã com escala 2 o Gravity desenha com o dobro.
 */
#ifndef GRAVITY_H
#define GRAVITY_H

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"

/* ---- tema ---- */

enum gv_curva {
	GV_CURVA_SUAVE, /* sai depressa e assenta devagar (ease-out cúbica) */
	GV_CURVA_MOLA,  /* passa um pouco do alvo e volta */
	GV_CURVA_LINEAR,
};

typedef struct gv_tema {
	/* cores (0-255, como no Clay); entre parênteses, o nome do token no design do LUMNA */
	Clay_Color fundo;            /* (bg) o fundo geral */
	Clay_Color superficie;       /* (surface) janelas e conteúdo */
	Clay_Color elevada;          /* (raised) barras, menus, cards e teclas */
	Clay_Color sob_cursor;       /* (hover) uma superfície sob o cursor */
	Clay_Color selecionado;      /* (selected) o fundo do item escolhido */
	Clay_Color borda;            /* (border) divisórias e bordas discretas */
	Clay_Color borda_controle;   /* (border-control) o contorno do que se toca */
	Clay_Color texto;            /* (text) texto principal e ícones essenciais */
	Clay_Color texto_secundario; /* (text-secondary) */
	Clay_Color texto_suave;      /* (text-muted) texto auxiliar */
	Clay_Color acento;           /* (accent) ações principais, deslizantes, janela ativa */
	Clay_Color acento_cursor;    /* (accent-hover) */
	Clay_Color acento_premido;   /* (accent-pressed) */
	Clay_Color sobre_acento;     /* (on-accent) o texto num botão violeta */
	Clay_Color foco;             /* (focus) o anel do foco do teclado */
	Clay_Color sucesso;          /* (success) */
	Clay_Color aviso;            /* (warning) */
	Clay_Color perigo;           /* (danger) erros e ações destrutivas */
	Clay_Color info;             /* (info) */
	Clay_Color desativado;       /* (disabled) */
	Clay_Color divisor;          /* (grip-idle) um divisor parado; arrastado, usa o acento */
	Clay_Color veu;              /* atrás de um painel por cima de tudo (o fundo, meio transparente) */
	Clay_Color icone;            /* os ícones dos cabeçalhos e das linhas */
	/* forma */
	float raio_pequeno;          /* botões, campos, seletores */
	float raio_medio;            /* linhas e itens */
	float raio_grande;           /* janelas, cartões e menus */
	float espaco;                /* a unidade dos espaçamentos (gv_e(n) = n × espaco) */
	float traco_icone;           /* grossura do traço dos ícones, em 24 px */
	/* texto */
	char fonte[64];
	float tamanho_legenda, tamanho_corpo, tamanho_destaque, tamanho_titulo;
	/* movimento */
	int duracao_rapida, duracao_normal, duracao_lenta; /* ms */
	enum gv_curva curva;
	bool animacoes;              /* falso: tudo muda na hora */
} gv_tema;

/* O tema activo. Os componentes lêem dele a cada quadro: mudar um campo muda a tela. */
extern gv_tema gv_t;

/* O tema do LUMNA, de fábrica. */
void gv_tema_padrao(gv_tema *tema);
/* Lê um tema.ini por cima do tema activo. NULL procura $GRAVITY_TEMA, depois
 * $XDG_CONFIG_HOME/gravity/tema.ini (~/.config/gravity/tema.ini). Devolve falso se não leu. */
bool gv_tema_carregar(const char *caminho);

static inline float gv_e(float n) { return n * gv_t.espaco; }
Clay_Color gv_cor(uint32_t rgba); /* 0xRRGGBBAA */
Clay_Color gv_alfa(Clay_Color cor, float alfa);

/* ---- app ---- */

enum gv_tipo {
	GV_JANELA, /* uma janela normal (xdg-shell) */
	GV_CAMADA, /* uma camada do LUMNA (layer-shell): barra, painel, teclado, bloqueio */
};

enum {
	GV_ANCORA_CIMA = 1,
	GV_ANCORA_BAIXO = 2,
	GV_ANCORA_ESQUERDA = 4,
	GV_ANCORA_DIREITA = 8,
	GV_ANCORA_TUDO = 15,
};

enum gv_nivel { GV_NIVEL_FUNDO, GV_NIVEL_BAIXO, GV_NIVEL_CIMA, GV_NIVEL_OVERLAY };

typedef struct gv_opcoes {
	const char *titulo;
	const char *app_id;
	enum gv_tipo tipo;
	int largura, altura;     /* o tamanho pedido (numa camada esticada, 0 é a saída inteira) */
	/* só camadas */
	enum gv_nivel nivel;
	uint32_t ancoras;
	int zona_exclusiva;
	bool teclado;            /* a camada quer o teclado (quando se toca nela) */
	bool teclado_exclusivo;  /* a camada fica com o teclado enquanto existir (um lançador, o bloqueio) */
	int margens[4];          /* a camada afasta-se das bordas: cima, direita, baixo, esquerda */
	const char *saida;       /* em que tela (o nome: "HDMI-A-1", ou "@principal": a primeira);
	                          * NULL: a que o compositor escolher */
} gv_opcoes;

typedef struct gv_app gv_app;
typedef void (*gv_desenho)(gv_app *app, void *dados);

gv_app *gv_app_criar(const gv_opcoes *opcoes);
/* Corre até gv_app_sair (ou o compositor fechar a janela). Devolve o código de saída. */
int gv_app_correr(gv_app *app, gv_desenho desenhar, void *dados);
void gv_app_sair(gv_app *app);
/* Janelas: minimizar, maximizar (ou voltar) e começar a arrastar (num aperto do ponteiro ou do dedo). */
void gv_app_minimizar(gv_app *app);
void gv_app_maximizar(gv_app *app);
void gv_app_mover(gv_app *app);
/* Pede outro quadro (os componentes e as animações já pedem sozinhos). NULL: a app actual. */
void gv_app_redesenhar(gv_app *app);
/* Pede um quadro daqui a uns segundos (um relógio, um aviso que some), sem desenhar até lá. */
void gv_acordar_em(double segundos);
/* Um descritor a vigiar no ciclo da app (um socket, um pipe); a função corre quando há o que ler. */
void gv_app_vigiar(gv_app *app, int fd, void (*pronto)(gv_app *app, int fd, void *dados), void *dados);
float gv_app_largura(gv_app *app);
float gv_app_altura(gv_app *app);
/* Segundos desde o arranque. */
double gv_agora(void);

/* ---- texto ---- */

enum gv_peso { GV_PESO_NORMAL, GV_PESO_MEDIO, GV_PESO_SEMINEGRITO, GV_PESO_NEGRITO };

/* Uma cópia que vive até ao fim do quadro (o Clay guarda só o ponteiro). */
Clay_String gv_str(const char *texto);
Clay_String gv_fmt(const char *formato, ...) __attribute__((format(printf, 1, 2)));
/* Um texto com o tamanho, a cor e o peso dados (0 no tamanho: o do corpo). */
void gv_texto(Clay_String texto, float tamanho, Clay_Color cor, enum gv_peso peso);
/* Atalho: texto do corpo na cor do tema. */
#define gv_rotulo(literal) gv_texto(CLAY_STRING(literal), 0, gv_t.texto, GV_PESO_NORMAL)

/* ---- ícones (os do LUMNA: o mesmo traço de 24 px do shell) ---- */

void gv_icone(const char *nome, float tamanho, Clay_Color cor);
bool gv_icone_existe(const char *nome);

/* ---- animação ---- */

/* O valor de agora a caminho do alvo. A chave identifica a animação (use o id do elemento:
 * id.id, ou id.id + 1, + 2... para mais de uma). Muda o alvo e ela parte do valor de agora. */
float gv_anim(uint32_t chave, float alvo, int duracao_ms);
Clay_Color gv_anim_cor(uint32_t chave, Clay_Color alvo, int duracao_ms);

/* ---- interação (do quadro anterior: o modo imediato decide com o layout já desenhado) ---- */

bool gv_por_cima(Clay_ElementId id);     /* o ponteiro (ou o dedo) está sobre ele */
bool gv_premido(Clay_ElementId id);      /* começou nele e ainda não soltou */
bool gv_tocado(Clay_ElementId id);       /* começou e acabou nele: um clique, um toque */
Clay_BoundingBox gv_caixa(Clay_ElementId id);
/* Onde está o ponteiro (ou o dedo) na superfície, em pixels lógicos; -1, -1 fora dela. */
Clay_Vector2 gv_ponteiro(void);
/* A tecla carregada neste quadro (um keysym do xkbcommon), ou 0. */
uint32_t gv_tecla(void);
/* Se esta tecla foi carregada neste quadro (com repetição, pode vir mais de uma). */
bool gv_tecla_carregada(uint32_t keysym);

enum { GV_MOD_SHIFT = 1, GV_MOD_CTRL = 2, GV_MOD_ALT = 4, GV_MOD_LOGO = 8 };
uint32_t gv_modificadores(void);

/* ---- área de transferência ---- */

void gv_copiar(const char *texto);
/* O texto copiado (desta app ou de outra), até ao fim do quadro; NULL se não houver. */
const char *gv_colar(void);

/* ---- componentes ---- */

enum gv_estilo_botao {
	GV_BOTAO_CHEIO,    /* a ação principal: fundo de acento */
	GV_BOTAO_ELEVADO,  /* uma ação comum: superfície elevada com contorno ("Parear", "Verificar") */
	GV_BOTAO_CONTORNO, /* só o contorno ("Adicionar dispositivo") */
	GV_BOTAO_SIMPLES,  /* só o conteúdo */
	GV_BOTAO_PERIGO,   /* destrutiva: contorno e texto de perigo ("Desconectar") */
};

/* Um botão com ícone e/ou texto (NULL num deles tira-o), do tamanho do conteúdo. Devolve
 * verdadeiro no toque. */
bool gv_botao(Clay_ElementId id, const char *icone, const char *texto, enum gv_estilo_botao estilo);
typedef struct gv_botao_opcoes {
	const char *icone, *texto;
	enum gv_estilo_botao estilo;
	bool crescer;     /* ocupa a largura que sobrar */
	bool desativado;
	float altura;     /* 0: 44 */
} gv_botao_opcoes;
bool gv_botao_ext(Clay_ElementId id, const gv_botao_opcoes *opcoes);
/* Um botão redondo só com ícone; "ligado" pinta-o como activo (perigo: em vermelho). */
bool gv_botao_redondo(Clay_ElementId id, const char *icone, float tamanho, bool ligado, bool perigo);
/* Um deslizante de 0 a 1. Devolve verdadeiro quando o valor mudou neste quadro. */
bool gv_deslizante(Clay_ElementId id, float *valor, float trilho, float puxador);
/* Um interruptor. Devolve verdadeiro quando mudou. */
bool gv_interruptor(Clay_ElementId id, bool *ligado);
/* Um campo de texto. A app guarda o texto (texto, com a capacidade dada); o cursor, a seleção
 * e a rolagem ficam com o Gravity. Devolve GV_CAMPO_MUDOU quando o texto mudou e GV_CAMPO_ENTER
 * quando a pessoa carregou Enter (ou "ir" no teclado do ecrã). */
enum gv_campo_tipo {
	GV_CAMPO_TEXTO,
	GV_CAMPO_BUSCA,   /* lupa à esquerda e um x para limpar */
	GV_CAMPO_SENHA,   /* pontos, e um olho para mostrar */
	GV_CAMPO_NUMERO,
	GV_CAMPO_EMAIL,
	GV_CAMPO_URL,
};
enum { GV_CAMPO_MUDOU = 1, GV_CAMPO_ENTER = 2 };
int gv_campo(Clay_ElementId id, char *texto, size_t capacidade, const char *dica, enum gv_campo_tipo tipo);
/* Um campo de várias linhas (uma mensagem, uma nota): o texto quebra na largura, o campo cresce
 * de linhas_min até linhas_max (0, 0: de 3 a 8) e depois rola por dentro. Enter quebra a linha;
 * Ctrl+Enter devolve GV_CAMPO_ENTER. */
int gv_campo_area(Clay_ElementId id, char *texto, size_t capacidade, const char *dica, int linhas_min, int linhas_max);
/* ---- foco ----
 * Um foco para a app inteira: Tab e Shift+Tab andam pelos controles na ordem em que aparecem,
 * Enter ou Espaço ativam, o anel violeta só aparece ao navegar pelo teclado. Os componentes do
 * Gravity já entram na fila; um elemento feito à mão entra com gv_focavel. */
void gv_focar(Clay_ElementId id);
bool gv_focado(Clay_ElementId id);
/* Põe o elemento id na fila do Tab e desenha o anel nele (chame DENTRO do bloco do elemento, com
 * o raio dos cantos dele). Devolve verdadeiro quando Enter ou Espaço o ativaram. */
bool gv_focavel(Clay_ElementId id, float raio);

/* Uma linha de ajuste: ícone, título, subtítulo (NULL: sem) e, à direita, um › (devolve
 * verdadeiro no toque) ou um interruptor (passe o bool; devolve verdadeiro quando mudou). */
bool gv_linha(Clay_ElementId id, const char *icone, const char *titulo, const char *subtitulo, bool *interruptor);
/* Abas com o sublinhado do acento (a linha de baixo, que vai de lado a lado, é de quem as põe:
 * assim cabe uma busca à direita). Devolve verdadeiro quando a pessoa trocou de aba. */
bool gv_abas(Clay_ElementId id, const char *const *nomes, int n, int *ativa);
/* A barra de título de uma janela: ícone, título, minimizar, maximizar e fechar; arrastar move. */
void gv_barra_janela(gv_app *app, Clay_ElementId id, const char *icone, const char *titulo);

/* ---- as janelas abertas (para a dock e o alternador) ----
 * Do compositor, pelo wlr-foreign-toplevel-management (o Orbit tem). A lista muda sozinha (e
 * pede um quadro) quando uma janela abre, fecha, muda de título ou ganha o foco. */
typedef struct gv_janela {
	uint32_t id;
	const char *titulo, *app_id;
	bool ativa, minimizada, maximizada, tela_cheia;
	const char *saida; /* a tela onde se vê agora ("" se em nenhuma: noutra área de trabalho) */
	bool pronta; /* (interno) já chegou o primeiro "done" */
} gv_janela;
/* As janelas abertas, pela ordem em que abriram; devolve quantas (até max). Os ponteiros valem
 * até ao fim do quadro. */
int gv_janelas(const gv_janela **lista, int max);
bool gv_janelas_disponivel(void);
void gv_janela_ativar(uint32_t id);
void gv_janela_fechar(uint32_t id);
void gv_janela_maximizar(uint32_t id, bool sim);
void gv_janela_tela_cheia(uint32_t id, bool sim);
/* Numa camada larga com um pedaço só à vista (a dock, que ocupa a largura toda mas só a pílula
 * se vê), só esse elemento recebe o ponteiro e o toque: o resto passa para as janelas de baixo. */
void gv_app_entrada_em(gv_app *app, Clay_ElementId id);
/* Uma camada muda de tamanho e de zona exclusiva com a app a correr (a dock que passa a barra). */
void gv_app_camada(gv_app *app, int largura, int altura, int zona_exclusiva);
/* Muda as âncoras e se fica com o teclado (os avisos, que passam do canto à tela inteira com a
 * central). Vale com o gv_app_camada que se chama logo a seguir, que também dá o tamanho. */
void gv_app_camada_ancoras(gv_app *app, uint32_t ancoras, bool teclado_exclusivo);
/* As telas, pelo nome, na ordem em que o compositor as anunciou (a primeira é a principal). */
int gv_saidas(gv_app *app, const char **nomes, int max);
/* Uma tela entrou ou saiu desde a última vez que se perguntou. */
bool gv_saidas_mudaram(gv_app *app);

/* ---- popups e menus ---- */
enum gv_popup_lado { GV_POPUP_ABAIXO, GV_POPUP_ACIMA, GV_POPUP_DIREITA, GV_POPUP_ESQUERDA };
enum gv_alinhar { GV_ALINHAR_INICIO, GV_ALINHAR_CENTRO, GV_ALINHAR_FIM };
typedef struct gv_popup_opcoes {
	enum gv_popup_lado lado;   /* de que lado da âncora abre */
	enum gv_alinhar alinhar;   /* ao longo da âncora: início, centro ou fim */
	float largura;             /* 0: a do conteúdo */
	float distancia;           /* da âncora (0: 6 px) */
	bool sem_grab;             /* de passar o rato (uma prévia, uma dica): não prende o ponteiro
	                            * nem fecha ao tocar fora da app; quem o abre é que o fecha */
} gv_popup_opcoes;
/* Um popup preso a um elemento (a âncora): uma superfície própria, que pode sair da janela (o
 * menu da topbar), com sombra, que chega esmaecendo. Fecha ao tocar fora (na app ou fora dela),
 * com Esc ou com gv_popup_fechar. O conteúdo vai no bloco, como qualquer outro:
 *   if (gv_botao(CLAY_ID("bt"), "bluetooth", NULL, GV_BOTAO_SIMPLES)) gv_popup_alternar(CLAY_ID("menu-bt"));
 *   GV_POPUP(CLAY_ID("menu-bt"), CLAY_ID("bt"), .alinhar = GV_ALINHAR_FIM, .largura = 380) {
 *       ...linhas do menu...
 *   }
 */
#define GV_POPUP(id, ancora, ...) \
	if (gv__popup_comecar((id), (ancora), &(gv_popup_opcoes){ __VA_ARGS__ })) \
		for (int gv__p = 0; gv__p < 1; gv__p++, gv__popup_fim()) CLAY((id), gv__popup_declaracao(id))
void gv_popup_abrir(Clay_ElementId id);
void gv_popup_fechar(Clay_ElementId id);
void gv_popup_alternar(Clay_ElementId id);
bool gv_popup_aberto(Clay_ElementId id);
/* Uma linha de menu: ícone (NULL: sem), texto e, à direita, um atalho ou detalhe (NULL: sem).
 * Devolve verdadeiro no toque. */
bool gv_menu_item(Clay_ElementId id, const char *icone, const char *texto, const char *detalhe);
/* Uma lista suspensa (dropdown): mostra a opção escolhida e abre as outras num popup logo abaixo,
 * da largura dela. Devolve verdadeiro quando a escolha mudou. */
bool gv_lista_suspensa(Clay_ElementId id, const char *const *opcoes, int n, int *escolhida);
/* Pediram o menu deste elemento: botão direito do rato, o dedo parado meio segundo em cima (e o
 * soltar já não conta como toque), ou a tecla Menu / Shift+F10 com ele focado. Abra um GV_POPUP
 * preso a ele. Peça-o antes de declarar elementos de dentro (o de fora fica com o pedido). */
bool gv_menu_contexto(Clay_ElementId id);
/* Seguraram este elemento meio segundo (o dedo ou o botão do rato, sem arrastar): o soltar já não
 * conta como toque. Também abre gv_menu_contexto, por isso peça primeiro o que quiser. */
bool gv_segurou(Clay_ElementId id);
/* (do GV_POPUP) */
bool gv__popup_comecar(Clay_ElementId id, Clay_ElementId ancora, const gv_popup_opcoes *opcoes);
Clay_ElementDeclaration gv__popup_declaracao(Clay_ElementId id);
void gv__popup_fim(void);

/* ---- imagens ---- */
enum gv_ajuste {
	GV_AJUSTE_COBRIR,  /* enche a caixa e corta o que sobra (papel de parede, capas) */
	GV_AJUSTE_CONTER,  /* cabe inteira, com sobra nos lados */
	GV_AJUSTE_ESTICAR, /* deforma até à caixa */
};
/* Uma imagem (PNG, JPEG, WebP, SVG...) como fundo do elemento d, nos cantos dele: descodifica numa
 * thread, no tamanho da caixa, e aparece esmaecendo. Os filhos de d vão por cima.
 *   CLAY(CLAY_ID("capa"), gv_imagem(CLAY_ID("capa"), "/caminho/capa.jpg", GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
 *       .layout = { .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200) } },
 *       .cornerRadius = CLAY_CORNER_RADIUS(12) })) {}
 * O custom.customData de d é da imagem. */
Clay_ElementDeclaration gv_imagem(Clay_ElementId id, const char *caminho, enum gv_ajuste ajuste, Clay_ElementDeclaration d);
/* largura / altura da imagem (0 enquanto não se sabe), para um .aspectRatio. */
float gv_imagem_proporcao(const char *caminho);
/* O ícone de um app pelo tema de ícones (o Icon= do .desktop: um nome ou um caminho), em
 * tamanho × tamanho; sem ícone, o desenho genérico. O tema é o de GRAVITY_ICONES, depois hicolor. */
void gv_icone_app(const char *nome, float tamanho);

/* ---- rolagem ---- */
enum gv_rolagem_eixo { GV_ROLAGEM_VERTICAL, GV_ROLAGEM_HORIZONTAL, GV_ROLAGEM_AMBOS };
/* Uma área que rola: devolve a declaração d com o corte, o deslocamento e a barra. Arrastar com o
 * dedo (com inércia e elástico nas pontas), a roda do rato (Shift: de lado) e a barra.
 *   CLAY(CLAY_ID("lista"), gv_rolagem(CLAY_ID("lista"), GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
 *       .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {
 *       ...os itens...
 *   }
 * O userData da declaração é da rolagem. */
Clay_ElementDeclaration gv_rolagem(Clay_ElementId id, enum gv_rolagem_eixo eixos, Clay_ElementDeclaration d);
/* Leva a área a (x, y) — deslizando, se animado. */
void gv_rolar_para(Clay_ElementId id, float x, float y, bool animado);
/* Quanto a área já rolou. */
Clay_Vector2 gv_rolagem_posicao(Clay_ElementId id);

/* Um separador de 1 px na cor da borda. */
void gv_separador(bool vertical, float comprimento);

/* A declaração de um cartão (superfície, borda, cantos e sombra do tema) para o CLAY():
 *   CLAY(CLAY_ID("cartao"), gv_cartao()) { ... }
 * Mude o que quiser depois: Clay_ElementDeclaration c = gv_cartao(); c.layout.padding = ...; */
Clay_ElementDeclaration gv_cartao(void);

/* Uma borda à volta (o CLAY_BORDER_ALL do Clay põe também linhas entre os filhos). */
#define GV_BORDA(largura) ((Clay_BorderWidth){ (largura), (largura), (largura), (largura), 0 })

/* Sombra num elemento: .userData = gv_sombra(raio_desfoque, deslocamento_y, alfa). */
void *gv_sombra(float desfoque, float desloca_y, float alfa);

#endif
