/* O editor de fotos: a prévia à esquerda, as abas Ajustar / Recortar / Girar / Filtros embaixo e o painel à direita. A conta é a de servicos/edicao.c
 * (a mesma da cópia final); aqui só ficam a tela, o desfazer/refazer e as threads: a prévia é refeita fora do quadro (a mais recente vence) e,
 * enquanto se arrasta um deslizante, numa foto menor; ao soltar, na prévia nítida. */
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "edicao.h"

#define HIST_MAX 100
#define BASE_MAX 1600    /* o lado maior da prévia nítida */
#define RAPIDA_MAX 640   /* e o da que se mostra enquanto se arrasta */
#define MINI_LADO 132    /* a miniatura dos filtros */

enum { A_AJUSTAR, A_RECORTAR, A_GIRAR, A_FILTROS };
enum { C_EXPO, C_CONTRASTE, C_REALCES, C_SOMBRAS, C_TEMP, C_SAT, C_INTENS, C_N };

static const struct { const char *nome; float v; } PROPS[] = {
	{ "Livre", 0 }, { "Original", -1 }, { "1:1", 1 }, { "4:3", 4.0f / 3 }, { "3:2", 3.0f / 2 }, { "16:9", 16.0f / 9 }, { "3:4", 3.0f / 4 }, { "2:3", 2.0f / 3 }, { "9:16", 9.0f / 16 },
};
#define N_PROPS ((int)G_N_ELEMENTS(PROPS))

static struct {
	unsigned sessao;                 /* muda ao abrir e ao fechar: o que chega de uma sessão velha é jogado fora */
	bool ativo, carregando;
	char caminho[4096], nome[256];
	time_t data;
	int voltar_para;
	GdkPixbuf *base, *rapida, *mini; /* a foto (já com a orientação do EXIF) em três tamanhos */
	cairo_surface_t *original;       /* ela como abriu: para "Comparar original" */
	int ow, oh;                      /* o tamanho real da foto, com a orientação */
	ed_params p;
	ed_params hist[HIST_MAX];
	int nh, ih;                      /* hist[0..nh-1]; a posição de agora é ih */
	int aba;
	cairo_surface_t *prev;           /* a prévia pronta */
	bool prev_sem_recorte;           /* ela foi feita sem o recorte (a aba Recortar) */
	bool renderizando, quer_rapido;
	unsigned seq;                    /* o pedido mais novo */
	bool comparar;
	float zoom, pan_x, pan_y;
	int prop;                        /* o índice em PROPS */
	bool prop_inv;
	bool luz_aberta, cor_aberta;
	cairo_surface_t *fmini[ED_F_N];  /* os filtros aplicados na miniatura */
	ed_params fmini_chave;
	bool fmini_ok;
	bool salvando, dlg_sair;
	int slider_ativo;
	int alca;                        /* o arrasto do recorte: a alça (-1 nada) */
	Clay_Vector2 ultimo;
	Clay_Vector2 pan_ultimo;
	bool pan_ativo;
} E = { .luz_aberta = true, .cor_aberta = true, .slider_ativo = -1, .alca = -1 };

static unsigned seq_global;

/* ---- as contas ---- */

static float get(int c) {
	switch (c) {
	case C_EXPO: return E.p.exposicao;
	case C_CONTRASTE: return (float)E.p.contraste;
	case C_REALCES: return (float)E.p.realces;
	case C_SOMBRAS: return (float)E.p.sombras;
	case C_TEMP: return (float)E.p.temperatura;
	case C_SAT: return (float)E.p.saturacao;
	default: return (float)E.p.intensidade;
	}
}

static void set(int c, float v) {
	switch (c) {
	case C_EXPO: E.p.exposicao = v; break;
	case C_CONTRASTE: E.p.contraste = (int)lroundf(v); break;
	case C_REALCES: E.p.realces = (int)lroundf(v); break;
	case C_SOMBRAS: E.p.sombras = (int)lroundf(v); break;
	case C_TEMP: E.p.temperatura = (int)lroundf(v); break;
	case C_SAT: E.p.saturacao = (int)lroundf(v); break;
	default: E.p.intensidade = (int)lroundf(v); break;
	}
}

static void faixa(int c, float *min, float *max) {
	*min = c == C_EXPO ? -3 : c == C_INTENS ? 0 : -100;
	*max = c == C_EXPO ? 3 : 100;
}

static void geo_dims(int *W, int *H) {
	*W = (E.p.giro & 1) ? E.oh : E.ow;
	*H = (E.p.giro & 1) ? E.ow : E.oh;
}

static float prop_atual(void) {
	float v = PROPS[E.prop].v;
	if (E.prop == 0) {
		return 0;
	}
	if (v < 0) {
		int W, H;
		geo_dims(&W, &H);
		v = (float)W / (float)H;
	}
	return E.prop_inv ? 1 / v : v;
}

/* ---- da pixbuf para o cairo ---- */

cairo_surface_t *superficie_de(GdkPixbuf *pb) {
	int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), nc = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		return s;
	}
	cairo_surface_flush(s);
	unsigned char *d = cairo_image_surface_get_data(s);
	int ds = cairo_image_surface_get_stride(s);
	const guchar *src = gdk_pixbuf_get_pixels(pb);
	for (int y = 0; y < h; y++) {
		const guchar *l = src + (gsize)y * (gsize)rs;
		uint32_t *o = (uint32_t *)(d + (gsize)y * (gsize)ds);
		for (int x = 0; x < w; x++, l += nc) {
			unsigned a = nc == 4 ? l[3] : 255;
			if (a == 255) {
				o[x] = 0xff000000u | ((uint32_t)l[0] << 16) | ((uint32_t)l[1] << 8) | l[2];
			} else {
				o[x] = ((uint32_t)a << 24) | ((uint32_t)(l[0] * a / 255) << 16) | ((uint32_t)(l[1] * a / 255) << 8) | (uint32_t)(l[2] * a / 255);
			}
		}
	}
	cairo_surface_mark_dirty(s);
	return s;
}

