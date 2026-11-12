/*
 * lumna-calculadora: a calculadora-fita. Em vez de teclado e visor, a conta é escrita como texto, em português, e o resultado aparece enquanto se digita.
 * Cada conta que se confirma (Enter) vai para a fita; uma conta que começa por operador continua do resultado de cima ("+ 16" depois de 384 é 384 + 16).
 *
 *   15% de 89,90      200 + 10%      5 km em milhas      3h20 × 4      100 °C em °F      (3 + 4) × 5      raiz(81)
 *
 * O teclado do desenho original (dígitos e operações) é um painel que se mostra ou esconde, para o toque. O motor é servicos/calc.c.
 * A fita fica em ~/.local/state/lumna/calculadora.txt.
 */
#define _GNU_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "calc.h"
#include "gravity.h"

#define MAX_FITA 300

struct linha {
	char expr[256];
	char texto[160];
};

static gv_app *app;
static struct linha fita[MAX_FITA];
static int n_fita;
static char entrada[256];
static char entrada_avaliada[256] = "\x01";
static struct calc_res previa;
static enum calc_estado estado_previa = CALC_VAZIO;
static bool teclado_visivel;
static int rolar_fim;      /* quadros ainda para rolar até o fim da fita */
static bool pedir_foco = true;
static bool cursor_no_fim;   /* o texto mudou por fora (tecla, fita): o cursor vai para o fim */
static bool depurando;
static char aviso[160];
static double aviso_ate;

/* ---- guardar ---- */

static char *arquivo_fita(void) {
	return g_build_filename(g_get_user_state_dir(), "lumna", "calculadora.txt", NULL);
}

static char *arquivo_prefs(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "calculadora.ini", NULL);
}

static void fita_gravar(void) {
	char *c = arquivo_fita(), *pai = g_path_get_dirname(c);
	g_mkdir_with_parents(pai, 0700);
	GString *g = g_string_new(NULL);
	for (int i = 0; i < n_fita; i++) {
		g_string_append_printf(g, "%s\t%s\n", fita[i].expr, fita[i].texto);
	}
	g_file_set_contents(c, g->str, -1, NULL);
	g_string_free(g, TRUE);
	g_free(pai);
	g_free(c);
}

static void fita_ler(void) {
	char *c = arquivo_fita(), *dados = NULL;
	if (g_file_get_contents(c, &dados, NULL, NULL)) {
		char **ls = g_strsplit(dados, "\n", -1);
		for (int i = 0; ls[i] && n_fita < MAX_FITA; i++) {
			char *tab = strchr(ls[i], '\t');
			if (!tab || !g_utf8_validate(ls[i], -1, NULL)) {
				continue;
			}
			*tab = '\0';
			g_strlcpy(fita[n_fita].expr, ls[i], sizeof(fita[0].expr));
			g_strlcpy(fita[n_fita].texto, tab + 1, sizeof(fita[0].texto));
			n_fita++;
		}
		g_strfreev(ls);
		g_free(dados);
	}
	g_free(c);
	char *p = arquivo_prefs();
	GKeyFile *kf = g_key_file_new();
	if (g_key_file_load_from_file(kf, p, G_KEY_FILE_NONE, NULL)) {
		teclado_visivel = g_key_file_get_boolean(kf, "calculadora", "teclado", NULL);
	}
	g_key_file_free(kf);
	g_free(p);
}

static void prefs_gravar(void) {
	char *p = arquivo_prefs(), *pai = g_path_get_dirname(p);
	g_mkdir_with_parents(pai, 0700);
	GKeyFile *kf = g_key_file_new();
	g_key_file_set_boolean(kf, "calculadora", "teclado", teclado_visivel);
	g_key_file_save_to_file(kf, p, NULL);
	g_key_file_free(kf);
	g_free(pai);
	g_free(p);
}

static void avisar(const char *t) {
	g_strlcpy(aviso, t, sizeof(aviso));
	aviso_ate = gv_agora() + 2.5;
	gv_app_redesenhar(app);
}

/* ---- a conta ---- */

/* uma conta que começa por operador continua do resultado de cima */
static void efetiva(const char *txt, char *saida, size_t tam) {
	const char *p = txt;
	while (*p == ' ') {
		p++;
	}
	bool op = *p && (strchr("+*/^%", *p) || !strncmp(p, "\xC3\x97", 2) || !strncmp(p, "\xC3\xB7", 2) || (*p == '-' && p[1] == ' '));
	if (op && n_fita > 0 && !strstr(fita[n_fita - 1].texto, "e+") && !strstr(fita[n_fita - 1].texto, "e-")) {
		snprintf(saida, tam, "%s %s", fita[n_fita - 1].texto, p);
	} else {
		snprintf(saida, tam, "%s", txt);
	}
}

