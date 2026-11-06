/* usa o servicos/mpris.c como a tela de bloqueio: lê o estado e aperta "tocar/pausar" várias vezes, conferindo o ícone (musica.tocando) */
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include "mpris.h"

static struct mpris_estado e;

static void esperar(int fd, int ms) {
	struct pollfd p = { .fd = fd, .events = POLLIN };
	poll(&p, 1, ms);
	mpris_ler(&e);
}

int main(void) {
	int fd = mpris_iniciar();
	for (int i = 0; i < 30 && !e.tem; i++) {
		esperar(fd, 200);
	}
	if (!e.tem || !e.tocando) {
		printf("ERRO: devia ver o player tocando (tem=%d tocando=%d)\n", e.tem, e.tocando);
		return 1;
	}
	int falhas = 0;
	for (int i = 0; i < 5; i++) {
		bool queria = !e.tocando;
		mpris_tocar_pausar();
		esperar(fd, 2500); /* a leitura de verdade chega no máximo 1 s depois */
		sleep(2);
		mpris_ler(&e);
		printf("aperto %d: queria %s, o player está %s\n", i + 1, queria ? "tocando" : "pausado", e.tocando ? "tocando" : "pausado");
		if (e.tocando != queria) {
			falhas++;
		}
	}
	printf(falhas ? "FALHOU (%d)\n" : "mpris tocar/pausar ok\n", falhas);
	return falhas != 0;
}
