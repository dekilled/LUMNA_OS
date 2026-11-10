/* O serviço do editor de fotos (servicos/edicao.c): geometria, luz, cor, filtros, recorte, abrir (EXIF) e salvar a cópia.
 * ./testa_edicao PASTA   (PASTA com as fotos de fixtures_edicao.py e espaço para as cópias) */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "edicao.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { falhas++; fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static GdkPixbuf *cinza(int w, int h, int v, bool alfa) {
	GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, alfa, 8, w, h);
	gdk_pixbuf_fill(pb, ((guint32)v << 24) | ((guint32)v << 16) | ((guint32)v << 8) | 0xff);
	return pb;
}

static const guchar *px(GdkPixbuf *pb, int x, int y) {
	return gdk_pixbuf_get_pixels(pb) + (gsize)y * (gsize)gdk_pixbuf_get_rowstride(pb) + (gsize)x * (gsize)gdk_pixbuf_get_n_channels(pb);
}

static bool iguais(GdkPixbuf *a, GdkPixbuf *b) {
	if (gdk_pixbuf_get_width(a) != gdk_pixbuf_get_width(b) || gdk_pixbuf_get_height(a) != gdk_pixbuf_get_height(b)) {
		return false;
	}
	for (int y = 0; y < gdk_pixbuf_get_height(a); y++) {
		for (int x = 0; x < gdk_pixbuf_get_width(a); x++) {
			if (memcmp(px(a, x, y), px(b, x, y), 3)) {
				return false;
			}
		}
	}
	return true;
}

/* um pixel (v, v, v) depois dos ajustes */
static int um(int v, const ed_params *p, int canal) {
	GdkPixbuf *pb = cinza(2, 2, v, false);
	ed_cor(pb, p);
	int r = px(pb, 0, 0)[canal];
	g_object_unref(pb);
	return r;
}

static void teste_params(void) {
	ed_params p, q;
	ed_padrao(&p);
	ed_padrao(&q);
	CONFERE(!ed_alterado(&p) && !ed_tem_cor(&p), "o padrão é o original");
	CONFERE(ed_igual(&p, &q), "dois padrões são iguais");
	p.contraste = 5;
	CONFERE(ed_alterado(&p) && ed_tem_cor(&p) && !ed_igual(&p, &q), "um ajuste muda");
	ed_zerar_cor(&p);
	CONFERE(!ed_alterado(&p), "Restaurar ajustes volta ao padrão");
	p.filtro = ED_F_SEPIA;
	p.intensidade = 0;
	CONFERE(!ed_tem_cor(&p), "filtro com intensidade 0 não conta");
	p.intensidade = 100;
	ed_zerar_cor(&p);
	CONFERE(ed_tem_cor(&p), "Restaurar ajustes não mexe no filtro");
	ed_padrao(&p);
	p.recorte[2] = 0.5f;
	CONFERE(ed_alterado(&p), "o recorte conta");
}

/* a foto 5x3 de pixels todos diferentes, para ver para onde cada um vai */
static GdkPixbuf *xadrez(void) {
	GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 5, 3);
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 5; x++) {
			guchar *l = (guchar *)px(pb, x, y);
			l[0] = (guchar)(x * 50 + 5);
			l[1] = (guchar)(y * 80 + 7);
			l[2] = (guchar)(x * 10 + y);
		}
	}
	return pb;
}

