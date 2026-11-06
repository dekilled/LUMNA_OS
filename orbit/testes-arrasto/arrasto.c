/* aperta o botão no monitor onde o cursor está, anda dx pixels (passo a passo), e solta */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ponteiro.h"
int main(int argc, char **argv) {
	double dx = argc > 1 ? atof(argv[1]) : 1500;
	if (!strcmp(argc > 2 ? argv[2] : "", "so-mover")) {
		for (int i = 0; i < 30; i++) { ponteiro_mover(dx / 30, 0); usleep(20000); }
		return 0;
	}
	ponteiro_botao(0, true);
	usleep(100000);
	for (int i = 0; i < 40; i++) { ponteiro_mover(dx / 40, 0); usleep(20000); }
	usleep(200000);
	printf("movido\n"); fflush(stdout);
	if (argc > 3) { sleep(atoi(argv[3])); }
	ponteiro_botao(0, false);
	return 0;
}
