#include "edicao.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>

const char *const ED_FILTRO_NOME[ED_F_N] = { "Original", "P&B", "Sépia", "Vívido", "Frio", "Quente", "Desbotado", "Dramático", "Vintage" };

#define MIN_PX 16.0f

void ed_padrao(ed_params *p) {
	memset(p, 0, sizeof(*p));
	p->recorte[2] = p->recorte[3] = 1;
	p->intensidade = 100;
}

bool ed_igual(const ed_params *a, const ed_params *b) {
	return memcmp(a, b, sizeof(*a)) == 0; /* o struct é zerado em ed_padrao e só tem números e bool: sem sobra de preenchimento nos campos usados */
}

bool ed_tem_cor(const ed_params *p) {
	return p->exposicao != 0 || p->contraste || p->realces || p->sombras || p->temperatura || p->saturacao || (p->filtro != ED_F_NENHUM && p->intensidade > 0);
}

static bool recorte_inteiro(const float *r) {
	return r[0] <= 0.0005f && r[1] <= 0.0005f && r[2] >= 0.9995f && r[3] >= 0.9995f;
}

bool ed_alterado(const ed_params *p) {
	return ed_tem_cor(p) || p->giro || p->espelho_h || p->espelho_v || !recorte_inteiro(p->recorte);
}

void ed_zerar_cor(ed_params *p) {
	p->exposicao = 0;
	p->contraste = p->realces = p->sombras = p->temperatura = p->saturacao = 0;
}

static void recorte_zerar(ed_params *p) {
	p->recorte[0] = p->recorte[1] = 0;
	p->recorte[2] = p->recorte[3] = 1;
}

void ed_girar(ed_params *p, int dir) {
	p->giro = ((p->giro + (dir >= 0 ? 1 : 3)) % 4 + 4) % 4;
	if (p->espelho_h != p->espelho_v) { /* girar uma foto espelhada troca o eixo do espelho (R·Fh = Fv·R) */
		p->espelho_h = !p->espelho_h;
		p->espelho_v = !p->espelho_v;
	}
	recorte_zerar(p);
}

void ed_espelhar(ed_params *p, bool horizontal) {
	if (horizontal) {
		p->espelho_h = !p->espelho_h;
	} else {
		p->espelho_v = !p->espelho_v;
	}
	recorte_zerar(p);
}

/* ---- a geometria ---- */

void ed_recorte_pixels(const float r[4], int W, int H, int *x, int *y, int *w, int *h) {
	int x0 = (int)floorf(r[0] * (float)W + 0.5f), y0 = (int)floorf(r[1] * (float)H + 0.5f);
	int x1 = (int)floorf((r[0] + r[2]) * (float)W + 0.5f), y1 = (int)floorf((r[1] + r[3]) * (float)H + 0.5f);
	x0 = CLAMP(x0, 0, W - 1);
	y0 = CLAMP(y0, 0, H - 1);
	x1 = CLAMP(x1, x0 + 1, W);
	y1 = CLAMP(y1, y0 + 1, H);
	*x = x0;
	*y = y0;
	*w = x1 - x0;
	*h = y1 - y0;
}

GdkPixbuf *ed_geometria(GdkPixbuf *src, const ed_params *p, bool com_recorte) {
	GdkPixbuf *cur = g_object_ref(src);
	static const GdkPixbufRotation R[4] = { GDK_PIXBUF_ROTATE_NONE, GDK_PIXBUF_ROTATE_CLOCKWISE, GDK_PIXBUF_ROTATE_UPSIDEDOWN, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE };
	if (p->giro & 3) {
		GdkPixbuf *n = gdk_pixbuf_rotate_simple(cur, R[p->giro & 3]);
		g_object_unref(cur);
		cur = n;
	}
	if (p->espelho_h) {
		GdkPixbuf *n = gdk_pixbuf_flip(cur, TRUE);
		g_object_unref(cur);
		cur = n;
	}
	if (p->espelho_v) {
		GdkPixbuf *n = gdk_pixbuf_flip(cur, FALSE);
		g_object_unref(cur);
		cur = n;
	}
	if (com_recorte && !recorte_inteiro(p->recorte)) {
		int x, y, w, h;
		ed_recorte_pixels(p->recorte, gdk_pixbuf_get_width(cur), gdk_pixbuf_get_height(cur), &x, &y, &w, &h);
		GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(cur, x, y, w, h);
		GdkPixbuf *n = gdk_pixbuf_copy(sub); /* o recorte vira uma foto própria (e gravável) */
		g_object_unref(sub);
		g_object_unref(cur);
		cur = n;
	} else if (cur == src) {
		GdkPixbuf *n = gdk_pixbuf_copy(src); /* sempre uma cópia: quem chama pode gravar nela */
		g_object_unref(cur);
		cur = n;
	}
	return cur;
}