static void avaliar_entrada(void) {
	if (!strcmp(entrada, entrada_avaliada)) {
		return;
	}
	g_strlcpy(entrada_avaliada, entrada, sizeof(entrada_avaliada));
	char ef[512];
	efetiva(entrada, ef, sizeof(ef));
	estado_previa = calc_avaliar(ef, &previa);
}

static void confirmar(void) {
	avaliar_entrada();
	if (estado_previa == CALC_OK) {
		char ef[512];
		efetiva(entrada, ef, sizeof(ef));
		if (n_fita == MAX_FITA) {
			memmove(&fita[0], &fita[1], sizeof(fita[0]) * (MAX_FITA - 1));
			n_fita--;
		}
		calc_embelezar(ef, fita[n_fita].expr, sizeof(fita[0].expr));
		g_strlcpy(fita[n_fita].texto, previa.texto, sizeof(fita[0].texto));
		n_fita++;
		entrada[0] = '\0';
		rolar_fim = 3;
		fita_gravar();
	} else if (estado_previa == CALC_ERRO) {
		avisar(previa.erro);
	} else if (estado_previa == CALC_INCOMPLETO) {
		avisar("A conta está incompleta");
	}
	pedir_foco = true;
}

static void anexar(const char *s) {
	size_t l = strlen(entrada);
	if (l + strlen(s) < sizeof(entrada)) {
		memcpy(entrada + l, s, strlen(s) + 1);
	}
	pedir_foco = true;
	cursor_no_fim = true;
}

static void apagar_ultimo(void) {
	size_t l = strlen(entrada);
	if (l) {
		char *fim = entrada + l;
		char *ant = g_utf8_prev_char(fim);
		*ant = '\0';
	}
	pedir_foco = true;
	cursor_no_fim = true;
}

/* ---- depuração dos testes ---- */

struct reg {
	char nome[40];
	Clay_ElementId id;
};
static struct reg regs[400];
static int n_regs;

static void reg(const char *nome, Clay_ElementId id) {
	if (depurando && n_regs < (int)(sizeof(regs) / sizeof(regs[0]))) {
		g_strlcpy(regs[n_regs].nome, nome, sizeof(regs[n_regs].nome));
		regs[n_regs++].id = id;
	}
}

static void depurar(void) {
	if (!depurando) {
		return;
	}
	static GHashTable *antes;
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	for (int i = 0; i < n_regs; i++) {
		Clay_BoundingBox b = gv_caixa(regs[i].id);
		if (b.width <= 0) {
			continue;
		}
		char *l = g_strdup_printf("%.0f %.0f %.0f %.0f", b.x, b.y, b.width, b.height);
		char *a = g_hash_table_lookup(antes, regs[i].nome);
		if (!a || strcmp(a, l)) {
			printf("pos %s %s\n", regs[i].nome, l);
			g_hash_table_insert(antes, g_strdup(regs[i].nome), l);
		} else {
			g_free(l);
		}
	}
	n_regs = 0;
	/* (os colchetes dentro do texto viram parênteses, para o teste ler "[...]" sem ambiguidade) */
	char *e = g_strdup(entrada), *u = g_strdup(n_fita ? fita[n_fita - 1].expr : ""), *r = g_strdup(n_fita ? fita[n_fita - 1].texto : "");
	for (char *s = e; *s; s++) *s = (*s == '[' || *s == ']') ? '(' : *s;
	char *est = g_strdup_printf("estado entrada=[%s] estado_previa=%d previa=[%s] erro=[%s] linhas=%d teclado=%d ultima=[%s] ultimo=[%s] aviso=[%s]", e, estado_previa,
		estado_previa == CALC_OK ? previa.texto : "", estado_previa == CALC_ERRO || estado_previa == CALC_INCOMPLETO ? previa.erro : "", n_fita, teclado_visivel, u, r,
		aviso_ate > gv_agora() ? aviso : "");
	static char *ultimo;
	if (!ultimo || strcmp(ultimo, est)) {
		printf("%s\n", est);
		g_free(ultimo);
		ultimo = est;
	} else {
		g_free(est);
	}
	g_free(e);
	g_free(u);
	g_free(r);
	fflush(stdout);
}

/* ---- as peças ---- */

static Clay_Color transp = { 0, 0, 0, 0 };