static void teste_geometria(void) {
	GdkPixbuf *src = xadrez();
	ed_params p;
	ed_padrao(&p);
	GdkPixbuf *g = ed_geometria(src, &p, true);
	CONFERE(iguais(g, src) && g != src, "sem mudar nada, a foto é a mesma (numa cópia)");
	g_object_unref(g);
	p.giro = 1;
	g = ed_geometria(src, &p, true);
	CONFERE(gdk_pixbuf_get_width(g) == 3 && gdk_pixbuf_get_height(g) == 5, "girar um quarto troca largura e altura");
	/* o pixel de cima à esquerda vai para cima à direita (sentido horário) */
	CONFERE(!memcmp(px(g, 2, 0), px(src, 0, 0), 3), "o canto de cima à esquerda vai para cima à direita");
	g_object_unref(g);
	/* girar 4 vezes volta */
	ed_padrao(&p);
	for (int i = 0; i < 4; i++) {
		ed_girar(&p, 1);
	}
	g = ed_geometria(src, &p, true);
	CONFERE(iguais(g, src) && p.giro == 0, "quatro giros à direita voltam ao começo");
	g_object_unref(g);
	/* o recorte: 2x1 no canto de baixo à direita */
	ed_padrao(&p);
	p.recorte[0] = 0.6f;
	p.recorte[1] = 0.67f;
	p.recorte[2] = 0.4f;
	p.recorte[3] = 0.33f;
	g = ed_geometria(src, &p, true);
	CONFERE(gdk_pixbuf_get_width(g) == 2 && gdk_pixbuf_get_height(g) == 1, "recorte %dx%d", gdk_pixbuf_get_width(g), gdk_pixbuf_get_height(g));
	CONFERE(!memcmp(px(g, 1, 0), px(src, 4, 2), 3), "o recorte pega o canto certo");
	g_object_unref(g);
	g = ed_geometria(src, &p, false);
	CONFERE(gdk_pixbuf_get_width(g) == 5, "sem recorte (a tela do recorte) a foto vem inteira");
	g_object_unref(g);
	/* recorte maluco não quebra: fora da foto, zero, negativo */
	float R[][4] = { { -1, -1, 0.0f, 0.0f }, { 2, 2, 1, 1 }, { 0.5f, 0.5f, -0.2f, 5 }, { 0, 0, 1e-9f, 1e-9f } };
	for (size_t i = 0; i < G_N_ELEMENTS(R); i++) {
		ed_padrao(&p);
		memcpy(p.recorte, R[i], sizeof(R[i]));
		g = ed_geometria(src, &p, true);
		CONFERE(g && gdk_pixbuf_get_width(g) >= 1 && gdk_pixbuf_get_height(g) >= 1, "recorte maluco %zu", i);
		if (g) {
			g_object_unref(g);
		}
	}
	g_object_unref(src);
}

