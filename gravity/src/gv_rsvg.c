/*
 * Arquivos SVG (os ícones dos apps do tema Papirus, papéis de parede em SVG) pelo librsvg, direto. O gdk-pixbuf lia SVG por um carregador que vinha com o librsvg; o
 * librsvg 2.60+ deixou de trazê-lo (no PC do Eric, `gdk-pixbuf-query-loaders | grep -c svg` dava 0 depois de uma atualização completa): todo ícone SVG falhava em silêncio e o
 * lançador mostrava o desenho genérico no lugar de cada app. Aqui o librsvg é carregado na hora (dlopen: não pede cabeçalhos nem muda a compilação), só com o que
 * precisamos: abrir o arquivo, saber o tamanho natural e desenhar num cairo. Sem a biblioteca, tudo devolve falso e quem chama cai no gdk-pixbuf.
 */
#include "gv_interno.h"

#include <ctype.h>
#include <dlfcn.h>
#include <glib.h>
#include <glib-object.h>
#include <string.h>

typedef struct {
	double x, y, width, height;
} rsvg_rect;
typedef struct {
	int width, height;
	double em, ex;
} rsvg_dim;

static void *(*f_novo)(const char *, GError **);
static gboolean (*f_tamanho)(void *, double *, double *);
static void (*f_dimensoes)(void *, rsvg_dim *);
static gboolean (*f_desenhar)(void *, cairo_t *, const rsvg_rect *, GError **);
static bool lib_ok;

static void carregar(void) {
	static gsize feito = 0;
	if (g_once_init_enter(&feito)) {
		void *h = dlopen("librsvg-2.so.2", RTLD_LAZY | RTLD_LOCAL);
		if (h) {
			f_novo = (void *(*)(const char *, GError **))dlsym(h, "rsvg_handle_new_from_file");
			f_tamanho = (gboolean (*)(void *, double *, double *))dlsym(h, "rsvg_handle_get_intrinsic_size_in_pixels");
			f_dimensoes = (void (*)(void *, rsvg_dim *))dlsym(h, "rsvg_handle_get_dimensions");
			f_desenhar = (gboolean (*)(void *, cairo_t *, const rsvg_rect *, GError **))dlsym(h, "rsvg_handle_render_document");
			lib_ok = f_novo && f_desenhar && (f_tamanho || f_dimensoes);
		}
		g_once_init_leave(&feito, 1);
	}
}

bool gv__svg_disponivel(void) {
	carregar();
	return lib_ok;
}

/* o nome acaba em .svg ou .svgz (sem diferenciar maiúsculas)? */
bool gv__eh_svg(const char *caminho) {
	size_t n = caminho ? strlen(caminho) : 0;
	if (n > 5 && !g_ascii_strcasecmp(caminho + n - 5, ".svgz")) {
		return true;
	}
	return n > 4 && !g_ascii_strcasecmp(caminho + n - 4, ".svg");
}

/* o tamanho natural, em pixels */
bool gv__svg_tamanho(const char *caminho, int *w, int *h) {
	if (!gv__svg_disponivel() || !caminho) {
		return false;
	}
	GError *erro = NULL;
	void *doc = f_novo(caminho, &erro);
	if (!doc) {
		g_clear_error(&erro);
		return false;
	}
	double dw = 0, dh = 0;
	bool ok = false;
	if (f_tamanho && f_tamanho(doc, &dw, &dh) && dw > 0 && dh > 0) {
		ok = true;
	} else if (f_dimensoes) {
		rsvg_dim d = { 0 };
		f_dimensoes(doc, &d);
		if (d.width > 0 && d.height > 0) {
			dw = d.width;
			dh = d.height;
			ok = true;
		}
	}
	if (!ok) { /* um SVG sem tamanho nem viewBox: um ícone quadrado */
		dw = dh = 48;
		ok = true;
	}
	g_object_unref(doc);
	*w = (int)(dw + 0.5) > 0 ? (int)(dw + 0.5) : 1;
	*h = (int)(dh + 0.5) > 0 ? (int)(dh + 0.5) : 1;
	return ok;
}

/* desenha o SVG nítido num quadro de w × h (cabe inteiro, centrado: o librsvg respeita a proporção) */
cairo_surface_t *gv__svg_rasterizar(const char *caminho, int w, int h) {
	if (!gv__svg_disponivel() || !caminho || w < 1 || h < 1) {
		return NULL;
	}
	GError *erro = NULL;
	void *doc = f_novo(caminho, &erro);
	if (!doc) {
		g_clear_error(&erro);
		return NULL;
	}
	cairo_surface_t *sup = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (cairo_surface_status(sup) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(sup);
		g_object_unref(doc);
		return NULL;
	}
	cairo_t *cr = cairo_create(sup);
	rsvg_rect vp = { 0, 0, (double)w, (double)h };
	gboolean ok = f_desenhar(doc, cr, &vp, &erro);
	g_clear_error(&erro);
	cairo_destroy(cr);
	g_object_unref(doc);
	if (!ok) {
		cairo_surface_destroy(sup);
		return NULL;
	}
	return sup;
}
