/*
 * lumna-console: o terminal do LUMNA, em Gravity. O terminal em si (a libvterm e o pty) é servicos/terminal.c; aqui fica a janela: a grade
 * desenhada com o cairo, as abas, a seleção com o mouse, o histórico, o colar e as teclas.
 *
 *   lumna-console                    um shell na pasta pessoal
 *   lumna-console --pasta DIR        o shell nessa pasta
 *   lumna-console -e comando args... roda o comando (a aba fecha quando ele acaba)
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

#include "gravity.h"
#include "terminal.h"

#define MAX_ABAS 16
#define PAD 10
#define FAMILIA "JetBrains Mono,DejaVu Sans Mono,Noto Sans Mono,monospace"

struct aba {
	terminal *t;
	int desloc;          /* quantas linhas acima do fim se está olhando (0: no fim) */
	int hist_antes;      /* o histórico no quadro anterior: para segurar o que se lê quando entra texto novo */
	bool morreu;
	/* a seleção, em linhas absolutas (0 é a primeira do histórico): fica no lugar quando entra texto novo */
	bool sel_tem, sel_arrasta;
	int sel_l1, sel_c1, sel_l2, sel_c2;
};

static gv_app *app;
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

static void ler_pronto(gv_app *a, int fd, void *dados) {
	struct aba *aba = dados;
	if (!terminal_ler(aba->t)) {
		aba->morreu = true;
	}
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
	a->t = t;
	aplicar_cores(a);
	gv_app_vigiar(app, terminal_fd(t), ler_pronto, a);
	ativa = nabas++;
	return true;
}

static void fechar_aba(int i) {
	struct aba *a = &abas[i];
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
		uint32_t m = s >= 'a' && s <= 'z' ? s - 32 : s; /* a letra, de qualquer caixa */
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
		gv_barra_janela(a, CLAY_ID("co-barra"), "terminal", "LUMNA Terminal");
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
			puts("lumna-console [--pasta DIR] [-e comando args...]");
			return 0;
		}
	}
	medir();
	app = gv_app_criar(&(gv_opcoes){ .titulo = "LUMNA Terminal", .app_id = "lumna-console", .largura = (int)(82 * cw) + 2 * PAD, .altura = (int)(26 * ch) + 2 * PAD + 54 });
	if (!app) {
		return 1;
	}
	if (!nova_aba(cwd_inicial, comando_inicial)) {
		fprintf(stderr, "lumna-console: não deu para abrir o terminal\n");
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
