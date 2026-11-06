/* servicos/bateria.c com um /sys falso. ./testa_bateria PASTA */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bateria.h"

static void arq(const char *pasta, const char *nome, const char *conteudo) {
	g_mkdir_with_parents(pasta, 0755);
	char *c = g_build_filename(pasta, nome, NULL);
	g_file_set_contents(c, conteudo, -1, NULL);
	g_free(c);
}

int main(int argc, char **argv) {
	const char *raiz = argv[1];
	setenv("LUMNA_RAIZ_SISTEMA", raiz, 1);
	struct bateria_info b;
	assert(!bateria_ler(&b) && !b.existe); /* sem nada: sem bateria */
	char *ps = g_build_filename(raiz, "sys/class/power_supply", NULL), *ac = g_build_filename(ps, "AC", NULL), *bat = g_build_filename(ps, "BAT0", NULL),
	     *fone = g_build_filename(ps, "hid-aa:bb-battery", NULL);
	arq(ac, "type", "Mains\n");
	arq(fone, "type", "Battery\n");
	arq(fone, "capacity", "10\n");
	assert(!bateria_ler(&b)); /* a bateria do fone não é a do computador */
	arq(bat, "type", "Battery\n");
	arq(bat, "capacity", "76\n");
	arq(bat, "status", "Discharging\n");
	arq(bat, "energy_now", "38000000\n");
	arq(bat, "energy_full", "50000000\n");
	arq(bat, "energy_full_design", "55000000\n");
	arq(bat, "power_now", "10000000\n");
	arq(bat, "cycle_count", "210\n");
	arq(bat, "technology", "Li-ion\n");
	assert(bateria_ler(&b) && b.existe && !strcmp(b.nome, "BAT0"));
	assert(b.pct == 76 && !b.carregando && !b.na_tomada && !strcmp(b.estado, "Descarregando"));
	assert(b.saude_pct == 91 && b.ciclos == 210 && !strcmp(b.tecnologia, "Li-ion"));
	assert(b.minutos == 228 && b.watts > 9.9 && b.watts < 10.1); /* 38 Wh a 10 W: 3,8 h */
	puts("descarregando ok");
	arq(bat, "status", "Charging\n");
	assert(bateria_ler(&b) && b.carregando && b.na_tomada && b.minutos == 72); /* faltam 12 Wh a 10 W: 1,2 h */
	arq(bat, "status", "Full\n");
	arq(bat, "power_now", "0\n");
	assert(bateria_ler(&b) && !b.carregando && b.na_tomada && b.minutos == -1 && !strcmp(b.estado, "Cheia"));
	puts("carregando e cheia ok");
	/* a bateria em carga/corrente (µAh e µA): troca a BAT0 por uma BAT1 desse jeito */
	const char *arqs[] = { "type", "capacity", "status", "energy_now", "energy_full", "energy_full_design", "power_now", "cycle_count", "technology" };
	for (size_t i = 0; i < sizeof(arqs) / sizeof(arqs[0]); i++) {
		char *c = g_build_filename(bat, arqs[i], NULL);
		g_remove(c);
		g_free(c);
	}
	g_rmdir(bat);
	char *bat1 = g_build_filename(ps, "BAT1", NULL);
	arq(bat1, "type", "Battery\n");
	arq(bat1, "capacity", "50\n");
	arq(bat1, "status", "Discharging\n");
	arq(bat1, "charge_now", "2000000\n");
	arq(bat1, "charge_full", "4000000\n");
	arq(bat1, "charge_full_design", "5000000\n");
	arq(bat1, "current_now", "1000000\n");
	arq(bat1, "voltage_now", "12000000\n");
	assert(bateria_ler(&b) && !strcmp(b.nome, "BAT1") && b.pct == 50 && b.saude_pct == 80 && b.minutos == 120 && b.watts > 11.9 && b.watts < 12.1);
	puts("bateria ok");
	return 0;
}