/* ---- luz, cor e filtros ---- */

static float lin_de_srgb[256];
#define ENC_N 4096
static float srgb_de_lin[ENC_N + 1];
static GOnce tabelas_uma_vez = G_ONCE_INIT;

static gpointer tabelas_iniciar(gpointer d) {
	(void)d;
	for (int i = 0; i < 256; i++) {
		float v = (float)i / 255.0f;
		lin_de_srgb[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
	}
	for (int i = 0; i <= ENC_N; i++) {
		float l = (float)i / ENC_N;
		srgb_de_lin[i] = l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1 / 2.4f) - 0.055f;
	}
	return NULL;
}

static inline float codifica(float l) {
	if (l <= 0) {
		return 0;
	}
	if (l >= 1) {
		return 1;
	}
	return srgb_de_lin[(int)(l * ENC_N + 0.5f)];
}

static inline float limita(float v) {
	return v < 0 ? 0 : v > 1 ? 1 : v;
}

static inline float luma(float r, float g, float b) {
	return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

static inline void sat_em(float *r, float *g, float *b, float fator) {
	float y = luma(*r, *g, *b);
	*r = y + (*r - y) * fator;
	*g = y + (*g - y) * fator;
	*b = y + (*b - y) * fator;
}

static inline void contraste_em(float *r, float *g, float *b, float fator) {
	*r = 0.5f + (*r - 0.5f) * fator;
	*g = 0.5f + (*g - 0.5f) * fator;
	*b = 0.5f + (*b - 0.5f) * fator;
}

static void filtro_aplicar(int f, float *r, float *g, float *b) {
	switch (f) {
	case ED_F_PB: {
		float y = limita(0.5f + (luma(*r, *g, *b) - 0.5f) * 1.1f);
		*r = *g = *b = y;
		break;
	}
	case ED_F_SEPIA: {
		float R = *r, G = *g, B = *b;
		*r = 0.393f * R + 0.769f * G + 0.189f * B;
		*g = 0.349f * R + 0.686f * G + 0.168f * B;
		*b = 0.272f * R + 0.534f * G + 0.131f * B;
		break;
	}
	case ED_F_VIVIDO:
		sat_em(r, g, b, 1.4f);
		contraste_em(r, g, b, 1.1f);
		break;
	case ED_F_FRIO:
		*r *= 0.93f;
		*b = *b * 1.07f + 0.01f;
		break;
	case ED_F_QUENTE:
		*r = *r * 1.07f + 0.01f;
		*b *= 0.9f;
		break;
	case ED_F_DESBOTADO:
		*r = *r * 0.82f + 0.12f;
		*g = *g * 0.82f + 0.12f;
		*b = *b * 0.82f + 0.12f;
		sat_em(r, g, b, 0.8f);
		break;
	case ED_F_DRAMATICO:
		*r *= 0.92f;
		*g *= 0.92f;
		*b *= 0.92f;
		contraste_em(r, g, b, 1.35f);
		sat_em(r, g, b, 0.85f);
		break;
	case ED_F_VINTAGE: {
		float R = *r * 0.85f + 0.08f, G = *g * 0.85f + 0.08f, B = *b * 0.85f + 0.08f;
		float sr = 0.393f * R + 0.769f * G + 0.189f * B, sg = 0.349f * R + 0.686f * G + 0.168f * B, sb = 0.272f * R + 0.534f * G + 0.131f * B;
		*r = R * 0.6f + sr * 0.4f;
		*g = G * 0.6f + sg * 0.4f;
		*b = B * 0.6f + sb * 0.4f;
		break;
	}
	default:
		break;
	}
}

void ed_cor(GdkPixbuf *pb, const ed_params *p) {
	if (!ed_tem_cor(p)) {
		return;
	}
	g_once(&tabelas_uma_vez, tabelas_iniciar, NULL);
	int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), nc = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
	guchar *base = gdk_pixbuf_get_pixels(pb);
	bool linear = p->exposicao != 0 || p->temperatura != 0;
	float mult = exp2f(p->exposicao);
	float t = (float)p->temperatura / 100.0f;
	float k_r = 1 + 0.30f * t, k_b = 1 - 0.30f * t;
	float som = (float)p->sombras / 100.0f * 0.6f, rea = (float)p->realces / 100.0f * 0.6f;
	float fc = 1 + (float)p->contraste / 100.0f;
	float fs = 1 + (float)p->saturacao / 100.0f;
	bool tonal = p->sombras || p->realces;
	float forca = p->filtro != ED_F_NENHUM ? (float)p->intensidade / 100.0f : 0;
	for (int y = 0; y < h; y++) {
		guchar *l = base + (gsize)y * (gsize)rs;
		for (int x = 0; x < w; x++, l += nc) {
			float r, g, b;
			if (linear) {
				r = lin_de_srgb[l[0]] * mult * k_r;
				g = lin_de_srgb[l[1]] * mult;
				b = lin_de_srgb[l[2]] * mult * k_b;
				r = codifica(r);
				g = codifica(g);
				b = codifica(b);
			} else {
				r = (float)l[0] / 255.0f;
				g = (float)l[1] / 255.0f;
				b = (float)l[2] / 255.0f;
			}
			if (tonal) {
				float Y = luma(r, g, b), inv = 1 - Y;
				float d = som * inv * inv + rea * Y * Y;
				r += d;
				g += d;
				b += d;
			}
			if (p->contraste) {
				contraste_em(&r, &g, &b, fc);
			}
			if (p->saturacao) {
				sat_em(&r, &g, &b, fs);
			}
			r = limita(r);
			g = limita(g);
			b = limita(b);
			if (forca > 0) {
				float fr = r, fg = g, fb = b;
				filtro_aplicar(p->filtro, &fr, &fg, &fb);
				r = limita(r + (limita(fr) - r) * forca);
				g = limita(g + (limita(fg) - g) * forca);
				b = limita(b + (limita(fb) - b) * forca);
			}
			l[0] = (guchar)(r * 255.0f + 0.5f);
			l[1] = (guchar)(g * 255.0f + 0.5f);
			l[2] = (guchar)(b * 255.0f + 0.5f);
		}
	}
}

