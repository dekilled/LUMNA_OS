/*
 * Teclado e mouse → Teclado e Mouse e touchpad: os layouts (e a tecla que troca de um para outro), o Caps Lock, a
 * repetição das teclas, o botão primário do mouse (trocar para canhotos), a velocidade e a aceleração do
 * ponteiro, a rolagem natural e o toque no touchpad. Vão para ~/.config/lumna/entrada.ini no Aplicar e o Orbit
 * aplica na hora (servicos/entrada.c). O desenho do ponteiro e os ícones ficam para Temas.
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "entrada.h"

#define MAX_LAYOUTS_LISTA 512
#define MAX_VARIANTES 96

static struct entrada_cfg cfg, gravada;
static struct entrada_dispositivos disp;
static bool lido;

static struct entrada_item lays[MAX_LAYOUTS_LISTA];
static const char *lay_nomes[MAX_LAYOUTS_LISTA];
static int nlays;

struct linha {
	int li;                                  /* o layout, em lays[] */
	struct entrada_item vars[MAX_VARIANTES]; /* [0] é o padrão (id vazio) */
	const char *var_nomes[MAX_VARIANTES];
	int nvars, vi;
};
static struct linha rows[ENTRADA_MAX_LAYOUTS];
static int nrows;
static int grp_i, caps_i;
static char outras[256]; /* as outras opções do XKB, que o Painel não mexe */

static const char *const grp_ids[] = { "", "grp:alt_shift_toggle", "grp:ctrl_shift_toggle", "grp:win_space_toggle", "grp:alt_space_toggle", "grp:caps_toggle" };
static const char *const grp_nomes[] = { "Nenhuma", "Alt + Shift", "Ctrl + Shift", "Super + Espaço", "Alt + Espaço", "Caps Lock" };
#define NGRP 6
static const char *const caps_ids[] = { "", "ctrl:nocaps", "caps:escape" };
static const char *const caps_nomes[] = { "Normal", "Vira Ctrl", "Vira Esc" };
#define NCAPS 3

static int achar_layout(const char *id) {
	for (int i = 0; i < nlays; i++) {
		if (!strcmp(lays[i].id, id)) {
			return i;
		}
	}
	return 0;
}

static void carregar_variantes(struct linha *r) {
	r->nvars = 0;
	g_strlcpy(r->vars[0].id, "", sizeof(r->vars[0].id));
	g_strlcpy(r->vars[0].nome, "Padrão", sizeof(r->vars[0].nome));
	r->nvars = 1 + entrada_variantes(lays[r->li].id, r->vars + 1, MAX_VARIANTES - 1);
	for (int i = 1; i < r->nvars; i++) {
		/* "Portuguese (Brazil, no dead keys)" → "no dead keys": o nome do layout já está na lista ao lado */
		char *virg = strstr(r->vars[i].nome, ", ");
		if (virg) {
			char curto[96];
			g_strlcpy(curto, virg + 2, sizeof(curto));
			size_t n = strlen(curto);
			if (n && curto[n - 1] == ')') {
				curto[n - 1] = '\0';
			}
			g_strlcpy(r->vars[i].nome, curto, sizeof(r->vars[i].nome));
		}
	}
	for (int i = 0; i < r->nvars; i++) {
		r->var_nomes[i] = r->vars[i].nome;
	}
	r->vi = 0;
}

