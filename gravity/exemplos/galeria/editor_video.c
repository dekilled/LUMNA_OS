/* O editor de vídeos: a prévia (parada ou tocando, sem som), a faixa do tempo com as alças de recorte, e o painel com Recorte, Áudio e Girar.
 * A conta é de servicos/edicao_video.c (ffmpeg). Tudo que demora (quadros, miniaturas, tocar, exportar) roda em thread; o que chega entra no começo do quadro. */
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "edicao_video.h"

#define PREVIA_MAX 640
#define TIRAS 10
#define TIRA_LADO 160
#define FPS 15
#define MIN_TRECHO 0.5

static struct {
	unsigned sessao;
	bool ativo, carregando;
	char caminho[4096], nome[256];
	time_t data;
	ev_info info;
	ev_params p;
	double t;                        /* o ponto da prévia */
	cairo_surface_t *quadro;
	int fw, fh;
	cairo_surface_t *tiras[TIRAS];
	int tiras_giro;                  /* o giro com que as miniaturas foram feitas */
	bool tiras_pedidas;
	bool tocando;
	bool qrodando;
	unsigned qseq;
	int modo;                        /* o arrasto na faixa: 0 nada, 1 início, 2 fim, 3 ponto */
	bool exportando;
	bool dlg_sair;
	int voltar_para;
} V;

static unsigned seq_global;
static volatile gint exp_permil;
static volatile int *exp_cancelar;

/* ---- texto ---- */

static const char *tempo_texto(double s) {
	int n = (int)floor(s + 0.0001);
	if (n >= 3600) {
		return gv_fmt("%d:%02d:%02d", n / 3600, n / 60 % 60, n % 60).chars;
	}
	return gv_fmt("%02d:%02d", n / 60, n % 60).chars;
}

/* ---- um quadro parado (a mais recente vence) ---- */

typedef struct {
	unsigned sessao, seq;
	char caminho[4096];
	double t;
	int giro;
	cairo_surface_t *sup;
} qjob;

static void q_iniciar(void);

static void q_pronto(void *d) {
	qjob *j = d;
	V.qrodando = false;
	if (j->sessao == V.sessao && V.ativo && j->sup) {
		if (V.quadro) {
			cairo_surface_destroy(V.quadro);
		}
		V.quadro = j->sup;
		j->sup = NULL;
		V.fw = cairo_image_surface_get_width(V.quadro);
		V.fh = cairo_image_surface_get_height(V.quadro);
		gv_app_redesenhar(G.app);
	}
	if (j->sup) {
		cairo_surface_destroy(j->sup);
	}
	bool de_novo = V.ativo && (j->sessao != V.sessao || j->seq != V.qseq);
	g_free(j);
	if (de_novo) {
		q_iniciar();
	}
}

static gpointer q_thread(gpointer d) {
	qjob *j = d;
	GdkPixbuf *pb = ev_quadro(j->caminho, j->t, j->giro, PREVIA_MAX);
	if (pb) {
		j->sup = superficie_de(pb);
		g_object_unref(pb);
	}
	gv_na_linha_principal(q_pronto, j);
	return NULL;
}

static void q_iniciar(void) {
	qjob *j = g_new0(qjob, 1);
	j->sessao = V.sessao;
	j->seq = V.qseq;
	g_strlcpy(j->caminho, V.caminho, sizeof(j->caminho));
	j->t = V.t;
	j->giro = V.p.giro;
	V.qrodando = true;
	g_thread_unref(g_thread_new("galeria-video-quadro", q_thread, j));
}

static void pedir_quadro(void) {
	V.qseq = ++seq_global;
	if (!V.qrodando) {
		q_iniciar();
	}
}

/* ---- as miniaturas da faixa ---- */

typedef struct {
	unsigned sessao;
	char caminho[4096];
	double dur;
	int giro;
	cairo_surface_t *sup[TIRAS];
} tjob;

static void tiras_limpar(void) {
	for (int i = 0; i < TIRAS; i++) {
		if (V.tiras[i]) {
			cairo_surface_destroy(V.tiras[i]);
			V.tiras[i] = NULL;
		}
	}
}

