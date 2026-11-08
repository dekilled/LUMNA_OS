/* O serviço teclado_im.c contra um Orbit de verdade: espera o campo abrir e escreve, apaga e manda Enter. teste_im.sh confere o que o app de mentira viu. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "teclado_im.h"

static void campo(void *dados, const ti_campo *c) {
	printf("campo ativo=%d proposito=%u dica=%u antes=[%s]\n", c->ativo, c->proposito, c->dica, c->antes);
	fflush(stdout);
}

static void espera(ti_cliente *c, double segundos) {
	time_t fim = time(NULL) + (time_t)segundos + 1;
	while (time(NULL) < fim) {
		ti_despachar(c);
		usleep(20000);
	}
}

int main(int argc, char **argv) {
	ti_ouvinte o = { .campo = campo };
	ti_cliente *c = ti_abrir(&o, NULL, argc > 1 ? argv[1] : "us", argc > 2 ? argv[2] : "");
	if (!c) {
		fprintf(stderr, "não deu para ligar o método de entrada\n");
		return 1;
	}
	puts("ligado");
	fflush(stdout);
	espera(c, 3);
	ti_texto(c, "olá");
	espera(c, 0.5);
	ti_texto(c, " 😀");
	espera(c, 0.5);
	ti_apagar(c); /* tira o emoji */
	espera(c, 0.5);
	ti_apagar(c); /* tira o espaço */
	espera(c, 0.5);
	ti_tecla(c, TI_KEY_ENTER);
	espera(c, 0.5);
	ti_tecla(c, TI_KEY_LEFT);
	espera(c, 1);
	ti_fechar(c);
	puts("fim");
	return 0;
}
