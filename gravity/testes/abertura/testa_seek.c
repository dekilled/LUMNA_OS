/* Mede, com o libmpv de verdade, o quanto a imagem para quando a abertura salta (o laço volta, ou sai do laço para o fim): o intervalo entre quadros novos
 * em volta dos saltos contra o intervalo normal (~33 ms). ./testa_seek ARQUIVO.mp4 ABERTURA.ini [pronto_em] */
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "abertura_tempo.h"
#include "video_player.h"

static double mono(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
	setenv("LUMNA_GALERIA_AO", "null", 0); /* (quem já pôs outra saída, como pcm, fica com ela) */
	ab_tempos tp;
	if (argc < 3 || !ab_ler_tempos(argv[2], &tp)) {
		fprintf(stderr, "uso: testa_seek arquivo.mp4 abertura.ini [pronto_em]\n");
		return 2;
	}
	double pronto_em = argc > 3 ? atof(argv[3]) : -1;
	ab_eventos ev = { -1, -1 };
	vp *p = vp_criar(NULL, NULL);
	double ultimo_salto_e = -10, t0 = mono(), ultimo_alvo = -1, ultimo_e = 0, ultimo_quadro = t0, maior_normal = 0;
	bool abriu = false;
	double fim_total = (pronto_em >= 0 ? pronto_em : 9.0) + 2 * tp.trecho + tp.dur_fim + 1.0;
	printf("%8s %8s %8s\n", "e", "alvo", "intervalo(ms)");
	while (mono() - t0 < fim_total) {
		double e = mono() - t0;
		if (pronto_em >= 0 && ev.pronto < 0 && e >= pronto_em) {
			ev.pronto = e;
		}
		bool acabou;
		double alvo = ab_posicao(&tp, &ev, e, &acabou);
		if (!abriu) {
			abriu = true;
			vp_abrir(p, argv[1], 0, false, 0);
			ultimo_alvo = alvo;
			ultimo_e = e;
		} else if (!acabou) {
			bool salto = fabs(alvo - (ultimo_alvo + (e - ultimo_e))) > 0.08;
			if (salto) {
				vp_buscar(p, alvo);
				ultimo_salto_e = e;
				printf("salto em e=%.3f -> busca %.3f\n", e, alvo);
			}
			ultimo_alvo = alvo;
			ultimo_e = e;
		}
		vp_estado es;
		vp_atualizar(p, &es);
		uint8_t *d;
		int st;
		if (vp_quadro(p, 480, 270, &d, &st)) {
			double agora = mono();
			double gap = (agora - ultimo_quadro) * 1000;
			if ((e - ultimo_salto_e < 0.25) || gap > 70) {
				printf("%8.3f %8.3f %8.1f%s\n", e, alvo, gap, gap > 70 ? "  <-- parada" : "");
			}
			if (gap < 70 || e < 0.3) {
				if (gap > maior_normal && e > 0.5) {
					maior_normal = gap;
				}
			}
			ultimo_quadro = agora;
		}
		if (ultimo_e == e && ultimo_alvo == alvo) {
			/* nada */
		}
		g_usleep(4000);
	}
	vp_liberar(p);
	return 0;
}
