/*
 * O teclado na tela do LUMNA (lumna-teclado): o que cada tecla é e onde fica. Só dados e contas (sem Wayland, sem desenho), para
 * os testes poderem conferir tudo sem tela.
 *
 *   - O layout é o da língua de quem usa (o do Orbit: br = ABNT2, us = US-intl; os outros caem no US com os acentos de sempre).
 *   - Há quatro páginas: letras, símbolos (duas) e emojis. A letra de cima (Shift) vale uma vez; duas vezes seguidas trava (Caps).
 *   - Uma tecla segurada abre as variantes (a → á à â ã ä), como no celular.
 *   - Numa tela larga (o PC) entram Tab, Esc e as setas; numa estreita (o celular) a linha fica mais enxuta.
 */
#ifndef LUMNA_TECLADO_LAYOUT_H
#define LUMNA_TECLADO_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

enum tl_tipo {
	TL_CHAR,    /* escreve texto */
	TL_SHIFT,
	TL_APAGAR,
	TL_ENTER,
	TL_ESPACO,
	TL_PAGINA,  /* troca de página (destino) */
	TL_IDIOMA,  /* troca o layout (US ↔ pt-BR...) */
	TL_FECHAR,  /* esconde o teclado */
	TL_TAB,
	TL_ESC,
	TL_SETA,    /* destino: TL_SETA_* */
	TL_CATEGORIA, /* na página de emojis: troca de categoria (destino) */
};

enum tl_pagina_id { TL_LETRAS, TL_SIMB1, TL_SIMB2, TL_EMOJI };
enum tl_seta { TL_ESQ, TL_DIR, TL_CIMA, TL_BAIXO };

typedef struct tl_tecla {
	enum tl_tipo tipo;
	char rotulo[16];  /* o que se desenha */
	char texto[16];   /* o que escreve (TL_CHAR), já na caixa certa (Shift aplicado) */
	char alts[48];    /* as variantes, separadas por espaço ("á à â ã ä"); vazio se não há */
	float peso;       /* largura relativa na linha */
	int destino;      /* página, categoria, seta ou layout, conforme o tipo */
	bool ativa;       /* desenhar como ligada (Shift ligado, a categoria de agora) */
} tl_tecla;

#define TL_MAX_LINHAS 8
#define TL_MAX_POR_LINHA 16
#define TL_MAX_TECLAS 96

typedef struct tl_pagina {
	int nlinhas;
	int por_linha[TL_MAX_LINHAS];
	tl_tecla teclas[TL_MAX_TECLAS]; /* as linhas, uma depois da outra */
	int primeira[TL_MAX_LINHAS];    /* o índice da primeira tecla de cada linha */
	int total;
} tl_pagina;

enum tl_shift { TL_SHIFT_NAO, TL_SHIFT_UMA, TL_SHIFT_TRAVADO };

typedef struct tl_estado {
	int layout;              /* índice em tl_layouts */
	enum tl_pagina_id pagina;
	enum tl_shift shift;
	int categoria;           /* a categoria de emojis aberta */
	bool largo;              /* tela de PC: entram Tab, Esc e as setas */
} tl_estado;

/* ---- os layouts ---- */
int tl_layouts_n(void);
const char *tl_layout_nome(int i);   /* "pt-BR", "US" */
const char *tl_layout_sigla(int i);  /* "PT", "EN" (o que a tecla de idioma mostra) */
/* O layout para o do Orbit (orbit-msg entrada: layout "br", variante "abnt2" / "us"...). Sem correspondência: o US. */
int tl_layout_do_orbit(const char *layout, const char *variante);

/* ---- emojis ---- */
int tl_categorias_n(void);
const char *tl_categoria_rotulo(int i);   /* o emoji que representa a categoria */
const char *tl_categoria_nome(int i);
int tl_categoria_emojis(int i, const char *const **lista); /* a categoria 0 são os recentes (da pessoa) */
/* Os recentes: o último usado vai para a frente; guarda até TL_MAX_RECENTES. */
#define TL_MAX_RECENTES 24
void tl_recentes_usar(const char *emoji);
int tl_recentes_n(void);
const char *tl_recentes_get(int i);
void tl_recentes_limpar(void);

/* ---- montar a página de agora ---- */
void tl_montar(const tl_estado *estado, tl_pagina *saida);

/* ---- geometria ---- */
typedef struct tl_ret { float x, y, w, h; } tl_ret;
/* Cada tecla no retângulo largura × altura (com a folga entre elas); devolve quantas. rets tem de caber pagina->total. */
int tl_geometria(const tl_pagina *pagina, float largura, float altura, float folga, tl_ret *rets);
/* A tecla sob o ponto (x, y), ou a mais perto se o dedo caiu na folga; -1 se está fora do teclado. */
int tl_tecla_em(const tl_ret *rets, int n, float x, float y, float folga);

/* ---- o que uma tecla faz ---- */
enum tl_resultado {
	TL_NADA,
	TL_TEXTO,   /* escreveu `texto` */
	TL_REDESENHAR, /* mudou de página/shift/layout: só redesenha */
	TL_APAGAR_UM,
	TL_ENTER_,
	TL_TAB_,
	TL_ESC_,
	TL_SETA_,   /* texto vazio; `seta` diz qual */
	TL_ESCONDER,
};
typedef struct tl_acao {
	enum tl_resultado resultado;
	char texto[16];
	enum tl_seta seta;
} tl_acao;
/* Aplica o toque curto na tecla ao estado (Shift, página, layout...) e diz o que mandar ao app. */
tl_acao tl_tocar(tl_estado *estado, const tl_tecla *tecla);

/* Depois de `antes` (o texto antes do cursor) a próxima letra deve ser maiúscula: o campo está vazio, ou a frase acabou (". ", "! ", "? ", uma linha nova). */
bool tl_capitalizar_agora(const char *antes);

/* Minúscula → maiúscula de um texto UTF-8 (á → Á, ç → Ç). */
void tl_maiuscula(const char *texto, char *saida, size_t tam);

#endif
