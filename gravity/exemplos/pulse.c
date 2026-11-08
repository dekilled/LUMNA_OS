/*
 * lumna-pulse: o Pulse Terminal, o terminal do LUMNA, em Gravity. O terminal em si (a libvterm e o pty) é servicos/terminal.c; aqui fica a janela: a grade
 * desenhada com o cairo, as abas, a seleção com o mouse, o histórico, o colar e as teclas.
 *
 *   lumna-pulse                    um shell na pasta pessoal
 *   lumna-pulse --pasta DIR        o shell nessa pasta
 *   lumna-pulse -e comando args... roda o comando (a aba fecha quando ele acaba)
 *
 * Atalhos (os que não colidem com programas de terminal):
 *   Ctrl+Shift+C / V     copiar / colar          Shift+Insert  colar
 *   Ctrl+Shift+T / W     nova aba / fechar aba   Ctrl+PgUp / PgDn  (ou Ctrl+Tab)  trocar de aba
 *   Shift+PgUp / PgDn    rolar o histórico       Shift+Home / End  início / fim
 *   Ctrl+= / Ctrl+-      aumentar / diminuir a letra       Ctrl+0  tamanho normal
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <glib.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "emma_terminal.h"
#include "emmac.h"
#include "gravity.h"
#include "pulsedb.h"
#include "terminal.h"

#define MAX_ABAS 16
#define PAD 10
#define FAMILIA "JetBrains Mono,DejaVu Sans Mono,Noto Sans Mono,monospace"

/* a sugestão da Emma que está à espera de uma decisão (um cartão sobre a grade) */
struct sugestao {
	bool ativa;
	char *cmd;          /* o comando sugerido (NULL: a Emma só explicou o erro) */
	char *explicacao;
	int64_t interacao;  /* a linha em emma_interactions (para gravar o que a pessoa decidiu) */
	gint64 armada_em;   /* o Enter só vale daqui para frente: quem dá Enter por hábito logo depois do erro não roda a sugestão sem querer */
	bool perigoso;      /* destrutivo: o Enter só põe no prompt, a pessoa confirma de novo */
};

/* o campo da Emma (Ctrl+E): o que a pessoa quer fazer, em palavras, e a Emma devolve um comando (que vira uma sugestão como as da correção) */
enum nl_estado { NL_FECHADO, NL_DIGITANDO, NL_PENSANDO, NL_AVISO };
struct nl {
	enum nl_estado estado;
	char texto[512];
	int cursor;          /* em bytes, sempre no começo de um caractere */
	char aviso[200];     /* NL_AVISO: o que dizer (some sozinho) */
	gint64 aviso_ate;
};

struct aba {
	struct nl nl;
	int id;              /* não muda quando as abas andam (a resposta da Emma acha a aba por ele) */
	int geracao;         /* cada comando novo, ou tecla nova, invalida o pedido que estava no ar */
	struct sugestao sug;
	terminal *t;
	int desloc;          /* quantas linhas acima do fim se está olhando (0: no fim) */
	int hist_antes;      /* o histórico no quadro anterior: para segurar o que se lê quando entra texto novo */
	int64_t ultimo_cmd;  /* o último comando que acabou nesta aba (o id no banco; 0: nenhum) */
	int ultimo_exit;
	bool morreu;
	/* a seleção, em linhas absolutas (0 é a primeira do histórico): fica no lugar quando entra texto novo */
	bool sel_tem, sel_arrasta;
	int sel_l1, sel_c1, sel_l2, sel_c2;
};

static gv_app *app;
/* a memória e a Emma do Pulse (só em terminal de shell: com -e não há o que lembrar). NULL quando o banco não abriu: o terminal funciona igual, sem a Emma */
static pulsedb *banco;
static int64_t sessao_db;
static emt *emma;
static int proximo_id_aba;
#define ARMAR_US 600000 /* 0,6 s */
static struct aba abas[MAX_ABAS];
static int nabas, ativa;
static float tam_fonte = 15, tam_padrao = 15;
static float cw, ch; /* a célula, em pixels lógicos */
static int colunas = 80, linhas = 24;
static char *cwd_inicial, *comando_inicial;
static uint32_t cores_chave;

#define ID_GRADE CLAY_ID("co-grade")

/* ---- as cores ---- */