static void t_pronto(void *d) {
	tjob *j = d;
	if (j->sessao == V.sessao && V.ativo && j->giro == V.p.giro) {
		tiras_limpar();
		for (int i = 0; i < TIRAS; i++) {
			V.tiras[i] = j->sup[i];
			j->sup[i] = NULL;
		}
		V.tiras_giro = j->giro;
		V.tiras_pedidas = false;
		gv_app_redesenhar(G.app);
	} else if (j->sessao == V.sessao && V.ativo) {
		V.tiras_pedidas = false; /* o giro mudou no meio: refaz */
	}
	for (int i = 0; i < TIRAS; i++) {
		if (j->sup[i]) {
			cairo_surface_destroy(j->sup[i]);
		}
	}
	g_free(j);
}

static gpointer t_thread(gpointer d) {
	tjob *j = d;
	for (int i = 0; i < TIRAS; i++) {
		GdkPixbuf *pb = ev_quadro(j->caminho, j->dur * (i + 0.5) / TIRAS, j->giro, TIRA_LADO);
		if (pb) {
			j->sup[i] = superficie_de(pb);
			g_object_unref(pb);
		}
	}
	gv_na_linha_principal(t_pronto, j);
	return NULL;
}

static void pedir_tiras(void) {
	if (V.tiras_pedidas) {
		return;
	}
	V.tiras_pedidas = true;
	tjob *j = g_new0(tjob, 1);
	j->sessao = V.sessao;
	g_strlcpy(j->caminho, V.caminho, sizeof(j->caminho));
	j->dur = V.info.duracao;
	j->giro = V.p.giro;
	g_thread_unref(g_thread_new("galeria-video-tiras", t_thread, j));
}

/* ---- o som da prévia: um mpv só de áudio, do mesmo trecho, que nasce e morre com o Play ---- */

static GPid aud_pid;
static int aud_vol = -1;
static bool aud_mudo;

static void aud_acabou(GPid pid, gint estado, gpointer d) {
	g_spawn_close_pid(pid);
	if (aud_pid == pid) {
		aud_pid = 0;
	}
}

static void audio_parar(void) {
	if (aud_pid) {
		kill(aud_pid, SIGTERM);
		aud_pid = 0;
	}
}

static void audio_iniciar(double t0) {
	audio_parar();
	aud_vol = V.p.volume;
	aud_mudo = V.p.mudo;
	if (!V.info.audio || V.p.mudo || V.p.volume <= 0) {
		return;
	}
	char *mpv = g_find_program_in_path("mpv");
	if (!mpv) {
		return;
	}
	char ini[32], fim[32], vol[32], *ao = NULL;
	g_snprintf(ini, sizeof(ini), "--start=%.3f", t0);
	g_snprintf(fim, sizeof(fim), "--end=%.3f", V.p.fim);
	g_snprintf(vol, sizeof(vol), "--volume=%d", V.p.volume);
	if (g_getenv("LUMNA_GALERIA_AO")) {
		ao = g_strdup_printf("--ao=%s", g_getenv("LUMNA_GALERIA_AO"));
	}
	char *argv[] = { mpv, "--no-video", "--no-terminal", "--really-quiet", "--no-config", "--volume-max=200", ini, fim, vol, ao ? ao : "--idle=no", "--", V.caminho, NULL };
	GPid pid = 0;
	if (g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &pid, NULL)) {
		aud_pid = pid;
		g_child_watch_add(pid, aud_acabou, NULL);
	}
	g_free(ao);
	g_free(mpv);
}

/* ---- tocar a prévia (com som: o ffmpeg dá os quadros, o mpv o áudio) ---- */

typedef struct {
	gint refs;
	ev_toque *t;
	int w, h;
	double t0;
	GMutex m;
	uint8_t *slot;
	bool novo, postado, fim;
	long n;
} toca;

static toca *T;

static void toca_unref(toca *c) {
	if (g_atomic_int_dec_and_test(&c->refs)) {
		ev_toque_liberar(c->t);
		g_mutex_clear(&c->m);
		g_free(c->slot);
		g_free(c);
	}
}

static void tocar_parar(void) {
	audio_parar();
	if (T) {
		ev_toque_parar(T->t);
		toca_unref(T);
		T = NULL;
	}
	V.tocando = false;
}