/* a cfg (listas por vírgula) vira as linhas da tela */
static void descompor(void) {
	char **ls = g_strsplit(cfg.layout, ",", -1), **vs = g_strsplit(cfg.variante, ",", -1);
	nrows = 0;
	for (int i = 0; ls[i] && nrows < ENTRADA_MAX_LAYOUTS; i++) {
		if (!*ls[i]) {
			continue;
		}
		struct linha *r = &rows[nrows++];
		r->li = achar_layout(ls[i]);
		carregar_variantes(r);
		for (int k = 0; vs[0] && k <= i && vs[k]; k++) {
			if (k == i) {
				for (int j = 0; j < r->nvars; j++) {
					if (!strcmp(r->vars[j].id, vs[k])) {
						r->vi = j;
					}
				}
			}
		}
	}
	if (nrows == 0) {
		rows[nrows].li = achar_layout("br");
		carregar_variantes(&rows[nrows++]);
	}
	g_strfreev(ls);
	g_strfreev(vs);
	grp_i = caps_i = 0;
	outras[0] = '\0';
	char **op = g_strsplit(cfg.opcoes, ",", -1);
	for (int i = 0; op[i]; i++) {
		bool conhecida = false;
		for (int k = 1; k < NGRP; k++) {
			if (!strcmp(op[i], grp_ids[k])) {
				grp_i = k;
				conhecida = true;
			}
		}
		for (int k = 1; k < NCAPS; k++) {
			if (!strcmp(op[i], caps_ids[k])) {
				caps_i = k;
				conhecida = true;
			}
		}
		if (!conhecida && *op[i]) {
			g_strlcat(outras, outras[0] ? "," : "", sizeof(outras));
			g_strlcat(outras, op[i], sizeof(outras));
		}
	}
	g_strfreev(op);
}

/* as linhas da tela viram a cfg */
static void compor(void) {
	GString *l = g_string_new(NULL), *v = g_string_new(NULL), *o = g_string_new(outras);
	for (int i = 0; i < nrows; i++) {
		g_string_append_printf(l, "%s%s", i ? "," : "", lays[rows[i].li].id);
		g_string_append_printf(v, "%s%s", i ? "," : "", rows[i].vars[rows[i].vi].id);
	}
	if (nrows > 1 && grp_i > 0) {
		g_string_append_printf(o, "%s%s", o->len ? "," : "", grp_ids[grp_i]);
	}
	if (caps_i > 0) {
		g_string_append_printf(o, "%s%s", o->len ? "," : "", caps_ids[caps_i]);
	}
	g_strlcpy(cfg.layout, l->str, sizeof(cfg.layout));
	g_strlcpy(cfg.variante, v->str, sizeof(cfg.variante));
	g_strlcpy(cfg.opcoes, o->str, sizeof(cfg.opcoes));
	g_string_free(l, TRUE);
	g_string_free(v, TRUE);
	g_string_free(o, TRUE);
}

void pn_teclado_abrir(void) {
	if (nlays == 0) {
		nlays = entrada_layouts(lays, MAX_LAYOUTS_LISTA);
		for (int i = 0; i < nlays; i++) {
			lay_nomes[i] = lays[i].nome;
		}
	}
	entrada_cfg_ler(&cfg);
	entrada_dispositivos(&disp);
	descompor();
	compor(); /* "br" com "abnt2" e "br" puro são o mesmo: o que se compara é a forma que o Painel grava */
	gravada = cfg;
	lido = true;
}

static bool cfg_igual(const struct entrada_cfg *a, const struct entrada_cfg *b) {
	return !strcmp(a->layout, b->layout) && !strcmp(a->variante, b->variante) && !strcmp(a->opcoes, b->opcoes) && a->repeticao_taxa == b->repeticao_taxa &&
		a->repeticao_atraso == b->repeticao_atraso && a->mouse.primario_direito == b->mouse.primario_direito && a->mouse.velocidade == b->mouse.velocidade &&
		a->mouse.plano == b->mouse.plano && a->mouse.natural == b->mouse.natural && a->touchpad.tocar == b->touchpad.tocar &&
		a->touchpad.natural == b->touchpad.natural && a->touchpad.primario_direito == b->touchpad.primario_direito &&
		a->touchpad.digitar_desativa == b->touchpad.digitar_desativa && a->touchpad.velocidade == b->touchpad.velocidade && a->touchpad.plano == b->touchpad.plano;
}

bool pn_teclado_pendente(void) {
	if (!lido) {
		return false;
	}
	compor(); /* as linhas da tela viram a cfg para comparar */
	return !cfg_igual(&cfg, &gravada);
}

void pn_teclado_aplicar(void) {
	compor();
	if (entrada_cfg_gravar(&cfg)) {
		gravada = cfg;
	}
}

void pn_teclado_descartar(void) {
	cfg = gravada;
	descompor();
}