GdkPixbuf *ed_processar(GdkPixbuf *src, const ed_params *p, bool com_recorte) {
	GdkPixbuf *g = ed_geometria(src, p, com_recorte);
	ed_cor(g, p);
	return g;
}

/* ---- o recorte ---- */

void ed_recorte_proporcao(float r[4], float prop, int W, int H) {
	if (prop <= 0 || W <= 0 || H <= 0) {
		r[0] = r[1] = 0;
		r[2] = r[3] = 1;
		return;
	}
	float w = (float)W, h = (float)W / prop;
	if (h > (float)H) {
		h = (float)H;
		w = h * prop;
	}
	r[2] = w / (float)W;
	r[3] = h / (float)H;
	r[0] = (1 - r[2]) / 2;
	r[1] = (1 - r[3]) / 2;
}

void ed_recorte_arrastar(float r[4], int alca, float dx, float dy, float prop, int W, int H) {
	float fw = (float)W, fh = (float)H;
	float x0 = r[0] * fw, y0 = r[1] * fh, x1 = x0 + r[2] * fw, y1 = y0 + r[3] * fh;
	float mx = dx * fw, my = dy * fh;
	if (alca == ED_ALCA_MOVER) {
		float w = x1 - x0, h = y1 - y0;
		x0 = CLAMP(x0 + mx, 0, fw - w);
		y0 = CLAMP(y0 + my, 0, fh - h);
		x1 = x0 + w;
		y1 = y0 + h;
	} else {
		bool oeste = alca == ED_ALCA_NO || alca == ED_ALCA_O || alca == ED_ALCA_SO;
		bool leste = alca == ED_ALCA_NE || alca == ED_ALCA_E || alca == ED_ALCA_SE;
		bool norte = alca == ED_ALCA_NO || alca == ED_ALCA_N || alca == ED_ALCA_NE;
		bool sul = alca == ED_ALCA_SO || alca == ED_ALCA_S || alca == ED_ALCA_SE;
		bool canto = (oeste || leste) && (norte || sul);
		/* a âncora é o lado (ou canto) oposto ao que se puxa */
		float ax = oeste ? x1 : x0, ay = norte ? y1 : y0;
		float w = x1 - x0, h = y1 - y0;
		if (oeste) {
			w = x1 - CLAMP(x0 + mx, 0, x1 - MIN_PX);
		} else if (leste) {
			w = CLAMP(x1 + mx, x0 + MIN_PX, fw) - x0;
		}
		if (norte) {
			h = y1 - CLAMP(y0 + my, 0, y1 - MIN_PX);
		} else if (sul) {
			h = CLAMP(y1 + my, y0 + MIN_PX, fh) - y0;
		}
		float wmax = (oeste ? x1 : fw - x0), hmax = (norte ? y1 : fh - y0);
		if (prop > 0) {
			if (canto) {
				if (w / h > prop) {
					w = h * prop;
				} else {
					h = w / prop;
				}
			} else if (oeste || leste) { /* a altura acompanha, em volta do centro */
				float cy = (y0 + y1) / 2;
				hmax = 2 * fminf(cy, fh - cy);
				h = w / prop;
			} else {
				float cx = (x0 + x1) / 2;
				wmax = 2 * fminf(cx, fw - cx);
				w = h * prop;
			}
			if (h > hmax) {
				h = hmax;
				w = h * prop;
			}
			if (w > wmax) {
				w = wmax;
				h = w / prop;
			}
			if (w < MIN_PX) {
				w = MIN_PX;
				h = w / prop;
			}
			if (h < MIN_PX) {
				h = MIN_PX;
				w = h * prop;
			}
		}
		if (oeste) {
			x0 = ax - w;
			x1 = ax;
		} else if (leste) {
			x0 = ax;
			x1 = ax + w;
		} else if (prop > 0) {
			float cx = (x0 + x1) / 2;
			x0 = cx - w / 2;
			x1 = cx + w / 2;
		}
		if (norte) {
			y0 = ay - h;
			y1 = ay;
		} else if (sul) {
			y0 = ay;
			y1 = ay + h;
		} else if (prop > 0) {
			float cy = (y0 + y1) / 2;
			y0 = cy - h / 2;
			y1 = cy + h / 2;
		}
	}
	r[0] = x0 / fw;
	r[1] = y0 / fh;
	r[2] = (x1 - x0) / fw;
	r[3] = (y1 - y0) / fh;
}