static void toca_pronto(void *d) {
	toca *c = d;
	if (c == T && V.ativo) {
		g_mutex_lock(&c->m);
		c->postado = false;
		bool novo = c->novo, fim = c->fim;
		cairo_surface_t *s = NULL;
		if (novo) {
			s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, c->w, c->h);
			cairo_surface_flush(s);
			unsigned char *dst = cairo_image_surface_get_data(s);
			int st = cairo_image_surface_get_stride(s);
			for (int y = 0; y < c->h; y++) {
				memcpy(dst + (gsize)y * (gsize)st, c->slot + (gsize)y * (gsize)c->w * 4, (gsize)c->w * 4);
			}
			cairo_surface_mark_dirty(s);
			c->novo = false;
		}
		long n = c->n;
		g_mutex_unlock(&c->m);
		if (s) {
			if (V.quadro) {
				cairo_surface_destroy(V.quadro);
			}
			V.quadro = s;
			V.fw = c->w;
			V.fh = c->h;
			V.t = c->t0 + (double)n / FPS;
		}
		if (fim || V.t >= V.p.fim - 0.02) {
			tocar_parar();
			V.t = fim && V.t < V.p.fim - 0.3 ? V.t : V.p.fim;
			pedir_quadro();
		}
		gv_app_redesenhar(G.app);
	} else if (c != T) {
		g_mutex_lock(&c->m);
		c->postado = false;
		g_mutex_unlock(&c->m);
	}
	toca_unref(c);
}

static gpointer toca_thread(gpointer d) {
	toca *c = d;
	uint8_t *tmp = g_malloc((gsize)c->w * (gsize)c->h * 4);
	while (ev_toque_proximo(c->t, tmp)) {
		g_mutex_lock(&c->m);
		memcpy(c->slot, tmp, (gsize)c->w * (gsize)c->h * 4);
		c->novo = true;
		c->n++;
		bool postar = !c->postado;
		c->postado = true;
		g_mutex_unlock(&c->m);
		if (postar) {
			g_atomic_int_inc(&c->refs);
			gv_na_linha_principal(toca_pronto, c);
		}
	}
	g_free(tmp);
	g_mutex_lock(&c->m);
	c->fim = true;
	bool postar = !c->postado;
	c->postado = true;
	g_mutex_unlock(&c->m);
	if (postar) {
		g_atomic_int_inc(&c->refs);
		gv_na_linha_principal(toca_pronto, c);
	}
	toca_unref(c);
	return NULL;
}

static void tocar_iniciar(void) {
	if (!V.quadro || V.fw <= 0) {
		return;
	}
	if (V.t < V.p.inicio || V.t >= V.p.fim - 0.1) {
		V.t = V.p.inicio;
	}
	toca *c = g_new0(toca, 1);
	c->refs = 2; /* a thread e o V */
	c->w = V.fw & ~1;
	c->h = V.fh & ~1;
	c->t0 = V.t;
	c->slot = g_malloc0((gsize)c->w * (gsize)c->h * 4);
	g_mutex_init(&c->m);
	c->t = ev_tocar(V.caminho, V.t, V.p.giro, c->w, c->h, FPS);
	if (!c->t) {
		aviso("Não deu para tocar a prévia.");
		g_mutex_clear(&c->m);
		g_free(c->slot);
		g_free(c);
		return;
	}
	T = c;
	V.tocando = true;
	audio_iniciar(V.t);
	g_thread_unref(g_thread_new("galeria-video-tocar", toca_thread, c));
}

static void alternar_toque(void) {
	if (V.tocando) {
		tocar_parar();
		pedir_quadro();
	} else {
		tocar_iniciar();
	}
}

/* ---- abrir e fechar ---- */

typedef struct {
	unsigned sessao;
	char caminho[4096];
	ev_info info;
	bool ok;
} ajob;

static void fechar(void) {
	tocar_parar();
	V.ativo = false;
	V.sessao++;
	if (V.quadro) {
		cairo_surface_destroy(V.quadro);
		V.quadro = NULL;
	}
	tiras_limpar();
	G.tela = T_BIBLIOTECA;
}

static void a_pronto(void *d) {
	ajob *j = d;
	if (j->sessao == V.sessao && V.ativo) {
		V.carregando = false;
		if (!j->ok) {
			aviso("Não deu para abrir este vídeo para editar.");
			fechar();
		} else {
			V.info = j->info;
			ev_padrao(&V.p, j->info.duracao);
			V.t = 0;
			pedir_quadro();
			pedir_tiras();
		}
		gv_app_redesenhar(G.app);
	}
	g_free(j);
}

static gpointer a_thread(gpointer d) {
	ajob *j = d;
	j->ok = ev_sondar(j->caminho, &j->info);
	gv_na_linha_principal(a_pronto, j);
	return NULL;
}

