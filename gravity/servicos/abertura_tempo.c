#include "abertura_tempo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* o instante (desde o começo) em que o end começa */
double ab_inicio_do_fim(const ab_tempos *t, const ab_eventos *ev) {
	if (ev->pulo >= 0) {
		return ev->pulo;
	}
	if (ev->pronto < 0) {
		return -1; /* ainda não se sabe */
	}
	double ex = ev->pronto > t->frente ? ev->pronto : t->frente; /* pronto antes do start acabar: sai quando o start acaba */
	double u = ex - t->frente;
	double c = fmod(u, 2 * t->trecho);
	double resta = c < t->trecho ? t->trecho - c : c - t->trecho; /* o que falta de F (em R, depois de espelhar, falta o que já andou de R) */
	return ex + resta;
}

double ab_posicao(const ab_tempos *t, const ab_eventos *ev, double e, bool *acabou) {
	if (acabou) {
		*acabou = false;
	}
	if (e < 0) {
		e = 0;
	}
	double inicio_fim = ab_inicio_do_fim(t, ev);
	if (inicio_fim >= 0 && e >= inicio_fim) {
		double ee = e - inicio_fim;
		if (ee >= t->dur_fim) {
			if (acabou) {
				*acabou = true;
			}
			ee = t->dur_fim;
		}
		return t->fim + ee;
	}
	if (e < t->frente) {
		return e;
	}
	if (ev->pronto >= 0) {
		double ex = ev->pronto > t->frente ? ev->pronto : t->frente;
		if (e >= ex) {
			double c = fmod(ex - t->frente, 2 * t->trecho);
			if (c >= t->trecho) {
				/* estava em R: espelha para F no mesmo quadro (R a r de andar = F a trecho - r de andar) e segue por F */
				double r = c - t->trecho;
				return t->frente + (t->trecho - r) + (e - ex);
			}
			return t->frente + c + (e - ex);
		}
	}
	return t->frente + fmod(e - t->frente, 2 * t->trecho); /* o laço: F, R, F, R... */
}

bool ab_ler_tempos(const char *caminho, ab_tempos *t) {
	FILE *f = fopen(caminho, "r");
	if (!f) {
		return false;
	}
	double frente = -1, trecho = -1, fim = -1, dur_fim = -1;
	char linha[200], chave[64];
	double v;
	while (fgets(linha, sizeof(linha), f)) {
		if (sscanf(linha, " %63[^= ] = %lf", chave, &v) != 2) {
			continue;
		}
		if (!strcmp(chave, "frente")) {
			frente = v;
		} else if (!strcmp(chave, "duracao_trecho")) {
			trecho = v;
		} else if (!strcmp(chave, "fim")) {
			fim = v;
		} else if (!strcmp(chave, "duracao_fim")) {
			dur_fim = v;
		}
	}
	fclose(f);
	if (frente <= 0 || trecho <= 0 || fim <= frente || dur_fim <= 0) {
		return false;
	}
	*t = (ab_tempos){ frente, trecho, fim, dur_fim };
	return true;
}
