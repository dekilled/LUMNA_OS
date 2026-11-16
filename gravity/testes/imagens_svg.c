/* Arquivos SVG pelo librsvg (gv_rsvg.c): o gdk-pixbuf não lê mais SVG com o librsvg 2.60+ (os ícones do Papirus ficavam sem carregar). Compila com libgravity.a. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <cairo.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

bool gv__svg_disponivel(void);
bool gv__eh_svg(const char *caminho);
bool gv__svg_tamanho(const char *caminho, int *w, int *h);
cairo_surface_t *gv__svg_rasterizar(const char *caminho, int w, int h);

static uint32_t pixel(cairo_surface_t *s, int x, int y) {
	cairo_surface_flush(s);
	return *(uint32_t *)(cairo_image_surface_get_data(s) + (size_t)y * cairo_image_surface_get_stride(s) + 4 * x);
}

int main(void) {
	assert(gv__eh_svg("/a/b/firefox.svg") && gv__eh_svg("x.SVG") && gv__eh_svg("x.svgz") && !gv__eh_svg("x.png") && !gv__eh_svg("svg") && !gv__eh_svg(NULL));
	if (!gv__svg_disponivel()) {
		puts("imagens_svg: dispensado (sem o librsvg neste ambiente)");
		return 0;
	}
	char *dir = g_dir_make_tmp("svgXXXXXX", NULL);
	/* um ícone 48x48: fundo azul opaco com um quadrado vermelho no meio (24..72% do lado) */
	char *a = g_build_filename(dir, "icone.svg", NULL);
	g_file_set_contents(a, "<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' viewBox='0 0 48 48'><rect width='48' height='48' fill='#0000ff'/><rect x='12' y='12' width='24' height='24' fill='#ff0000'/></svg>", -1, NULL);
	int w = 0, h = 0;
	assert(gv__svg_tamanho(a, &w, &h) && w == 48 && h == 48);
	/* nítido em qualquer tamanho: 200x200 (maior que o natural): o quadrado vermelho continua no meio, o azul nas pontas */
	cairo_surface_t *s = gv__svg_rasterizar(a, 200, 200);
	assert(s && cairo_image_surface_get_width(s) == 200 && cairo_image_surface_get_height(s) == 200);
	assert(pixel(s, 100, 100) == 0xFFFF0000u);   /* vermelho opaco no centro */
	assert(pixel(s, 4, 4) == 0xFF0000FFu);        /* azul opaco na ponta */
	cairo_surface_destroy(s);
	/* sem tamanho nem viewBox: um ícone quadrado de 48 */
	char *b = g_build_filename(dir, "semtamanho.svg", NULL);
	g_file_set_contents(b, "<svg xmlns='http://www.w3.org/2000/svg'><circle cx='10' cy='10' r='8' fill='#00ff00'/></svg>", -1, NULL);
	assert(gv__svg_tamanho(b, &w, &h) && w > 0 && h > 0);
	/* o que não é SVG, ou está quebrado, falha sem derrubar */
	char *c = g_build_filename(dir, "quebrado.svg", NULL);
	g_file_set_contents(c, "isto não é um svg", -1, NULL);
	assert(!gv__svg_rasterizar(c, 32, 32));
	assert(!gv__svg_rasterizar("/nao/existe.svg", 32, 32));
	assert(!gv__svg_tamanho("/nao/existe.svg", &w, &h));
	assert(!gv__svg_rasterizar(a, 0, 10));
	puts("imagens_svg: ok");
	return 0;
}