void editor_video_abrir(const gl_midia *m) {
	tocar_parar();
	if (V.quadro) {
		cairo_surface_destroy(V.quadro);
		V.quadro = NULL;
	}
	tiras_limpar();
	V.sessao++;
	V.ativo = true;
	V.carregando = true;
	V.tiras_pedidas = false;
	V.exportando = false;
	V.dlg_sair = false;
	V.modo = 0;
	V.fw = V.fh = 0;
	memset(&V.info, 0, sizeof(V.info));
	ev_padrao(&V.p, 1);
	g_strlcpy(V.caminho, m->caminho, sizeof(V.caminho));
	g_strlcpy(V.nome, m->nome, sizeof(V.nome));
	V.data = m->data;
	G.tela = T_EDITOR_VIDEO;
	ajob *j = g_new0(ajob, 1);
	j->sessao = V.sessao;
	g_strlcpy(j->caminho, m->caminho, sizeof(j->caminho));
	g_thread_unref(g_thread_new("galeria-video-abrir", a_thread, j));
}

/* ---- exportar ---- */

typedef struct {
	unsigned sessao;
	char caminho[4096];
	ev_params p;
	time_t data;
	volatile int cancelar;
	char *destino, *erro;
} xjob;

static void x_progresso(double f, void *d) {
	(void)d;
	g_atomic_int_set(&exp_permil, (int)(f * 1000));
}

static void x_pronto(void *d) {
	xjob *j = d;
	bool nossa = j->sessao == V.sessao && V.ativo;
	if (nossa) {
		V.exportando = false;
		exp_cancelar = NULL;
	}
	if (j->destino) {
		char *nome = g_path_get_basename(j->destino);
		aviso("Cópia salva: %s", nome);
		g_free(nome);
		recarregar_biblioteca();
		if (nossa) {
			fechar();
		}
	} else if (j->erro) {
		aviso("Não deu para exportar a cópia: %s", j->erro);
	} else {
		aviso("Exportação cancelada.");
	}
	gv_app_redesenhar(G.app);
	g_free(j->destino);
	g_free(j->erro);
	g_free(j);
}

static gpointer x_thread(gpointer d) {
	xjob *j = d;
	GError *e = NULL;
	j->destino = ev_exportar(j->caminho, &j->p, j->data, x_progresso, NULL, &j->cancelar, &e);
	if (!j->destino && e) {
		j->erro = g_strdup(e->message);
	}
	g_clear_error(&e);
	gv_na_linha_principal(x_pronto, j);
	return NULL;
}

static void exportar(void) {
	if (V.exportando || V.carregando || !ev_alterado(&V.p, V.info.duracao)) {
		return;
	}
	tocar_parar();
	V.exportando = true;
	g_atomic_int_set(&exp_permil, 0);
	xjob *j = g_new0(xjob, 1);
	j->sessao = V.sessao;
	g_strlcpy(j->caminho, V.caminho, sizeof(j->caminho));
	j->p = V.p;
	j->data = V.data;
	exp_cancelar = &j->cancelar;
	g_thread_unref(g_thread_new("galeria-video-exportar", x_thread, j));
}

static void sair(void) {
	if (V.exportando) {
		if (exp_cancelar) {
			g_atomic_int_set(exp_cancelar, 1); /* Cancelar durante a exportação cancela a exportação */
		}
		return;
	}
	if (ev_alterado(&V.p, V.info.duracao)) {
		V.dlg_sair = true;
		return;
	}
	fechar();
}

/* ---- as contas da faixa ---- */

#define FAIXA_MARGEM 16.0f
#define FAIXA_Y 30.0f
#define FAIXA_ALTURA 72.0f

static float t_para_x(Clay_BoundingBox cx, double t) {
	float w = cx.width - 2 * FAIXA_MARGEM;
	return cx.x + FAIXA_MARGEM + (float)(t / V.info.duracao) * w;
}

static double x_para_t(Clay_BoundingBox cx, float x) {
	float w = cx.width - 2 * FAIXA_MARGEM;
	return CLAMP((double)(x - cx.x - FAIXA_MARGEM) / w, 0, 1) * V.info.duracao;
}

static void retangulo(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
	cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
	cairo_close_path(cr);
}