void pn_teclado_restaurar(void) {
	struct entrada_cfg p;
	entrada_cfg_padrao(&p);
	g_strlcpy(cfg.layout, p.layout, sizeof(cfg.layout));
	g_strlcpy(cfg.variante, p.variante, sizeof(cfg.variante));
	cfg.opcoes[0] = '\0';
	cfg.repeticao_taxa = p.repeticao_taxa;
	cfg.repeticao_atraso = p.repeticao_atraso;
	descompor();
}

void pn_mouse_restaurar(void) {
	struct entrada_cfg p;
	entrada_cfg_padrao(&p);
	cfg.mouse = p.mouse;
	cfg.touchpad = p.touchpad;
}

/* ---- Teclado ---- */

static char teste[128];

void pn_teclado_desenhar(void) {
	if (!lido) {
		pn_teclado_abrir();
	}
	if (disp.orbit && !disp.teclado) {
		gv_texto(CLAY_STRING("Nenhum teclado físico ligado agora: o que mudar vale quando houver um."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	pn_subtitulo(CLAY_ID("te-s1"), "Layouts");
	if (nlays == 0) {
		gv_texto(CLAY_STRING("Não achei a lista de layouts do XKB neste sistema."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	int remover = -1;
	for (int i = 0; i < nrows && nlays > 0; i++) {
		struct linha *r = &rows[i];
		pn_linha_comecar(CLAY_SID(gv_fmt("te-lay%d", i)), "keyboard", i == 0 ? "Layout principal" : gv_fmt("Layout %d", i + 1).chars,
			i == 0 ? "O que o teclado escreve" : "Troca com a tecla abaixo");
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(250) } } }) {
			if (gv_lista_suspensa(CLAY_SID(gv_fmt("te-lay-l%d", i)), lay_nomes, nlays, &r->li)) {
				carregar_variantes(r);
			}
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(220) } } }) {
			gv_lista_suspensa(CLAY_SID(gv_fmt("te-lay-v%d", i)), r->var_nomes, r->nvars, &r->vi);
		}
		if (nrows > 1 && gv_botao(CLAY_SID(gv_fmt("te-lay-x%d", i)), "close", NULL, GV_BOTAO_SIMPLES)) {
			remover = i;
		}
		pn_linha_acabar();
	}
	if (remover >= 0) {
		for (int k = remover; k < nrows - 1; k++) {
			rows[k] = rows[k + 1];
		}
		nrows--;
	}
	if (nrows < ENTRADA_MAX_LAYOUTS && nlays > 0 && gv_botao(CLAY_ID("te-add"), "plus", "Adicionar outro layout", GV_BOTAO_ELEVADO)) {
		rows[nrows].li = achar_layout("us");
		carregar_variantes(&rows[nrows]);
		nrows++;
		if (grp_i == 0) {
			grp_i = 1;
		}
	}
	if (nrows > 1) {
		pn_ajuste_lista(CLAY_ID("te-grp"), "keyboard", "Trocar de layout com", "Uma combinação que não atrapalhe seus atalhos", grp_nomes, NGRP, &grp_i);
	}
	pn_subtitulo(CLAY_ID("te-s2"), "Teclas");
	pn_ajuste_lista(CLAY_ID("te-caps"), "keyboard", "Caps Lock", "Muita gente troca por Ctrl ou Esc", caps_nomes, NCAPS, &caps_i);
	pn_linha_comecar(CLAY_ID("te-taxa"), "clock", "Velocidade de repetição", gv_fmt("%d teclas por segundo", cfg.repeticao_taxa).chars);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(260) } } }) {
		float v = (float)(cfg.repeticao_taxa - 1) / 99.0f;
		if (gv_deslizante(CLAY_ID("te-taxa-d"), &v, 0, 0)) {
			cfg.repeticao_taxa = 1 + (int)(v * 99.0f + 0.5f);
		}
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("te-atraso"), "clock", "Espera para repetir", gv_fmt("%d ms depois de apertar", cfg.repeticao_atraso).chars);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(260) } } }) {
		float v = (float)(cfg.repeticao_atraso - 100) / 1900.0f;
		if (gv_deslizante(CLAY_ID("te-atraso-d"), &v, 0, 0)) {
			cfg.repeticao_atraso = 100 + (int)(v * 1900.0f + 0.5f);
		}
	}
	pn_linha_acabar();
	pn_subtitulo(CLAY_ID("te-s3"), "Teste");
	CLAY(CLAY_ID("te-teste"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 0, 8 } } }) {
		gv_campo(CLAY_ID("te-teste-c"), teste, sizeof(teste), "Digite aqui; o que mudou vale depois do Aplicar", GV_CAMPO_TEXTO);
	}
}