static uint32_t rgb_de(Clay_Color c) {
	return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static const uint32_t ANSI[16] = {
	0x2b2d33, 0xe58b98, 0x8fd6a0, 0xe5c987, 0x8fa8f0, 0xb79cf0, 0x7fd4dc, 0xd8d4e4,
	0x5a5d68, 0xf2a1ac, 0xa6e5b4, 0xf0d9a0, 0xa8bcf7, 0xc9b5f7, 0x9be3ea, 0xf4f1fa,
};

static void aplicar_cores(struct aba *a) {
	terminal_cores(a->t, rgb_de(gv_t.texto), rgb_de(gv_t.fundo), ANSI);
}

/* ---- a fonte ---- */

static PangoFontDescription *fonte_de(bool negrito, bool italico) {
	static PangoFontDescription *cache[4];
	static float tamanho_cache;
	if (tamanho_cache != tam_fonte) {
		for (int i = 0; i < 4; i++) {
			if (cache[i]) {
				pango_font_description_free(cache[i]);
				cache[i] = NULL;
			}
		}
		tamanho_cache = tam_fonte;
	}
	int i = (negrito ? 1 : 0) + (italico ? 2 : 0);
	if (!cache[i]) {
		PangoFontDescription *d = pango_font_description_new();
		pango_font_description_set_family(d, FAMILIA);
		pango_font_description_set_absolute_size(d, tam_fonte * PANGO_SCALE);
		pango_font_description_set_weight(d, negrito ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
		pango_font_description_set_style(d, italico ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
		cache[i] = d;
	}
	return cache[i];
}

/* o tamanho de uma célula, medido com a fonte de agora */
static void medir(void) {
	static cairo_surface_t *sup;
	static cairo_t *cr;
	if (!cr) {
		sup = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
		cr = cairo_create(sup);
	}
	PangoLayout *l = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(l, fonte_de(false, false));
	pango_layout_set_text(l, "MMMMMMMMMMMMMMMMMMMM", -1);
	PangoRectangle ink, logico;
	pango_layout_get_extents(l, &ink, &logico);
	cw = (float)logico.width / PANGO_SCALE / 20.0f;
	ch = (float)logico.height / PANGO_SCALE;
	g_object_unref(l);
	if (cw < 4) {
		cw = 4;
	}
	ch = ceilf(ch);
	if (ch < 8) {
		ch = 8;
	}
}

/* ---- as abas ---- */

/* ---- a correção de erro com a Emma ---- */

static struct aba *aba_por_id(int id) {
	for (int i = 0; i < nabas; i++) {
		if (abas[i].id == id) {
			return &abas[i];
		}
	}
	return NULL;
}

static void sug_limpar(struct aba *a) {
	g_free(a->sug.cmd);
	g_free(a->sug.explicacao);
	memset(&a->sug, 0, sizeof(a->sug));
}

/* a pessoa decidiu (ou seguiu em frente): o cartão some e a decisão vai para o banco */
static void sug_decidir(struct aba *a, const char *decisao) {
	if (!a->sug.ativa) {
		return;
	}
	if (banco && a->sug.interacao) {
		pulsedb_interacao_decisao(banco, a->sug.interacao, decisao);
	}
	sug_limpar(a);
	gv_app_redesenhar(app);
}

/* Enter (executar) ou Tab (só pôr no prompt para editar). O comando entra como colar (nada de Tab ou Enter escondido nele) e só roda com o Enter de verdade. */
static void sug_aceitar(struct aba *a, bool executar) {
	if (!a->sug.ativa || !a->sug.cmd) {
		return;
	}
	bool roda = executar && !a->sug.perigoso && !strchr(a->sug.cmd, '\n');
	terminal_colar(a->t, a->sug.cmd);
	if (roda) {
		terminal_tecla(a->t, XKB_KEY_Return, "", 0);
	}
	a->desloc = 0;
	sug_decidir(a, roda ? "aplicada" : "inserida");
}

/* a sugestão chegou: o cartão aparece (a explicação sozinha também aparece, sem comando) */
static void sug_mostrar(struct aba *a, char *cmd, char *expl, int64_t interacao) {
	a->sug.ativa = true;
	a->sug.cmd = cmd;
	a->sug.explicacao = expl;
	a->sug.interacao = interacao;
	a->sug.perigoso = cmd && emt_comando_perigoso(cmd, NULL);
	a->sug.armada_em = g_get_monotonic_time() + ARMAR_US;
	gv_acordar_em(0.7);
	gv_app_redesenhar(app);
}

struct emma_ctx {
	int aba_id, geracao;
	bool nl;       /* veio do campo da Emma (Ctrl+E), e não de uma correção */
	char *comando; /* a correção: o comando que falhou */
};

static void emma_pronta(const char *resposta, int64_t interacao, void *dados) {
	struct emma_ctx *c = dados;
	struct aba *a = aba_por_id(c->aba_id);
	if (a && a->geracao == c->geracao && c->nl) {
		/* o pedido em palavras: a pessoa esperava, então diz o que houve (sem travar nada: some sozinho) */
		if (a->nl.estado == NL_PENSANDO) {
			char *cmd = NULL, *expl = NULL;
			if (resposta) {
				emt_separar(resposta, &cmd, &expl);
			}
			if (cmd || expl) {
				a->nl.estado = NL_FECHADO;
				sug_limpar(a);
				sug_mostrar(a, cmd, expl, interacao);
			} else {
				g_free(cmd);
				g_free(expl);
				a->nl.estado = NL_AVISO;
				g_strlcpy(a->nl.aviso, emma && !emt_ligado(emma) ? "A Emma está desligada no terminal (emma=nao em ~/.config/lumna/pulse.ini)."
					: "A Emma não respondeu agora. Tente de novo daqui a pouco.", sizeof(a->nl.aviso));
				a->nl.aviso_ate = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
				gv_acordar_em(3.1);
				gv_app_redesenhar(app);
			}
		}
	} else if (a && a->geracao == c->geracao && resposta) {
		char *cmd, *expl;
		emt_separar(resposta, &cmd, &expl);
		char *digitado = terminal_linha_digitada(a->t); /* se a pessoa já começou a digitar, não atrapalha */
		bool livre = !digitado || !*digitado;
		g_free(digitado);
		bool igual = cmd && c->comando && !strcmp(cmd, c->comando);
		if (livre && !igual && (cmd || expl) && !a->sug.ativa && a->nl.estado == NL_FECHADO) {
			sug_mostrar(a, cmd, expl, interacao);
			cmd = expl = NULL;
		}
		g_free(cmd);
		g_free(expl);
	}
	g_free(c->comando);
	g_free(c);
}

/* ---- o campo da Emma (Ctrl+E) ---- */

/* Ctrl+E só abre com o prompt vazio: no bash ele é "ir para o fim da linha", e numa linha vazia não faz nada (nada se perde); com texto digitado continua sendo do shell */
static bool nl_pode_abrir(struct aba *a) {
	if (!emma || !banco || !terminal_integrado(a->t) || terminal_tela_alternativa(a->t)) {
		return false;
	}
	char *dig = terminal_linha_digitada(a->t);
	bool vazio = dig && !*g_strstrip(dig);
	g_free(dig);
	return vazio;
}

static void nl_fechar(struct aba *a) {
	if (a->nl.estado == NL_PENSANDO) {
		a->geracao++; /* a resposta que ainda vier não vale */
	}
	memset(&a->nl, 0, sizeof(a->nl));
	gv_app_redesenhar(app);
}

static void nl_abrir(struct aba *a) {
	sug_limpar(a);
	memset(&a->nl, 0, sizeof(a->nl));
	a->nl.estado = NL_DIGITANDO;
	a->desloc = 0;
	gv_app_redesenhar(app);
}

static void nl_enviar(struct aba *a) {
	char *t = g_strstrip(g_strdup(a->nl.texto));
	if (!*t) {
		g_free(t);
		return;
	}
	a->nl.estado = NL_PENSANDO;
	struct emma_ctx *c = g_new0(struct emma_ctx, 1);
	c->aba_id = a->id;
	c->geracao = ++a->geracao;
	c->nl = true;
	/* o contexto (a pasta onde o shell está, os últimos 3 comandos e as últimas sessões) é montado pelo emma_terminal */
	emt_pedir(emma, &(struct emt_pedido){ .tipo = EMT_COMANDO, .cwd = terminal_pasta(a->t), .texto = t }, emma_pronta, c);
	g_free(t);
	gv_acordar_em(0.4);
	gv_app_redesenhar(app);
}

static void nl_inserir(struct aba *a, const char *txt) {
	size_t n = strlen(txt), atual = strlen(a->nl.texto);
	if (atual + n >= sizeof(a->nl.texto)) {
		return;
	}
	memmove(a->nl.texto + a->nl.cursor + n, a->nl.texto + a->nl.cursor, atual - (size_t)a->nl.cursor + 1);
	memcpy(a->nl.texto + a->nl.cursor, txt, n);
	a->nl.cursor += (int)n;
}

static void nl_apagar(struct aba *a, int de, int ate) {
	if (de < 0 || ate > (int)strlen(a->nl.texto) || de >= ate) {
		return;
	}
	memmove(a->nl.texto + de, a->nl.texto + ate, strlen(a->nl.texto) - (size_t)ate + 1);
	a->nl.cursor = de;
}

/* uma tecla no campo: sempre consumida (nada vai ao shell com o campo aberto) */
static void nl_tecla(struct aba *a, uint32_t s, const char *txt, uint32_t mods) {
	bool ctrl = mods & GV_MOD_CTRL, shift = mods & GV_MOD_SHIFT, alt = mods & GV_MOD_ALT;
	if (a->nl.estado == NL_AVISO) {
		nl_fechar(a); /* qualquer tecla fecha o aviso */
		return;
	}
	if (s == XKB_KEY_Escape) {
		nl_fechar(a);
		return;
	}
	if (a->nl.estado == NL_PENSANDO) {
		return; /* esperando a Emma: só o Esc vale */
	}
	char *t = a->nl.texto;
	int len = (int)strlen(t);
	if ((s == XKB_KEY_Return || s == XKB_KEY_KP_Enter) && !shift) {
		nl_enviar(a);
	} else if (s == XKB_KEY_BackSpace) {
		if (a->nl.cursor > 0) {
			nl_apagar(a, (int)(g_utf8_prev_char(t + a->nl.cursor) - t), a->nl.cursor);
		}
	} else if (s == XKB_KEY_Delete) {
		if (a->nl.cursor < len) {
			nl_apagar(a, a->nl.cursor, (int)(g_utf8_next_char(t + a->nl.cursor) - t));
		}
	} else if (s == XKB_KEY_Left) {
		if (a->nl.cursor > 0) {
			a->nl.cursor = (int)(g_utf8_prev_char(t + a->nl.cursor) - t);
		}
	} else if (s == XKB_KEY_Right) {
		if (a->nl.cursor < len) {
			a->nl.cursor = (int)(g_utf8_next_char(t + a->nl.cursor) - t);
		}
	} else if (s == XKB_KEY_Home || (ctrl && (s == 'a' || s == 'A'))) {
		a->nl.cursor = 0;
	} else if (s == XKB_KEY_End || (ctrl && (s == 'e' || s == 'E') && !shift)) {
		a->nl.cursor = len;
	} else if (ctrl && (s == 'u' || s == 'U')) {
		nl_apagar(a, 0, a->nl.cursor);
	} else if (ctrl && (s == 'k' || s == 'K')) {
		nl_apagar(a, a->nl.cursor, len);
	} else if (ctrl && (s == 'w' || s == 'W')) {
		int i = a->nl.cursor;
		while (i > 0 && t[i - 1] == ' ') {
			i--;
		}
		while (i > 0 && t[i - 1] != ' ') {
			i--;
		}
		nl_apagar(a, i, a->nl.cursor);
	} else if ((ctrl && shift && (s == 'v' || s == 'V')) || (shift && !ctrl && s == XKB_KEY_Insert)) {
		const char *c = gv_colar();
		if (c && *c) {
			char *copia = g_strdup(c);
			for (char *p = copia; *p; p++) {
				if (*p == '\n' || *p == '\r' || *p == '\t') {
					*p = ' '; /* uma linha só */
				}
			}
			nl_inserir(a, copia);
			g_free(copia);
		}
	} else if (txt && *txt && !ctrl && !alt) {
		nl_inserir(a, txt);
	}
	gv_app_redesenhar(app);
}

/* o que o shell avisou (a integração): cada comando que acabou vai para a memória do Pulse; o que falhou vai à Emma pedir a correção */
static void eventos_da_aba(struct aba *aba) {
	struct term_evento e;
	while (terminal_evento(aba->t, &e)) {
		if (e.tipo == TERM_EV_COMANDO_INICIO) {
			aba->geracao++; /* um comando novo: o pedido de antes não vale mais */
			sug_limpar(aba);
		} else if (e.tipo == TERM_EV_COMANDO_FIM && e.comando) {
			if (banco) {
				aba->ultimo_cmd = pulsedb_comando_gravar(banco, sessao_db, e.pasta ? e.pasta : "", e.comando, e.exit_code, e.saida ? e.saida : "");
				aba->ultimo_exit = e.exit_code;
			}
			if (emma && banco && emt_falha_vale_a_pena(e.comando, e.exit_code, NULL)) {
				struct emma_ctx *c = g_new0(struct emma_ctx, 1);
				c->aba_id = aba->id;
				c->geracao = aba->geracao;
				c->comando = g_strdup(e.comando);
				emt_pedir(emma, &(struct emt_pedido){ .tipo = EMT_CORRECAO, .cwd = e.pasta ? e.pasta : "", .comando = e.comando, .saida = e.saida ? e.saida : "",
					.exit_code = e.exit_code, .comando_id = aba->ultimo_cmd }, emma_pronta, c);
			}
		}
		terminal_evento_limpar(&e);
	}
}

static void ler_pronto(gv_app *a, int fd, void *dados) {
	struct aba *aba = dados;
	if (!terminal_ler(aba->t)) {
		aba->morreu = true;
	}
	eventos_da_aba(aba);
	gv_app_redesenhar(a);
}

static bool nova_aba(const char *cwd, const char *comando) {
	if (nabas >= MAX_ABAS) {
		return false;
	}
	terminal *t = terminal_abrir(colunas, linhas, cwd, comando);
	if (!t) {
		return false;
	}
	struct aba *a = &abas[nabas];
	memset(a, 0, sizeof(*a));
	a->id = ++proximo_id_aba;
	a->t = t;
	aplicar_cores(a);
	gv_app_vigiar(app, terminal_fd(t), ler_pronto, a);
	ativa = nabas++;
	return true;
}

static void fechar_aba(int i) {
	struct aba *a = &abas[i];
	sug_limpar(a);
	gv_app_desvigiar(app, terminal_fd(a->t));
	terminal_fechar(a->t);
	memmove(a, a + 1, sizeof(*a) * (size_t)(nabas - i - 1));
	nabas--;
	if (ativa >= nabas) {
		ativa = nabas - 1;
	}
	if (ativa < 0) {
		ativa = 0;
	}
	/* as abas andaram: os dados que o gv_app_vigiar guarda (o ponteiro da aba) mudaram de lugar; refaz as vigias */
	for (int k = i; k < nabas; k++) {
		gv_app_desvigiar(app, terminal_fd(abas[k].t));
		gv_app_vigiar(app, terminal_fd(abas[k].t), ler_pronto, &abas[k]);
	}
}

/* o nome da aba: o título que o programa pôs, ou a pasta onde o shell está */
static const char *nome_aba(struct aba *a, char *buf, size_t n) {
	const char *titulo = terminal_titulo(a->t);
	if (titulo[0]) {
		return titulo;
	}
	const char *pasta = terminal_pasta(a->t);
	const char *home = g_get_home_dir();
	if (!pasta[0]) {
		return "Terminal";
	}
	if (home && !strcmp(pasta, home)) {
		return "~";
	}
	const char *base = strrchr(pasta, '/');
	snprintf(buf, n, "%s", base && base[1] ? base + 1 : pasta);
	return buf;
}

/* ---- a seleção ---- */

static void sel_limpar(struct aba *a) {
	a->sel_tem = false;
	a->sel_arrasta = false;
}

/* a linha absoluta (0 = a primeira do histórico) de uma linha lógica (negativa no histórico) */
static int abs_de(struct aba *a, int linha) {
	return terminal_historico(a->t) + linha;
}

static void sel_ordenada(struct aba *a, int *l1, int *c1, int *l2, int *c2) {
	*l1 = a->sel_l1;
	*c1 = a->sel_c1;
	*l2 = a->sel_l2;
	*c2 = a->sel_c2;
	if (*l1 > *l2 || (*l1 == *l2 && *c1 > *c2)) {
		int x = *l1;
		*l1 = *l2;
		*l2 = x;
		x = *c1;
		*c1 = *c2;
		*c2 = x;
	}
}

static char *sel_texto(struct aba *a) {
	if (!a->sel_tem) {
		return NULL;
	}
	int l1, c1, l2, c2;
	sel_ordenada(a, &l1, &c1, &l2, &c2);
	int h = terminal_historico(a->t);
	return terminal_texto(a->t, l1 - h, c1, l2 - h, c2);
}

static void copiar(struct aba *a) {
	char *s = sel_texto(a);
	if (s && *s) {
		gv_copiar(s);
	}
	free(s);
}

static void colar(struct aba *a) {
	const char *s = gv_colar();
	if (s && *s) {
		terminal_colar(a->t, s);
		a->desloc = 0;
		sel_limpar(a);
	}
}

/* ---- o desenho da grade ---- */

struct grade_dados {
	struct aba *aba;
};

static void cor_rgb(cairo_t *cr, uint32_t c, double alfa) {
	cairo_set_source_rgba(cr, ((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0, (c & 0xff) / 255.0, alfa);
}

/* o cartão da Emma: sobre a grade, logo abaixo do prompt (ou acima, quando embaixo não cabe), no acento do LUMNA para não se confundir com a saída dos programas */
static PangoLayout *texto_layout(cairo_t *cr, const char *txt, const char *familia, double tamanho, double largura, bool negrito) {
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *d = pango_font_description_new();
	pango_font_description_set_family(d, familia);
	pango_font_description_set_absolute_size(d, tamanho * PANGO_SCALE);
	pango_font_description_set_weight(d, negrito ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	pango_layout_set_font_description(l, d);
	pango_font_description_free(d);
	pango_layout_set_width(l, (int)(largura * PANGO_SCALE));
	pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_text(l, txt, -1);
	return l;
}

/* o quadro do cartão (fundo, borda, faixa lilás), abaixo do prompt ou acima dele; devolve onde começa o texto */
static void card_quadro(cairo_t *cr, Clay_BoundingBox caixa, int cursor_linha, double alt, double larg, double *tx, double *ty) {
	const double pad = 12, faixa = 4;
	double ox = caixa.x + PAD, oy = caixa.y + PAD;
	double x = ox, y = oy + (cursor_linha + 1) * ch + 6;
	if (y + alt > caixa.y + caixa.height - 4) {
		y = oy + cursor_linha * ch - alt - 6; /* embaixo não cabe: acima do prompt */
	}
	if (y < caixa.y + 4) {
		y = caixa.y + 4;
	}
	/* o fundo, a borda e a faixa lilás */
	cairo_save(cr);
	double r = 10;
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + larg - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + larg - r, y + alt - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + alt - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, gv_t.elevada.r / 255.0, gv_t.elevada.g / 255.0, gv_t.elevada.b / 255.0, 0.98);
	cairo_fill_preserve(cr);
	cairo_clip(cr);
	cairo_new_path(cr);
	cairo_set_source_rgba(cr, gv_t.acento.r / 255.0, gv_t.acento.g / 255.0, gv_t.acento.b / 255.0, 1);
	cairo_rectangle(cr, x, y, faixa, alt);
	cairo_fill(cr);
	cairo_reset_clip(cr);
	cairo_set_source_rgba(cr, gv_t.acento.r / 255.0, gv_t.acento.g / 255.0, gv_t.acento.b / 255.0, 0.55);
	cairo_set_line_width(cr, 1);
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + larg - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + larg - r, y + alt - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + alt - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
	cairo_stroke(cr);
	cairo_restore(cr);
	*tx = x + faixa + pad;
	*ty = y + pad;
}

static void desenhar_sugestao(cairo_t *cr, Clay_BoundingBox caixa, struct aba *a, int cursor_linha) {
	const double pad = 12, faixa = 4;
	double larg = caixa.width - 2 * PAD;
	if (larg > 820) {
		larg = 820;
	}
	if (larg < 120) {
		return;
	}
	double inner = larg - 2 * pad - faixa;
	double tam_ui = tam_fonte * 0.85;
	const char *ui = gv_t.fonte[0] ? gv_t.fonte : "Inter";
	const char *dica = !a->sug.cmd ? "Esc fecha" : a->sug.perigoso ? "Enter coloca no prompt (comando destrutivo: você confirma de novo)  ·  Esc descarta"
		: "Enter executa  ·  Tab coloca no prompt para editar  ·  Esc descarta";
	PangoLayout *cab = texto_layout(cr, a->sug.cmd ? "Emma sugere" : "Emma", ui, tam_ui, inner, true);
	PangoLayout *cmd = a->sug.cmd ? texto_layout(cr, a->sug.cmd, FAMILIA, tam_fonte, inner - 16, false) : NULL;
	PangoLayout *exp = a->sug.explicacao && *a->sug.explicacao ? texto_layout(cr, a->sug.explicacao, ui, tam_ui, inner, false) : NULL;
	PangoLayout *dic = texto_layout(cr, dica, ui, tam_ui * 0.92, inner, false);
	int w, hc, hm = 0, he = 0, hd;
	pango_layout_get_pixel_size(cab, &w, &hc);
	if (cmd) {
		pango_layout_get_pixel_size(cmd, &w, &hm);
	}
	if (exp) {
		pango_layout_get_pixel_size(exp, &w, &he);
	}
	pango_layout_get_pixel_size(dic, &w, &hd);
	double alt = pad + hc + 6 + (cmd ? hm + 12 + 6 : 0) + (exp ? he + 6 : 0) + hd + pad;
	double tx, ty;
	card_quadro(cr, caixa, cursor_linha, alt, larg, &tx, &ty);
	/* o texto */
	cairo_set_source_rgba(cr, gv_t.acento.r / 255.0, gv_t.acento.g / 255.0, gv_t.acento.b / 255.0, 1);
	cairo_move_to(cr, tx, ty);
	pango_cairo_show_layout(cr, cab);
	ty += hc + 6;
	if (cmd) {
		cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
		cairo_rectangle(cr, tx, ty, inner, hm + 12);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, gv_t.texto.r / 255.0, gv_t.texto.g / 255.0, gv_t.texto.b / 255.0, 1);
		cairo_move_to(cr, tx + 8, ty + 6);
		pango_cairo_show_layout(cr, cmd);
		ty += hm + 12 + 6;
	}
	if (exp) {
		cairo_set_source_rgba(cr, gv_t.texto_secundario.r / 255.0, gv_t.texto_secundario.g / 255.0, gv_t.texto_secundario.b / 255.0, 1);
		cairo_move_to(cr, tx, ty);
		pango_cairo_show_layout(cr, exp);
		ty += he + 6;
	}
	cairo_set_source_rgba(cr, gv_t.texto_suave.r / 255.0, gv_t.texto_suave.g / 255.0, gv_t.texto_suave.b / 255.0, 1);
	cairo_move_to(cr, tx, ty);
	pango_cairo_show_layout(cr, dic);
	g_object_unref(cab);
	if (cmd) {
		g_object_unref(cmd);
	}
	if (exp) {
		g_object_unref(exp);
	}
	g_object_unref(dic);
}

/* o cartão do campo da Emma: digitando (com o cursor), pensando, ou um aviso curto */
static void desenhar_nl(cairo_t *cr, Clay_BoundingBox caixa, struct aba *a, int cursor_linha) {
	const double pad = 12, faixa = 4;
	double larg = caixa.width - 2 * PAD;
	if (larg > 820) {
		larg = 820;
	}
	if (larg < 120) {
		return;
	}
	double inner = larg - 2 * pad - faixa;
	double tam_ui = tam_fonte * 0.85;
	const char *ui = gv_t.fonte[0] ? gv_t.fonte : "Inter";
	static const char *pontos[] = { "", ".", "..", "..." };
	char cab_txt[96];
	const char *dica;
	switch (a->nl.estado) {
	case NL_PENSANDO:
		snprintf(cab_txt, sizeof(cab_txt), "Emma · pensando%s", pontos[(int)(gv_agora() * 2.5) % 4]);
		dica = "Esc cancela";
		break;
	case NL_AVISO:
		snprintf(cab_txt, sizeof(cab_txt), "Emma");
		dica = "Qualquer tecla fecha";
		break;
	default:
		snprintf(cab_txt, sizeof(cab_txt), "Emma · o que você quer fazer?");
		dica = "Enter pede o comando  ·  Esc cancela  ·  nada roda sem a sua confirmação";
		break;
	}
	bool vazio = a->nl.estado == NL_DIGITANDO && !a->nl.texto[0];
	const char *corpo = a->nl.estado == NL_AVISO ? a->nl.aviso : vazio ? "ex.: listar os arquivos com mais de 100 MB nesta pasta" : a->nl.texto;
	PangoLayout *cab = texto_layout(cr, cab_txt, ui, tam_ui, inner, true);
	PangoLayout *cor = texto_layout(cr, corpo, a->nl.estado == NL_AVISO ? ui : FAMILIA, a->nl.estado == NL_AVISO ? tam_ui : tam_fonte, inner - 16, false);
	PangoLayout *dic = texto_layout(cr, dica, ui, tam_ui * 0.92, inner, false);
	int w, hc, hk, hd;
	pango_layout_get_pixel_size(cab, &w, &hc);
	pango_layout_get_pixel_size(cor, &w, &hk);
	pango_layout_get_pixel_size(dic, &w, &hd);
	double alt = pad + hc + 6 + hk + 12 + 6 + hd + pad;
	double tx, ty;
	card_quadro(cr, caixa, cursor_linha, alt, larg, &tx, &ty);
	cairo_set_source_rgba(cr, gv_t.acento.r / 255.0, gv_t.acento.g / 255.0, gv_t.acento.b / 255.0, 1);
	cairo_move_to(cr, tx, ty);
	pango_cairo_show_layout(cr, cab);
	ty += hc + 6;
	/* o campo: um fundo escuro com o texto (ou a dica em cinza) e o cursor */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
	cairo_rectangle(cr, tx, ty, inner, hk + 12);
	cairo_fill(cr);
	Clay_Color c = vazio ? gv_t.texto_suave : a->nl.estado == NL_PENSANDO ? gv_t.texto_secundario : gv_t.texto;
	cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0, 1);
	cairo_move_to(cr, tx + 8, ty + 6);
	pango_cairo_show_layout(cr, cor);
	if (a->nl.estado == NL_DIGITANDO) {
		PangoRectangle pos;
		pango_layout_get_cursor_pos(cor, vazio ? 0 : a->nl.cursor, &pos, NULL);
		cairo_set_source_rgba(cr, gv_t.acento.r / 255.0, gv_t.acento.g / 255.0, gv_t.acento.b / 255.0, 1);
		cairo_rectangle(cr, tx + 8 + (double)pos.x / PANGO_SCALE, ty + 6 + (double)pos.y / PANGO_SCALE, 2, (double)pos.height / PANGO_SCALE);
		cairo_fill(cr);
	}
	ty += hk + 12 + 6;
	cairo_set_source_rgba(cr, gv_t.texto_suave.r / 255.0, gv_t.texto_suave.g / 255.0, gv_t.texto_suave.b / 255.0, 1);
	cairo_move_to(cr, tx, ty);
	pango_cairo_show_layout(cr, dic);
	g_object_unref(cab);
	g_object_unref(cor);
	g_object_unref(dic);
}

static void desenhar_grade(cairo_t *cr, Clay_BoundingBox caixa, void *dados) {
	struct aba *a = ((struct grade_dados *)dados)->aba;
	terminal *t = a->t;
	double ox = caixa.x + PAD, oy = caixa.y + PAD;
	int cols = terminal_colunas(t), rows = terminal_linhas(t);

	cairo_save(cr);
	cairo_rectangle(cr, caixa.x, caixa.y, caixa.width, caixa.height);
	cairo_clip(cr);

	int cl, cc;
	bool cv;
	terminal_cursor(t, &cl, &cc, &cv);
	cv = cv && a->desloc == 0;

	int sl1 = 0, sc1 = 0, sl2 = 0, sc2 = 0;
	if (a->sel_tem) {
		sel_ordenada(a, &sl1, &sc1, &sl2, &sc2);
	}
	int h = terminal_historico(t);

	PangoLayout *lay = pango_cairo_create_layout(cr);
	/* os fundos das células que não são o fundo do terminal */
	for (int r = 0; r < rows; r++) {
		int linha = r - a->desloc;
		double y = oy + r * ch;
		int c = 0;
		while (c < cols) {
			struct term_celula cel;
			terminal_celula(t, linha, c, &cel);
			if (cel.bg_padrao) {
				c++;
				continue;
			}
			int c2 = c + 1;
			while (c2 < cols) {
				struct term_celula o;
				terminal_celula(t, linha, c2, &o);
				if (o.bg_padrao || o.bg != cel.bg) {
					break;
				}
				c2++;
			}
			cor_rgb(cr, cel.bg, 1);
			cairo_rectangle(cr, ox + c * cw, y, (c2 - c) * cw, ch);
			cairo_fill(cr);
			c = c2;
		}
	}
	/* a seleção */
	if (a->sel_tem) {
		cairo_set_source_rgba(cr, gv_t.acento.r / 255.0, gv_t.acento.g / 255.0, gv_t.acento.b / 255.0, 0.35);
		for (int r = 0; r < rows; r++) {
			int linha_abs = h + r - a->desloc;
			if (linha_abs < sl1 || linha_abs > sl2) {
				continue;
			}
			int ini = linha_abs == sl1 ? sc1 : 0;
			int fim = linha_abs == sl2 ? sc2 : cols - 1;
			if (fim < ini) {
				continue;
			}
			cairo_rectangle(cr, ox + ini * cw, oy + r * ch, (fim - ini + 1) * cw, ch);
			cairo_fill(cr);
		}
	}
	/* o cursor: um bloco do acento (o caractere por cima, escuro) */
	if (cv && cl < rows && cc < cols) {
		cor_rgb(cr, rgb_de(gv_t.acento), 1);
		cairo_rectangle(cr, ox + cc * cw, oy + cl * ch, cw, ch);
		cairo_fill(cr);
	}
	/* o texto: runs de ASCII com o mesmo estilo saem de uma vez; o resto, célula a célula */
	for (int r = 0; r < rows; r++) {
		int linha = r - a->desloc;
		double y = oy + r * ch;
		GString *run = g_string_new(NULL);
		int run_ini = -1;
		struct term_celula run_estilo = { 0 };
		bool run_cursor = false;
		for (int c = 0; c <= cols; c++) {
			struct term_celula cel = { 0 };
			bool tem = c < cols && terminal_celula(t, linha, c, &cel);
			bool no_cursor = cv && r == cl && c == cc;
			bool vazio = !tem || !cel.texto[0] || (cel.texto[0] == ' ' && !cel.texto[1]);
			bool ascii = tem && cel.texto[0] && !cel.texto[1] && (unsigned char)cel.texto[0] < 0x7f && (unsigned char)cel.texto[0] >= 0x20;
			if (no_cursor && !vazio) {
				cel.fg = rgb_de(gv_t.fundo);
			}
			/* decorações (sublinhado, riscado) por célula */
			if (tem && (cel.sublinhado || cel.riscado)) {
				cor_rgb(cr, cel.fg, 1);
				cairo_set_line_width(cr, 1);
				if (cel.sublinhado) {
					cairo_move_to(cr, ox + c * cw, y + ch - 1.5);
					cairo_line_to(cr, ox + (c + 1) * cw, y + ch - 1.5);
				}
				if (cel.riscado) {
					cairo_move_to(cr, ox + c * cw, y + ch / 2);
					cairo_line_to(cr, ox + (c + 1) * cw, y + ch / 2);
				}
				cairo_stroke(cr);
			}
			bool segue = run_ini >= 0 && ascii && !no_cursor && !run_cursor && cel.fg == run_estilo.fg && cel.negrito == run_estilo.negrito &&
				cel.italico == run_estilo.italico;
			if (run_ini >= 0 && vazio && !no_cursor && c < cols) {
				g_string_append_c(run, ' '); /* um vazio no meio do run continua o run */
				continue;
			}
			if (run_ini >= 0 && segue) {
				g_string_append_c(run, cel.texto[0]);
				continue;
			}
			if (run_ini >= 0) {
				/* o run acabou: desenha sem os espaços do fim */
				while (run->len && run->str[run->len - 1] == ' ') {
					g_string_truncate(run, run->len - 1);
				}
				if (run->len) {
					pango_layout_set_font_description(lay, fonte_de(run_estilo.negrito, run_estilo.italico));
					pango_layout_set_text(lay, run->str, (int)run->len);
					cor_rgb(cr, run_estilo.fg, 1);
					cairo_move_to(cr, ox + run_ini * cw, y);
					pango_cairo_show_layout(cr, lay);
				}
				g_string_truncate(run, 0);
				run_ini = -1;
			}
			if (vazio) {
				continue;
			}
			if (ascii && !no_cursor) {
				run_ini = c;
				run_estilo = cel;
				run_cursor = false;
				g_string_append_c(run, cel.texto[0]);
				continue;
			}
			/* um caractere fora do ASCII, ou o do cursor: sozinho, no lugar da célula */
			pango_layout_set_font_description(lay, fonte_de(cel.negrito, cel.italico));
			pango_layout_set_text(lay, cel.texto, -1);
			cor_rgb(cr, cel.fg, 1);
			cairo_move_to(cr, ox + c * cw, y);
			pango_cairo_show_layout(cr, lay);
		}
		g_string_free(run, TRUE);
	}
	g_object_unref(lay);
	/* uma barrinha à direita quando se olha o histórico */
	if (a->desloc > 0 && h > 0) {
		double total = h + rows;
		double alt = caixa.height * rows / total;
		if (alt < 24) {
			alt = 24;
		}
		double topo = (caixa.height - alt) * (h - a->desloc) / h;
		cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
		cairo_rectangle(cr, caixa.x + caixa.width - 6, caixa.y + topo, 4, alt);
		cairo_fill(cr);
	}
	if (a->nl.estado != NL_FECHADO && a->desloc == 0) {
		desenhar_nl(cr, caixa, a, cl);
	} else if (a->sug.ativa && a->desloc == 0) {
		desenhar_sugestao(cr, caixa, a, cl);
	}
	cairo_restore(cr);
}

/* ---- o teclado ---- */

static bool tecla_so_modificador(uint32_t s) {
	return (s >= 0xffe1 && s <= 0xffee) || s == 0xfe03 || s == 0xff7e || s == 0xff7f;
}

static uint32_t mods_term(uint32_t m) {
	return (m & GV_MOD_SHIFT ? TERM_SHIFT : 0) | (m & GV_MOD_CTRL ? TERM_CTRL : 0) | (m & GV_MOD_ALT ? TERM_ALT : 0);
}

static void zoom(float novo) {
	if (novo < 8) {
		novo = 8;
	}
	if (novo > 40) {
		novo = 40;
	}
	tam_fonte = novo;
	medir();
}

static void rolar(struct aba *a, int n) {
	if (terminal_tela_alternativa(a->t)) {
		return;
	}
	a->desloc += n;
	int h = terminal_historico(a->t);
	if (a->desloc > h) {
		a->desloc = h;
	}
	if (a->desloc < 0) {
		a->desloc = 0;
	}
}

static void teclas(struct aba *a) {
	gv_teclado_cru();
	uint32_t mods = gv_modificadores();
	bool ctrl = mods & GV_MOD_CTRL, shift = mods & GV_MOD_SHIFT;
	for (int i = 0;; i++) {
		uint32_t s = gv_tecla_n(i);
		if (!s) {
			break;
		}
		const char *txt = gv_tecla_texto_n(i);
		if (tecla_so_modificador(s)) {
			continue;
		}
		if (a->nl.estado != NL_FECHADO) {
			nl_tecla(a, s, txt, mods);
			continue;
		}
		if (a->sug.ativa) {
			bool sem_mods = !(mods & (GV_MOD_CTRL | GV_MOD_ALT | GV_MOD_SHIFT | GV_MOD_LOGO));
			if (s == XKB_KEY_Escape && sem_mods) {
				sug_decidir(a, "descartada");
				continue;
			}
			if ((s == XKB_KEY_Return || s == XKB_KEY_KP_Enter) && sem_mods && a->sug.cmd && g_get_monotonic_time() >= a->sug.armada_em) {
				sug_aceitar(a, true);
				continue;
			}
			if (s == XKB_KEY_Tab && sem_mods && a->sug.cmd) {
				sug_aceitar(a, false);
				continue;
			}
			/* qualquer outra tecla (ou um Enter cedo demais, ou com só uma explicação): a sugestão fica para trás e a tecla segue para o shell */
			a->geracao++;
			sug_decidir(a, "ignorada");
		}
		uint32_t m = s >= 'a' && s <= 'z' ? s - 32 : s; /* a letra, de qualquer caixa */
		if (ctrl && !shift && !(mods & GV_MOD_ALT) && m == 'E' && nl_pode_abrir(a)) {
			nl_abrir(a);
			continue;
		}
		if (ctrl && shift) {
			if (m == 'C') {
				copiar(a);
				continue;
			}
			if (m == 'V') {
				colar(a);
				continue;
			}
			if (m == 'T') {
				nova_aba(terminal_pasta(a->t), NULL);
				return; /* a aba de agora mudou */
			}
			if (m == 'W') {
				a->morreu = true;
				continue;
			}
			if (s == XKB_KEY_ISO_Left_Tab) {
				ativa = (ativa + nabas - 1) % nabas;
				return;
			}
		}
		if (ctrl && !shift && (s == XKB_KEY_Page_Up || s == XKB_KEY_Page_Down)) {
			ativa = (ativa + (s == XKB_KEY_Page_Down ? 1 : nabas - 1)) % nabas;
			return;
		}
		if (ctrl && !shift && s == XKB_KEY_Tab) {
			ativa = (ativa + 1) % nabas;
			return;
		}
		if (ctrl && !shift && (s == '=' || s == '+' || s == XKB_KEY_KP_Add)) {
			zoom(tam_fonte + 1);
			continue;
		}
		if (ctrl && !shift && (s == '-' || s == XKB_KEY_KP_Subtract || s == XKB_KEY_minus)) {
			zoom(tam_fonte - 1);
			continue;
		}
		if (ctrl && !shift && s == '0') {
			zoom(tam_padrao);
			continue;
		}
		if (shift && !ctrl && s == XKB_KEY_Insert) {
			colar(a);
			continue;
		}
		if (shift && !ctrl && !terminal_tela_alternativa(a->t)) {
			if (s == XKB_KEY_Page_Up) {
				rolar(a, terminal_linhas(a->t) - 1);
				continue;
			}
			if (s == XKB_KEY_Page_Down) {
				rolar(a, -(terminal_linhas(a->t) - 1));
				continue;
			}
			if (s == XKB_KEY_Home) {
				rolar(a, 1 << 20);
				continue;
			}
			if (s == XKB_KEY_End) {
				a->desloc = 0;
				continue;
			}
		}
		terminal_tecla(a->t, s, txt, mods_term(mods));
		a->desloc = 0;
		sel_limpar(a);
	}
}

/* ---- o mouse ---- */

static bool celula_sob(Clay_BoundingBox g, Clay_Vector2 p, int *linha, int *coluna) {
	double x = (p.x - (g.x + PAD)) / cw, y = (p.y - (g.y + PAD)) / ch;
	int c = (int)floor(x), l = (int)floor(y);
	int cols = terminal_colunas(abas[ativa].t), rows = terminal_linhas(abas[ativa].t);
	bool dentro = c >= 0 && c < cols && l >= 0 && l < rows;
	*coluna = c < 0 ? 0 : (c >= cols ? cols - 1 : c);
	*linha = l < 0 ? 0 : (l >= rows ? rows - 1 : l);
	return dentro;
}

static void mouse(struct aba *a, Clay_BoundingBox g) {
	static bool antes_premido;
	static bool botao_enviado;
	static int cliques;
	static double ultimo_clique;
	static int ult_l = -1, ult_c = -1;
	static int mov_l = -1, mov_c = -1;
	bool premido = gv_premido(ID_GRADE);
	bool por_cima = gv_por_cima(ID_GRADE);
	Clay_Vector2 p = gv_ponteiro();
	int l, c;
	celula_sob(g, p, &l, &c);
	uint32_t mods = gv_modificadores();
	bool do_programa = terminal_mouse_ativo(a->t) && !(mods & GV_MOD_SHIFT);

	/* a roda */
	float roda = gv_roda();
	if (por_cima && roda != 0) {
		static float resto;
		resto += roda;
		int passos = (int)(resto / 5.0f); /* ~3 linhas por clique da roda */
		resto -= passos * 5.0f;
		if (do_programa) {
			for (int i = 0; i < abs(passos); i++) {
				terminal_mouse_botao(a->t, l, c, passos < 0 ? 4 : 5, true, mods_term(mods));
			}
		} else if (terminal_tela_alternativa(a->t)) {
			/* um editor/paginador sem mouse: a roda vira as setas, como nos outros terminais */
			for (int i = 0; i < abs(passos) * 3 && i < 30; i++) {
				terminal_tecla(a->t, passos < 0 ? XKB_KEY_Up : XKB_KEY_Down, "", 0);
			}
		} else {
			rolar(a, -passos * 3);
		}
	}

	if (premido && !antes_premido) {
		if (do_programa) {
			terminal_mouse_botao(a->t, l, c, 1, true, mods_term(mods));
			botao_enviado = true;
		} else {
			double agora = gv_agora();
			cliques = (agora - ultimo_clique < 0.4 && l == ult_l && c == ult_c) ? cliques + 1 : 1;
			ultimo_clique = agora;
			ult_l = l;
			ult_c = c;
			int linha_abs = abs_de(a, l - a->desloc);
			if (cliques == 2) {
				int ini, fim;
				terminal_palavra(a->t, l - a->desloc, c, &ini, &fim);
				a->sel_tem = true;
				a->sel_l1 = a->sel_l2 = linha_abs;
				a->sel_c1 = ini;
				a->sel_c2 = fim;
				a->sel_arrasta = false;
			} else if (cliques >= 3) {
				a->sel_tem = true;
				a->sel_l1 = a->sel_l2 = linha_abs;
				a->sel_c1 = 0;
				a->sel_c2 = terminal_colunas(a->t) - 1;
				a->sel_arrasta = false;
				cliques = 0;
			} else {
				sel_limpar(a);
				a->sel_l1 = a->sel_l2 = linha_abs;
				a->sel_c1 = a->sel_c2 = c;
				a->sel_arrasta = true;
			}
		}
	} else if (premido) {
		if (botao_enviado) {
			if (l != mov_l || c != mov_c) {
				terminal_mouse_mover(a->t, l, c, mods_term(mods));
			}
		} else if (a->sel_arrasta) {
			int linha_abs = abs_de(a, l - a->desloc);
			if (linha_abs != a->sel_l1 || c != a->sel_c1) {
				a->sel_tem = true;
			}
			a->sel_l2 = linha_abs;
			a->sel_c2 = c;
			/* arrastando para fora, por cima ou por baixo: o histórico anda */
			if (p.y < g.y + PAD && a->desloc < terminal_historico(a->t)) {
				rolar(a, 1);
			} else if (p.y > g.y + g.height - PAD && a->desloc > 0) {
				rolar(a, -1);
			}
			gv_app_redesenhar(app);
		}
	} else if (antes_premido) {
		if (botao_enviado) {
			terminal_mouse_botao(a->t, l, c, 1, false, mods_term(mods));
			botao_enviado = false;
		}
		a->sel_arrasta = false;
	}
	mov_l = l;
	mov_c = c;
	antes_premido = premido;
}

/* ---- a janela ---- */

static void aba_chip(int i) {
	struct aba *a = &abas[i];
	char buf[256];
	const char *nome = nome_aba(a, buf, sizeof(buf));
	char curto[40];
	g_strlcpy(curto, nome, sizeof(curto));
	if (strlen(nome) >= sizeof(curto)) {
		curto[sizeof(curto) - 2] = '\0';
		strcat(curto, "…");
	}
	bool eh_ativa = i == ativa;
	Clay_ElementId id = CLAY_IDI("co-aba", i);
	bool sobre = gv_por_cima(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(80, 220), CLAY_SIZING_FIXED(32) }, .padding = { 12, 6, 0, 0 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = eh_ativa ? gv_t.selecionado : (sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
		gv_texto(gv_str(curto), gv_t.tamanho_corpo - 1, eh_ativa ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
		Clay_ElementId x = CLAY_IDI("co-fecha", i);
		CLAY(x, { .layout = { .sizing = { CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(x) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(10) }) {
			gv_icone("close", 12, gv_t.texto_secundario);
		}
	}
}

static void desenhar(gv_app *a, void *dados) {
	(void)dados;
	/* o fim das abas cujo processo acabou */
	for (int i = nabas - 1; i >= 0; i--) {
		if (abas[i].morreu) {
			fechar_aba(i);
		}
	}
	if (nabas == 0) {
		gv_app_sair(a);
		return;
	}
	struct aba *aba = &abas[ativa];

	/* as cores acompanham o tema */
	uint32_t chave = rgb_de(gv_t.texto) ^ (rgb_de(gv_t.fundo) * 31u);
	if (chave != cores_chave) {
		cores_chave = chave;
		for (int i = 0; i < nabas; i++) {
			aplicar_cores(&abas[i]);
		}
	}
	terminal_enviar(aba->t);
	teclas(aba);
	aba = &abas[ativa];

	/* o histórico cresceu com texto novo: quem está lendo o que subiu não perde o lugar */
	for (int i = 0; i < nabas; i++) {
		int h = terminal_historico(abas[i].t);
		if (abas[i].desloc > 0 && h > abas[i].hist_antes) {
			abas[i].desloc += h - abas[i].hist_antes;
			if (abas[i].desloc > h) {
				abas[i].desloc = h;
			}
		}
		abas[i].hist_antes = h;
		if (terminal_campainha(abas[i].t)) {
			gv_app_redesenhar(a);
		}
	}

	Clay_BoundingBox g = gv_caixa(ID_GRADE);
	if (g.width > 0 && g.height > 0 && cw > 0) {
		int cols = (int)((g.width - 2 * PAD) / cw), rows = (int)((g.height - 2 * PAD) / ch);
		if (cols < 2) {
			cols = 2;
		}
		if (rows < 1) {
			rows = 1;
		}
		if (cols != colunas || rows != linhas) {
			colunas = cols;
			linhas = rows;
			for (int i = 0; i < nabas; i++) {
				terminal_redimensionar(abas[i].t, colunas, linhas);
			}
		}
		mouse(aba, g);
	}

	static struct grade_dados gd;
	gd.aba = aba;
	bool menu = gv_menu_no_ponteiro(ID_GRADE, CLAY_ID("co-menu"));
	(void)menu;
	CLAY(CLAY_ID("co-janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_barra_janela(a, CLAY_ID("co-barra"), "terminal", "Pulse Terminal");
		/* a faixa de abas só aparece com duas ou mais (uma aba nova: Ctrl+Shift+T, o botão direito ou o + da faixa depois) */
		if (nabas > 1) {
			CLAY(CLAY_ID("co-abas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 10, 10, 4, 4 }, .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.elevada }) {
				for (int i = 0; i < nabas; i++) {
					aba_chip(i);
				}
				if (gv_botao_ext(CLAY_ID("co-nova"), &(gv_botao_opcoes){ .icone = "plus", .estilo = GV_BOTAO_SIMPLES, .altura = 32 })) {
					nova_aba(terminal_pasta(aba->t), NULL);
				}
			}
		}
		CLAY(ID_GRADE, gv_livre(desenhar_grade, &gd, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
			.backgroundColor = gv_t.fundo, .cornerRadius = { .bottomLeft = gv_t.raio_grande, .bottomRight = gv_t.raio_grande } })) {}
	}

	/* os cliques nas abas (depois do layout: o Clay já sabe onde estão) */
	for (int i = 0; i < nabas; i++) {
		if (gv_tocado(CLAY_IDI("co-fecha", i))) {
			abas[i].morreu = true;
		} else if (gv_tocado(CLAY_IDI("co-aba", i))) {
			ativa = i;
		}
	}
	GV_MENU_PONTEIRO(CLAY_ID("co-menu")) {
		if (gv_menu_item(CLAY_ID("co-m-copiar"), "copy", "Copiar", "Ctrl+Shift+C")) {
			copiar(aba);
			gv_popup_fechar(CLAY_ID("co-menu"));
		}
		if (gv_menu_item(CLAY_ID("co-m-colar"), "paste", "Colar", "Ctrl+Shift+V")) {
			colar(aba);
			gv_popup_fechar(CLAY_ID("co-menu"));
		}
		if (gv_menu_item(CLAY_ID("co-m-nova"), "plus", "Nova aba", "Ctrl+Shift+T")) {
			nova_aba(terminal_pasta(aba->t), NULL);
			gv_popup_fechar(CLAY_ID("co-menu"));
		}
	}
	if (terminal_enviar(abas[ativa].t)) {
		gv_acordar_em(0.02); /* o que ainda não coube no pty (um colar grande) */
	}
	for (int i = 0; i < nabas; i++) {
		if (abas[i].nl.estado == NL_AVISO && g_get_monotonic_time() >= abas[i].nl.aviso_ate) {
			nl_fechar(&abas[i]);
		} else if (abas[i].nl.estado == NL_PENSANDO) {
			gv_acordar_em(0.4); /* os pontinhos andam */
		}
	}
	if (emma && emt_pendentes(emma) > 0) {
		emt_tick(emma);
		gv_acordar_em(0.5); /* os prazos dos pedidos à Emma */
	}
}

int main(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--pasta") && i + 1 < argc) {
			cwd_inicial = argv[++i];
		} else if (!strcmp(argv[i], "-e") && i + 1 < argc) {
			GString *c = g_string_new(NULL);
			for (int k = i + 1; k < argc; k++) {
				char *q = g_shell_quote(argv[k]);
				g_string_append_printf(c, "%s%s", k > i + 1 ? " " : "", q);
				g_free(q);
			}
			comando_inicial = g_string_free(c, FALSE);
			break;
		} else if (!strcmp(argv[i], "--ajuda") || !strcmp(argv[i], "--help")) {
			puts("lumna-pulse [--pasta DIR] [-e comando args...]\n"
				"  --esquema            mostra o SQL do banco do Pulse (a memória: sessões, comandos, conversas com a Emma)\n"
				"  --migrar-banco       cria ou atualiza o banco do Pulse e diz a versão\n"
				"  --limpar-historico   apaga tudo o que o Pulse lembra");
			return 0;
		} else if (!strcmp(argv[i], "--esquema")) {
			char *sql = pulsedb_esquema_sql();
			fputs(sql, stdout);
			g_free(sql);
			return 0;
		} else if (!strcmp(argv[i], "--migrar-banco") || !strcmp(argv[i], "--limpar-historico")) {
			char *erro = NULL;
			pulsedb *db = pulsedb_abrir(NULL, &erro);
			if (!db) {
				fprintf(stderr, "lumna-pulse: %s\n", erro ? erro : "o banco não abriu");
				g_free(erro);
				return 1;
			}
			if (!strcmp(argv[i], "--limpar-historico")) {
				pulsedb_apagar_tudo(db);
				puts("O histórico do Pulse foi apagado.");
			} else {
				printf("Banco do Pulse na versão %d (a mais nova é %d).\n", pulsedb_versao(db), pulsedb_versao_atual());
			}
			pulsedb_fechar(db);
			return 0;
		}
	}
	medir();
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Pulse Terminal", .app_id = "lumna-pulse", .largura = (int)(82 * cw) + 2 * PAD, .altura = (int)(26 * ch) + 2 * PAD + 54 });
	if (!app) {
		return 1;
	}
	setenv("LUMNA_TERMINAL_PRIMEIRA", "1", 1); /* o ~/.bashrc mostra o fastfetch só na primeira aba (as outras não herdam isto) */
	bool abriu = nova_aba(cwd_inicial, comando_inicial);
	unsetenv("LUMNA_TERMINAL_PRIMEIRA");
	if (!abriu) {
		fprintf(stderr, "lumna-pulse: não deu para abrir o terminal\n");
		return 1;
	}
	if (!comando_inicial) {
		char *erro = NULL;
		banco = pulsedb_abrir(NULL, &erro);
		if (banco) {
			pulsedb_podar(banco, 365);
			sessao_db = pulsedb_sessao_iniciar(banco, cwd_inicial ? cwd_inicial : g_get_home_dir());
			struct emt_config cfg;
			emt_config_padrao(&cfg);
			emt_config_ler(&cfg, NULL);
			emmac_entregar = gv_na_linha_principal; /* a resposta da Emma volta na linha principal do Gravity */
			emma = emt_novo(banco, sessao_db, &cfg);
		} else {
			fprintf(stderr, "lumna-pulse: sem memória (%s); o terminal funciona igual, sem a Emma\n", erro ? erro : "?");
			g_free(erro);
		}
	}
	int saida = gv_app_correr(app, desenhar, NULL);
	if (banco) {
		pulsedb_sessao_encerrar(banco, sessao_db);
		emt_liberar(emma);
		pulsedb_fechar(banco);
	}
	return saida;
}