static void desenhar_previa(cairo_t *cr, Clay_BoundingBox cx, void *dados) {
	(void)dados;
	if (!V.quadro || cx.width < 40) {
		return;
	}
	double iw = cairo_image_surface_get_width(V.quadro), ih = cairo_image_surface_get_height(V.quadro);
	double f = fmin((cx.width - 40) / iw, (cx.height - 20) / ih);
	double w = iw * f, h = ih * f, x = cx.x + (cx.width - w) / 2, y = cx.y + (cx.height - h) / 2;
	cairo_save(cr);
	retangulo(cr, x, y, w, h, 8);
	cairo_clip(cr);
	cairo_translate(cr, x, y);
	cairo_scale(cr, f, f);
	cairo_set_source_surface(cr, V.quadro, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
}

static void desenhar_faixa(cairo_t *cr, Clay_BoundingBox cx, void *dados) {
	(void)dados;
	if (V.info.duracao <= 0 || cx.width < 80) {
		return;
	}
	float w = cx.width - 2 * FAIXA_MARGEM, x0 = cx.x + FAIXA_MARGEM, y0 = cx.y + FAIXA_Y;
	/* a régua: o começo, o fim e quatro marcas no meio */
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 12);
	for (int i = 0; i <= 4; i++) {
		double t = V.info.duracao * i / 4;
		const char *s = tempo_texto(t);
		cairo_text_extents_t te;
		cairo_text_extents(cr, s, &te);
		float x = x0 + w * (float)i / 4;
		cairo_set_source_rgba(cr, 0.72, 0.72, 0.78, 1);
		cairo_move_to(cr, CLAMP(x - te.width / 2, cx.x, cx.x + cx.width - te.width), cx.y + 12);
		cairo_show_text(cr, s);
		cairo_set_source_rgba(cr, 0.5, 0.5, 0.56, 1);
		cairo_set_line_width(cr, 1);
		cairo_move_to(cr, x, cx.y + 18);
		cairo_line_to(cr, x, cx.y + 24);
		cairo_stroke(cr);
	}
	/* as miniaturas */
	cairo_save(cr);
	retangulo(cr, x0, y0, w, FAIXA_ALTURA, 6);
	cairo_clip(cr);
	cairo_set_source_rgba(cr, 0.1, 0.1, 0.13, 1);
	cairo_paint(cr);
	for (int i = 0; i < TIRAS; i++) {
		cairo_surface_t *s = V.tiras[i];
		if (!s) {
			continue;
		}
		double iw = cairo_image_surface_get_width(s), ih = cairo_image_surface_get_height(s), cw = w / TIRAS;
		double f = fmax(cw / iw, FAIXA_ALTURA / ih);
		cairo_save(cr);
		cairo_rectangle(cr, x0 + cw * i, y0, cw, FAIXA_ALTURA);
		cairo_clip(cr);
		cairo_translate(cr, x0 + cw * i + (cw - iw * f) / 2, y0 + (FAIXA_ALTURA - ih * f) / 2);
		cairo_scale(cr, f, f);
		cairo_set_source_surface(cr, s, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	/* o que fica de fora escurece */
	float xi = t_para_x(cx, V.p.inicio), xf = t_para_x(cx, V.p.fim);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.6);
	cairo_rectangle(cr, x0, y0, xi - x0, FAIXA_ALTURA);
	cairo_rectangle(cr, xf, y0, x0 + w - xf, FAIXA_ALTURA);
	cairo_fill(cr);
	cairo_restore(cr);
	/* a moldura do trecho e as alças */
	cairo_set_source_rgba(cr, 0.69, 0.55, 1, 1);
	cairo_set_line_width(cr, 3);
	cairo_rectangle(cr, xi, y0 - 1, xf - xi, FAIXA_ALTURA + 2);
	cairo_stroke(cr);
	for (int k = 0; k < 2; k++) {
		float x = k ? xf : xi;
		retangulo(cr, x - 7, y0 - 1, 14, FAIXA_ALTURA + 2, 4);
		cairo_set_source_rgba(cr, 0.69, 0.55, 1, 1);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
		cairo_set_line_width(cr, 1.5);
		cairo_move_to(cr, x - 2, y0 + FAIXA_ALTURA / 2 - 7);
		cairo_line_to(cr, x - 2, y0 + FAIXA_ALTURA / 2 + 7);
		cairo_move_to(cr, x + 2, y0 + FAIXA_ALTURA / 2 - 7);
		cairo_line_to(cr, x + 2, y0 + FAIXA_ALTURA / 2 + 7);
		cairo_stroke(cr);
	}
	/* o ponto da prévia */
	float xt = t_para_x(cx, V.t);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_set_line_width(cr, 2);
	cairo_move_to(cr, xt, cx.y + 26);
	cairo_line_to(cr, xt, y0 + FAIXA_ALTURA + 4);
	cairo_stroke(cr);
	cairo_move_to(cr, xt - 6, cx.y + 22);
	cairo_line_to(cr, xt + 6, cx.y + 22);
	cairo_line_to(cr, xt, cx.y + 30);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.69, 0.55, 1, 1);
	cairo_fill(cr);
}