/* ---- abrir e salvar ---- */

GdkPixbuf *ed_abrir(const char *caminho, int max_lado, GError **erro) {
	int w = 0, h = 0;
	GdkPixbuf *pb;
	if (max_lado > 0 && gdk_pixbuf_get_file_info(caminho, &w, &h) && (w > max_lado || h > max_lado)) {
		pb = gdk_pixbuf_new_from_file_at_size(caminho, max_lado, max_lado, erro);
	} else {
		pb = gdk_pixbuf_new_from_file(caminho, erro);
	}
	if (!pb) {
		return NULL;
	}
	GdkPixbuf *o = gdk_pixbuf_apply_embedded_orientation(pb);
	g_object_unref(pb);
	if (!o) {
		return NULL;
	}
	if (!gdk_pixbuf_get_pixels(o) || gdk_pixbuf_get_bits_per_sample(o) != 8 || gdk_pixbuf_get_n_channels(o) < 3) {
		g_object_unref(o);
		g_set_error(erro, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_UNKNOWN_TYPE, "formato de pixel sem suporte");
		return NULL;
	}
	return o;
}

static char *copia_nome(const char *origem, const char *ext) {
	char *base = g_path_get_basename(origem), *dir = g_path_get_dirname(origem);
	char *ponto = strrchr(base, '.');
	if (ponto && ponto != base) {
		*ponto = 0;
	}
	char *r = NULL;
	for (int n = 1; n < 10000; n++) {
		char *nome = n == 1 ? g_strdup_printf("%s-editada.%s", base, ext) : g_strdup_printf("%s-editada-%d.%s", base, n, ext);
		char *c = g_build_filename(dir, nome, NULL);
		g_free(nome);
		if (!g_file_test(c, G_FILE_TEST_EXISTS)) {
			r = c;
			break;
		}
		g_free(c);
	}
	g_free(base);
	g_free(dir);
	return r;
}