static GdkPixbuf *reduzir(GdkPixbuf *pb, int lado) {
	int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
	if (w <= lado && h <= lado) {
		return g_object_ref(pb);
	}
	float f = (float)lado / (float)MAX(w, h);
	return gdk_pixbuf_scale_simple(pb, MAX(1, (int)lroundf((float)w * f)), MAX(1, (int)lroundf((float)h * f)), GDK_INTERP_BILINEAR);
}

/* ---- a prévia, numa thread (a mais recente vence) ---- */

typedef struct {
	unsigned sessao, seq;
	ed_params p;
	GdkPixbuf *base;
	bool sem_recorte;
	cairo_surface_t *sup;
} render;

static void render_iniciar(void);

static void render_pronto(void *d) {
	render *r = d;
	E.renderizando = false;
	if (r->sessao == E.sessao && E.ativo) {
		if (E.prev) {
			cairo_surface_destroy(E.prev);
		}
		E.prev = r->sup;
		r->sup = NULL;
		E.prev_sem_recorte = r->sem_recorte;
		gv_app_redesenhar(G.app);
	}
	if (r->sup) {
		cairo_surface_destroy(r->sup);
	}
	g_object_unref(r->base);
	bool de_novo = E.ativo && E.base && (r->sessao != E.sessao || r->seq != E.seq);
	g_free(r);
	if (de_novo) {
		render_iniciar();
	}
}

static gpointer render_thread(gpointer d) {
	render *r = d;
	GdkPixbuf *o = ed_processar(r->base, &r->p, !r->sem_recorte);
	r->sup = superficie_de(o);
	g_object_unref(o);
	gv_na_linha_principal(render_pronto, r);
	return NULL;
}

static void render_iniciar(void) {
	render *r = g_new0(render, 1);
	r->sessao = E.sessao;
	r->seq = E.seq;
	r->p = E.p;
	r->base = g_object_ref(E.quer_rapido ? E.rapida : E.base);
	r->sem_recorte = E.aba == A_RECORTAR;
	E.renderizando = true;
	g_thread_unref(g_thread_new("galeria-edicao", render_thread, r));
}

static void pedir(bool rapido) {
	if (!E.base) {
		return;
	}
	E.seq = ++seq_global;
	E.quer_rapido = rapido;
	if (!E.renderizando) {
		render_iniciar();
	}
}

/* ---- o desfazer ---- */

static void commit(void) {
	if (E.nh > 0 && ed_igual(&E.p, &E.hist[E.ih])) {
		return;
	}
	E.nh = E.ih + 1; /* o que se tinha refeito some */
	if (E.nh == HIST_MAX) {
		memmove(&E.hist[0], &E.hist[1], sizeof(E.hist[0]) * (HIST_MAX - 1));
		E.nh--;
	}
	E.hist[E.nh] = E.p;
	E.ih = E.nh;
	E.nh++;
}

static void restaurar_passo(int ih) {
	E.ih = ih;
	E.p = E.hist[ih];
	E.prop = 0;
	E.prop_inv = false;
	pedir(false);
}

static void desfazer(void) {
	if (E.ih > 0) {
		restaurar_passo(E.ih - 1);
	}
}

static void refazer(void) {
	if (E.ih + 1 < E.nh) {
		restaurar_passo(E.ih + 1);
	}
}

/* ---- abrir e fechar ---- */

typedef struct {
	unsigned sessao;
	char caminho[4096];
	GdkPixbuf *base, *rapida, *mini;
	cairo_surface_t *original;
	int ow, oh;
	char *erro;
} carga_ed;

static void limpar_minis(void) {
	for (int i = 0; i < ED_F_N; i++) {
		if (E.fmini[i]) {
			cairo_surface_destroy(E.fmini[i]);
			E.fmini[i] = NULL;
		}
	}
	E.fmini_ok = false;
}

static void liberar(void) {
	if (E.base) {
		g_object_unref(E.base);
	}
	if (E.rapida) {
		g_object_unref(E.rapida);
	}
	if (E.mini) {
		g_object_unref(E.mini);
	}
	E.base = E.rapida = E.mini = NULL;
	if (E.original) {
		cairo_surface_destroy(E.original);
	}
	if (E.prev) {
		cairo_surface_destroy(E.prev);
	}
	E.original = E.prev = NULL;
	limpar_minis();
}

static void fechar(void) {
	E.ativo = false;
	E.sessao++;
	liberar();
	G.tela = E.voltar_para == T_VISUALIZADOR && G.aberta >= 0 && G.aberta < G.nvis ? T_VISUALIZADOR : T_BIBLIOTECA;
}

static void carga_pronta(void *d) {
	carga_ed *c = d;
	if (c->sessao == E.sessao && E.ativo) {
		E.carregando = false;
		if (c->erro) {
			aviso("Não deu para abrir esta foto para editar.");
			fechar();
		} else {
			E.base = c->base;
			E.rapida = c->rapida;
			E.mini = c->mini;
			E.original = c->original;
			E.ow = c->ow;
			E.oh = c->oh;
			c->base = c->rapida = c->mini = NULL;
			c->original = NULL;
			pedir(false);
		}
		gv_app_redesenhar(G.app);
	}
	if (c->base) {
		g_object_unref(c->base);
	}
	if (c->rapida) {
		g_object_unref(c->rapida);
	}
	if (c->mini) {
		g_object_unref(c->mini);
	}
	if (c->original) {
		cairo_surface_destroy(c->original);
	}
	g_free(c->erro);
	g_free(c);
}

static gpointer carga_thread(gpointer d) {
	carga_ed *c = d;
	GError *e = NULL;
	c->base = ed_abrir(c->caminho, BASE_MAX, &e);
	if (!c->base) {
		c->erro = g_strdup(e ? e->message : "erro");
		g_clear_error(&e);
	} else {
		c->rapida = reduzir(c->base, RAPIDA_MAX);
		c->mini = reduzir(c->base, MINI_LADO);
		c->original = superficie_de(c->base);
		int fw = 0, fh = 0;
		bool tem = gdk_pixbuf_get_file_info(c->caminho, &fw, &fh) && fw > 0 && fh > 0;
		int bw = gdk_pixbuf_get_width(c->base), bh = gdk_pixbuf_get_height(c->base);
		if (tem && ((fw > fh) != (bw > bh)) && fw != fh && bw != bh) { /* o EXIF girou a foto: o tamanho dela é o de lado */
			int t = fw;
			fw = fh;
			fh = t;
		}
		c->ow = tem ? fw : bw;
		c->oh = tem ? fh : bh;
	}
	gv_na_linha_principal(carga_pronta, c);
	return NULL;
}