static void interacao_da_faixa(Clay_ElementId faixa) {
	if (V.info.duracao <= 0) {
		return;
	}
	Clay_BoundingBox cx = gv_caixa(faixa);
	if (cx.width <= 0) {
		return;
	}
	Clay_Vector2 p = gv_ponteiro();
	bool premido = gv_premido(faixa);
	if (premido && V.modo == 0) {
		float y0 = cx.y + FAIXA_Y;
		float xi = t_para_x(cx, V.p.inicio), xf = t_para_x(cx, V.p.fim);
		float raio = gv_ponteiro_e_toque() ? 22 : 12;
		bool na_faixa_y = p.y >= y0 - 10 && p.y <= y0 + FAIXA_ALTURA + 10;
		if (na_faixa_y && fabsf(p.x - xi) <= raio && fabsf(p.x - xi) <= fabsf(p.x - xf)) {
			V.modo = 1;
		} else if (na_faixa_y && fabsf(p.x - xf) <= raio) {
			V.modo = 2;
		} else {
			V.modo = 3;
		}
		if (V.tocando) {
			tocar_parar();
		}
	}
	if (premido && V.modo) {
		double t = x_para_t(cx, p.x);
		if (V.modo == 1) {
			V.p.inicio = t;
			ev_ajustar(&V.p, V.info.duracao, MIN_TRECHO);
			V.p.inicio = fminf((float)V.p.inicio, (float)(V.p.fim - MIN_TRECHO));
			V.t = V.p.inicio;
		} else if (V.modo == 2) {
			V.p.fim = t;
			ev_ajustar(&V.p, V.info.duracao, MIN_TRECHO);
			V.t = V.p.fim;
		} else {
			V.t = t;
		}
		pedir_quadro();
	}
	if (!premido) {
		V.modo = 0;
	}
}

/* ---- os pedaços da tela ---- */

static bool seta_tempo(Clay_ElementId id, const char *icone) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(16) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {
		gv_icone(icone, 14, gv_t.texto_secundario);
	}
	return gv_tocado(id);
}

static void campo_tempo(int k, const char *rotulo, double valor) {
	CLAY(CLAY_IDI("vd-lin", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(42) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_IDI("vd-rot", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_FIXED(120) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("vd-caixa", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 14, 8, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(tempo_texto(valor)), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
			}
			if (k < 2) {
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2 } }) {
					if (seta_tempo(CLAY_IDI("vd-mais", (uint32_t)k), "chevron-up")) {
						if (k == 0) {
							V.p.inicio += 1;
						} else {
							V.p.fim += 1;
						}
						ev_ajustar(&V.p, V.info.duracao, MIN_TRECHO);
						V.t = k == 0 ? V.p.inicio : V.p.fim;
						pedir_quadro();
					}
					if (seta_tempo(CLAY_IDI("vd-menos", (uint32_t)k), "chevron-down")) {
						if (k == 0) {
							V.p.inicio -= 1;
						} else {
							V.p.fim -= 1;
						}
						ev_ajustar(&V.p, V.info.duracao, MIN_TRECHO);
						V.t = k == 0 ? V.p.inicio : V.p.fim;
						pedir_quadro();
					}
				}
			}
		}
	}
}

