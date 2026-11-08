/*
 * lumna-arquivos (LUMNA Explorer): o explorador de arquivos do LUMNA OS.
 *
 *   barra de título · barra de ferramentas (voltar, avançar, subir, caminho, busca, novo, ordenar, grade/lista, ajustes)
 *   barra lateral (Espaços, Coleções, Fixados) · a pasta em grade ou lista (só desenha o que está à vista) · painel de detalhes
 *   barra de status (quantos itens, o que está selecionado, o tamanho dos ícones)
 *
 * O que sabe do disco está em servicos/arquivos.c (testado sem tela). Abrir um arquivo usa o aplicativo padrão do tipo (Painel > Aplicativos padrão).
 * A pasta de agora é vigiada (inotify): o que muda por fora aparece aqui.
 *
 *   lumna-arquivos [pasta]
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <pango/pangocairo.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "arquivos.h"
#include "gravity.h"

/* ---- estado ---- */

enum vista { V_PASTA, V_RECENTES, V_FAVORITOS };
#define ESPECIAL_RECENTES "@recentes"
#define ESPECIAL_FAVORITOS "@favoritos"

static gv_app *app;
static enum vista vista;
static char pasta[4096];
static ar_lista lista;
static char **cam;          /* o caminho completo de cada entrada de lista */
static bool *sel;           /* selecionado (parelho com lista.v) */
static int *vis, nvis;      /* o que aparece (índices em lista.v), depois do filtro e da busca */
static int ancora = -1;     /* o último clicado (o começo de uma faixa com Shift) */
static int foco = -1;       /* o item do teclado */
static char erro_pasta[256];
static ar_hist hist;
static enum ar_ordem ordem = AR_O_NOME;
static bool decrescente;
static bool ocultos;
static enum ar_filtro filtro = AR_F_TUDO;
static char busca[128];
static bool modo_lista;
static float zoom = 0.4f;   /* 0..1: o tamanho dos ícones da grade */
static bool detalhes = true;
static int inotify_fd = -1;
static int inotify_wd = -1;
static char aviso[256];
static double aviso_ate;
static double ultimo_clique;
static int ultimo_item = -1;  /* o último item clicado (para o duplo clique) */
static int menu_item = -1;  /* o item do menu de contexto */
static bool focar_nome;     /* o formulário do + acabou de abrir: o campo do nome pega o foco */
static int novo_tipo;       /* 0 nada, 1 pasta, 2 arquivo de texto: o formulário do "+" */
static char novo_nome[256];
static int n_pastas, n_arquivos;
static int cont_filtros[6];

#define ID_CONTEUDO CLAY_ID("conteudo")

static void avisar(const char *formato, ...) __attribute__((format(printf, 1, 2)));
static void avisar(const char *formato, ...) {
	va_list ap;
	va_start(ap, formato);
	vsnprintf(aviso, sizeof(aviso), formato, ap);
	va_end(ap);
	aviso_ate = gv_agora() + 4;
	gv_acordar_em(4.2);
	gv_app_redesenhar(app);
}

/* ---- carregar a pasta ---- */

static void liberar_lista(void) {
	if (cam) {
		for (int i = 0; i < lista.n; i++) {
			g_free(cam[i]);
		}
		g_free(cam);
		cam = NULL;
	}
	g_free(sel);
	g_free(vis);
	sel = NULL;
	vis = NULL;
	nvis = 0;
	ar_liberar(&lista);
}

static void refiltrar(void) {
	g_free(vis);
	vis = g_new(int, lista.n > 0 ? lista.n : 1);
	nvis = ar_filtrar(&lista, busca, filtro, vis);
	n_pastas = n_arquivos = 0;
	memset(cont_filtros, 0, sizeof(cont_filtros));
	for (int i = 0; i < lista.n; i++) {
		if (lista.v[i].pasta) {
			n_pastas++;
		} else {
			n_arquivos++;
		}
		for (int f = 0; f < 6; f++) {
			cont_filtros[f] += ar_no_filtro(&lista.v[i], (enum ar_filtro)f);
		}
	}
}

static void nomear_caminhos(void) {
	cam = g_new0(char *, lista.n > 0 ? lista.n : 1);
	for (int i = 0; i < lista.n; i++) {
		cam[i] = vista == V_PASTA ? ar_unir(pasta, lista.v[i].nome) : NULL;
	}
}

static void vigiar_pasta(void) {
	if (inotify_fd < 0) {
		return;
	}
	if (inotify_wd >= 0) {
		inotify_rm_watch(inotify_fd, inotify_wd);
		inotify_wd = -1;
	}
	if (vista == V_PASTA) {
		inotify_wd = inotify_add_watch(inotify_fd, pasta, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF);
	}
}

/* (Re)lê o que a vista mostra. preservar: mantém a seleção pelos nomes (uma atualização por inotify). */
static void carregar(bool preservar) {
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	if (preservar && sel) {
		for (int i = 0; i < lista.n; i++) {
			if (sel[i]) {
				g_ptr_array_add(nomes, g_strdup(cam[i] ? cam[i] : lista.v[i].nome));
			}
		}
	}
	liberar_lista();
	erro_pasta[0] = '\0';
	if (vista == V_PASTA) {
		char *erro = NULL;
		if (!ar_listar(pasta, ocultos, &lista, &erro)) {
			snprintf(erro_pasta, sizeof(erro_pasta), "%s", erro ? erro : "Não consegui abrir esta pasta");
			g_free(erro);
			memset(&lista, 0, sizeof(lista));
		}
		ar_ordenar(&lista, ordem, decrescente);
		nomear_caminhos();
	} else {
		ar_lugar lug[200];
		int n = vista == V_RECENTES ? ar_recentes(lug, 200) : ar_favoritos(lug, 200);
		lista.v = g_new(ar_entrada, n > 0 ? n : 1);
		lista.cap = n > 0 ? n : 1;
		cam = g_new0(char *, n > 0 ? n : 1);
		for (int i = 0; i < n; i++) {
			if (ar_entrada_de(lug[i].caminho, &lista.v[lista.n])) {
				cam[lista.n++] = g_strdup(lug[i].caminho);
			}
		}
		ar_lugares_liberar(lug, n);
	}
	sel = g_new0(bool, lista.n > 0 ? lista.n : 1);
	for (guint k = 0; k < nomes->len; k++) {
		for (int i = 0; i < lista.n; i++) {
			if (!strcmp(nomes->pdata[k], cam[i] ? cam[i] : lista.v[i].nome)) {
				sel[i] = true;
			}
		}
	}
	g_ptr_array_free(nomes, TRUE);
	refiltrar();
	if (!preservar) {
		ancora = foco = ultimo_item = -1;
	} else if (foco >= lista.n) {
		foco = -1;
	}
	vigiar_pasta();
	gv_app_redesenhar(app);
}

static void ir_para(const char *destino, bool na_historia) {
	if (!strcmp(destino, ESPECIAL_RECENTES)) {
		vista = V_RECENTES;
	} else if (!strcmp(destino, ESPECIAL_FAVORITOS)) {
		vista = V_FAVORITOS;
	} else {
		vista = V_PASTA;
		snprintf(pasta, sizeof(pasta), "%s", destino);
	}
	if (na_historia) {
		ar_hist_ir(&hist, destino);
	}
	busca[0] = '\0';
	filtro = AR_F_TUDO;
	carregar(false);
	gv_rolar_para(ID_CONTEUDO, 0, 0, false);
}

static void subir(void) {
	if (vista != V_PASTA) {
		return;
	}
	char *pai = ar_pai(pasta);
	if (pai) {
		ir_para(pai, true);
		g_free(pai);
	}
}

static void voltar(void) {
	const char *d = ar_hist_voltar(&hist);
	if (d) {
		ir_para(d, false);
	}
}