void editor_abrir(const gl_midia *m, int voltar_para) {
	liberar();
	E.sessao++;
	E.ativo = true;
	E.carregando = true;
	E.renderizando = false;
	g_strlcpy(E.caminho, m->caminho, sizeof(E.caminho));
	g_strlcpy(E.nome, m->nome, sizeof(E.nome));
	E.data = m->data;
	E.voltar_para = voltar_para;
	ed_padrao(&E.p);
	E.hist[0] = E.p;
	E.nh = 1;
	E.ih = 0;
	E.aba = A_AJUSTAR;
	E.comparar = false;
	E.zoom = 1;
	E.pan_x = E.pan_y = 0;
	E.prop = 0;
	E.prop_inv = false;
	E.salvando = false;
	E.dlg_sair = false;
	E.slider_ativo = -1;
	E.alca = -1;
	E.pan_ativo = false;
	G.tela = T_EDITOR;
	carga_ed *c = g_new0(carga_ed, 1);
	c->sessao = E.sessao;
	g_strlcpy(c->caminho, m->caminho, sizeof(c->caminho));
	g_thread_unref(g_thread_new("galeria-editor-abrir", carga_thread, c));
}

/* ---- salvar a cópia, numa thread ---- */

typedef struct {
	unsigned sessao;
	char caminho[4096];
	ed_params p;
	int formato, qualidade;
	time_t data;
	char *destino, *erro;
} salvar_ed;

static void salvo_pronto(void *d) {
	salvar_ed *s = d;
	bool nossa = s->sessao == E.sessao && E.ativo;
	if (nossa) {
		E.salvando = false;
	}
	if (s->destino) {
		char *nome = g_path_get_basename(s->destino);
		aviso("Cópia salva: %s", nome);
		g_free(nome);
		recarregar_biblioteca();
		if (nossa) {
			fechar();
		}
	} else {
		aviso("Não deu para salvar a cópia: %s", s->erro ? s->erro : "erro");
	}
	gv_app_redesenhar(G.app);
	g_free(s->destino);
	g_free(s->erro);
	g_free(s);
}

static gpointer salvar_thread(gpointer d) {
	salvar_ed *s = d;
	GError *e = NULL;
	s->destino = ed_salvar_copia(s->caminho, &s->p, s->formato, s->qualidade, s->data, &e);
	if (!s->destino) {
		s->erro = g_strdup(e ? e->message : "erro");
	}
	g_clear_error(&e);
	gv_na_linha_principal(salvo_pronto, s);
	return NULL;
}

static void salvar(void) {
	if (E.salvando || E.carregando || !ed_alterado(&E.p)) {
		return;
	}
	E.salvando = true;
	salvar_ed *s = g_new0(salvar_ed, 1);
	s->sessao = E.sessao;
	g_strlcpy(s->caminho, E.caminho, sizeof(s->caminho));
	s->p = E.p;
	s->formato = G.cfg.formato;
	s->qualidade = G.cfg.qualidade_jpeg;
	s->data = E.data;
	g_thread_unref(g_thread_new("galeria-editor-salvar", salvar_thread, s));
}

static void sair(void) {
	if (E.salvando) {
		return;
	}
	if (ed_alterado(&E.p)) {
		E.dlg_sair = true;
		return;
	}
	fechar();
}

/* ---- o desenho da prévia ---- */

static void caminho_redondo(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
	cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
	cairo_close_path(cr);
}

#define MARGEM 24.0f

/* onde a foto cai na caixa da prévia (com o zoom e o arrasto) */
static bool retangulo_da_foto(Clay_BoundingBox cx, cairo_surface_t *s, float *x, float *y, float *w, float *h) {
	if (!s || cx.width <= 0) {
		return false;
	}
	float iw = (float)cairo_image_surface_get_width(s), ih = (float)cairo_image_surface_get_height(s);
	float aw = cx.width - 2 * MARGEM, ah = cx.height - 2 * MARGEM;
	if (aw < 10 || ah < 10 || iw < 1 || ih < 1) {
		return false;
	}
	float fit = fminf(aw / iw, ah / ih);
	*w = iw * fit * E.zoom;
	*h = ih * fit * E.zoom;
	float lim_x = fmaxf(0, (*w - aw) / 2), lim_y = fmaxf(0, (*h - ah) / 2);
	float px = CLAMP(E.pan_x, -lim_x, lim_x), py = CLAMP(E.pan_y, -lim_y, lim_y);
	*x = cx.x + cx.width / 2 - *w / 2 + px;
	*y = cx.y + cx.height / 2 - *h / 2 + py;
	return true;
}

