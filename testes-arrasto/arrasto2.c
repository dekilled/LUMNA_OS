#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ponteiro.h"
static void anda(double dx, double dy) {
	int n = 40;
	for (int i = 0; i < n; i++) { ponteiro_mover(dx / n, dy / n); usleep(15000); }
}
int main(int argc, char **argv) {
	if (!strcmp(argv[1], "casa")) { anda(-9000, -9000); return 0; }
	if (!strcmp(argv[1], "mover")) { anda(atof(argv[2]), atof(argv[3])); return 0; }
	if (!strcmp(argv[1], "arrastar")) {
		ponteiro_botao(0, true);
		usleep(150000);
		anda(atof(argv[2]), atof(argv[3]));
		usleep(300000);
		ponteiro_botao(0, false);
		return 0;
	}
	return 1;
}