static void avancar(void) {
	const char *d = ar_hist_avancar(&hist);
	if (d) {
		ir_para(d, false);
	}
}

static void abrir_arquivo(const char *caminho) {
	char *uri = g_filename_to_uri(caminho, NULL, NULL);
	GError *err = NULL;
	if (!uri || !g_app_info_launch_default_for_uri(uri, NULL, &err)) {
		char *base = g_path_get_basename(caminho);
		avisar("Não consegui abrir %s: %s", base, err && err->message ? err->message : "nenhum aplicativo para este tipo");
		g_free(base);
		g_clear_error(&err);
	} else {
		ar_recente_registrar(caminho);
	}
	g_free(uri);
}

static void abrir_item(int i) {
	if (i < 0 || i >= lista.n) {
		return;
	}
	if (lista.v[i].pasta) {
		ir_para(cam[i], true);
	} else {
		abrir_arquivo(cam[i]);
	}
}

static void inotify_pronto(gv_app *a, int fd, void *dados) {
	char buf[4096];
	bool mudou = false;
	while (read(fd, buf, sizeof(buf)) > 0) {
		mudou = true;
	}
	if (mudou && vista == V_PASTA) {
		carregar(true);
	}
}

/* ---- seleção ---- */

static int n_selecionados(void) {
	int n = 0;
	for (int i = 0; i < lista.n; i++) {
		n += sel[i];
	}
	return n;
}

static int primeiro_selecionado(void) {
	for (int i = 0; i < lista.n; i++) {
		if (sel[i]) {
			return i;
		}
	}
	return -1;
}

static void limpar_selecao(void) {
	memset(sel, 0, (size_t)(lista.n > 0 ? lista.n : 1));
}

static int pos_em_vis(int idx) {
	for (int k = 0; k < nvis; k++) {
		if (vis[k] == idx) {
			return k;
		}
	}
	return -1;
}

static void clicar_item(int idx, uint32_t mods) {
	if (mods & GV_MOD_SHIFT && ancora >= 0) {
		int a = pos_em_vis(ancora), b = pos_em_vis(idx);
		if (a >= 0 && b >= 0) {
			if (!(mods & GV_MOD_CTRL)) {
				limpar_selecao();
			}
			for (int k = a < b ? a : b; k <= (a < b ? b : a); k++) {
				sel[vis[k]] = true;
			}
		}
	} else if (mods & GV_MOD_CTRL) {
		sel[idx] = !sel[idx];
		ancora = idx;
	} else {
		limpar_selecao();
		sel[idx] = true;
		ancora = idx;
	}
	foco = idx;
}

/* ---- desenho dos ícones (feitos à mão, sem arquivo de imagem) ---- */

typedef struct ic_dados {
	enum ar_tipo tipo;
	char selo[8];
	bool link;
} ic_dados;
static ic_dados ic_pool[1536];
static int ic_n;

static void cor_rgb(cairo_t *cr, uint32_t rgb, double a) {
	cairo_set_source_rgba(cr, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0, a);
}

static void rr(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

static uint32_t cor_do_tipo(enum ar_tipo t) {
	switch (t) {
	case AR_PDF: return 0xE5484D;
	case AR_IMAGEM: return 0x00A2C7;
	case AR_AUDIO: return 0xA78BFA;
	case AR_VIDEO: return 0xD6409F;
	case AR_MARKDOWN: return 0x3E63DD;
	case AR_PLANILHA: return 0x30A46C;
	case AR_APRESENTACAO: return 0xF76B15;
	case AR_DOCUMENTO: return 0x3E8EF7;
	case AR_COMPACTADO: return 0xE5A100;
	case AR_CODIGO: return 0x12A594;
	default: return 0x8B8D98;
	}
}

static void desenhar_pasta(cairo_t *cr, double x, double y, double w, double h) {
	double aba = h * 0.16;
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y, 0, y + h);
	Clay_Color a = gv_t.acento;
	cairo_pattern_add_color_stop_rgb(g, 0, fmin(1, a.r / 255.0 + 0.30), fmin(1, a.g / 255.0 + 0.24), fmin(1, a.b / 255.0 + 0.26));
	cairo_pattern_add_color_stop_rgb(g, 1, fmin(1, a.r / 255.0 + 0.10), fmin(1, a.g / 255.0 + 0.06), fmin(1, a.b / 255.0 + 0.10));
	/* a aba de trás, mais escura */
	rr(cr, x, y + h * 0.04, w * 0.46, aba * 2.2, h * 0.07);
	cairo_set_source_rgb(cr, fmin(1, a.r / 255.0 + 0.16), fmin(1, a.g / 255.0 + 0.10), fmin(1, a.b / 255.0 + 0.14));
	cairo_fill(cr);
	rr(cr, x, y + aba, w, h - aba, h * 0.10);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

static void desenhar_pagina(cairo_t *cr, double x, double y, double w, double h, const ic_dados *d) {
	double pw = w * 0.74, ph = h, px = x + (w - pw) / 2, dobra = pw * 0.28;
	cairo_new_path(cr);
	cairo_move_to(cr, px + h * 0.06, y);
	cairo_line_to(cr, px + pw - dobra, y);
	cairo_line_to(cr, px + pw, y + dobra);
	cairo_line_to(cr, px + pw, y + ph - h * 0.06);
	cairo_curve_to(cr, px + pw, y + ph, px + pw, y + ph, px + pw - h * 0.06, y + ph);
	cairo_line_to(cr, px + h * 0.06, y + ph);
	cairo_curve_to(cr, px, y + ph, px, y + ph, px, y + ph - h * 0.06);
	cairo_line_to(cr, px, y + h * 0.06);
	cairo_curve_to(cr, px, y, px, y, px + h * 0.06, y);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.95, 0.95, 0.97);
	cairo_fill(cr);
	/* a dobra */
	cairo_move_to(cr, px + pw - dobra, y);
	cairo_line_to(cr, px + pw - dobra, y + dobra);
	cairo_line_to(cr, px + pw, y + dobra);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.82, 0.82, 0.88);
	cairo_fill(cr);
	/* o selo colorido do tipo */
	double sw = pw * 0.60, sh = sw * 0.62, sx = px + (pw - sw) / 2, sy = y + ph * 0.42;
	rr(cr, sx, sy, sw, sh, sh * 0.22);
	cor_rgb(cr, cor_do_tipo(d->tipo), 1);
	cairo_fill(cr);
	if (d->selo[0]) {
		PangoLayout *l = pango_cairo_create_layout(cr);
		char fonte[96];
		snprintf(fonte, sizeof(fonte), "%s Bold %.1f", gv_t.fonte, sh * (strlen(d->selo) > 3 ? 0.30 : 0.36) * 0.75);
		PangoFontDescription *fd = pango_font_description_from_string(fonte);
		pango_layout_set_font_description(l, fd);
		pango_font_description_free(fd);
		pango_layout_set_text(l, d->selo, -1);
		int tw, th;
		pango_layout_get_pixel_size(l, &tw, &th);
		cairo_move_to(cr, sx + (sw - tw) / 2.0, sy + (sh - th) / 2.0);
		cairo_set_source_rgb(cr, 1, 1, 1);
		pango_cairo_show_layout(cr, l);
		g_object_unref(l);
	}
}