/* ---- Mouse e touchpad ---- */

static float vel_para_barra(double v) {
	return (float)((v + 1.0) / 2.0);
}

static void grupo_ponteiro(const char *pref, bool touchpad, bool *primario_direito, double *velocidade, bool *plano, bool *natural) {
	int canhoto = *primario_direito ? 1 : 0;
	static const char *const botoes[] = { "Esquerdo", "Direito" };
	pn_linha_comecar(CLAY_SID(gv_fmt("%s-bot", pref)), "mouse", "Botão primário", "Qual botão faz o clique de sempre. Trocar ajuda quem usa o mouse com a mão esquerda");
	if (pn_segmentos(CLAY_SID(gv_fmt("%s-bot-s", pref)), botoes, 2, &canhoto)) {
		*primario_direito = canhoto == 1;
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_SID(gv_fmt("%s-vel", pref)), "mouse", "Velocidade do ponteiro", gv_fmt("%s", *velocidade < -0.3 ? "Lenta" : *velocidade > 0.3 ? "Rápida" : "Média").chars);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(260) } } }) {
		float v = vel_para_barra(*velocidade);
		if (gv_deslizante(CLAY_SID(gv_fmt("%s-vel-d", pref)), &v, 0, 0)) {
			*velocidade = (double)v * 2.0 - 1.0;
			if (*velocidade > -0.04 && *velocidade < 0.04) {
				*velocidade = 0; /* o meio fica no meio */
			}
		}
	}
	pn_linha_acabar();
	bool acel = !*plano;
	pn_linha_comecar(CLAY_SID(gv_fmt("%s-acel", pref)), "zap", "Aceleração do ponteiro", "Mexer mais rápido corre mais. Desligada, o ponteiro anda sempre o mesmo");
	if (gv_interruptor(CLAY_SID(gv_fmt("%s-acel-v", pref)), &acel)) {
		*plano = !acel;
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_SID(gv_fmt("%s-nat", pref)), "refresh", "Rolagem natural", "O conteúdo acompanha o dedo, como no celular");
	gv_interruptor(CLAY_SID(gv_fmt("%s-nat-v", pref)), natural);
	pn_linha_acabar();
	(void)touchpad;
}

void pn_mouse_desenhar(void) {
	if (!lido) {
		pn_teclado_abrir();
	}
	if (disp.orbit && !disp.ponteiro_ajustavel) {
		gv_texto(CLAY_STRING("Aqui não há mouse ou touchpad de verdade ligado ao Orbit (ele está dentro de outro sistema): os ajustes ficam gravados e valem no aparelho."),
			gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	pn_subtitulo(CLAY_ID("mo-s1"), disp.orbit && !disp.mouse ? "Mouse (nenhum ligado agora)" : "Mouse");
	grupo_ponteiro("mouse", false, &cfg.mouse.primario_direito, &cfg.mouse.velocidade, &cfg.mouse.plano, &cfg.mouse.natural);
	pn_subtitulo(CLAY_ID("mo-s2"), disp.orbit && !disp.touchpad ? "Touchpad (nenhum ligado agora)" : "Touchpad");
	pn_linha_comecar(CLAY_ID("mo-tocar"), "mouse", "Tocar para clicar", "Um toque no touchpad vale um clique");
	gv_interruptor(CLAY_ID("mo-tocar-v"), &cfg.touchpad.tocar);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("mo-dwt"), "keyboard", "Ignorar o touchpad ao digitar", "Evita clique sem querer com a palma da mão");
	gv_interruptor(CLAY_ID("mo-dwt-v"), &cfg.touchpad.digitar_desativa);
	pn_linha_acabar();
	grupo_ponteiro("touchpad", true, &cfg.touchpad.primario_direito, &cfg.touchpad.velocidade, &cfg.touchpad.plano, &cfg.touchpad.natural);
}