char *ed_salvar_copia(const char *origem, const ed_params *p, int formato, int qualidade_jpeg, time_t data, GError **erro) {
	GdkPixbuf *src = ed_abrir(origem, 0, erro);
	if (!src) {
		return NULL;
	}
	GdkPixbuf *pb = ed_processar(src, p, true);
	g_object_unref(src);
	/* o formato da cópia */
	const char *tipo, *ext;
	char *ext_orig = strrchr(origem, '.') ? g_ascii_strdown(strrchr(origem, '.') + 1, -1) : g_strdup("");
	bool era_jpeg = !strcmp(ext_orig, "jpg") || !strcmp(ext_orig, "jpeg") || !strcmp(ext_orig, "jpe");
	bool era_png = !strcmp(ext_orig, "png");
	if (formato == 1 || (formato == 0 && era_jpeg)) {
		tipo = "jpeg";
		ext = formato == 0 ? ext_orig : "jpg";
	} else if (formato == 2 || (formato == 0 && era_png)) {
		tipo = "png";
		ext = "png";
	} else { /* o mesmo do original, mas um formato que não gravamos (webp, gif, bmp...): PNG se tem transparência, senão JPEG */
		tipo = gdk_pixbuf_get_has_alpha(pb) ? "png" : "jpeg";
		ext = gdk_pixbuf_get_has_alpha(pb) ? "png" : "jpg";
	}
	if (!strcmp(tipo, "jpeg") && gdk_pixbuf_get_has_alpha(pb)) { /* o JPEG não tem transparência: sobre o branco */
		GdkPixbuf *plano = gdk_pixbuf_composite_color_simple(pb, gdk_pixbuf_get_width(pb), gdk_pixbuf_get_height(pb), GDK_INTERP_NEAREST, 255, 1024, 0xffffffff, 0xffffffff);
		g_object_unref(pb);
		pb = plano;
	}
	char *destino = pb ? copia_nome(origem, ext) : NULL;
	char *tmp = NULL;
	char *resultado = NULL;
	if (destino) {
		char *dir = g_path_get_dirname(destino), *nome = g_path_get_basename(destino);
		char *tnome = g_strdup_printf(".%s.lumna-tmp", nome);
		tmp = g_build_filename(dir, tnome, NULL);
		g_free(tnome);
		g_free(dir);
		g_free(nome);
		char q[16];
		g_snprintf(q, sizeof(q), "%d", CLAMP(qualidade_jpeg, 1, 100));
		gboolean ok = !strcmp(tipo, "jpeg") ? gdk_pixbuf_save(pb, tmp, "jpeg", erro, "quality", q, NULL) : gdk_pixbuf_save(pb, tmp, "png", erro, NULL);
		if (ok && g_rename(tmp, destino) == 0) {
			if (data > 0) {
				struct utimbuf t = { data, data };
				utime(destino, &t);
			}
			resultado = g_strdup(destino);
		} else {
			if (ok && erro && !*erro) {
				g_set_error(erro, G_FILE_ERROR, g_file_error_from_errno(errno), "não deu para gravar %s: %s", destino, g_strerror(errno));
			}
			g_unlink(tmp);
		}
	} else if (erro && !*erro) {
		g_set_error(erro, G_FILE_ERROR, G_FILE_ERROR_FAILED, "não deu para preparar a cópia");
	}
	g_free(ext_orig);
	g_free(tmp);
	g_free(destino);
	if (pb) {
		g_object_unref(pb);
	}
	return resultado;
}