static void desenhar_icone(cairo_t *cr, Clay_BoundingBox c, void *dados) {
	const ic_dados *d = dados;
	double s = fmin(c.width, c.height);
	double x = c.x + (c.width - s) / 2, y = c.y + (c.height - s) / 2;
	if (d->tipo == AR_PASTA) {
		desenhar_pasta(cr, x + s * 0.04, y + s * 0.10, s * 0.92, s * 0.80);
	} else {
		desenhar_pagina(cr, x + s * 0.04, y + s * 0.04, s * 0.92, s * 0.92, d);
	}
	if (d->link) { /* uma setinha no canto: é um atalho */
		cairo_arc(cr, x + s * 0.82, y + s * 0.82, s * 0.12, 0, 2 * M_PI);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_fill(cr);
		cairo_move_to(cr, x + s * 0.76, y + s * 0.88);
		cairo_line_to(cr, x + s * 0.88, y + s * 0.76);
		cairo_move_to(cr, x + s * 0.78, y + s * 0.76);
		cairo_line_to(cr, x + s * 0.88, y + s * 0.76);
		cairo_line_to(cr, x + s * 0.88, y + s * 0.86);
		cairo_set_source_rgb(cr, 0.2, 0.2, 0.25);
		cairo_set_line_width(cr, s * 0.035);
		cairo_stroke(cr);
	}
}

/* O ícone de uma entrada: uma imagem mostra a miniatura (o Gravity descodifica numa thread); o resto é desenhado. */
static void icone(Clay_ElementId id, int idx, float tam) {
	const ar_entrada *e = &lista.v[idx];
	if (e->tipo == AR_IMAGEM && cam[idx] && e->tamanho < 40 * 1024 * 1024) {
		float prop = gv_imagem_proporcao(cam[idx]);
		float w = tam, h = tam;
		if (prop > 0) {
			if (prop >= 1) {
				h = tam / prop;
			} else {
				w = tam * prop;
			}
		}
		CLAY(CLAY_ID_LOCAL("mini"), { .layout = { .sizing = { CLAY_SIZING_FIXED(tam), CLAY_SIZING_FIXED(tam) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(id, gv_imagem(id, cam[idx], GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
				.layout = { .sizing = { CLAY_SIZING_FIXED(w * 0.92), CLAY_SIZING_FIXED(h * 0.92) } },
				.cornerRadius = CLAY_CORNER_RADIUS(6), .backgroundColor = gv_t.elevada })) {}
		}
		return;
	}
	ic_dados *d = &ic_pool[ic_n++ % (int)(sizeof(ic_pool) / sizeof(ic_pool[0]))];
	d->tipo = e->tipo;
	d->link = e->link;
	ar_selo(e, d->selo, sizeof(d->selo));
	CLAY(id, gv_livre(desenhar_icone, d, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(tam), CLAY_SIZING_FIXED(tam) } } })) {}
}

/* ---- barra de ferramentas ---- */

static void botao_barra(Clay_ElementId id, const char *ic, bool ativo, bool desativado, bool *clicado) {
	gv_botao_opcoes o = { .icone = ic, .estilo = ativo ? GV_BOTAO_ELEVADO : GV_BOTAO_SIMPLES, .desativado = desativado, .altura = 40 };
	*clicado = gv_botao_ext(id, &o);
}

static const char *nome_da_vista(void) {
	if (vista == V_RECENTES) {
		return "Recentes";
	}
	if (vista == V_FAVORITOS) {
		return "Favoritos";
	}
	ar_parte p[32];
	int n = ar_partes(pasta, p, 32);
	const char *r = gv_str(p[n - 1].rotulo).chars;
	ar_partes_liberar(p, n);
	return r;
}

