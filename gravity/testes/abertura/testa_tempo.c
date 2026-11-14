/* A linha do tempo da abertura (servicos/abertura_tempo.c): o laço, a saída sem salto, o pulo. ./testa_tempo */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "abertura_tempo.h"

static int falhas;
#define CONFERE(c, msg)                                                                                                                                       \
	do {                                                                                                                                                      \
		if (!(c)) {                                                                                                                                           \
			fprintf(stderr, "FALHOU: %s (linha %d)\n", msg, __LINE__);                                                                                            \
			falhas++;                                                                                                                                         \
		}                                                                                                                                                     \
	} while (0)
#define PERTO(a, b) (fabs((a) - (b)) < 1e-6)

int main(void) {
	const ab_tempos t = { 4.5, 1.5, 7.5, 0.75 }; /* start 4.5; F 4.5..6; R 6..7.5; end 7.5..8.25 */
	bool ac;
	ab_eventos nada = { -1, -1 };

	/* o start toca inteiro, sem pronto */
	CONFERE(PERTO(ab_posicao(&t, &nada, 0, &ac), 0), "começa em 0");
	CONFERE(PERTO(ab_posicao(&t, &nada, 3.2, &ac), 3.2), "o start anda no ritmo do relógio");
	/* o laço: F, R, F, R (a posição volta para o começo de F quando R acaba) */
	CONFERE(PERTO(ab_posicao(&t, &nada, 4.5, &ac), 4.5), "F começa em frente");
	CONFERE(PERTO(ab_posicao(&t, &nada, 5.9, &ac), 5.9), "no fim de F");
	CONFERE(PERTO(ab_posicao(&t, &nada, 7.4, &ac), 7.4), "no fim de R");
	CONFERE(PERTO(ab_posicao(&t, &nada, 7.5, &ac), 4.5), "depois de R, volta ao começo de F (o laço)");
	CONFERE(PERTO(ab_posicao(&t, &nada, 10.0, &ac), 7.0), "segundo giro do laço (10.0 está em R: 4.5 + 5.5 mod 3.0)");
	CONFERE(PERTO(ab_posicao(&t, &nada, 100.0, &ac), 4.5 + fmod(95.5, 3.0)) && !ac, "o laço não acaba sozinho");

	/* pronto antes de o start acabar: segue o start, F inteiro, e vai para o end sem R */
	ab_eventos cedo = { 2.0, -1 };
	CONFERE(PERTO(ab_inicio_do_fim(&t, &cedo), 6.0), "pronto cedo: o end começa quando F acaba (4.5 + 1.5)");
	CONFERE(PERTO(ab_posicao(&t, &cedo, 5.9, &ac), 5.9), "F até o fim");
	CONFERE(PERTO(ab_posicao(&t, &cedo, 6.0, &ac), 7.5), "do fim de F direto para o começo do end");
	CONFERE(PERTO(ab_posicao(&t, &cedo, 6.5, &ac), 8.0) && !ac, "o end anda");
	CONFERE(ab_posicao(&t, &cedo, 6.75, &ac) >= 8.25 - 1e-6 && ac, "o end terminou");

	/* pronto no meio de F (u = 0.6 em F): termina F */
	ab_eventos emF = { 5.1, -1 };
	CONFERE(PERTO(ab_inicio_do_fim(&t, &emF), 6.0), "pronto em F: end quando F acaba");
	CONFERE(PERTO(ab_posicao(&t, &emF, 5.1, &ac), 5.1), "continua em F, sem salto");
	/* pronto no meio de R (u = 2.0 -> c = 2.0, r = 0.5): espelha */
	ab_eventos emR = { 6.5, -1 };
	CONFERE(PERTO(ab_inicio_do_fim(&t, &emR), 6.5 + 0.5), "pronto em R: faltam r segundos de F depois de espelhar");
	CONFERE(PERTO(ab_posicao(&t, &emR, 6.4, &ac), 6.4), "antes do pronto, R segue");
	CONFERE(PERTO(ab_posicao(&t, &emR, 6.5, &ac), 4.5 + (1.5 - 0.5)), "no pronto, pula para o mesmo quadro de F (R a 0.5 = F a 1.0)");
	CONFERE(PERTO(ab_posicao(&t, &emR, 6.9, &ac), 4.5 + 1.0 + 0.4), "e segue por F");
	CONFERE(PERTO(ab_posicao(&t, &emR, 7.0, &ac), 7.5), "F acabou: end");
	/* pronto bem tarde, em F do segundo giro */
	ab_eventos tarde = { 4.5 + 3.0 + 0.2, -1 }; /* u = 3.2 -> c = 0.2 */
	CONFERE(PERTO(ab_inicio_do_fim(&t, &tarde), 7.7 + 1.3), "pronto tarde, em F: faltam 1.3");
	/* o mesmo quadro: a posição do espelho vem da MESMA imagem (R(c) = F(trecho - r)) */
	for (double r = 0.1; r < 1.5; r += 0.2) {
		ab_eventos ev = { 4.5 + 1.5 + r, -1 };
		double pos_antes = 4.5 + 1.5 + r; /* em R */
		double mir = ab_posicao(&t, &ev, 4.5 + 1.5 + r, &ac);
		/* na emenda de F e R: R[c] mostra o quadro que F mostra em frente + trecho - (c - trecho) */
		CONFERE(PERTO(mir, 4.5 + (1.5 - r)), "o espelho pega o quadro igual");
		(void)pos_antes;
	}

	/* o pulo vai direto ao end, mesmo sem pronto */
	ab_eventos pulo = { -1, 3.0 };
	CONFERE(PERTO(ab_posicao(&t, &pulo, 2.9, &ac), 2.9), "antes do pulo");
	CONFERE(PERTO(ab_posicao(&t, &pulo, 3.0, &ac), 7.5), "pulou: end");
	CONFERE(ab_posicao(&t, &pulo, 3.8, &ac) >= 8.25 - 1e-6 && ac, "o end do pulo termina");

	/* ler o abertura.ini do gera.sh */
	ab_tempos lido;
	CONFERE(ab_ler_tempos(TEMPOS_INI, &lido), "lê o abertura.ini");
	CONFERE(lido.fim > lido.frente && lido.trecho > 1 && lido.dur_fim > 0.5, "valores do abertura.ini");
	CONFERE(PERTO(lido.fim, lido.frente + 2 * lido.trecho) || fabs(lido.fim - lido.frente - 2 * lido.trecho) < 1e-3, "fim = frente + 2 * trecho");
	CONFERE(!ab_ler_tempos("/nao/existe.ini", &lido), "arquivo que falta devolve falso");

	if (falhas) {
		fprintf(stderr, "%d falhas\n", falhas);
		return 1;
	}
	printf("abertura_tempo: ok\n");
	return 0;
}
