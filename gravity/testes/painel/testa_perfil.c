/* servicos/perfil.c com o powerprofilesctl falso de testes/painel/bin */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "perfil.h"

int main(void) {
	struct perfil_info p;
	assert(perfil_ler(&p) && p.existe && p.n == 3 && !strcmp(p.atual, "balanced"));
	assert(!strcmp(p.lista[0], "performance") && !strcmp(p.lista[2], "power-saver") && !p.degradado);
	char erro[200] = "";
	assert(perfil_definir("power-saver", erro, sizeof(erro)));
	assert(perfil_ler(&p) && !strcmp(p.atual, "power-saver"));
	assert(!perfil_definir("turbo", erro, sizeof(erro)) && !perfil_definir("", erro, sizeof(erro)));
	assert(!strcmp(perfil_nome("power-saver"), "Economia") && !strcmp(perfil_nome("performance"), "Desempenho"));
	puts("perfil ok");
	return 0;
}