static void caminho_barra(void) {
	CLAY(CLAY_ID("caminho"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 14, 14, 0, 0 }, .childGap = 6,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .clip = { .horizontal = true } }) {
		if (vista != V_PASTA) {
			gv_icone(vista == V_RECENTES ? "clock" : "star", 20, gv_t.texto_secundario);
			gv_texto(gv_str(nome_da_vista()), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		} else {
		ar_parte p[32];
		int n = ar_partes(pasta, p, 32);
		gv_icone("folder", 20, gv_t.texto_secundario);
		int ini = n > 4 ? n - 4 : 0; /* um caminho comprido mostra só o fim */
		if (ini) {
			gv_texto(CLAY_STRING("…"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("/"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		for (int i = ini; i < n; i++) {
			Clay_ElementId id = CLAY_IDI("parte", (uint32_t)i);
			bool ultimo = i == n - 1;
			CLAY(id, { .layout = { .padding = { 6, 6, 3, 3 } }, .cornerRadius = CLAY_CORNER_RADIUS(6),
				.backgroundColor = gv_por_cima(id) && !ultimo ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 } }) {
				gv_texto(gv_str(p[i].rotulo), gv_t.tamanho_corpo, ultimo ? gv_t.texto : gv_t.texto_secundario, ultimo ? GV_PESO_MEDIO : GV_PESO_NORMAL);
			}
			if (gv_tocado(id) && !ultimo) {
				ir_para(p[i].caminho, true);
			}
			if (!ultimo) {
				gv_texto(CLAY_STRING("/"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		ar_partes_liberar(p, n);
		}
	}
}

static const char *const NOMES_ORDEM[] = { "Nome", "Data de modificação", "Tamanho", "Tipo" };

static void criar_novo(void) {
	if (!novo_nome[0] || vista != V_PASTA) {
		return;
	}
	char *alvo = ar_unir(pasta, novo_nome);
	for (int k = 2; g_file_test(alvo, G_FILE_TEST_EXISTS) && k < 1000; k++) {
		g_free(alvo);
		char *n2 = g_strdup_printf("%s (%d)", novo_nome, k);
		alvo = ar_unir(pasta, n2);
		g_free(n2);
	}
	bool ok;
	if (novo_tipo == 1) {
		ok = g_mkdir(alvo, 0755) == 0;
	} else {
		FILE *f = fopen(alvo, "wx");
		ok = f != NULL;
		if (f) {
			fclose(f);
		}
	}
	if (!ok) {
		avisar("Não consegui criar: %s", g_strerror(errno));
	} else {
		carregar(false);
		for (int i = 0; i < lista.n; i++) {
			if (!strcmp(cam[i], alvo)) {
				limpar_selecao();
				sel[i] = true;
				foco = ancora = i;
			}
		}
	}
	g_free(alvo);
	novo_tipo = 0;
	novo_nome[0] = '\0';
	gv_popup_fechar(CLAY_ID("menu-novo"));
}

static void barra_ferramentas(void) {
	CLAY(CLAY_ID("ferramentas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 16, 16, 0, 0 }, .childGap = 8,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		bool c;
		botao_barra(CLAY_ID("t-voltar"), "arrow-left", false, !ar_hist_pode_voltar(&hist), &c);
		if (c) {
			voltar();
		}
		botao_barra(CLAY_ID("t-avancar"), "arrow-right", false, !ar_hist_pode_avancar(&hist), &c);
		if (c) {
			avancar();
		}
		char *pai = vista == V_PASTA ? ar_pai(pasta) : NULL;
		botao_barra(CLAY_ID("t-subir"), "arrow-up", false, !pai, &c);
		g_free(pai);
		if (c) {
			subir();
		}
		CLAY(CLAY_ID("t-div"), { .layout = { .sizing = { CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(28) } }, .backgroundColor = gv_t.borda }) {}
		caminho_barra();
		CLAY(CLAY_ID("t-busca"), { .layout = { .sizing = { CLAY_SIZING_FIXED(280) } } }) {
			if (gv_campo(CLAY_ID("busca"), busca, sizeof(busca), gv_fmt("Buscar em %s", nome_da_vista()).chars, GV_CAMPO_BUSCA) & GV_CAMPO_MUDOU) {
				refiltrar();
			}
		}
		botao_barra(CLAY_ID("t-novo"), "plus", false, vista != V_PASTA, &c);
		if (c) {
			novo_tipo = 0;
			gv_popup_alternar(CLAY_ID("menu-novo"));
		}
		botao_barra(CLAY_ID("t-ordem"), "sliders", false, false, &c);
		if (c) {
			gv_popup_alternar(CLAY_ID("menu-ordem"));
		}
		botao_barra(CLAY_ID("t-grade"), "grid", !modo_lista, false, &c);
		if (c) {
			modo_lista = false;
		}
		botao_barra(CLAY_ID("t-lista"), "list", modo_lista, false, &c);
		if (c) {
			modo_lista = true;
		}
		botao_barra(CLAY_ID("t-ajustes"), "settings", false, false, &c);
		if (c) {
			gv_popup_alternar(CLAY_ID("menu-ajustes"));
		}
	}

	GV_POPUP(CLAY_ID("menu-novo"), CLAY_ID("t-novo"), .alinhar = GV_ALINHAR_FIM, .largura = 260) {
		CLAY(CLAY_ID("mn-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(8), .childGap = 4 } }) {
			if (!novo_tipo) {
				if (gv_menu_item(CLAY_ID("mn-pasta"), "folder-plus", "Nova pasta", NULL)) {
					novo_tipo = 1;
					focar_nome = true;
					snprintf(novo_nome, sizeof(novo_nome), "Nova pasta");
				}
				if (gv_menu_item(CLAY_ID("mn-arq"), "file", "Novo arquivo de texto", NULL)) {
					novo_tipo = 2;
					focar_nome = true;
					snprintf(novo_nome, sizeof(novo_nome), "Novo arquivo.txt");
				}
			} else {
				gv_texto(novo_tipo == 1 ? CLAY_STRING("Nome da pasta") : CLAY_STRING("Nome do arquivo"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
				int r = gv_campo(CLAY_ID("mn-nome"), novo_nome, sizeof(novo_nome), "Nome", GV_CAMPO_TEXTO);
				if (focar_nome) {
					gv_focar(CLAY_ID("mn-nome"));
					focar_nome = false;
				}
				if (r & GV_CAMPO_ENTER) {
					criar_novo();
				} else if (gv_botao(CLAY_ID("mn-criar"), NULL, "Criar", GV_BOTAO_CHEIO)) {
					criar_novo();
				}
			}
		}
	}
	GV_POPUP(CLAY_ID("menu-ordem"), CLAY_ID("t-ordem"), .alinhar = GV_ALINHAR_FIM, .largura = 260) {
		CLAY(CLAY_ID("mo-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(8), .childGap = 2 } }) {
			gv_texto(CLAY_STRING("Ordenar por"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
			for (int o = 0; o < 4; o++) {
				if (gv_menu_item(CLAY_IDI("mo-o", (uint32_t)o), ordem == (enum ar_ordem)o ? "check" : NULL, NOMES_ORDEM[o], NULL)) {
					ordem = (enum ar_ordem)o;
					carregar(true);
					gv_popup_fechar(CLAY_ID("menu-ordem"));
				}
			}
			CLAY(CLAY_ID("mo-div"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
			if (gv_menu_item(CLAY_ID("mo-asc"), !decrescente ? "check" : NULL, "Crescente", NULL)) {
				decrescente = false;
				carregar(true);
				gv_popup_fechar(CLAY_ID("menu-ordem"));
			}
			if (gv_menu_item(CLAY_ID("mo-desc"), decrescente ? "check" : NULL, "Decrescente", NULL)) {
				decrescente = true;
				carregar(true);
				gv_popup_fechar(CLAY_ID("menu-ordem"));
			}
		}
	}
	GV_POPUP(CLAY_ID("menu-ajustes"), CLAY_ID("t-ajustes"), .alinhar = GV_ALINHAR_FIM, .largura = 300) {
		CLAY(CLAY_ID("ma-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(10), .childGap = 8 } }) {
			bool ligado = ocultos;
			CLAY(CLAY_ID("ma-l1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .childGap = 10 } }) {
				CLAY(CLAY_ID("ma-t1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Mostrar arquivos ocultos"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
				if (gv_interruptor(CLAY_ID("ma-i1"), &ligado)) {
					ocultos = ligado;
					carregar(true);
				}
			}
			bool pd = detalhes;
			CLAY(CLAY_ID("ma-l2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .childGap = 10 } }) {
				CLAY(CLAY_ID("ma-t2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Painel de detalhes"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
				if (gv_interruptor(CLAY_ID("ma-i2"), &pd)) {
					detalhes = pd;
				}
			}
		}
	}
}

/* ---- barra lateral ---- */

static void item_lateral(Clay_ElementId id, const char *ic, const char *rotulo, bool ativo, const char *destino) {
	bool sobre = gv_por_cima(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(38) }, .padding = { 12, 10, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ativo ? gv_t.selecionado : sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		gv_icone(ic, 20, ativo ? gv_t.texto : gv_t.texto_secundario);
		gv_texto(gv_str(rotulo), gv_t.tamanho_corpo - 1, ativo ? gv_t.texto : gv_t.texto_secundario, ativo ? GV_PESO_MEDIO : GV_PESO_NORMAL);
	}
	if (gv_tocado(id)) {
		ir_para(destino, true);
	}
}

static void titulo_lateral(const char *t) {
	CLAY_AUTO_ID({ .layout = { .padding = { 12, 8, 14, 6 } } }) {
		gv_texto(gv_str(t), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
	}
}

static void barra_lateral(void) {
	Clay_ElementId rol = CLAY_ID("lateral-rolagem");
	CLAY(CLAY_ID("lateral"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(236), CLAY_SIZING_GROW(0) } },
		.border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 10, 10, 4, 12 }, .childGap = 2 } })) {
			const char *casa = g_get_home_dir();
			titulo_lateral("Espaços");
			item_lateral(CLAY_ID("l-inicio"), "home", "Início", vista == V_PASTA && !strcmp(pasta, casa), casa);
			item_lateral(CLAY_ID("l-recentes"), "clock", "Recentes", vista == V_RECENTES, ESPECIAL_RECENTES);
			item_lateral(CLAY_ID("l-favoritos"), "star", "Favoritos", vista == V_FAVORITOS, ESPECIAL_FAVORITOS);
			CLAY(CLAY_ID("l-d1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) }, .padding = { 0, 0, 0, 0 } }, .backgroundColor = gv_t.borda }) {}
			titulo_lateral("Coleções");
			ar_lugar col[16];
			int n = ar_colecoes(col, 16);
			for (int i = 0; i < n; i++) {
				item_lateral(CLAY_IDI("l-col", (uint32_t)i), col[i].icone, col[i].rotulo, vista == V_PASTA && !strcmp(pasta, col[i].caminho), col[i].caminho);
			}
			ar_lugares_liberar(col, n);
			ar_lugar fav[16];
			int nf = ar_favoritos(fav, 16);
			int pastas_fav = 0;
			for (int i = 0; i < nf; i++) {
				pastas_fav += g_file_test(fav[i].caminho, G_FILE_TEST_IS_DIR);
			}
			if (pastas_fav) {
				CLAY(CLAY_ID("l-d2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
				titulo_lateral("Fixados");
				for (int i = 0; i < nf; i++) {
					if (g_file_test(fav[i].caminho, G_FILE_TEST_IS_DIR)) {
						item_lateral(CLAY_IDI("l-fav", (uint32_t)i), "star", fav[i].rotulo, vista == V_PASTA && !strcmp(pasta, fav[i].caminho), fav[i].caminho);
					}
				}
			}
			ar_lugares_liberar(fav, nf);
		}
	}
}

/* ---- o conteúdo ---- */

static int colunas;      /* quantas por linha na grade (a conta do quadro anterior) */

static void abrir_menu_do_item(int i) {
	if (!sel[i]) {
		limpar_selecao();
		sel[i] = true;
		ancora = foco = i;
	}
	menu_item = i;
}

static void celula_grade(int idx, float tam, float cw, float ch) {
	Clay_ElementId id = CLAY_IDI("cel", (uint32_t)idx);
	const ar_entrada *e = &lista.v[idx];
	if (e->pasta && e->itens < -1 + 1 && e->itens == -1) {
		lista.v[idx].itens = ar_contar_itens(cam[idx], ocultos);
	}
	bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-item"));
	if (pediu) {
		abrir_menu_do_item(idx);
	}
	bool sobre = gv_por_cima(id);
	Clay_Color fundo = sel[idx] ? gv_alfa(gv_t.acento, 0.18f) : sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 };
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(cw), CLAY_SIZING_FIXED(ch) }, .padding = { 6, 6, 10, 6 },
		.childGap = 6, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .backgroundColor = fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = sel[idx] ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 }, .width = GV_BORDA(2) } }) {
		icone(CLAY_IDI("cel-ic", (uint32_t)idx), idx, tam);
		CLAY(CLAY_IDI("cel-nome", (uint32_t)idx), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
			.clip = { .horizontal = true, .vertical = true } }) {
			CLAY(CLAY_IDI("cel-nome-t", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0, (gv_t.tamanho_corpo + 6) * 2) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .clip = { .vertical = true } }) {
				gv_texto(gv_str(e->nome), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			if (e->pasta && e->itens >= 0) {
				gv_texto(gv_fmt(e->itens == 1 ? "%d item" : "%d itens", e->itens), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
	}
}

static void linha_lista(int idx, int linha_par) {
	Clay_ElementId id = CLAY_IDI("lin", (uint32_t)idx);
	const ar_entrada *e = &lista.v[idx];
	if (e->pasta && e->itens == -1) {
		lista.v[idx].itens = ar_contar_itens(cam[idx], ocultos);
	}
	bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-item"));
	if (pediu) {
		abrir_menu_do_item(idx);
	}
	bool sobre = gv_por_cima(id);
	Clay_Color fundo = sel[idx] ? gv_alfa(gv_t.acento, 0.18f) : sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 };
	char data[48], tam[32];
	ar_data_humana(e->mtime, time(NULL), data, sizeof(data));
	if (e->pasta) {
		snprintf(tam, sizeof(tam), e->itens >= 0 ? (e->itens == 1 ? "%d item" : "%d itens") : "", e->itens);
	} else {
		ar_tamanho_humano(e->tamanho, tam, sizeof(tam));
	}
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) }, .padding = { 14, 14, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		icone(CLAY_IDI("lin-ic", (uint32_t)idx), idx, 30);
		CLAY(CLAY_IDI("lin-nome", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(e->nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("lin-data", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
			gv_texto(gv_str(data), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("lin-tam", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_FIXED(90) } } }) {
			gv_texto(gv_str(tam), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("lin-tipo", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_FIXED(130) } } }) {
			gv_texto(gv_str(ar_tipo_nome(e)), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
	(void)linha_par;
}

/* um clique (ou toque) num item: seleciona; o segundo em seguida abre */
static void tratar_clique(int idx, Clay_ElementId id) {
	if (!gv_tocado(id)) {
		return;
	}
	double agora = gv_agora();
	uint32_t mods = gv_modificadores();
	if (idx == ultimo_item && agora - ultimo_clique < 0.45 && !(mods & (GV_MOD_CTRL | GV_MOD_SHIFT))) {
		abrir_item(idx);
		ultimo_item = -1;
		return;
	}
	clicar_item(idx, mods);
	ultimo_item = idx;
	ultimo_clique = agora;
}

static void cabecalho_lista(void) {
	static const struct { const char *nome; enum ar_ordem o; float larg; } COLS[] = {
		{ "Nome", AR_O_NOME, 0 }, { "Modificado", AR_O_DATA, 150 }, { "Tamanho", AR_O_TAMANHO, 90 }, { "Tipo", AR_O_TIPO, 130 } };
	CLAY(CLAY_ID("cab-lista"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34) }, .padding = { 16 + 14 + 30 + 14, 16 + 14 + 6, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		for (int c = 0; c < 4; c++) {
			Clay_ElementId id = CLAY_IDI("cab-col", (uint32_t)c);
			CLAY(id, { .layout = { .sizing = { COLS[c].larg ? CLAY_SIZING_FIXED(COLS[c].larg) : CLAY_SIZING_GROW(0) }, .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(gv_str(COLS[c].nome), gv_t.tamanho_legenda + 1, ordem == COLS[c].o ? gv_t.texto : gv_t.texto_suave, GV_PESO_MEDIO);
				if (ordem == COLS[c].o) {
					gv_icone(decrescente ? "chevron-down" : "chevron-up", 14, gv_t.texto_suave);
				}
			}
			if (gv_tocado(id)) {
				if (ordem == COLS[c].o) {
					decrescente = !decrescente;
				} else {
					ordem = COLS[c].o;
					decrescente = false;
				}
				carregar(true);
			}
		}
	}
}

static const char *const ROTULO_FILTRO[] = { "Tudo", "Pastas", "Documentos", "Imagens", "Mídia", "Outros" };
static const char *const ICONE_FILTRO[] = { NULL, "folder", "document", "image", "video", "file" };

static void cabecalho_conteudo(void) {
	CLAY(CLAY_ID("cabeca"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 18, 8 }, .childGap = 4 } }) {
		gv_texto(gv_str(nome_da_vista()), 32, gv_t.texto, GV_PESO_NORMAL);
		const char *sub;
		if (erro_pasta[0]) {
			sub = "Esta pasta não abriu";
		} else if (n_pastas && n_arquivos) {
			sub = gv_fmt("%d %s · %d %s", n_pastas, n_pastas == 1 ? "pasta" : "pastas", n_arquivos, n_arquivos == 1 ? "arquivo" : "arquivos").chars;
		} else if (n_pastas) {
			sub = gv_fmt("%d %s", n_pastas, n_pastas == 1 ? "pasta" : "pastas").chars;
		} else if (n_arquivos) {
			sub = gv_fmt("%d %s", n_arquivos, n_arquivos == 1 ? "arquivo" : "arquivos").chars;
		} else {
			sub = "Vazia";
		}
		gv_texto(gv_str(sub), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	/* os filtros: só os que têm algo (e o que está ligado); se nenhum separa nada, a faixa nem aparece */
	int uteis = 0;
	for (int f = 1; f < 6; f++) {
		uteis += cont_filtros[f] && cont_filtros[f] != cont_filtros[0];
	}
	if (!uteis && filtro == AR_F_TUDO) {
		return;
	}
	CLAY(CLAY_ID("filtros"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 4, 10 }, .childGap = 10 } }) {
		for (int f = 0; f < 6; f++) {
			if (f && !cont_filtros[f] && filtro != (enum ar_filtro)f) {
				continue;
			}
			if (f && cont_filtros[f] == cont_filtros[0] && filtro != (enum ar_filtro)f) {
				continue; /* é tudo: o filtro não separa nada */
			}
			Clay_ElementId id = CLAY_IDI("filtro", (uint32_t)f);
			bool ligado = filtro == (enum ar_filtro)f;
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(f ? 132 : 96), CLAY_SIZING_FIXED(38) }, .childGap = 8,
				.childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = ligado ? gv_t.acento : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(19),
				.border = { .color = ligado ? gv_t.acento : gv_t.borda, .width = GV_BORDA(1) } }) {
				if (ICONE_FILTRO[f]) {
					gv_icone(ICONE_FILTRO[f], 16, ligado ? gv_t.sobre_acento : gv_t.texto_secundario);
				}
				gv_texto(gv_str(ROTULO_FILTRO[f]), gv_t.tamanho_corpo - 1, ligado ? gv_t.sobre_acento : gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (gv_tocado(id)) {
				filtro = (enum ar_filtro)f;
				refiltrar();
				gv_rolar_para(ID_CONTEUDO, 0, 0, false);
			}
		}
	}
}

static void conteudo(void) {
	CLAY(CLAY_ID("principal"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		cabecalho_conteudo();
		if (modo_lista && !erro_pasta[0]) {
			cabecalho_lista();
		}
		bool menu_fundo = gv_menu_no_ponteiro(CLAY_ID("rolagem-fundo"), CLAY_ID("menu-fundo"));
		(void)menu_fundo;
		CLAY(ID_CONTEUDO, gv_rolagem(ID_CONTEUDO, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 16, 16, 6, 16 } } })) {
			float larg = gv_caixa(ID_CONTEUDO).width - 32 - 12;
			if (larg < 200) {
				larg = 800;
			}
			float topo = gv_rolagem_posicao(ID_CONTEUDO).y;
			float alt_view = gv_caixa(ID_CONTEUDO).height > 0 ? gv_caixa(ID_CONTEUDO).height : 600;
			if (erro_pasta[0]) {
				CLAY(CLAY_ID("erro"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 8, 40, 0 }, .childGap = 10,
					.childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
					gv_icone("lock", 40, gv_t.texto_suave);
					gv_texto(CLAY_STRING("Não foi possível abrir esta pasta"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(erro_pasta), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			} else if (!nvis) {
				CLAY(CLAY_ID("vazio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 8, 60, 0 }, .childGap = 10,
					.childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
					gv_icone(busca[0] ? "search" : vista == V_FAVORITOS ? "star" : vista == V_RECENTES ? "clock" : "folder", 44, gv_t.texto_suave);
					gv_texto(busca[0] ? CLAY_STRING("Nada com esse nome aqui") : vista == V_FAVORITOS ? CLAY_STRING("Nenhum favorito ainda") :
						vista == V_RECENTES ? CLAY_STRING("Nada aberto recentemente") : CLAY_STRING("Esta pasta está vazia"), gv_t.tamanho_destaque, gv_t.texto_secundario, GV_PESO_NORMAL);
					if (vista == V_FAVORITOS) {
						gv_texto(CLAY_STRING("Clique com o botão direito num item e escolha Favoritar"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
					}
				}
			} else if (!modo_lista) {
				float tam = 72 + zoom * 80;
				float cw = tam + 44, ch = tam + 14 + 10 + 6 + (gv_t.tamanho_corpo + 6) * 2 + 20;
				int cols = (int)(larg / cw);
				colunas = cols < 1 ? 1 : cols;
				int linhas = (nvis + colunas - 1) / colunas;
				int r0 = (int)(topo / ch) - 1, r1 = (int)((topo + alt_view) / ch) + 1;
				r0 = r0 < 0 ? 0 : r0;
				r1 = r1 >= linhas ? linhas - 1 : r1;
				if (r0 > 0) {
					CLAY(CLAY_ID("esp-cima"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(r0 * ch) } } }) {}
				}
				for (int r = r0; r <= r1; r++) {
					CLAY(CLAY_IDI("grade-linha", (uint32_t)r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ch) }, .childGap = 0 } }) {
						for (int c = 0; c < colunas; c++) {
							int k = r * colunas + c;
							if (k >= nvis) {
								break;
							}
							celula_grade(vis[k], tam, cw, ch);
						}
					}
				}
				if (r1 < linhas - 1) {
					CLAY(CLAY_ID("esp-baixo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((linhas - 1 - r1) * ch) } } }) {}
				}
			} else {
				float ch = 44;
				int r0 = (int)(topo / ch) - 2, r1 = (int)((topo + alt_view) / ch) + 2;
				r0 = r0 < 0 ? 0 : r0;
				r1 = r1 >= nvis ? nvis - 1 : r1;
				if (r0 > 0) {
					CLAY(CLAY_ID("esp-cima"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(r0 * ch) } } }) {}
				}
				for (int k = r0; k <= r1; k++) {
					linha_lista(vis[k], k & 1);
				}
				if (r1 < nvis - 1) {
					CLAY(CLAY_ID("esp-baixo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((nvis - 1 - r1) * ch) } } }) {}
				}
			}
		}
	}
}

/* os cliques são tratados depois do desenho: o Clay já sabe onde cada um está */
static void tratar_cliques_conteudo(void) {
	bool fundo = gv_tocado(ID_CONTEUDO); /* primeiro: quem pede o aperto depois ganha, e o item vale mais que o fundo */
	bool algum = false;
	for (int k = 0; k < nvis; k++) {
		int idx = vis[k];
		Clay_ElementId id = modo_lista ? CLAY_IDI("lin", (uint32_t)idx) : CLAY_IDI("cel", (uint32_t)idx);
		if (gv_caixa(id).width > 0 && gv_tocado(id)) {
			tratar_clique(idx, id);
			algum = true;
		}
	}
	if (!algum && fundo && !(gv_modificadores() & (GV_MOD_CTRL | GV_MOD_SHIFT))) {
		limpar_selecao();
		ancora = foco = -1;
	}
}

/* ---- painel de detalhes ---- */

static char det_caminho[4096];
static int64_t det_bytes;
static int det_itens;
static bool det_completo;
static bool permissoes_abertas;

static void linha_detalhe(const char *rotulo, const char *valor) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(84) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(valor), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

static void painel_detalhes(void) {
	int n = n_selecionados();
	int idx = n == 1 ? primeiro_selecionado() : -1;
	ar_entrada pasta_atual;
	const ar_entrada *e = NULL;
	const char *caminho = NULL;
	if (idx >= 0) {
		e = &lista.v[idx];
		caminho = cam[idx];
	} else if (n == 0 && vista == V_PASTA && ar_entrada_de(pasta, &pasta_atual)) {
		e = &pasta_atual;
		caminho = pasta;
	}
	CLAY(CLAY_ID("detalhes"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(310), CLAY_SIZING_GROW(0) }, .padding = { 20, 20, 18, 14 }, .childGap = 14 },
		.border = { .color = gv_t.borda, .width = { .left = 1 } } }) {
		CLAY(CLAY_ID("det-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("det-titulo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(n > 1 ? gv_fmt("%d itens", n).chars : e ? (e == &pasta_atual ? nome_da_vista() : e->nome) : nome_da_vista()), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao_redondo(CLAY_ID("det-fechar"), "close", 32, false, false)) {
				detalhes = false;
			}
		}
		if (n > 1) {
			int64_t total = 0;
			int pastas = 0;
			for (int i = 0; i < lista.n; i++) {
				if (sel[i]) {
					total += lista.v[i].tamanho;
					pastas += lista.v[i].pasta;
				}
			}
			char t[32];
			ar_tamanho_humano(total, t, sizeof(t));
			CLAY(CLAY_ID("det-multi"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(150) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande) }) {
				gv_icone("stack", 56, gv_t.texto_suave);
			}
			linha_detalhe("Selecionados", gv_fmt("%d %s, %d %s", pastas, pastas == 1 ? "pasta" : "pastas", n - pastas, n - pastas == 1 ? "arquivo" : "arquivos").chars);
			linha_detalhe("Tamanho", t);
		} else if (e) {
			/* a prévia */
			CLAY(CLAY_ID("det-previa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(210) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
				if (e->tipo == AR_IMAGEM && caminho) {
					CLAY(CLAY_ID("det-img"), gv_imagem(CLAY_ID("det-img"), caminho, GV_AJUSTE_CONTER, (Clay_ElementDeclaration){
						.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) })) {}
				} else {
					ic_dados *d = &ic_pool[ic_n++ % (int)(sizeof(ic_pool) / sizeof(ic_pool[0]))];
					d->tipo = e->tipo;
					d->link = e->link;
					ar_selo(e, d->selo, sizeof(d->selo));
					CLAY(CLAY_ID("det-icone"), gv_livre(desenhar_icone, d, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(140), CLAY_SIZING_FIXED(140) } } })) {}
				}
			}
			CLAY(CLAY_ID("det-linhas"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
				linha_detalhe("Tipo", ar_tipo_nome(e));
				if (e->pasta && caminho) {
					if (strcmp(det_caminho, caminho)) {
						snprintf(det_caminho, sizeof(det_caminho), "%s", caminho);
						det_completo = ar_tamanho_pasta(caminho, 20000, &det_bytes, &det_itens);
					}
					int itens = ar_contar_itens(caminho, ocultos);
					linha_detalhe("Itens", itens >= 0 ? gv_fmt("%d %s", itens, itens == 1 ? "item" : "itens").chars : "—");
					char t[32];
					ar_tamanho_humano(det_bytes, t, sizeof(t));
					linha_detalhe("Tamanho", det_completo ? t : gv_fmt("mais de %s", t).chars);
				} else {
					char t[32];
					ar_tamanho_humano(e->tamanho, t, sizeof(t));
					linha_detalhe("Tamanho", t);
				}
				if (caminho) {
					char *pai = ar_pai(caminho);
					const char *p = pai ? pai : "/";
					size_t n = g_utf8_strlen(p, -1);
					linha_detalhe("Local", n > 22 ? gv_fmt("…%s", g_utf8_offset_to_pointer(p, (glong)n - 21)).chars : p);
					g_free(pai);
				}
				char data[48];
				ar_data_humana(e->mtime, time(NULL), data, sizeof(data));
				linha_detalhe("Modificado", data);
			}
			CLAY(CLAY_ID("det-div"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
			if (caminho) {
				bool fav = ar_eh_favorito(caminho);
				gv_botao_opcoes o = { .icone = "star", .texto = fav ? "Remover dos favoritos" : "Favoritar", .estilo = GV_BOTAO_SIMPLES, .crescer = true };
				if (gv_botao_ext(CLAY_ID("det-fav"), &o)) {
					ar_favorito_alternar(caminho);
					if (vista == V_FAVORITOS) {
						carregar(true);
					}
				}
			}
			/* as permissões, recolhidas */
			Clay_ElementId pid = CLAY_ID("det-perm");
			CLAY(pid, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("shield", 20, gv_t.texto_secundario);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Permissões"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				gv_icone(permissoes_abertas ? "chevron-up" : "chevron-down", 18, gv_t.texto_suave);
			}
			if (gv_tocado(pid)) {
				permissoes_abertas = !permissoes_abertas;
			}
			if (permissoes_abertas) {
				char p[16];
				ar_permissoes_texto(e->modo, p, sizeof(p));
				linha_detalhe("Modo", gv_fmt("%s (%04o)", p, (unsigned)(e->modo & 07777)).chars);
				linha_detalhe("Dono", e->uid == getuid() ? "Você" : gv_fmt("usuário %u", (unsigned)e->uid).chars);
			}
		}
	}
}

/* ---- barra de status ---- */

static void barra_status(void) {
	CLAY(CLAY_ID("status"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 20, 20, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		const char *texto;
		bool dentro = aviso[0] && gv_agora() < aviso_ate;
		int n = n_selecionados();
		if (dentro) {
			texto = aviso;
		} else if (n > 0) {
			int pastas = 0;
			for (int i = 0; i < lista.n; i++) {
				pastas += sel[i] && lista.v[i].pasta;
			}
			if (n == 1) {
				texto = gv_fmt("%d %s · 1 %s selecionada", nvis, nvis == 1 ? "item" : "itens", pastas ? "pasta" : "arquivo").chars;
				if (!pastas) {
					texto = gv_fmt("%d %s · 1 arquivo selecionado", nvis, nvis == 1 ? "item" : "itens").chars;
				}
			} else {
				texto = gv_fmt("%d itens · %d selecionados", nvis, n).chars;
			}
		} else {
			texto = gv_fmt("%d %s", nvis, nvis == 1 ? "item" : "itens").chars;
		}
		CLAY(CLAY_ID("status-texto"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, dentro ? gv_t.aviso : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (!modo_lista) {
			gv_icone("minus", 16, gv_t.texto_suave);
			CLAY(CLAY_ID("zoom-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(120) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_deslizante(CLAY_ID("zoom"), &zoom, 5, 16);
			}
			gv_icone("plus", 16, gv_t.texto_suave);
		}
		bool c;
		botao_barra(CLAY_ID("s-grade"), "grid", !modo_lista, false, &c);
		if (c) {
			modo_lista = false;
		}
		botao_barra(CLAY_ID("s-lista"), "list", modo_lista, false, &c);
		if (c) {
			modo_lista = true;
		}
	}
}

/* ---- menus de contexto ---- */

static void menus_de_contexto(void) {
	GV_MENU_PONTEIRO(CLAY_ID("menu-item")) {
		CLAY(CLAY_ID("mi-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			int i = menu_item;
			if (i >= 0 && i < lista.n) {
				if (gv_menu_item(CLAY_ID("mi-abrir"), lista.v[i].pasta ? "folder" : "external", "Abrir", "Enter")) {
					abrir_item(i);
					gv_popup_fechar(CLAY_ID("menu-item"));
				}
				bool fav = ar_eh_favorito(cam[i]);
				if (gv_menu_item(CLAY_ID("mi-fav"), "star", fav ? "Remover dos favoritos" : "Favoritar", NULL)) {
					ar_favorito_alternar(cam[i]);
					if (vista == V_FAVORITOS) {
						carregar(true);
					}
					gv_popup_fechar(CLAY_ID("menu-item"));
				}
				if (gv_menu_item(CLAY_ID("mi-copiar"), "copy", "Copiar caminho", NULL)) {
					gv_copiar(cam[i]);
					avisar("Caminho copiado");
					gv_popup_fechar(CLAY_ID("menu-item"));
				}
			}
		}
	}
	GV_MENU_PONTEIRO(CLAY_ID("menu-fundo")) {
		CLAY(CLAY_ID("mf-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			if (vista == V_PASTA && gv_menu_item(CLAY_ID("mf-pasta"), "folder-plus", "Nova pasta", NULL)) {
				novo_tipo = 1;
				focar_nome = true;
				snprintf(novo_nome, sizeof(novo_nome), "Nova pasta");
				gv_popup_fechar(CLAY_ID("menu-fundo"));
				gv_popup_abrir(CLAY_ID("menu-novo"));
			}
			if (gv_menu_item(CLAY_ID("mf-atualizar"), "refresh", "Atualizar", NULL)) {
				carregar(true);
				gv_popup_fechar(CLAY_ID("menu-fundo"));
			}
			if (vista == V_PASTA && gv_menu_item(CLAY_ID("mf-copiar"), "copy", "Copiar caminho da pasta", NULL)) {
				gv_copiar(pasta);
				avisar("Caminho copiado");
				gv_popup_fechar(CLAY_ID("menu-fundo"));
			}
		}
	}
}

/* ---- teclado ---- */

static void mover_foco(int dk) {
	if (!nvis) {
		return;
	}
	int atual = foco >= 0 ? pos_em_vis(foco) : -1;
	int novo = atual < 0 ? (dk > 0 ? 0 : nvis - 1) : atual + dk;
	novo = novo < 0 ? 0 : novo >= nvis ? nvis - 1 : novo;
	uint32_t mods = gv_modificadores();
	int idx = vis[novo];
	if (mods & GV_MOD_SHIFT && ancora >= 0) {
		clicar_item(idx, GV_MOD_SHIFT);
		foco = idx;
	} else {
		limpar_selecao();
		sel[idx] = true;
		ancora = foco = idx;
	}
	/* mantém o item à vista */
	float ch = modo_lista ? 44 : (72 + zoom * 80) + 14 + 10 + 6 + (gv_t.tamanho_corpo + 6) * 2 + 20;
	int linha = modo_lista ? novo : novo / colunas;
	float topo = gv_rolagem_posicao(ID_CONTEUDO).y, alt = gv_caixa(ID_CONTEUDO).height;
	if (linha * ch < topo) {
		gv_rolar_para(ID_CONTEUDO, 0, linha * ch, true);
	} else if ((linha + 1) * ch > topo + alt - 12) {
		gv_rolar_para(ID_CONTEUDO, 0, (linha + 1) * ch - alt + 12, true);
	}
}

static void teclado_atalhos(void) {
	bool digitando = gv_focado(CLAY_ID("busca")) || gv_focado(CLAY_ID("mn-nome"));
	uint32_t mods = gv_modificadores();
	for (int i = 0;; i++) {
		uint32_t k = gv_tecla_n(i);
		if (!k) {
			break;
		}
		if (mods & GV_MOD_ALT) {
			if (k == XKB_KEY_Left) {
				voltar();
			} else if (k == XKB_KEY_Right) {
				avancar();
			} else if (k == XKB_KEY_Up) {
				subir();
			}
			continue;
		}
		if (mods & GV_MOD_CTRL) {
			if (k == XKB_KEY_f) {
				gv_focar(CLAY_ID("busca"));
			} else if (k == XKB_KEY_h && !digitando) {
				ocultos = !ocultos;
				carregar(true);
			} else if (k == XKB_KEY_a && !digitando) {
				for (int j = 0; j < nvis; j++) {
					sel[vis[j]] = true;
				}
			} else if (k == XKB_KEY_r) {
				carregar(true);
			}
			continue;
		}
		if (k == XKB_KEY_Escape) {
			if (busca[0]) {
				busca[0] = '\0';
				refiltrar();
			} else {
				limpar_selecao();
			}
			continue;
		}
		if (digitando) {
			continue;
		}
		int passo = colunas > 0 && !modo_lista ? colunas : 1;
		switch (k) {
		case XKB_KEY_Left: mover_foco(-1); break;
		case XKB_KEY_Right: mover_foco(1); break;
		case XKB_KEY_Up: mover_foco(-(modo_lista ? 1 : passo)); break;
		case XKB_KEY_Down: mover_foco(modo_lista ? 1 : passo); break;
		case XKB_KEY_Home: foco = -1; mover_foco(1); break;
		case XKB_KEY_End: foco = -1; mover_foco(-1); break;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			for (int j = 0; j < lista.n; j++) {
				if (sel[j]) {
					abrir_item(j);
					break;
				}
			}
			break;
		case XKB_KEY_BackSpace: subir(); break;
		default: break;
		}
	}
}


/* Em teste (LUMNA_ARQUIVOS_DEBUG=1): onde está cada coisa (em pixels da janela), para os testes clicarem no lugar certo. Só escreve o que mudou. */
static void debug_posicoes(void) {
	static int ligado = -1;
	if (ligado < 0) {
		ligado = getenv("LUMNA_ARQUIVOS_DEBUG") != NULL;
	}
	if (!ligado) {
		return;
	}
	static GHashTable *antes;
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	GPtrArray *ids = g_ptr_array_new();
	static const char *const FIXOS[] = { "t-voltar", "t-avancar", "t-subir", "busca", "t-novo", "t-ordem", "t-grade", "t-lista", "t-ajustes", "l-inicio", "l-recentes", "l-favoritos",
		"det-fav", "det-fechar", "zoom", "s-grade", "s-lista", "mn-pasta", "mn-nome", "mn-criar", "mi-abrir", "mi-fav", "mi-copiar", "mo-o", "mf-pasta", "mf-atualizar", NULL };
	for (int i = 0; FIXOS[i]; i++) {
		g_ptr_array_add(nomes, g_strdup(FIXOS[i]));
		Clay_String cs = { .chars = FIXOS[i], .length = (int)strlen(FIXOS[i]) };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashString(cs, 0).id));
	}
	for (int i = 0; i < 6; i++) {
		g_ptr_array_add(nomes, g_strdup_printf("filtro/%d", i));
		Clay_String cs = { .chars = "filtro", .length = 6 };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)i, 0).id));
	}
	for (int i = 0; i < 4; i++) {
		g_ptr_array_add(nomes, g_strdup_printf("mo-o/%d", i));
		Clay_String cs = { .chars = "mo-o", .length = 4 };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)i, 0).id));
	}
	for (int i = 0; i < 4; i++) {
		g_ptr_array_add(nomes, g_strdup_printf("parte/%d", i));
		Clay_String cs = { .chars = "parte", .length = 5 };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)i, 0).id));
	}
	for (int k = 0; k < nvis; k++) {
		int idx = vis[k];
		g_ptr_array_add(nomes, g_strdup_printf("item/%s", lista.v[idx].nome));
		Clay_String cs = { .chars = modo_lista ? "lin" : "cel", .length = 3 };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)idx, 0).id));
	}
	for (guint i = 0; i < nomes->len; i++) {
		Clay_BoundingBox b = gv_caixa((Clay_ElementId){ .id = GPOINTER_TO_UINT(ids->pdata[i]) });
		if (b.width <= 0) {
			continue;
		}
		char *linha = g_strdup_printf("%.0f %.0f %.0f %.0f", b.x, b.y, b.width, b.height);
		char *antigo = g_hash_table_lookup(antes, nomes->pdata[i]);
		if (!antigo || strcmp(antigo, linha)) {
			printf("pos %s %s\n", (char *)nomes->pdata[i], linha);
			g_hash_table_insert(antes, g_strdup(nomes->pdata[i]), linha);
		} else {
			g_free(linha);
		}
	}
	char *estado = g_strdup_printf("vista=%d pasta=%s nvis=%d sel=%d busca=[%s] ocultos=%d ordem=%d%s lista=%d filtro=%d", vista, vista == V_PASTA ? pasta : "-", nvis, n_selecionados(),
		busca, ocultos, ordem, decrescente ? "-" : "+", modo_lista, filtro);
	static char *estado_antes;
	if (!estado_antes || strcmp(estado_antes, estado)) {
		printf("estado %s\n", estado);
		g_free(estado_antes);
		estado_antes = estado;
	} else {
		g_free(estado);
	}
	/* a ordem em que os itens aparecem */
	GString *ordem_txt = g_string_new("ordem");
	for (int k = 0; k < nvis && k < 40; k++) {
		g_string_append_printf(ordem_txt, " %s%s", lista.v[vis[k]].nome, sel[vis[k]] ? "*" : "");
	}
	static char *ordem_antes;
	if (!ordem_antes || strcmp(ordem_antes, ordem_txt->str)) {
		printf("%s\n", ordem_txt->str);
		g_free(ordem_antes);
		ordem_antes = g_strdup(ordem_txt->str);
	}
	g_string_free(ordem_txt, TRUE);
	fflush(stdout);
	g_ptr_array_free(nomes, TRUE);
	g_ptr_array_free(ids, TRUE);
}

/* ---- a janela ---- */

static void desenhar(gv_app *a, void *dados) {
	ic_n = 0;
	teclado_atalhos();
	CLAY(CLAY_ID("janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_barra_janela(a, CLAY_ID("barra"), "folder", "LUMNA Explorer");
		barra_ferramentas();
		CLAY(CLAY_ID("corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			barra_lateral();
			conteudo();
			if (detalhes && lista.n >= 0) {
				painel_detalhes();
			}
		}
		barra_status();
	}
	menus_de_contexto();
	tratar_cliques_conteudo();
	debug_posicoes();
}

int main(int argc, char **argv) {
	char inicio[4096];
	snprintf(inicio, sizeof(inicio), "%s", argc > 1 && g_file_test(argv[1], G_FILE_TEST_IS_DIR) ? argv[1] : g_get_home_dir());
	char *real = realpath(inicio, NULL);
	if (real) {
		snprintf(inicio, sizeof(inicio), "%s", real);
		free(real);
	}
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Arquivos", .app_id = "lumna-arquivos", .largura = 1240, .altura = 760 });
	if (!app) {
		return 1;
	}
	inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (inotify_fd >= 0) {
		gv_app_vigiar(app, inotify_fd, inotify_pronto, NULL);
	}
	ir_para(inicio, true);
	int r = gv_app_correr(app, desenhar, NULL);
	liberar_lista();
	ar_hist_liberar(&hist);
	return r;
}