static void desenhar_previa(cairo_t *cr, Clay_BoundingBox cx, void *dados) {
	(void)dados;
	cairo_surface_t *s = E.comparar && E.original ? E.original : E.prev;
	float x, y, w, h;
	if (!retangulo_da_foto(cx, s, &x, &y, &w, &h)) {
		return;
	}
	float aw = cx.width - 2 * MARGEM, ah = cx.height - 2 * MARGEM;
	cairo_save(cr);
	cairo_rectangle(cr, cx.x + MARGEM, cx.y + MARGEM, aw, ah); /* o zoom não passa da moldura */
	cairo_clip(cr);
	cairo_save(cr);
	caminho_redondo(cr, x, y, w, h, 6);
	cairo_clip(cr);
	cairo_translate(cr, x, y);
	cairo_scale(cr, w / cairo_image_surface_get_width(s), h / cairo_image_surface_get_height(s));
	cairo_set_source_surface(cr, s, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_restore(cr);
	/* a aba Recortar: a foto toda, o resto escurecido, a grade dos terços e as alças */
	if (E.aba == A_RECORTAR && !E.comparar && E.prev_sem_recorte) {
		float rx = x + E.p.recorte[0] * w, ry = y + E.p.recorte[1] * h, rw = E.p.recorte[2] * w, rh = E.p.recorte[3] * h;
		cairo_save(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.58);
		cairo_rectangle(cr, x, y, w, h);
		cairo_rectangle(cr, rx, ry, rw, rh);
		cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
		cairo_set_line_width(cr, 1);
		for (int i = 1; i < 3; i++) {
			cairo_move_to(cr, rx + rw * (float)i / 3, ry);
			cairo_line_to(cr, rx + rw * (float)i / 3, ry + rh);
			cairo_move_to(cr, rx, ry + rh * (float)i / 3);
			cairo_line_to(cr, rx + rw, ry + rh * (float)i / 3);
		}
		cairo_stroke(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
		cairo_set_line_width(cr, 1.5);
		cairo_rectangle(cr, rx, ry, rw, rh);
		cairo_stroke(cr);
		/* os cantos em L e as barrinhas do meio */
		cairo_set_line_width(cr, 4);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		float L = fminf(18, fminf(rw, rh) / 3);
		const float cantos[4][4] = { { rx, ry, 1, 1 }, { rx + rw, ry, -1, 1 }, { rx + rw, ry + rh, -1, -1 }, { rx, ry + rh, 1, -1 } };
		for (int i = 0; i < 4; i++) {
			cairo_move_to(cr, cantos[i][0] + cantos[i][2] * L, cantos[i][1]);
			cairo_line_to(cr, cantos[i][0], cantos[i][1]);
			cairo_line_to(cr, cantos[i][0], cantos[i][1] + cantos[i][3] * L);
		}
		float B = fminf(12, fminf(rw, rh) / 6);
		cairo_move_to(cr, rx + rw / 2 - B, ry);
		cairo_line_to(cr, rx + rw / 2 + B, ry);
		cairo_move_to(cr, rx + rw / 2 - B, ry + rh);
		cairo_line_to(cr, rx + rw / 2 + B, ry + rh);
		cairo_move_to(cr, rx, ry + rh / 2 - B);
		cairo_line_to(cr, rx, ry + rh / 2 + B);
		cairo_move_to(cr, rx + rw, ry + rh / 2 - B);
		cairo_line_to(cr, rx + rw, ry + rh / 2 + B);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
}

static void desenhar_miniatura(cairo_t *cr, Clay_BoundingBox cx, void *dados) {
	cairo_surface_t *s = E.fmini[(intptr_t)dados];
	if (!s) {
		return;
	}
	double iw = cairo_image_surface_get_width(s), ih = cairo_image_surface_get_height(s);
	double f = fmax(cx.width / iw, cx.height / ih);
	cairo_save(cr);
	caminho_redondo(cr, cx.x, cx.y, cx.width, cx.height, 8);
	cairo_clip(cr);
	cairo_translate(cr, cx.x + (cx.width - iw * f) / 2, cx.y + (cx.height - ih * f) / 2);
	cairo_scale(cr, f, f);
	cairo_set_source_surface(cr, s, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* ---- as miniaturas dos filtros ---- */

static void atualizar_minis(void) {
	if (!E.mini) {
		return;
	}
	ed_params chave = E.p;
	chave.filtro = 0;
	chave.intensidade = 100;
	if (E.fmini_ok && ed_igual(&chave, &E.fmini_chave)) {
		return;
	}
	limpar_minis();
	for (int f = 0; f < ED_F_N; f++) {
		ed_params q = E.p;
		q.filtro = f;
		q.intensidade = 100;
		GdkPixbuf *o = ed_processar(E.mini, &q, true);
		E.fmini[f] = superficie_de(o);
		g_object_unref(o);
	}
	E.fmini_chave = chave;
	E.fmini_ok = true;
}

/* ---- o interior ---- */

static const char *valor_texto(int c) {
	float v = get(c);
	if (c == C_EXPO) {
		if (fabsf(v) < 0.05f) {
			return "0";
		}
		char *s = g_strdup_printf("%s%.1f", v > 0.04f ? "+" : v < -0.04f ? "\xe2\x88\x92" : "", fabsf(v) < 0.05f ? 0 : fabsf(v));
		for (char *p = s; *p; p++) {
			if (*p == '.') {
				*p = ',';
			}
		}
		const char *r = gv_fmt("%s", s).chars;
		g_free(s);
		return r;
	}
	if (c == C_INTENS) {
		return gv_fmt("%d%%", (int)v).chars;
	}
	int n = (int)v;
	return gv_fmt("%s%d", n > 0 ? "+" : n < 0 ? "\xe2\x88\x92" : "", abs(n)).chars;
}

static void linha_deslizante(int c, const char *rotulo) {
	float min, max;
	faixa(c, &min, &max);
	CLAY(CLAY_IDI("ed-lin", (uint32_t)c), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(42) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_IDI("ed-rot", (uint32_t)c), { .layout = { .sizing = { CLAY_SIZING_FIXED(96) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("ed-slc", (uint32_t)c), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			float v01 = (get(c) - min) / (max - min);
			if (gv_deslizante(CLAY_IDI("ed-sl", (uint32_t)c), &v01, 4, 14)) {
				float v = min + v01 * (max - min);
				v = c == C_EXPO ? roundf(v * 10) / 10 : roundf(v);
				if (c != C_INTENS && fabsf(v) < (c == C_EXPO ? 0.15f : 3)) {
					v = 0; /* o meio gruda: dá para voltar a zero sem errar */
				}
				if (v != get(c)) {
					set(c, v);
					pedir(true);
				}
				E.slider_ativo = c;
			}
		}
		Clay_ElementId chip = CLAY_IDI("ed-chip", (uint32_t)c);
		CLAY(chip, { .layout = { .sizing = { CLAY_SIZING_FIXED(58), CLAY_SIZING_FIXED(30) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(chip) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
			.border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(gv_str(valor_texto(c)), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
		}
		if (gv_tocado(chip) && get(c) != (c == C_INTENS ? 100.0f : 0.0f)) { /* tocar no valor zera (a intensidade volta a 100) */
			set(c, c == C_INTENS ? 100 : 0);
			commit();
			pedir(false);
		}
	}
}

static bool cabecalho_secao(Clay_ElementId id, const char *icone, const char *titulo, bool aberta) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(42) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		gv_icone(icone, 22, gv_t.texto_secundario);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
		}
		gv_icone(aberta ? "chevron-up" : "chevron-down", 20, gv_t.texto_secundario);
	}
	return gv_tocado(id);
}

static bool botao_painel(Clay_ElementId id, const char *icone, const char *texto, bool ligado) {
	return gv_botao_ext(id, &(gv_botao_opcoes){ .icone = icone, .texto = texto, .estilo = ligado ? GV_BOTAO_CHEIO : GV_BOTAO_CONTORNO, .altura = 44, .crescer = true });
}

static void painel_ajustar(void) {
	gv_texto(CLAY_STRING("Ajustar foto"), gv_t.tamanho_destaque + 4, gv_t.texto, GV_PESO_NORMAL);
	gv_separador(false, 0);
	if (cabecalho_secao(CLAY_ID("ed-sec-luz"), "sun", "Luz", E.luz_aberta)) {
		E.luz_aberta = !E.luz_aberta;
	}
	if (E.luz_aberta) {
		linha_deslizante(C_EXPO, "Exposição");
		linha_deslizante(C_CONTRASTE, "Contraste");
		linha_deslizante(C_REALCES, "Realces");
		linha_deslizante(C_SOMBRAS, "Sombras");
	}
	gv_separador(false, 0);
	if (cabecalho_secao(CLAY_ID("ed-sec-cor"), "venn", "Cor", E.cor_aberta)) {
		E.cor_aberta = !E.cor_aberta;
	}
	if (E.cor_aberta) {
		linha_deslizante(C_TEMP, "Temperatura");
		linha_deslizante(C_SAT, "Saturação");
	}
	gv_separador(false, 0);
	if (gv_botao_ext(CLAY_ID("ed-restaurar"), &(gv_botao_opcoes){ .icone = "restore", .texto = "Restaurar ajustes", .estilo = GV_BOTAO_CONTORNO, .altura = 44, .crescer = true })) {
		ed_params antes = E.p;
		ed_zerar_cor(&E.p);
		if (!ed_igual(&antes, &E.p)) {
			commit();
			pedir(false);
		}
	}
}

static void escolher_proporcao(int i) {
	E.prop = i;
	E.prop_inv = false;
	int W, H;
	geo_dims(&W, &H);
	ed_recorte_proporcao(E.p.recorte, prop_atual(), W, H);
	commit();
	pedir(false);
}

static void painel_recortar(void) {
	gv_texto(CLAY_STRING("Recortar"), gv_t.tamanho_destaque + 4, gv_t.texto, GV_PESO_NORMAL);
	gv_separador(false, 0);
	gv_texto(CLAY_STRING("Proporção"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	for (int linha = 0; linha < 3; linha++) {
		CLAY(CLAY_IDI("ed-props", (uint32_t)linha), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
			for (int k = 0; k < 3; k++) {
				int i = linha * 3 + k;
				if (i < N_PROPS && gv_botao_ext(CLAY_IDI("ed-prop", (uint32_t)i), &(gv_botao_opcoes){ .texto = PROPS[i].nome, .estilo = E.prop == i ? GV_BOTAO_CHEIO : GV_BOTAO_CONTORNO, .altura = 40, .crescer = true })) {
					escolher_proporcao(i);
				}
			}
		}
	}
	if (gv_botao_ext(CLAY_ID("ed-inverter"), &(gv_botao_opcoes){ .icone = "rotate", .texto = "Inverter (deitada/em pé)", .estilo = GV_BOTAO_CONTORNO, .altura = 44,   .crescer = true, .desativado = E.prop == 0 || fabsf(PROPS[E.prop].v - 1) < 1e-4f })) {
		E.prop_inv = !E.prop_inv;
		int W, H;
		geo_dims(&W, &H);
		ed_recorte_proporcao(E.p.recorte, prop_atual(), W, H);
		commit();
		pedir(false);
	}
	int W, H, x, y, w, h;
	geo_dims(&W, &H);
	ed_recorte_pixels(E.p.recorte, W, H, &x, &y, &w, &h);
	gv_texto(gv_fmt("Tamanho final: %d × %d px", w, h), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	gv_texto(CLAY_STRING("Arraste os cantos e as bordas na foto; arraste por dentro para mover."), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
	gv_separador(false, 0);
	if (gv_botao_ext(CLAY_ID("ed-recorte-restaurar"), &(gv_botao_opcoes){ .icone = "restore", .texto = "Restaurar recorte", .estilo = GV_BOTAO_CONTORNO, .altura = 44, .crescer = true })) {
		E.prop = 0;
		E.prop_inv = false;
		E.p.recorte[0] = E.p.recorte[1] = 0;
		E.p.recorte[2] = E.p.recorte[3] = 1;
		commit();
		pedir(false);
	}
}

static void geometria_mudou(void) {
	E.prop = 0;
	E.prop_inv = false;
	E.zoom = 1;
	E.pan_x = E.pan_y = 0;
	commit();
	pedir(false);
}

static void painel_girar(void) {
	gv_texto(CLAY_STRING("Girar e espelhar"), gv_t.tamanho_destaque + 4, gv_t.texto, GV_PESO_NORMAL);
	gv_separador(false, 0);
	if (botao_painel(CLAY_ID("ed-girar-e"), "restore", "Girar à esquerda", false)) {
		ed_girar(&E.p, -1);
		geometria_mudou();
	}
	if (botao_painel(CLAY_ID("ed-girar-d"), "refresh", "Girar à direita", false)) {
		ed_girar(&E.p, 1);
		geometria_mudou();
	}
	if (botao_painel(CLAY_ID("ed-esp-h"), "flip-h", "Espelhar na horizontal", E.p.espelho_h)) {
		ed_espelhar(&E.p, true);
		geometria_mudou();
	}
	if (botao_painel(CLAY_ID("ed-esp-v"), "flip-v", "Espelhar na vertical", E.p.espelho_v)) {
		ed_espelhar(&E.p, false);
		geometria_mudou();
	}
	gv_texto(gv_fmt("Girada %d° para a direita", (E.p.giro & 3) * 90), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	gv_separador(false, 0);
	if (gv_botao_ext(CLAY_ID("ed-girar-restaurar"), &(gv_botao_opcoes){ .icone = "restore", .texto = "Restaurar giro", .estilo = GV_BOTAO_CONTORNO, .altura = 44, .crescer = true })) {
		E.p.giro = 0;
		E.p.espelho_h = E.p.espelho_v = false;
		E.p.recorte[0] = E.p.recorte[1] = 0;
		E.p.recorte[2] = E.p.recorte[3] = 1;
		geometria_mudou();
	}
}

static void painel_filtros(void) {
	atualizar_minis();
	gv_texto(CLAY_STRING("Filtros"), gv_t.tamanho_destaque + 4, gv_t.texto, GV_PESO_NORMAL);
	gv_separador(false, 0);
	for (int linha = 0; linha < 3; linha++) {
		CLAY(CLAY_IDI("ed-fl", (uint32_t)linha), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
			for (int k = 0; k < 3; k++) {
				int f = linha * 3 + k;
				Clay_ElementId id = CLAY_IDI("ed-filtro", (uint32_t)f);
				bool ativo = E.p.filtro == f;
				CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6, .padding = CLAY_PADDING_ALL(4), .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
					.backgroundColor = ativo ? gv_alfa(gv_t.acento, 0.18f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
					.border = { .color = ativo ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 }, .width = GV_BORDA(2) } }) {
					CLAY(CLAY_IDI("ed-fm", (uint32_t)f), gv_livre(desenhar_miniatura, (void *)(intptr_t)f, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(72) } } })) {}
					gv_texto(gv_str(ED_FILTRO_NOME[f]), gv_t.tamanho_legenda + 1, ativo ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				if (gv_tocado(id) && E.p.filtro != f) {
					E.p.filtro = f;
					if (f != ED_F_NENHUM && E.p.intensidade == 0) {
						E.p.intensidade = 100;
					}
					commit();
					pedir(false);
				}
			}
		}
	}
	if (E.p.filtro != ED_F_NENHUM) {
		gv_separador(false, 0);
		linha_deslizante(C_INTENS, "Intensidade");
	}
}

/* ---- a barra do alto, a prévia e as abas ---- */

static bool aba_botao(int i, const char *icone, const char *texto) {
	Clay_ElementId id = CLAY_IDI("ed-aba", (uint32_t)i);
	bool ativa = E.aba == i;
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(46) }, .padding = { 18, 18, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ativa ? gv_alfa(gv_t.acento, 0.16f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
		.border = { .color = ativa ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 }, .width = GV_BORDA(1) } }) {
		gv_icone(icone, 22, ativa ? gv_t.texto : gv_t.texto_secundario);
		gv_texto(gv_str(texto), gv_t.tamanho_corpo, ativa ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	return gv_tocado(id);
}

static void trocar_aba(int a) {
	if (a == E.aba) {
		return;
	}
	bool recorte_antes = E.aba == A_RECORTAR;
	E.aba = a;
	E.alca = -1;
	if (a == A_RECORTAR) {
		E.zoom = 1;
		E.pan_x = E.pan_y = 0;
	}
	if (recorte_antes || a == A_RECORTAR) {
		pedir(false); /* a prévia da aba Recortar vem sem o recorte */
	}
}

static const float ZOOMS[] = { 1, 2, 4, 8 };

static void zoom_para(float z, Clay_Vector2 foco, Clay_BoundingBox cx) {
	float novo = fmaxf(1.0f, fminf(8.0f, z));
	if (E.zoom > 0) {
		float fator = novo / E.zoom;
		float ox = foco.x - (cx.x + cx.width / 2), oy = foco.y - (cx.y + cx.height / 2);
		E.pan_x = ox - (ox - E.pan_x) * fator;
		E.pan_y = oy - (oy - E.pan_y) * fator;
	}
	E.zoom = novo;
	if (novo <= 1.001f) {
		E.pan_x = E.pan_y = 0;
	}
}

static int hit_alca(float px, float py, float rx, float ry, float rw, float rh, float raio) {
	bool oeste = fabsf(px - rx) <= raio, leste = fabsf(px - (rx + rw)) <= raio, norte = fabsf(py - ry) <= raio, sul = fabsf(py - (ry + rh)) <= raio;
	bool dentro_x = px >= rx - raio && px <= rx + rw + raio, dentro_y = py >= ry - raio && py <= ry + rh + raio;
	if (oeste && norte) return ED_ALCA_NO;
	if (leste && norte) return ED_ALCA_NE;
	if (leste && sul) return ED_ALCA_SE;
	if (oeste && sul) return ED_ALCA_SO;
	if (norte && dentro_x) return ED_ALCA_N;
	if (sul && dentro_x) return ED_ALCA_S;
	if (oeste && dentro_y) return ED_ALCA_O;
	if (leste && dentro_y) return ED_ALCA_E;
	if (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh) return ED_ALCA_MOVER;
	return -1;
}

static void interacao_da_previa(Clay_ElementId canvas) {
	Clay_BoundingBox cx = gv_caixa(canvas);
	cairo_surface_t *s = E.comparar && E.original ? E.original : E.prev;
	float x, y, w, h;
	if (!retangulo_da_foto(cx, s, &x, &y, &w, &h)) {
		return;
	}
	Clay_Vector2 p = gv_ponteiro();
	bool sobre = p.x >= cx.x && p.x <= cx.x + cx.width && p.y >= cx.y && p.y <= cx.y + cx.height;
	bool premido = gv_premido(canvas);
	if (E.aba == A_RECORTAR && !E.comparar && E.prev_sem_recorte) {
		if (premido && E.alca < 0 && !E.pan_ativo) { /* começou agora */
			float rx = x + E.p.recorte[0] * w, ry = y + E.p.recorte[1] * h, rw = E.p.recorte[2] * w, rh = E.p.recorte[3] * h;
			E.alca = hit_alca(p.x, p.y, rx, ry, rw, rh, gv_ponteiro_e_toque() ? 28 : 16);
			if (E.alca >= 0) {
				E.ultimo = p;
			} else {
				E.pan_ativo = true; /* apertou fora: nada a arrastar até soltar */
			}
		} else if (premido && E.alca >= 0) {
			int W, H;
			geo_dims(&W, &H);
			ed_recorte_arrastar(E.p.recorte, E.alca, (p.x - E.ultimo.x) / w, (p.y - E.ultimo.y) / h, prop_atual(), W, H);
			E.ultimo = p;
			pedir(false);
		}
		if (!premido) {
			if (E.alca >= 0) {
				E.alca = -1;
				commit();
			}
			E.pan_ativo = false;
		}
		return;
	}
	/* as outras abas: a roda dá zoom em volta do ponteiro; arrastar anda quando a foto passa da moldura */
	float roda = sobre ? gv_roda() : 0;
	if (roda != 0) {
		zoom_para(E.zoom * expf(-roda * 0.04f), p, cx);
	}
	if (premido && E.zoom > 1.001f) {
		if (E.pan_ativo) {
			E.pan_x += p.x - E.pan_ultimo.x;
			E.pan_y += p.y - E.pan_ultimo.y;
		}
		E.pan_ativo = true;
		E.pan_ultimo = p;
		float aw = cx.width - 2 * MARGEM, ah = cx.height - 2 * MARGEM;
		E.pan_x = CLAMP(E.pan_x, -fmaxf(0, (w - aw) / 2), fmaxf(0, (w - aw) / 2));
		E.pan_y = CLAMP(E.pan_y, -fmaxf(0, (h - ah) / 2), fmaxf(0, (h - ah) / 2));
	} else {
		E.pan_ativo = false;
	}
}

static void barra_do_alto(void) {
	CLAY(CLAY_ID("ed-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 16, 16, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		if (gv_botao_ext(CLAY_ID("ed-voltar"), &(gv_botao_opcoes){ .icone = "arrow-left", .texto = "Voltar à galeria", .estilo = GV_BOTAO_SIMPLES, .altura = 42 })) {
			sair();
		}
		CLAY(CLAY_ID("ed-nome"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(E.nome), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
		}
		bool pode_desfazer = E.ih > 0, pode_refazer = E.ih + 1 < E.nh;
		CLAY(CLAY_ID("ed-undo-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) } } }) {
			if (pode_desfazer) {
				if (gv_botao_redondo(CLAY_ID("ed-undo"), "undo", 40, false, false)) {
					desfazer();
				}
			} else {
				CLAY(CLAY_ID("ed-undo-x"), { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_icone("undo", 22, gv_t.desativado);
				}
			}
		}
		CLAY(CLAY_ID("ed-redo-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) } } }) {
			if (pode_refazer) {
				if (gv_botao_redondo(CLAY_ID("ed-redo"), "redo", 40, false, false)) {
					refazer();
				}
			} else {
				CLAY(CLAY_ID("ed-redo-x"), { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_icone("redo", 22, gv_t.desativado);
				}
			}
		}
		if (gv_botao_ext(CLAY_ID("ed-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_CONTORNO, .altura = 42, .desativado = E.salvando })) {
			sair();
		}
		if (gv_botao_ext(CLAY_ID("ed-salvar"), &(gv_botao_opcoes){ .texto = E.salvando ? "Salvando…" : "Salvar cópia", .estilo = GV_BOTAO_CHEIO, .altura = 42,
			.desativado = E.salvando || E.carregando || !ed_alterado(&E.p) })) {
			salvar();
		}
	}
}

static void rodape_da_previa(Clay_ElementId canvas) {
	CLAY(CLAY_ID("ed-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(52) }, .padding = { 24, 24, 0, 0 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		Clay_ElementId cmp = CLAY_ID("ed-comparar");
		CLAY(cmp, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(40) }, .padding = { 10, 12, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_premido(cmp) ? gv_t.selecionado : gv_por_cima(cmp) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
			gv_icone("compare", 22, gv_t.texto_secundario);
			gv_texto(CLAY_STRING("Comparar original"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		E.comparar = gv_premido(cmp) && E.original; /* segurar mostra a foto como abriu */
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		bool zoom_ok = E.aba != A_RECORTAR;
		Clay_BoundingBox cx = gv_caixa(canvas);
		Clay_Vector2 centro = { cx.x + cx.width / 2, cx.y + cx.height / 2 };
		if (zoom_ok && gv_botao_redondo(CLAY_ID("ed-zoom-menos"), "zoom-out", 36, false, false)) {
			zoom_para(E.zoom / 2, centro, cx);
		}
		int idx = CLAMP((int)lroundf(log2f(E.zoom)), 0, 3);
		static const char *const NOMES[] = { "Ajustar à tela", "2×", "4×", "8×" };
		if (zoom_ok && gv_lista_suspensa(CLAY_ID("ed-zoom-lista"), NOMES, 4, &idx)) {
			zoom_para(ZOOMS[idx], centro, cx);
		}
		if (zoom_ok && gv_botao_redondo(CLAY_ID("ed-zoom-mais"), "zoom-in", 36, false, false)) {
			zoom_para(E.zoom * 2, centro, cx);
		}
	}
}

static void tela_pequena(const char *texto) {
	CLAY(CLAY_ID("ed-aguarde"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo + 2, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void dialogo_sair(void) {
	CLAY(CLAY_ID("ed-dlg"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("ed-dlg-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(440), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(22), .childGap = 14 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Descartar as alterações?"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Você mexeu nesta foto e ainda não salvou uma cópia. O original não muda de qualquer jeito."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("ed-dlg-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_botao_ext(CLAY_ID("ed-dlg-continuar"), &(gv_botao_opcoes){ .texto = "Continuar editando", .estilo = GV_BOTAO_ELEVADO })) {
					E.dlg_sair = false;
				}
				if (gv_botao_ext(CLAY_ID("ed-dlg-descartar"), &(gv_botao_opcoes){ .texto = "Descartar", .estilo = GV_BOTAO_PERIGO })) {
					E.dlg_sair = false;
					fechar();
				}
			}
		}
	}
}

static void teclado_do_editor(void) {
	if (E.dlg_sair) {
		if (gv_tecla() == 0xff1b) {
			E.dlg_sair = false;
		}
		return;
	}
	for (int i = 0;; i++) {
		uint32_t t = gv_tecla_n(i);
		if (!t) {
			break;
		}
		uint32_t mods = gv_modificadores();
		if (t == 0xff1b) {
			sair();
			return;
		} else if ((mods & GV_MOD_CTRL) && (t == 'z' || t == 'Z')) {
			if (mods & GV_MOD_SHIFT) {
				refazer();
			} else {
				desfazer();
			}
		} else if ((mods & GV_MOD_CTRL) && (t == 'y' || t == 'Y')) {
			refazer();
		} else if ((mods & GV_MOD_CTRL) && (t == 's' || t == 'S')) {
			salvar();
		}
	}
}

void editor_desenhar(void) {
	if (!E.ativo) {
		G.tela = T_BIBLIOTECA;
		return;
	}
	teclado_do_editor();
	barra_do_alto();
	if (E.carregando || !E.base) {
		tela_pequena("Abrindo a foto…");
	} else {
		CLAY(CLAY_ID("ed-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			CLAY(CLAY_ID("ed-esq"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
				Clay_ElementId canvas = CLAY_ID("ed-canvas");
				CLAY(canvas, gv_livre(desenhar_previa, NULL, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {}
				interacao_da_previa(canvas);
				rodape_da_previa(canvas);
				CLAY(CLAY_ID("ed-abas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(70) }, .childGap = 14, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
					.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
					if (aba_botao(A_AJUSTAR, "sliders", "Ajustar")) trocar_aba(A_AJUSTAR);
					if (aba_botao(A_RECORTAR, "crop", "Recortar")) trocar_aba(A_RECORTAR);
					if (aba_botao(A_GIRAR, "refresh", "Girar")) trocar_aba(A_GIRAR);
					if (aba_botao(A_FILTROS, "venn", "Filtros")) trocar_aba(A_FILTROS);
				}
			}
			Clay_ElementId rol = CLAY_ID("ed-painel-rol");
			CLAY(CLAY_ID("ed-painel"), { .layout = { .sizing = { CLAY_SIZING_FIXED(372), CLAY_SIZING_GROW(0) } }, .border = { .color = gv_t.borda, .width = { .left = 1 } } }) {
				CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
					.padding = { 24, 24, 20, 18 }, .childGap = 10 } })) {
					switch (E.aba) {
					case A_AJUSTAR: painel_ajustar(); break;
					case A_RECORTAR: painel_recortar(); break;
					case A_GIRAR: painel_girar(); break;
					default: painel_filtros(); break;
					}
				}
			}
		}
	}
	if (E.dlg_sair) {
		dialogo_sair();
	}
	/* soltar um deslizante: fica no histórico e a prévia nítida volta */
	if (E.slider_ativo >= 0 && !gv_ponteiro_baixo()) {
		E.slider_ativo = -1;
		commit();
		pedir(false);
	}
}

/* ---- para os testes (LUMNA_GALERIA_DEBUG) ---- */

void editor_estado(GString *s) {
	if (!E.ativo) {
		return;
	}
	int pw = E.prev ? cairo_image_surface_get_width(E.prev) : 0, ph = E.prev ? cairo_image_surface_get_height(E.prev) : 0;
	char cantos[160] = "-1,-1,-1";
	char tl[32] = "-1,-1,-1", tr[32] = "-1,-1,-1", bl[32] = "-1,-1,-1", br[32] = "-1,-1,-1";
	if (E.prev && pw > 0) {
		cairo_surface_flush(E.prev);
		const unsigned char *d = cairo_image_surface_get_data(E.prev);
		int st = cairo_image_surface_get_stride(E.prev);
		struct { int x, y; char *saida; } P[] = { { pw / 2, ph / 2, cantos }, { pw / 10, ph / 10, tl }, { pw * 9 / 10, ph / 10, tr }, { pw / 10, ph * 9 / 10, bl }, { pw * 9 / 10, ph * 9 / 10, br } };
		for (size_t i = 0; i < G_N_ELEMENTS(P); i++) {
			uint32_t px = *(const uint32_t *)(d + (gsize)P[i].y * (gsize)st + (gsize)P[i].x * 4);
			g_snprintf(P[i].saida, i == 0 ? sizeof(cantos) : 32, "%d,%d,%d", (int)((px >> 16) & 255), (int)((px >> 8) & 255), (int)(px & 255));
		}
	}
	g_string_append_printf(s, " ed=1 ed_aba=%d ed_exp=%.1f ed_con=%d ed_rea=%d ed_som=%d ed_tem=%d ed_sat=%d ed_giro=%d ed_eh=%d ed_ev=%d ed_rec=%.3f,%.3f,%.3f,%.3f ed_filtro=%d ed_int=%d"
		" ed_hist=%d/%d ed_alt=%d ed_prev=%dx%d ed_px=%s ed_tl=%s ed_tr=%s ed_bl=%s ed_br=%s ed_zoom=%.2f ed_salvando=%d ed_dlg=%d ed_prop=%d ed_inv=%d ed_carregando=%d ed_cmp=%d ed_busy=%d ed_orig=%dx%d",
		E.aba, E.p.exposicao, E.p.contraste, E.p.realces, E.p.sombras, E.p.temperatura, E.p.saturacao, E.p.giro, E.p.espelho_h, E.p.espelho_v,
		E.p.recorte[0], E.p.recorte[1], E.p.recorte[2], E.p.recorte[3], E.p.filtro, E.p.intensidade, E.ih, E.nh, ed_alterado(&E.p), pw, ph, cantos, tl, tr, bl, br, E.zoom,
		E.salvando, E.dlg_sair, E.prop, E.prop_inv, E.carregando, E.comparar, E.renderizando, E.ow, E.oh);
}