/* o que a pessoa faz na tela (girar, espelhar) tem de dar o mesmo que fazer na foto, em qualquer ordem */
static void teste_giro_espelho(void) {
	GdkPixbuf *src = xadrez();
	GRand *rnd = g_rand_new_with_seed(42);
	for (int rodada = 0; rodada < 300; rodada++) {
		ed_params p;
		ed_padrao(&p);
		GdkPixbuf *ref = g_object_ref(src);
		int n = g_rand_int_range(rnd, 1, 9);
		char hist[64] = "";
		for (int i = 0; i < n; i++) {
			int op = g_rand_int_range(rnd, 0, 4);
			GdkPixbuf *novo = NULL;
			if (op == 0) {
				ed_girar(&p, 1);
				novo = gdk_pixbuf_rotate_simple(ref, GDK_PIXBUF_ROTATE_CLOCKWISE);
			} else if (op == 1) {
				ed_girar(&p, -1);
				novo = gdk_pixbuf_rotate_simple(ref, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
			} else if (op == 2) {
				ed_espelhar(&p, true);
				novo = gdk_pixbuf_flip(ref, TRUE);
			} else {
				ed_espelhar(&p, false);
				novo = gdk_pixbuf_flip(ref, FALSE);
			}
			g_strlcat(hist, (char[]){ (char)("DEHV"[op]), 0 }, sizeof(hist));
			g_object_unref(ref);
			ref = novo;
		}
		GdkPixbuf *g = ed_geometria(src, &p, true);
		CONFERE(iguais(g, ref), "giros e espelhos em sequência (%s) não batem com a conta direta", hist);
		g_object_unref(g);
		g_object_unref(ref);
	}
	g_rand_free(rnd);
	g_object_unref(src);
}

static void teste_cor(void) {
	ed_params p;
	ed_padrao(&p);
	for (int v = 0; v < 256; v += 51) {
		CONFERE(um(v, &p, 0) == v, "sem ajustes o pixel %d não muda", v);
	}
	/* exposição: +1 passo de luz (em luz linear) num cinza médio */
	p.exposicao = 1;
	int e1 = um(128, &p, 1);
	CONFERE(e1 >= 173 && e1 <= 177, "+1 EV: 128 -> %d (esperado ~175)", e1);
	p.exposicao = -1;
	int e2 = um(128, &p, 1);
	CONFERE(e2 >= 89 && e2 <= 93, "-1 EV: 128 -> %d (esperado ~91)", e2);
	p.exposicao = 3;
	CONFERE(um(255, &p, 0) == 255 && um(0, &p, 0) == 0, "o branco fica branco e o preto, preto, por mais luz que se dê");
	p.exposicao = 0.3f;
	CONFERE(um(128, &p, 0) > 128 && um(128, &p, 0) < 150, "+0,3 clareia pouco");
	/* contraste */
	ed_padrao(&p);
	p.contraste = 100;
	CONFERE(um(191, &p, 0) >= 253 && um(64, &p, 0) <= 2, "contraste +100 estoura os extremos (%d, %d)", um(191, &p, 0), um(64, &p, 0));
	CONFERE(abs(um(128, &p, 0) - 128) <= 1, "o contraste não mexe no cinza médio");
	p.contraste = -100;
	CONFERE(abs(um(30, &p, 0) - 128) <= 1 && abs(um(220, &p, 0) - 128) <= 1, "contraste -100 vai tudo para o cinza médio");
	/* saturação */
	ed_padrao(&p);
	GdkPixbuf *cor = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2, 2);
	gdk_pixbuf_fill(cor, 0xc83c3cff); /* (200, 60, 60) */
	p.saturacao = -100;
	ed_cor(cor, &p);
	const guchar *c = px(cor, 0, 0);
	CONFERE(abs(c[0] - c[1]) <= 1 && abs(c[1] - c[2]) <= 1, "saturação -100 deixa cinza (%d %d %d)", c[0], c[1], c[2]);
	g_object_unref(cor);
	cor = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2, 2);
	gdk_pixbuf_fill(cor, 0xc83c3cff);
	p.saturacao = 50;
	ed_cor(cor, &p);
	c = px(cor, 0, 0);
	CONFERE(c[0] > 200 && c[1] < 60, "saturação +50 acentua (%d %d %d)", c[0], c[1], c[2]);
	g_object_unref(cor);
	/* temperatura */
	ed_padrao(&p);
	p.temperatura = 100;
	CONFERE(um(128, &p, 0) > 128 && um(128, &p, 2) < 128 && abs(um(128, &p, 1) - 128) <= 1, "temperatura + esquenta (mais vermelho, menos azul)");
	p.temperatura = -100;
	CONFERE(um(128, &p, 0) < 128 && um(128, &p, 2) > 128, "temperatura - esfria");
	/* sombras e realces */
	ed_padrao(&p);
	p.sombras = 100;
	CONFERE(um(30, &p, 0) > 60 && um(255, &p, 0) == 255, "sombras + clareia o escuro e deixa o branco em paz");
	ed_padrao(&p);
	p.realces = -100;
	CONFERE(um(230, &p, 0) < 200 && um(0, &p, 0) == 0, "realces - escurece o claro e deixa o preto em paz");
	/* filtros */
	ed_padrao(&p);
	p.filtro = ED_F_PB;
	GdkPixbuf *k = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2, 2);
	gdk_pixbuf_fill(k, 0x3278c8ff);
	GdkPixbuf *k2 = gdk_pixbuf_copy(k);
	ed_cor(k, &p);
	c = px(k, 0, 0);
	CONFERE(c[0] == c[1] && c[1] == c[2], "P&B: os três canais iguais");
	p.filtro = ED_F_SEPIA;
	ed_cor(k2, &p);
	c = px(k2, 0, 0);
	CONFERE(c[0] > c[1] && c[1] > c[2], "Sépia: vermelho > verde > azul (%d %d %d)", c[0], c[1], c[2]);
	g_object_unref(k);
	g_object_unref(k2);
	/* a intensidade do filtro */
	for (int f = 1; f < ED_F_N; f++) {
		ed_padrao(&p);
		p.filtro = f;
		p.intensidade = 0;
		CONFERE(um(100, &p, 0) == 100 && um(100, &p, 2) == 100, "filtro %s com intensidade 0 não muda", ED_FILTRO_NOME[f]);
		p.intensidade = 100;
		int cheio = um(100, &p, 0);
		p.intensidade = 50;
		int meio = um(100, &p, 0);
		CONFERE(meio >= MIN(100, cheio) - 1 && meio <= MAX(100, cheio) + 1, "filtro %s a 50%% fica entre o original e o cheio (%d, %d)", ED_FILTRO_NOME[f], meio, cheio);
	}
	/* a transparência não é tocada */
	GdkPixbuf *a = cinza(4, 4, 100, true);
	guchar *l = (guchar *)px(a, 1, 1);
	l[3] = 77;
	ed_padrao(&p);
	p.exposicao = 1;
	p.filtro = ED_F_SEPIA;
	ed_cor(a, &p);
	CONFERE(px(a, 1, 1)[3] == 77, "o alfa continua o mesmo");
	g_object_unref(a);
	/* uma foto com rowstride maior que a largura (subpixbuf) */
	GdkPixbuf *grande = cinza(10, 10, 100, false);
	GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(grande, 2, 2, 4, 4);
	p.filtro = ED_F_NENHUM;
	ed_cor(sub, &p);
	CONFERE(px(grande, 3, 3)[0] > 100 && px(grande, 0, 0)[0] == 100 && px(grande, 7, 7)[0] == 100, "a conta respeita o rowstride (só o miolo mudou)");
	g_object_unref(sub);
	g_object_unref(grande);
}

