#define _GNU_SOURCE
#include "aricone.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <string.h>

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

static void desenhar_pasta(cairo_t *cr, double x, double y, double w, double h, uint32_t rgb, const enum ar_tipo *previa, int nprevia) {
	double aba = h * 0.16;
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y, 0, y + h);
	Clay_Color a = gv_t.acento;
	if (rgb) {
		a = (Clay_Color){ (float)((rgb >> 16) & 255), (float)((rgb >> 8) & 255), (float)(rgb & 255), 255 };
	}
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
	/* as miniaturas do que há dentro: quatro páginas pequenas dispostas 2 por 2 no corpo da pasta */
	double mw = w * 0.30, mh = (h - aba) * 0.34;
	for (int i = 0; i < nprevia && i < 4; i++) {
		double mx = x + w * 0.14 + (i % 2) * (mw + w * 0.12), my = y + aba + (h - aba) * 0.16 + (i / 2) * (mh + (h - aba) * 0.08);
		rr(cr, mx, my, mw, mh, mw * 0.14);
		cairo_set_source_rgba(cr, 0.96, 0.96, 0.98, 0.95);
		cairo_fill(cr);
		rr(cr, mx + mw * 0.2, my + mh * 0.28, mw * 0.6, mh * 0.34, mw * 0.08);
		cor_rgb(cr, cor_do_tipo(previa[i]), 1);
		cairo_fill(cr);
	}
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

void ar_icone_desenhar(cairo_t *cr, Clay_BoundingBox c, void *dados) {
	const ic_dados *d = dados;
	double s = fmin(c.width, c.height);
	double x = c.x + (c.width - s) / 2, y = c.y + (c.height - s) / 2;
	if (d->tipo == AR_PASTA) {
		desenhar_pasta(cr, x + s * 0.04, y + s * 0.10, s * 0.92, s * 0.80, d->cor, d->previa, d->nprevia);
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