/* uma tecla do teclado: estilo 0 = número, 1 = operação, 2 = igual (acento), 3 = apagar/limpar */
static bool tecla(const char *id_s, const char *rotulo, int estilo) {
	Clay_ElementId id = CLAY_SID(gv_str(id_s));
	reg(id_s, id);
	bool sobre = gv_por_cima(id), premida = gv_premido(id);
	Clay_Color fundo, texto = gv_t.texto;
	switch (estilo) {
	case 1:
		fundo = gv_alfa(gv_t.acento, premida ? 0.34f : sobre ? 0.26f : 0.18f);
		texto = gv_t.acento;
		break;
	case 2:
		fundo = premida ? gv_alfa(gv_t.acento, 0.8f) : gv_t.acento;
		texto = (Clay_Color){ 20, 14, 36, 255 };
		break;
	case 3:
		fundo = premida ? gv_t.sob_cursor : sobre ? gv_t.sob_cursor : gv_t.elevada;
		texto = gv_t.texto_secundario;
		break;
	default:
		fundo = premida ? gv_t.sob_cursor : sobre ? gv_alfa(gv_t.sob_cursor, 0.8f) : gv_t.elevada;
		break;
	}
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = fundo, .cornerRadius = CLAY_CORNER_RADIUS(12), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		if (!strcmp(rotulo, "<")) {
			gv_icone("backspace", 24, texto);
		} else {
			gv_texto(gv_str(rotulo), 24, texto, estilo == 2 ? GV_PESO_MEDIO : GV_PESO_NORMAL);
		}
	}
	return gv_tocado(id);
}

static void teclado(void) {
	/* as quatro colunas: número e as operações à direita */
	static const struct { const char *id; const char *rot; const char *texto; int estilo; } T[6][4] = {
		{ { "t-limpar", "C", "", 3 }, { "t-ab", "(", "(", 3 }, { "t-fecha", ")", ")", 3 }, { "t-apaga", "<", "", 3 } },
		{ { "t-pct", "%", "%", 3 }, { "t-de", "de", " de ", 3 }, { "t-pot", "^", "^", 3 }, { "t-div", "÷", " ÷ ", 1 } },
		{ { "t-7", "7", "7", 0 }, { "t-8", "8", "8", 0 }, { "t-9", "9", "9", 0 }, { "t-mul", "×", " × ", 1 } },
		{ { "t-4", "4", "4", 0 }, { "t-5", "5", "5", 0 }, { "t-6", "6", "6", 0 }, { "t-sub", "−", " - ", 1 } },
		{ { "t-1", "1", "1", 0 }, { "t-2", "2", "2", 0 }, { "t-3", "3", "3", 0 }, { "t-soma", "+", " + ", 1 } },
		{ { "t-0", "0", "0", 0 }, { "t-virg", ",", ",", 0 }, { "t-em", "em", " em ", 3 }, { "t-igual", "=", "=", 2 } },
	};
	CLAY(CLAY_ID("calc-teclado"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(6 * 54 + 5 * 8) }, .padding = { 14, 14, 4, 14 }, .childGap = 8 } }) {
		for (int r = 0; r < 6; r++) {
			CLAY(CLAY_IDI("calc-tr", (uint32_t)r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
				for (int c = 0; c < 4; c++) {
					if (tecla(T[r][c].id, T[r][c].rot, T[r][c].estilo)) {
						const char *id = T[r][c].id;
						if (!strcmp(id, "t-igual")) {
							confirmar();
						} else if (!strcmp(id, "t-apaga")) {
							apagar_ultimo();
						} else if (!strcmp(id, "t-limpar")) {
							entrada[0] = '\0';
							pedir_foco = true;
						} else {
							anexar(T[r][c].texto);
						}
					}
				}
			}
		}
	}
}