static void teste_recorte(void) {
	float r[4];
	ed_recorte_proporcao(r, 1.0f, 4000, 3000);
	CONFERE(fabsf(r[2] - 0.75f) < 1e-4 && fabsf(r[3] - 1) < 1e-4 && fabsf(r[0] - 0.125f) < 1e-4 && r[1] == 0, "1:1 numa foto 4:3: %.3f %.3f %.3f %.3f", r[0], r[1], r[2], r[3]);
	ed_recorte_proporcao(r, 16.0f / 9.0f, 4000, 3000);
	CONFERE(fabsf(r[2] - 1) < 1e-4 && fabsf(r[3] - 0.75f) < 1e-4, "16:9 numa foto 4:3");
	ed_recorte_proporcao(r, 9.0f / 16.0f, 4000, 3000);
	CONFERE(fabsf(r[3] - 1) < 1e-4 && fabsf(r[2] * 4000 / (r[3] * 3000) - 9.0f / 16.0f) < 1e-3, "9:16 numa foto 4:3");
	ed_recorte_proporcao(r, 0, 4000, 3000);
	CONFERE(r[2] == 1 && r[3] == 1, "livre: a foto toda");
	/* mover nunca sai da foto */
	float m[4] = { 0.2f, 0.2f, 0.5f, 0.5f };
	ed_recorte_arrastar(m, ED_ALCA_MOVER, 5, 5, 0, 4000, 3000);
	CONFERE(fabsf(m[0] - 0.5f) < 1e-4 && fabsf(m[1] - 0.5f) < 1e-4 && m[2] == 0.5f, "mover para além do canto para no canto");
	ed_recorte_arrastar(m, ED_ALCA_MOVER, -9, -9, 0, 4000, 3000);
	CONFERE(m[0] == 0 && m[1] == 0, "mover para o outro canto");
	/* a alça leste (livre) */
	float l[4] = { 0.1f, 0.1f, 0.4f, 0.4f };
	ed_recorte_arrastar(l, ED_ALCA_E, 0.2f, 0, 0, 4000, 3000);
	CONFERE(fabsf(l[0] - 0.1f) < 1e-4 && fabsf(l[2] - 0.6f) < 1e-4 && fabsf(l[3] - 0.4f) < 1e-4, "alça leste só estica a largura");
	ed_recorte_arrastar(l, ED_ALCA_O, 9, 0, 0, 4000, 3000);
	CONFERE(l[2] * 4000 >= 15.9f && l[0] + l[2] <= 0.7001f, "a alça oeste não passa da outra borda (fica com 16 px)");
	/* de olho nas invariantes, a esmo: dentro da foto, 16 px no mínimo, e a proporção quando pedida */
	GRand *rnd = g_rand_new_with_seed(7);
	const int TAM[][2] = { { 4000, 3000 }, { 100, 80 }, { 3000, 4000 }, { 640, 640 } };
	const float PROP[] = { 0, 1, 4.0f / 3, 3.0f / 2, 16.0f / 9, 9.0f / 16, 3.0f / 4 };
	int ruins = 0;
	for (int i = 0; i < 20000 && ruins < 5; i++) {
		int W = TAM[i % 4][0], H = TAM[i % 4][1];
		float prop = PROP[g_rand_int_range(rnd, 0, 7)];
		float r2[4];
		ed_recorte_proporcao(r2, prop, W, H);
		/* parte de um retângulo qualquer dentro da foto (com a proporção, se pedida) */
		float s = (float)g_rand_double_range(rnd, 0.5, 1);
		float ww = r2[2] * s, hh = r2[3] * s;
		r2[0] += (r2[2] - ww) * (float)g_rand_double(rnd);
		r2[1] += (r2[3] - hh) * (float)g_rand_double(rnd);
		r2[2] = ww;
		r2[3] = hh;
		int alca = g_rand_int_range(rnd, 0, 9);
		for (int k = 0; k < 3; k++) {
			ed_recorte_arrastar(r2, alca, (float)g_rand_double_range(rnd, -0.6, 0.6), (float)g_rand_double_range(rnd, -0.6, 0.6), prop, W, H);
			float pw = r2[2] * (float)W, ph = r2[3] * (float)H;
			bool dentro = r2[0] >= -1e-5f && r2[1] >= -1e-5f && r2[0] + r2[2] <= 1 + 1e-4f && r2[1] + r2[3] <= 1 + 1e-4f;
			bool grande = pw >= 15.99f && ph >= 15.99f;
			bool propor = prop <= 0 || alca == ED_ALCA_MOVER || fabsf(pw / ph - prop) / prop < 0.01f;
			if (!dentro || !grande || !propor) {
				ruins++;
				CONFERE(false, "recorte %dx%d alça %d prop %.3f: %.4f %.4f %.4f %.4f (%.1fx%.1f px) dentro=%d grande=%d proporção=%d", W, H, alca, prop, r2[0], r2[1], r2[2], r2[3], pw, ph, dentro, grande, propor);
				break;
			}
		}
	}
	g_rand_free(rnd);
}

