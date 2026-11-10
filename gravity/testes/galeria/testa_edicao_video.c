/* O serviço do editor de vídeos (servicos/edicao_video.c) com vídeos de verdade feitos pelo ffmpeg. ./testa_edicao_video PASTA */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "edicao_video.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { falhas++; fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static bool sh(const char *cmd) {
	return system(cmd) == 0;
}

static double media_volume(const char *arq) {
	char *cmd = g_strdup_printf("ffmpeg -v info -nostdin -i '%s' -af volumedetect -vn -f null - 2>&1 | grep mean_volume | sed 's/.*mean_volume: //;s/ dB//' > /tmp/lumna-vol.txt", arq);
	sh(cmd);
	g_free(cmd);
	char *t = NULL;
	g_file_get_contents("/tmp/lumna-vol.txt", &t, NULL, NULL);
	double v = t && *t ? g_ascii_strtod(t, NULL) : -999;
	g_free(t);
	return v;
}

static int fase;
static double ultimo = -1;
static void prog(double f, void *d) {
	if (f + 1e-9 < ultimo) {
		falhas++;
		fprintf(stderr, "FALHOU: o progresso andou para trás (%f -> %f)\n", ultimo, f);
	}
	ultimo = f;
	fase++;
}

int main(int argc, char **argv) {
	const char *pasta = argc > 1 ? argv[1] : ".";
	char *v10 = g_build_filename(pasta, "dez.mp4", NULL), *mudo = g_build_filename(pasta, "mudo.mp4", NULL), *ruim = g_build_filename(pasta, "ruim.mp4", NULL);
	char *cmd = g_strdup_printf("ffmpeg -v error -y -f lavfi -i testsrc=duration=10:size=320x240:rate=15 -f lavfi -i sine=frequency=440:duration=10 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest '%s'", v10);
	if (!sh(cmd)) {
		fprintf(stderr, "não deu para fazer o vídeo de teste\n");
		return 2;
	}
	g_free(cmd);
	cmd = g_strdup_printf("ffmpeg -v error -y -f lavfi -i testsrc=duration=4:size=160x120:rate=10 -c:v libx264 -pix_fmt yuv420p '%s'", mudo);
	sh(cmd);
	g_free(cmd);
	g_file_set_contents(ruim, "isto nao e um video", -1, NULL);

	ev_info in;
	CONFERE(ev_sondar(v10, &in) && fabs(in.duracao - 10) < 0.3 && in.audio && in.video, "sondar: %.2f audio=%d", in.duracao, in.audio);
	CONFERE(ev_sondar(mudo, &in) && !in.audio && fabs(in.duracao - 4) < 0.3, "sondar um vídeo sem áudio");
	CONFERE(!ev_sondar(ruim, &in), "o arquivo que não é vídeo é recusado");
	CONFERE(!ev_sondar("/nao/existe.mp4", &in), "o que não existe também");

	ev_params p;
	ev_padrao(&p, 10);
	CONFERE(!ev_alterado(&p, 10), "o padrão é o vídeo inteiro");
	p.inicio = 6; p.fim = 30; ev_ajustar(&p, 10, 0.5);
	CONFERE(p.fim == 10 && p.inicio == 6 && ev_alterado(&p, 10), "o fim não passa do vídeo");
	p.inicio = 9.9; p.fim = 3; ev_ajustar(&p, 10, 0.5);
	CONFERE(p.fim - p.inicio >= 0.499 && p.inicio >= 0 && p.fim <= 10, "início e fim ficam em ordem (%f, %f)", p.inicio, p.fim);

	/* quadros */
	GdkPixbuf *q = ev_quadro(v10, 5, 0, 200);
	CONFERE(q && gdk_pixbuf_get_width(q) == 200 && gdk_pixbuf_get_height(q) == 150, "quadro reduzido a 200 de lado (%d)", q ? gdk_pixbuf_get_width(q) : 0);
	if (q) g_object_unref(q);
	q = ev_quadro(v10, 5, 1, 200);
	CONFERE(q && gdk_pixbuf_get_width(q) == 150 && gdk_pixbuf_get_height(q) == 200, "girado: em pé (%dx%d)", q ? gdk_pixbuf_get_width(q) : 0, q ? gdk_pixbuf_get_height(q) : 0);
	if (q) g_object_unref(q);
	q = ev_quadro(v10, 5, 0, 5000);
	CONFERE(q && gdk_pixbuf_get_width(q) == 320, "não aumenta");
	if (q) g_object_unref(q);
	CONFERE(!ev_quadro(ruim, 1, 0, 200), "quadro de arquivo ruim: nada, sem cair");
	GdkPixbuf *a = ev_quadro(v10, 1, 0, 160), *b = ev_quadro(v10, 8, 0, 160);
	CONFERE(a && b && memcmp(gdk_pixbuf_get_pixels(a), gdk_pixbuf_get_pixels(b), 160 * 120 * 3) != 0, "quadros de instantes diferentes são diferentes");
	if (a) g_object_unref(a);
	if (b) g_object_unref(b);

	/* tocar */
	ev_toque *t = ev_tocar(v10, 2, 0, 160, 120, 10);
	uint8_t *buf = g_malloc(160 * 120 * 4);
	int n = 0;
	while (t && ev_toque_proximo(t, buf) && n < 100) {
		n++;
	}
	CONFERE(n >= 70 && n <= 90, "tocar de 2 s a 10 s a 10 qps dá ~80 quadros (%d)", n);
	ev_toque_parar(t);
	ev_toque_liberar(t);
	t = ev_tocar(v10, 0, 0, 160, 120, 10);
	CONFERE(t && ev_toque_proximo(t, buf), "o primeiro quadro chega");
	ev_toque_parar(t); /* parar no meio */
	ev_toque_liberar(t);
	g_free(buf);

	/* exportar: recorte */
	ev_padrao(&p, 10);
	p.inicio = 2; p.fim = 6;
	GError *e = NULL;
	char *c1 = ev_exportar(v10, &p, 1700000000, prog, NULL, NULL, &e);
	char *esp = g_build_filename(pasta, "dez-editado.mp4", NULL);
	CONFERE(c1 && !strcmp(c1, esp), "a cópia: %s (%s)", c1 ? c1 : "nulo", e ? e->message : "");
	g_clear_error(&e);
	CONFERE(fase > 2 && ultimo == 1, "o progresso andou e chegou a 1 (%d chamadas, %f)", fase, ultimo);
	if (c1) {
		ev_info o;
		CONFERE(ev_sondar(c1, &o) && fabs(o.duracao - 4) < 0.4 && o.audio, "a cópia tem ~4 s e áudio (%.2f)", o.duracao);
		struct stat st;
		CONFERE(stat(c1, &st) == 0 && st.st_mtime == 1700000000, "a data pedida");
	}
	ev_info orig;
	ev_sondar(v10, &orig);
	CONFERE(fabs(orig.duracao - 10) < 0.3, "o original continua com 10 s");
	/* mudo */
	ev_padrao(&p, 10); p.fim = 3; p.mudo = true;
	char *c2 = ev_exportar(v10, &p, 0, NULL, NULL, NULL, &e);
	CONFERE(c2 && g_str_has_suffix(c2, "dez-editado-2.mp4"), "a segunda cópia não pisa na primeira");
	if (c2) { ev_info o; CONFERE(ev_sondar(c2, &o) && !o.audio, "silenciar tira o áudio"); }
	/* volume */
	ev_padrao(&p, 10); p.fim = 4; p.volume = 100;
	char *c3 = ev_exportar(v10, &p, 0, NULL, NULL, NULL, NULL);
	ev_padrao(&p, 10); p.fim = 4; p.volume = 50;
	char *c4 = ev_exportar(v10, &p, 0, NULL, NULL, NULL, NULL);
	if (c3 && c4) {
		double v1 = media_volume(c3), v2 = media_volume(c4);
		CONFERE(v1 > -60 && fabs((v1 - v2) - 6.0) < 1.5, "volume 50%% = ~6 dB a menos (%.1f -> %.1f)", v1, v2);
	} else CONFERE(false, "exportar com volume");
	/* girar */
	ev_padrao(&p, 10); p.fim = 2; p.giro = 1;
	char *c5 = ev_exportar(v10, &p, 0, NULL, NULL, NULL, NULL);
	if (c5) {
		GdkPixbuf *g = ev_quadro(c5, 0.5, 0, 1000);
		CONFERE(g && gdk_pixbuf_get_width(g) == 240 && gdk_pixbuf_get_height(g) == 320, "girar: 240x320 (%dx%d)", g ? gdk_pixbuf_get_width(g) : 0, g ? gdk_pixbuf_get_height(g) : 0);
		if (g) g_object_unref(g);
	} else CONFERE(false, "exportar girado");
	/* sem áudio de origem + volume pedido: não quebra */
	ev_padrao(&p, 4); p.volume = 150;
	char *c6 = ev_exportar(mudo, &p, 0, NULL, NULL, NULL, &e);
	CONFERE(c6 != NULL, "vídeo sem áudio com volume não quebra (%s)", e ? e->message : "");
	g_clear_error(&e);
	/* erros e cancelar */
	CONFERE(!ev_exportar(ruim, &p, 0, NULL, NULL, NULL, &e) && e, "vídeo ruim: erro");
	g_clear_error(&e);
	volatile int cancela = 1;
	ev_padrao(&p, 10);
	char *c7 = ev_exportar(v10, &p, 0, NULL, NULL, &cancela, &e);
	CONFERE(!c7 && !e, "cancelado: nada e sem erro");
	GDir *d = g_dir_open(pasta, 0, NULL);
	const char *nm;
	int cont = 0;
	while (d && (nm = g_dir_read_name(d))) {
		CONFERE(!strstr(nm, "lumna-tmp"), "sobrou temporário %s", nm);
		cont += strstr(nm, "-editado") != NULL;
	}
	if (d) g_dir_close(d);
	CONFERE(cont == 6, "seis cópias na pasta (%d)", cont);
	if (falhas) { fprintf(stderr, "edicao_video: %d falha(s)\n", falhas); return 1; }
	printf("edicao de vídeo (serviço): ok\n");
	return 0;
}