static void linha_da_fita(int i) {
	Clay_ElementId ex = CLAY_IDI("calc-ex", (uint32_t)i), rs = CLAY_IDI("calc-rs", (uint32_t)i), cp = CLAY_IDI("calc-cp", (uint32_t)i), lin = CLAY_IDI("calc-lin", (uint32_t)i);
	char nome[40];
	snprintf(nome, sizeof(nome), "fita/%d/expr", i);
	reg(nome, ex);
	snprintf(nome, sizeof(nome), "fita/%d/res", i);
	reg(nome, rs);
	snprintf(nome, sizeof(nome), "fita/%d/copiar", i);
	reg(nome, cp);
	bool sobre = gv_por_cima(lin) || gv_por_cima(ex) || gv_por_cima(rs) || gv_por_cima(cp);
	CLAY(lin, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 10, 18, 10, 10 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = sobre ? gv_alfa(gv_t.sob_cursor, 0.5f) : transp, .cornerRadius = CLAY_CORNER_RADIUS(10) }) {
		CLAY(cp, { .layout = { .sizing = { CLAY_SIZING_FIXED(34), CLAY_SIZING_FIXED(34) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(cp) ? gv_t.sob_cursor : transp, .cornerRadius = CLAY_CORNER_RADIUS(17) }) {
			if (sobre) {
				gv_icone("copy", 18, gv_t.texto_secundario);
			}
		}
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			CLAY(ex, { .layout = { .sizing = { CLAY_SIZING_FIT(0) }, .padding = { 4, 0, 2, 2 } } }) {
				gv_texto(gv_str(fita[i].expr), gv_t.tamanho_corpo, gv_por_cima(ex) ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(rs, { .layout = { .sizing = { CLAY_SIZING_FIT(0) }, .padding = { 4, 0, 0, 2 } } }) {
				gv_texto(gv_str(fita[i].texto), 30, gv_por_cima(rs) ? gv_t.acento : gv_t.texto, GV_PESO_MEDIO);
			}
		}
	}
	if (gv_tocado(cp)) {
		gv_copiar(fita[i].texto);
		avisar("Copiado");
	} else if (gv_tocado(ex)) {
		g_strlcpy(entrada, fita[i].expr, sizeof(entrada)); /* de volta para a linha, para editar */
		pedir_foco = true;
		cursor_no_fim = true;
	} else if (gv_tocado(rs)) {
		/* o resultado entra na conta que se escreve (só o número e a unidade) */
		if (entrada[0] && !g_str_has_suffix(entrada, " ") && !strchr("(+-×÷*/^", entrada[strlen(entrada) - 1])) {
			anexar(" ");
		}
		anexar(fita[i].texto);
	}
}

static void exemplo(int i, const char *conta) {
	Clay_ElementId id = CLAY_IDI("calc-exemplo", (uint32_t)i);
	char nome[40];
	snprintf(nome, sizeof(nome), "exemplo/%d", i);
	reg(nome, id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0) }, .padding = { 14, 14, 8, 8 } },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(16), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		gv_texto(gv_str(conta), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	if (gv_tocado(id)) {
		g_strlcpy(entrada, conta, sizeof(entrada));
		pedir_foco = true;
		cursor_no_fim = true;
	}
}

static void fita_vazia(void) {
	CLAY(CLAY_ID("calc-vazia"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(24), .childGap = 14,
		.childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("dialpad", 40, gv_t.texto_suave);
		gv_texto(CLAY_STRING("Escreva uma conta"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(300) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
			gv_texto(CLAY_STRING("O resultado aparece enquanto você digita. Experimente:"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("calc-exemplos"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
			exemplo(0, "15% de 89,90");
			exemplo(1, "5 km em milhas");
			exemplo(2, "3h20 × 4");
			exemplo(3, "(128 + 64) ÷ 3");
		}
	}
}

static void fita_area(void) {
	Clay_ElementId rol = CLAY_ID("calc-rol");
	if (n_fita == 0) {
		fita_vazia();
		return;
	}
	CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
		.padding = { 8, 8, 8, 8 }, .childGap = 2 } })) {
		for (int i = 0; i < n_fita; i++) {
			linha_da_fita(i);
			if (i + 1 < n_fita) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) }, .padding = { 14, 14, 0, 0 } } }) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_alfa(gv_t.borda, 0.7f) }) {}
				}
			}
		}
	}
	if (rolar_fim > 0) {
		gv_rolar_para(rol, 0, 1e7f, false);
		rolar_fim--;
		gv_app_redesenhar(app);
	}
}