static GdkPixbuf *abre(const char *pasta, const char *nome, int max) {
	char *c = g_build_filename(pasta, nome, NULL);
	GError *e = NULL;
	GdkPixbuf *pb = ed_abrir(c, max, &e);
	g_free(c);
	g_clear_error(&e);
	return pb;
}

static bool corpo_igual(const char *a, const char *b) {
	gchar *da = NULL, *db = NULL;
	gsize la = 0, lb = 0;
	bool r = g_file_get_contents(a, &da, &la, NULL) && g_file_get_contents(b, &db, &lb, NULL) && la == lb && !memcmp(da, db, la);
	g_free(da);
	g_free(db);
	return r;
}

static void teste_abrir_e_salvar(const char *pasta) {
	/* EXIF: a foto "deitada" (Orientation 6) vem em pé, com os cantos nos lugares certos */
	GdkPixbuf *g = abre(pasta, "girada.jpg", 0);
	CONFERE(g && gdk_pixbuf_get_width(g) == 480 && gdk_pixbuf_get_height(g) == 640, "a orientação do EXIF é aplicada (%dx%d)", g ? gdk_pixbuf_get_width(g) : 0, g ? gdk_pixbuf_get_height(g) : 0);
	if (g) {
		const guchar *tl = px(g, 10, 10), *tr = px(g, 470, 10), *bl = px(g, 10, 630), *br = px(g, 470, 630);
		CONFERE(tl[2] > 200 && tl[0] < 60, "azul em cima à esquerda");
		CONFERE(tr[0] > 200 && tr[1] < 60, "vermelho em cima à direita");
		CONFERE(bl[0] > 200 && bl[1] > 200 && bl[2] < 60, "amarelo embaixo à esquerda");
		CONFERE(br[1] > 200 && br[0] < 60, "verde embaixo à direita");
		g_object_unref(g);
	}
	g = abre(pasta, "ruido.jpg", 200);
	CONFERE(g && gdk_pixbuf_get_width(g) == 200 && gdk_pixbuf_get_height(g) == 150, "reduz até o lado pedido (%d)", g ? gdk_pixbuf_get_width(g) : 0);
	if (g) {
		g_object_unref(g);
	}
	g = abre(pasta, "ruido.jpg", 5000);
	CONFERE(g && gdk_pixbuf_get_width(g) == 800, "não aumenta");
	if (g) {
		g_object_unref(g);
	}
	CONFERE(!abre(pasta, "quebrada.jpg", 0), "o arquivo quebrado dá erro, não cai");
	CONFERE(!abre(pasta, "nao-existe.jpg", 0), "o que não existe dá erro");

	/* salvar a cópia */
	char *orig = g_build_filename(pasta, "ruido.jpg", NULL);
	char *antes = g_strdup_printf("%s.bak", orig);
	{
		gchar *d = NULL;
		gsize n = 0;
		g_file_get_contents(orig, &d, &n, NULL);
		g_file_set_contents(antes, d, (gssize)n, NULL);
		g_free(d);
	}
	ed_params p;
	ed_padrao(&p);
	p.exposicao = 1;
	p.recorte[0] = 0.25f;
	p.recorte[1] = 0.25f;
	p.recorte[2] = 0.5f;
	p.recorte[3] = 0.5f;
	GError *e = NULL;
	time_t data = 1700000000;
	char *c1 = ed_salvar_copia(orig, &p, 0, 90, data, &e);
	CONFERE(c1 && !e, "salvou a cópia: %s", e ? e->message : "");
	g_clear_error(&e);
	char *esp = g_build_filename(pasta, "ruido-editada.jpg", NULL);
	CONFERE(c1 && !strcmp(c1, esp), "o nome da cópia: %s", c1 ? c1 : "(nulo)");
	CONFERE(corpo_igual(orig, antes), "o original não mudou nem um byte");
	if (c1) {
		GdkPixbuf *cp = gdk_pixbuf_new_from_file(c1, NULL), *oo = gdk_pixbuf_new_from_file(orig, NULL);
		CONFERE(cp && gdk_pixbuf_get_width(cp) == 400 && gdk_pixbuf_get_height(cp) == 300, "a cópia tem o tamanho do recorte");
		if (cp && oo) {
			/* o miolo da cópia é mais claro que o do original */
			long soma_c = 0, soma_o = 0;
			for (int y = 0; y < 300; y++) {
				for (int x = 0; x < 400; x++) {
					soma_c += px(cp, x, y)[1];
					soma_o += px(oo, x + 200, y + 150)[1];
				}
			}
			CONFERE(soma_c > soma_o * 11 / 10, "a cópia ficou mais clara (%ld > %ld)", soma_c, soma_o);
		}
		if (cp) {
			g_object_unref(cp);
		}
		if (oo) {
			g_object_unref(oo);
		}
		struct stat st;
		CONFERE(stat(c1, &st) == 0 && st.st_mtime == data, "a cópia leva a data que se pediu");
	}
	char *c2 = ed_salvar_copia(orig, &p, 0, 90, 0, NULL);
	char *esp2 = g_build_filename(pasta, "ruido-editada-2.jpg", NULL);
	CONFERE(c2 && !strcmp(c2, esp2), "a segunda cópia não pisa na primeira: %s", c2 ? c2 : "(nulo)");
	/* a qualidade pesa */
	ed_padrao(&p);
	char *alta = ed_salvar_copia(orig, &p, 1, 100, 0, NULL), *baixa = ed_salvar_copia(orig, &p, 1, 50, 0, NULL);
	struct stat sa, sb;
	CONFERE(alta && baixa && stat(alta, &sa) == 0 && stat(baixa, &sb) == 0 && sb.st_size < sa.st_size * 8 / 10, "qualidade 50 pesa bem menos que 100 (%ld, %ld)", (long)(baixa ? sb.st_size : 0), (long)(alta ? sa.st_size : 0));
	/* PNG a pedido */
	char *png = ed_salvar_copia(orig, &p, 2, 90, 0, NULL);
	CONFERE(png && g_str_has_suffix(png, ".png"), "formato PNG a pedido");
	if (png) {
		GdkPixbuf *t = gdk_pixbuf_new_from_file(png, NULL);
		CONFERE(t && gdk_pixbuf_get_width(t) == 800, "o PNG abre com o tamanho todo");
		if (t) {
			g_object_unref(t);
		}
	}
	/* transparência: PNG fica PNG com alfa; vira JPEG sobre branco */
	char *alfa = g_build_filename(pasta, "alfa.png", NULL);
	char *a1 = ed_salvar_copia(alfa, &p, 0, 90, 0, NULL), *a2 = ed_salvar_copia(alfa, &p, 1, 90, 0, NULL);
	CONFERE(a1 && g_str_has_suffix(a1, ".png"), "PNG no formato 'o mesmo' continua PNG");
	if (a1) {
		GdkPixbuf *t = gdk_pixbuf_new_from_file(a1, NULL);
		CONFERE(t && gdk_pixbuf_get_has_alpha(t) && px(t, 150, 50)[3] == 0, "a transparência foi mantida");
		if (t) {
			g_object_unref(t);
		}
	}
	CONFERE(a2 && g_str_has_suffix(a2, ".jpg"), "PNG com alfa virando JPEG");
	if (a2) {
		GdkPixbuf *t = gdk_pixbuf_new_from_file(a2, NULL);
		CONFERE(t && !gdk_pixbuf_get_has_alpha(t) && px(t, 150, 50)[0] > 240 && px(t, 150, 50)[2] > 240, "a parte transparente virou branco");
		if (t) {
			g_object_unref(t);
		}
	}
	/* WebP: o 'mesmo do original' cai em JPEG (não gravamos WebP) */
	char *web = g_build_filename(pasta, "foto.webp", NULL);
	if (g_file_test(web, G_FILE_TEST_EXISTS) && gdk_pixbuf_get_file_info(web, NULL, NULL)) {
		char *w1 = ed_salvar_copia(web, &p, 0, 90, 0, NULL);
		CONFERE(w1 && g_str_has_suffix(w1, ".jpg"), "WebP vira JPEG: %s", w1 ? w1 : "(nulo)");
		g_free(w1);
	}
	/* erros */
	GError *e2 = NULL;
	char *quebrada = g_build_filename(pasta, "quebrada.jpg", NULL);
	char *q = ed_salvar_copia(quebrada, &p, 0, 90, 0, &e2);
	g_free(quebrada);
	CONFERE(!q && e2, "foto quebrada: erro e nenhuma cópia");
	g_clear_error(&e2);
	char *q2 = ed_salvar_copia("/nao/existe.jpg", &p, 0, 90, 0, &e2);
	CONFERE(!q2 && e2, "foto que não existe: erro");
	g_clear_error(&e2);
	/* nenhum resto de arquivo temporário */
	GDir *d = g_dir_open(pasta, 0, NULL);
	const char *n;
	while (d && (n = g_dir_read_name(d))) {
		CONFERE(!strstr(n, "lumna-tmp"), "sobrou um temporário: %s", n);
	}
	if (d) {
		g_dir_close(d);
	}
	g_free(orig); g_free(antes); g_free(c1); g_free(c2); g_free(esp); g_free(esp2); g_free(alta); g_free(baixa); g_free(png);
	g_free(alfa); g_free(a1); g_free(a2); g_free(web);
}

