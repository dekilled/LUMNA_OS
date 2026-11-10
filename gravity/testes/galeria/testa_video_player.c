/* O player de vídeo (servicos/video_player.c, libmpv por software) com vídeos de verdade feitos pelo ffmpeg. ./testa_video_player PASTA */
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video_player.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { falhas++; fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static gint acordou;
static void acorda(void *d) {
	(void)d;
	g_atomic_int_inc(&acordou);
}

static vp *P;
static vp_estado E;
static uint8_t *Q;
static int QS, QW = 400, QH = 300;
static bool quadro_mudou;

static void gira(double seg) {
	gint64 fim = g_get_monotonic_time() + (gint64)(seg * 1e6);
	while (g_get_monotonic_time() < fim) {
		vp_atualizar(P, &E);
		if (vp_quadro(P, QW, QH, &Q, &QS)) {
			quadro_mudou = true;
		}
		g_usleep(15000);
	}
}

#define ESPERA(cond, seg) ({ gint64 fim_ = g_get_monotonic_time() + (gint64)((seg) * 1e6); bool ok_ = false; while (g_get_monotonic_time() < fim_) { gira(0.05); if (cond) { ok_ = true; break; } } ok_; })

static bool sh(const char *c) {
	return system(c) == 0;
}

static long soma_quadro(void) {
	long s = 0;
	for (int y = 0; y < QH; y++) {
		for (int x = 0; x < QW; x++) {
			const uint8_t *p = Q + (size_t)y * (size_t)QS + (size_t)x * 4;
			s += p[0] + p[1] + p[2];
		}
	}
	return s;
}

static int pixel_preto(int x, int y) {
	const uint8_t *p = Q + (size_t)y * (size_t)QS + (size_t)x * 4;
	return p[0] + p[1] + p[2] < 12;
}

int main(int argc, char **argv) {
	g_setenv("LUMNA_GALERIA_AO", "null", TRUE);
	const char *pasta = argc > 1 ? argv[1] : ".";
	char cmd[2048];
	char *dez = g_build_filename(pasta, "dez.mp4", NULL), *leg = g_build_filename(pasta, "legenda.mp4", NULL), *mkv = g_build_filename(pasta, "duas.mkv", NULL), *mudo = g_build_filename(pasta, "mudo.mp4", NULL), *ruim = g_build_filename(pasta, "ruim.mp4", NULL);
	g_snprintf(cmd, sizeof(cmd), "ffmpeg -v error -y -f lavfi -i testsrc=duration=10:size=320x240:rate=15 -f lavfi -i sine=frequency=440:duration=10 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest '%s'", dez);
	if (!sh(cmd)) {
		fprintf(stderr, "não deu para fazer o vídeo de teste\n");
		return 2;
	}
	g_snprintf(cmd, sizeof(cmd), "cp '%s' '%s' && printf '1\\n00:00:00,500 --> 00:00:03,000\\nOlá legenda\\n\\n2\\n00:00:04,000 --> 00:00:06,000\\nSegunda\\n' > '%s/legenda.srt'", dez, leg, pasta);
	sh(cmd);
	g_snprintf(cmd, sizeof(cmd), "ffmpeg -v error -y -f lavfi -i testsrc=duration=4:size=160x120:rate=10 -f lavfi -i sine=frequency=330:duration=4 -f lavfi -i sine=frequency=550:duration=4 -map 0:v -map 1:a -map 2:a -c:v libx264 -pix_fmt yuv420p -c:a aac -metadata:s:a:0 language=por -metadata:s:a:0 title=Dublado -metadata:s:a:1 language=eng '%s'", mkv);
	sh(cmd);
	g_snprintf(cmd, sizeof(cmd), "ffmpeg -v error -y -f lavfi -i testsrc=duration=4:size=160x120:rate=10 -c:v libx264 -pix_fmt yuv420p '%s'", mudo);
	sh(cmd);
	g_file_set_contents(ruim, "isto nao e um video", -1, NULL);

	P = vp_criar(acorda, NULL);
	CONFERE(P != NULL, "o libmpv sobe");
	if (!P) {
		return 1;
	}
	/* abrir e tocar */
	CONFERE(vp_abrir(P, dez, 0, false, 80), "abrir");
	CONFERE(ESPERA(!E.carregando && E.dur > 5 && E.tocando && E.w > 0, 8), "carrega e toca (dur=%.2f)", E.dur);
	CONFERE(fabs(E.dur - 10) < 0.4 && E.w == 320 && E.h == 240 && E.naudio == 1 && E.nlegenda == 0 && E.volume == 80, "estado: %.2f s, %dx%d, %d áudio, %d legendas, volume %d", E.dur, E.w, E.h, E.naudio, E.nlegenda, E.volume);
	CONFERE(g_atomic_int_get(&acordou) > 5, "o aviso de quadro/evento é dado (%d)", g_atomic_int_get(&acordou));
	gira(1.0);
	CONFERE(E.pos > 0.6 && E.pos < 2.0, "o tempo anda (%.2f)", E.pos);
	/* o quadro: pixels de verdade e barras pretas num quadro mais largo que o vídeo */
	QW = 640; QH = 240;
	gira(0.3);
	CONFERE(soma_quadro() > 640L * 240 * 40, "o quadro não é preto (soma %ld)", soma_quadro());
	CONFERE(pixel_preto(5, 120) && pixel_preto(634, 120) && !pixel_preto(320, 120), "barras pretas dos lados e imagem no meio");
	QW = 400; QH = 300;
	gira(0.3);
	long antes = soma_quadro();
	uint8_t *copia = g_memdup2(Q, (gsize)QS * QH);
	/* pausar */
	vp_pausar(P, true);
	CONFERE(ESPERA(!E.tocando, 3), "pausa");
	double p0 = E.pos;
	gira(0.7);
	CONFERE(fabs(E.pos - p0) < 0.1, "pausado, o tempo não anda (%.2f -> %.2f)", p0, E.pos);
	/* buscar */
	vp_buscar(P, 6.0);
	CONFERE(ESPERA(E.pos > 5.8 && E.pos < 6.5, 4), "buscar 6 s (%.2f)", E.pos);
	gira(0.4);
	CONFERE(memcmp(copia, Q, (gsize)QS * QH) != 0, "o quadro mudou depois de buscar (soma %ld -> %ld)", antes, soma_quadro());
	g_free(copia);
	vp_pausar(P, false);
	CONFERE(ESPERA(E.tocando, 3), "tocar de novo");
	/* volume e velocidade */
	vp_volume(P, 40);
	char v[64];
	gira(0.3);
	CONFERE(vp_propriedade(P, "volume", v, sizeof(v)) && atof(v) == 40, "volume no mpv (%s)", v);
	CONFERE(E.volume == 40, "volume no estado (%d)", E.volume);
	vp_velocidade(P, 2.0);
	gira(0.3);
	double a = E.pos;
	gira(1.0);
	CONFERE(fabs(E.velocidade - 2.0) < 0.01 && E.pos - a > 1.5 && E.pos - a < 2.6, "velocidade 2x: %.2f s em 1 s", E.pos - a);
	vp_velocidade(P, 1.0);
	/* o fim, sem repetir */
	vp_buscar(P, 9.4);
	CONFERE(ESPERA(E.fim, 6), "chega ao fim e para (fim=%d pos=%.2f)", E.fim, E.pos);
	CONFERE(!E.tocando, "no fim não está tocando");
	vp_pausar(P, false);
	for (int i = 0; i < 8 && getenv("VPDBG"); i++) {
		gira(0.5);
		fprintf(stderr, "  pos=%.2f tocando=%d fim=%d\n", E.pos, E.tocando, E.fim);
	}
	CONFERE(ESPERA(E.tocando && E.pos < 3 && !E.fim, 5), "tocar no fim recomeça (pos=%.2f)", E.pos);
	/* repetir */
	vp_repetir(P, true);
	vp_buscar(P, 9.4);
	CONFERE(ESPERA(E.pos > 0 && E.pos < 3 && E.tocando && !E.fim, 8), "repetir: dá a volta (pos=%.2f)", E.pos);
	vp_repetir(P, false);
	/* legendas */
	CONFERE(vp_abrir(P, leg, 0, false, 50), "abrir o com legenda");
	CONFERE(ESPERA(!E.carregando && E.nlegenda == 1, 8), "acha a legenda ao lado (%d)", E.nlegenda);
	vp_faixa f[8];
	int n = vp_faixas(P, true, f, 8);
	CONFERE(n == 1 && f[0].id >= 1, "a lista de legendas tem 1 (%d: '%s')", n, n ? f[0].titulo : "");
	gira(0.4);
	vp_legenda(P, 0);
	CONFERE(ESPERA(E.legenda == 0, 3), "desligar a legenda");
	if (n) {
		vp_legenda(P, f[0].id);
		CONFERE(ESPERA(E.legenda == f[0].id, 3), "ligar a legenda (%d)", E.legenda);
	}
	/* faixas de áudio */
	CONFERE(vp_abrir(P, mkv, 0, false, 50), "abrir o com 2 áudios");
	CONFERE(ESPERA(!E.carregando && E.naudio == 2, 8), "2 faixas de áudio (%d)", E.naudio);
	n = vp_faixas(P, false, f, 8);
	CONFERE(n == 2 && strstr(f[0].titulo, "Dublado") && f[0].escolhida, "as faixas: '%s' (escolhida) e '%s'", n > 0 ? f[0].titulo : "", n > 1 ? f[1].titulo : "");
	if (n == 2) {
		vp_audio(P, f[1].id);
		CONFERE(ESPERA(E.audio == f[1].id, 3), "trocar a faixa de áudio (%d)", E.audio);
	}
	/* sem áudio */
	vp_abrir(P, mudo, 0, false, 50);
	CONFERE(ESPERA(!E.carregando && E.dur > 2, 8) && E.naudio == 0, "vídeo sem áudio: nenhuma faixa (%d)", E.naudio);
	/* erro */
	vp_abrir(P, ruim, 0, false, 50);
	CONFERE(ESPERA(E.erro, 8), "arquivo ruim: erro");
	vp_abrir(P, "/nao/existe.mp4", 0, false, 50);
	CONFERE(ESPERA(E.erro, 8), "arquivo que não existe: erro");
	CONFERE(vp_abrir(P, dez, 4.0, false, 50) && ESPERA(!E.carregando && E.pos > 3.5, 8), "depois de um erro abre outro, começando em 4 s (%.2f)", E.pos);
	/* parar */
	vp_parar(P);
	gira(0.3);
	CONFERE(!E.aberto && !E.tocando, "parar descarrega");
	/* um tamanho minúsculo e um muito grande não quebram */
	uint8_t *d;
	int s;
	vp_abrir(P, dez, 0, false, 50);
	ESPERA(E.tocando, 5);
	CONFERE(vp_quadro(P, 2, 2, &d, &s) || true, "quadro 2x2");
	CONFERE(!vp_quadro(P, 0, 0, &d, &s), "quadro 0x0 é recusado");
	vp_quadro(P, 1920, 1080, &d, &s);
	gira(0.3);
	CONFERE(s == 1920 * 4, "quadro 1920x1080");
	vp_liberar(P);
	g_free(dez); g_free(leg); g_free(mkv); g_free(mudo); g_free(ruim);
	if (falhas) {
		fprintf(stderr, "video_player: %d falha(s)\n", falhas);
		return 1;
	}
	printf("galeria (player de vídeo, libmpv): ok\n");
	return 0;
}