static void entrada_area(void) {
	CLAY(CLAY_ID("calc-entrada-area"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 10, 10 }, .childGap = 6 },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		/* a entrada, grande, numa pílula */
		CLAY(CLAY_ID("calc-pilula"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 16, 8, 0, 0 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(18),
			.border = { .color = gv_focado(CLAY_ID("calc-entrada")) ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(gv_focado(CLAY_ID("calc-entrada")) ? 2 : 1) } }) {
			if (cursor_no_fim) {
				gv_campo_cursor_no_fim(CLAY_ID("calc-entrada"));
				cursor_no_fim = false;
			}
			float corpo = gv_t.tamanho_corpo;
			gv_t.tamanho_corpo = 28; /* o campo lê o tamanho do tema a cada texto */
			int r = gv_campo_chat(CLAY_ID("calc-entrada"), entrada, sizeof(entrada), "15% de 89,90", 1, false);
			gv_t.tamanho_corpo = corpo;
			reg("entrada", CLAY_ID("calc-entrada"));
			for (char *s = entrada; *s; s++) { /* uma linha só */
				if (*s == '\n') {
					*s = ' ';
				}
			}
			if (r & GV_CAMPO_ENTER) {
				confirmar();
			}
			if (entrada[0]) {
				if (gv_botao_ext(CLAY_ID("calc-limpar-entrada"), &(gv_botao_opcoes){ .icone = "close", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
					entrada[0] = '\0';
					pedir_foco = true;
				}
			}
		}
		avaliar_entrada();
		/* o resultado ao vivo (ou o que falta) */
		CLAY(CLAY_ID("calc-previa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34) }, .padding = { 8, 8, 0, 0 }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (estado_previa == CALC_OK) {
				gv_texto(gv_str(gv_fmt("= %s", previa.texto).chars), 26, gv_t.acento, GV_PESO_MEDIO);
			} else if (estado_previa == CALC_ERRO) {
				gv_texto(gv_str(previa.erro), gv_t.tamanho_corpo, gv_t.aviso, GV_PESO_NORMAL);
			} else if (aviso[0] && gv_agora() < aviso_ate) {
				gv_texto(gv_str(aviso), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
}

static void barra_ferramentas(void) {
	CLAY(CLAY_ID("calc-ferr"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 14, 14, 0, 0 }, .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			if (aviso[0] && gv_agora() < aviso_ate && estado_previa == CALC_OK) {
				gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		if (gv_botao_ext(CLAY_ID("calc-b-limpar"), &(gv_botao_opcoes){ .icone = "trash", .estilo = GV_BOTAO_SIMPLES, .altura = 38, .desativado = n_fita == 0 })) {
			gv_popup_alternar(CLAY_ID("calc-menu-limpar"));
		}
		reg("btn/limpar", CLAY_ID("calc-b-limpar"));
		if (gv_botao_ext(CLAY_ID("calc-b-teclado"), &(gv_botao_opcoes){ .icone = "dialpad", .estilo = teclado_visivel ? GV_BOTAO_ELEVADO : GV_BOTAO_SIMPLES, .altura = 38 })) {
			teclado_visivel = !teclado_visivel;
			prefs_gravar();
			pedir_foco = true;
		}
		reg("btn/teclado", CLAY_ID("calc-b-teclado"));
	}
	GV_POPUP(CLAY_ID("calc-menu-limpar"), CLAY_ID("calc-b-limpar"), .alinhar = GV_ALINHAR_FIM, .largura = 240) {
		CLAY(CLAY_ID("calc-ml"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6) } }) {
			if (gv_menu_item(CLAY_ID("calc-mi-limpar"), "trash", "Limpar a fita", NULL)) {
				n_fita = 0;
				fita_gravar();
				gv_popup_fechar(CLAY_ID("calc-menu-limpar"));
				pedir_foco = true;
			}
			reg("menu/limpar", CLAY_ID("calc-mi-limpar"));
		}
	}
}

static void desenhar(gv_app *a, void *dados) {
	(void)a;
	(void)dados;
	for (int i = 0;; i++) {
		uint32_t s = gv_tecla_n(i);
		if (!s) {
			break;
		}
		if (s == XKB_KEY_Escape) { /* o campo larga o foco no Esc: volta, para continuar digitando */
			entrada[0] = '\0';
			pedir_foco = true;
		}
	}
	CLAY(CLAY_ID("calc-raiz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.fundo }) {
		gv_barra_janela(app, CLAY_ID("calc-barra"), "dialpad", "Calculadora");
		barra_ferramentas();
		CLAY(CLAY_ID("calc-fita"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			fita_area();
		}
		entrada_area();
		if (teclado_visivel) {
			teclado();
		}
	}
	if (pedir_foco) {
		gv_focar(CLAY_ID("calc-entrada"));
		pedir_foco = false;
	}
	if (aviso[0] && gv_agora() < aviso_ate) {
		gv_acordar_em(aviso_ate - gv_agora() + 0.05);
	}
	depurar();
}

int main(void) {
	depurando = getenv("LUMNA_CALCULADORA_DEBUG") != NULL;
	if (depurando) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
	fita_ler();
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Calculadora", .app_id = "lumna-calculadora", .tipo = GV_JANELA, .largura = 460, .altura = 700 });
	if (!app) {
		return 1;
	}
	rolar_fim = 3;
	return gv_app_correr(app, desenhar, NULL);
}