static void teste_velocidade(void) {
	GdkPixbuf *pb = cinza(1600, 1200, 120, false);
	ed_params p;
	ed_padrao(&p);
	p.exposicao = 0.3f;
	p.contraste = 12;
	p.realces = -18;
	p.sombras = 15;
	p.temperatura = 6;
	p.saturacao = 8;
	p.filtro = ED_F_VINTAGE;
	gint64 t0 = g_get_monotonic_time();
	for (int i = 0; i < 5; i++) {
		GdkPixbuf *o = ed_processar(pb, &p, true);
		g_object_unref(o);
	}
	double ms = (double)(g_get_monotonic_time() - t0) / 5000.0;
	printf("edicao: prévia 1600x1200 com tudo ligado: %.1f ms\n", ms);
	CONFERE(ms < 400, "a prévia demora demais: %.1f ms", ms);
	g_object_unref(pb);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "uso: testa_edicao PASTA\n");
		return 2;
	}
	teste_params();
	teste_geometria();
	teste_giro_espelho();
	teste_cor();
	teste_recorte();
	teste_abrir_e_salvar(argv[1]);
	teste_velocidade();
	if (falhas) {
		fprintf(stderr, "edicao: %d falha(s)\n", falhas);
		return 1;
	}
	printf("edicao (serviço): ok\n");
	return 0;
}