static void painel(void) {
	gv_texto(CLAY_STRING("Editar vídeo"), gv_t.tamanho_destaque + 4, gv_t.texto, GV_PESO_NORMAL);
	gv_separador(false, 0);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("cut", 22, gv_t.texto_secundario);
		gv_texto(CLAY_STRING("Recorte"), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
	}
	campo_tempo(0, "Início", V.p.inicio);
	campo_tempo(1, "Fim", V.p.fim);
	campo_tempo(2, "Duração final", V.p.fim - V.p.inicio);
	gv_separador(false, 0);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("volume", 22, gv_t.texto_secundario);
		gv_texto(CLAY_STRING("Áudio"), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
	}
	CLAY(CLAY_ID("vd-mudo-lin"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(CLAY_STRING("Silenciar áudio"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		gv_interruptor(CLAY_ID("vd-mudo"), &V.p.mudo);
	}
	CLAY(CLAY_ID("vd-vol-lin"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(42) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_ID("vd-vol-rot"), { .layout = { .sizing = { CLAY_SIZING_FIXED(70) } } }) {
			gv_texto(CLAY_STRING("Volume"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("vd-vol-c"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			float v01 = (float)V.p.volume / 200.0f;
			if (gv_deslizante(CLAY_ID("vd-vol"), &v01, 4, 14)) {
				int v = (int)lroundf(v01 * 200);
				V.p.volume = abs(v - 100) < 4 ? 100 : v; /* o 100% gruda */
			}
		}
		CLAY(CLAY_ID("vd-vol-n"), { .layout = { .sizing = { CLAY_SIZING_FIXED(52) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			gv_texto(gv_fmt("%d%%", V.p.volume), gv_t.tamanho_corpo, V.p.mudo ? gv_t.texto_suave : gv_t.texto, GV_PESO_NORMAL);
		}
	}
	if (!V.info.audio && !V.carregando) {
		gv_texto(CLAY_STRING("Este vídeo não tem áudio."), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	gv_separador(false, 0);
	if (gv_botao_ext(CLAY_ID("vd-girar"), &(gv_botao_opcoes){ .icone = "restore", .texto = "Girar vídeo", .estilo = GV_BOTAO_CONTORNO, .altura = 44 })) {
		V.p.giro = (V.p.giro + 1) & 3;
		tocar_parar();
		pedir_quadro();
		pedir_tiras();
	}
	if (V.p.giro) {
		gv_texto(gv_fmt("Girado %d° para a direita", V.p.giro * 90), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	gv_texto(CLAY_STRING("A prévia toca com o som (o volume e o mudo valem nela e na cópia). O som pode atrasar um instante em relação à imagem."), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
}

static void barra_do_alto(void) {
	bool alterado = ev_alterado(&V.p, V.info.duracao);
	CLAY(CLAY_ID("vd-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 16, 16, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		if (gv_botao_ext(CLAY_ID("vd-voltar"), &(gv_botao_opcoes){ .icone = "arrow-left", .texto = "Voltar à galeria", .estilo = GV_BOTAO_SIMPLES, .altura = 42, .desativado = V.exportando })) {
			sair();
		}
		CLAY(CLAY_ID("vd-nome"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } }, .clip = { .horizontal = true } }) {
			gv_icone("video", 22, gv_t.texto_secundario);
			gv_texto(gv_str(V.nome), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
		}
		if (gv_botao_ext(CLAY_ID("vd-cancelar"), &(gv_botao_opcoes){ .texto = V.exportando ? "Cancelar exportação" : "Cancelar", .estilo = GV_BOTAO_CONTORNO, .altura = 42 })) {
			sair();
		}
		const char *rot = V.exportando ? gv_fmt("Exportando %d%%", exp_permil / 10).chars : "Exportar cópia";
		if (gv_botao_ext(CLAY_ID("vd-exportar"), &(gv_botao_opcoes){ .texto = rot, .estilo = GV_BOTAO_CHEIO, .altura = 42, .desativado = V.exportando || V.carregando || !alterado })) {
			exportar();
		}
	}
}

static void dialogo_sair(void) {
	CLAY(CLAY_ID("vd-dlg"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("vd-dlg-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(440), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(22), .childGap = 14 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Descartar as alterações?"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Você mexeu neste vídeo e ainda não exportou uma cópia. O original não muda de qualquer jeito."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("vd-dlg-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_botao_ext(CLAY_ID("vd-dlg-continuar"), &(gv_botao_opcoes){ .texto = "Continuar editando", .estilo = GV_BOTAO_ELEVADO })) {
					V.dlg_sair = false;
				}
				if (gv_botao_ext(CLAY_ID("vd-dlg-descartar"), &(gv_botao_opcoes){ .texto = "Descartar", .estilo = GV_BOTAO_PERIGO })) {
					V.dlg_sair = false;
					fechar();
				}
			}
		}
	}
}

static void ir_para(double t) {
	if (V.tocando) {
		tocar_parar();
	}
	V.t = CLAMP(t, 0, V.info.duracao);
	pedir_quadro();
}

static void teclado(void) {
	if (V.dlg_sair) {
		if (gv_tecla() == 0xff1b) {
			V.dlg_sair = false;
		}
		return;
	}
	for (int i = 0;; i++) {
		uint32_t t = gv_tecla_n(i);
		if (!t) {
			break;
		}
		if (t == 0xff1b) {
			sair();
			return;
		} else if (t == ' ') {
			if (!gv_focado(CLAY_ID("vd-play"))) { /* com o botão em foco, é ele que trata a barra de espaço */
				alternar_toque();
			}
		} else if (t == 0xff51) {
			ir_para(V.t - 1);
		} else if (t == 0xff53) {
			ir_para(V.t + 1);
		} else if (t == 'i' || t == 'I') {
			V.p.inicio = V.t;
			ev_ajustar(&V.p, V.info.duracao, MIN_TRECHO);
		} else if (t == 'o' || t == 'O') {
			V.p.fim = V.t;
			ev_ajustar(&V.p, V.info.duracao, MIN_TRECHO);
		}
	}
}

void editor_video_desenhar(void) {
	if (!V.ativo) {
		G.tela = T_BIBLIOTECA;
		return;
	}
	teclado();
	if (V.tocando && (aud_vol != V.p.volume || aud_mudo != V.p.mudo)) {
		audio_iniciar(V.t);   /* mexeu no volume ou no mudo tocando: o som recomeça daqui */
	}
	barra_do_alto();
	if (V.carregando || V.info.duracao <= 0) {
		CLAY(CLAY_ID("vd-aguarde"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_texto(CLAY_STRING("Abrindo o vídeo…"), gv_t.tamanho_corpo + 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	} else {
		if (V.tiras_giro != V.p.giro && !V.tiras_pedidas) {
			pedir_tiras();
		}
		CLAY(CLAY_ID("vd-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			CLAY(CLAY_ID("vd-esq"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
				CLAY(CLAY_ID("vd-previa"), gv_livre(desenhar_previa, NULL, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {}
				CLAY(CLAY_ID("vd-controles"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .childGap = 14, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
					if (gv_botao_redondo(CLAY_ID("vd-play"), V.tocando ? "pause" : "play", 48, false, false)) {
						alternar_toque();
					}
					gv_texto(gv_fmt("%s / %s", tempo_texto(V.t), tempo_texto(V.info.duracao)), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
				}
				Clay_ElementId faixa = CLAY_ID("vd-faixa");
				CLAY(CLAY_ID("vd-faixa-cx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(136) }, .padding = { 14, 14, 0, 14 } } }) {
					CLAY(faixa, gv_livre(desenhar_faixa, NULL, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {}
				}
				interacao_da_faixa(faixa);
			}
			Clay_ElementId rol = CLAY_ID("vd-painel-rol");
			CLAY(CLAY_ID("vd-painel"), { .layout = { .sizing = { CLAY_SIZING_FIXED(372), CLAY_SIZING_GROW(0) } }, .border = { .color = gv_t.borda, .width = { .left = 1 } } }) {
				CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
					.padding = { 24, 24, 20, 18 }, .childGap = 10 } })) {
					painel();
				}
			}
		}
	}
	if (V.dlg_sair) {
		dialogo_sair();
	}
	if (V.exportando || V.tocando || V.qrodando || V.tiras_pedidas) {
		gv_acordar_em(0.25);
	}
}

/* ---- para os testes (LUMNA_GALERIA_DEBUG) ---- */

void editor_video_estado(GString *s) {
	if (!V.ativo) {
		return;
	}
	char px[32] = "-1,-1,-1";
	if (V.quadro && V.fw > 0) {
		cairo_surface_flush(V.quadro);
		const unsigned char *d = cairo_image_surface_get_data(V.quadro);
		int st = cairo_image_surface_get_stride(V.quadro);
		uint32_t v = *(const uint32_t *)(d + (gsize)(V.fh / 2) * (gsize)st + (gsize)(V.fw / 2) * 4);
		g_snprintf(px, sizeof(px), "%d,%d,%d", (int)((v >> 16) & 255), (int)((v >> 8) & 255), (int)(v & 255));
	}
	int tiras = 0;
	for (int i = 0; i < TIRAS; i++) {
		tiras += V.tiras[i] != NULL;
	}
	g_string_append_printf(s, " ev=1 ev_t=%.2f ev_ini=%.2f ev_fim=%.2f ev_dur=%.2f ev_mudo=%d ev_vol=%d ev_giro=%d ev_play=%d ev_quadro=%dx%d ev_tiras=%d ev_px=%s ev_exp=%d ev_alt=%d"
		" ev_dlg=%d ev_carregando=%d ev_audio=%d ev_busy=%d ev_aud=%d",
		V.t, V.p.inicio, V.p.fim, V.info.duracao, V.p.mudo, V.p.volume, V.p.giro, V.tocando, V.fw, V.fh, tiras, px, exp_permil, ev_alterado(&V.p, V.info.duracao),
		V.dlg_sair, V.carregando, V.info.audio, V.qrodando, aud_pid != 0);
}
