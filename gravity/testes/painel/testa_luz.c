/* servicos/luz.c: o horário, a temperatura e as rampas. ./testa_luz PASTA */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "luz.h"

int main(int argc, char **argv) {
	char *cfg = g_build_filename(argv[1], "cfg", NULL);
	setenv("XDG_CONFIG_HOME", cfg, 1);
	struct luz_cfg c;
	luz_ler(&c);
	assert(c.modo == LUZ_DESLIGADA && c.temperatura == 4000 && c.inicio == 1200 && c.fim == 360);
	assert(luz_kelvin_agora(&c, 22 * 60) == LUZ_NORMAL); /* desligada: nunca age */
	c.modo = LUZ_HORARIO;
	c.temperatura = 3500;
	c.inicio = 20 * 60;
	c.fim = 6 * 60;
	assert(luz_gravar(&c));
	struct luz_cfg d;
	luz_ler(&d);
	assert(d.modo == LUZ_HORARIO && d.temperatura == 3500 && d.inicio == 1200 && d.fim == 360);
	puts("ajuste ok");
	/* o horário passa da meia-noite: 20:00 a 06:00 */
	assert(luz_kelvin_agora(&c, 12 * 60) == LUZ_NORMAL);              /* meio-dia: normal */
	assert(luz_kelvin_agora(&c, 19 * 60 + 59) == LUZ_NORMAL);         /* um minuto antes */
	int meio = luz_kelvin_agora(&c, 20 * 60 + 15);                    /* na metade da subida */
	assert(meio > 3500 && meio < LUZ_NORMAL && abs(meio - (LUZ_NORMAL + 3500) / 2) < 5);
	assert(luz_kelvin_agora(&c, 20 * 60 + 30) == 3500);               /* chegou no auge */
	assert(luz_kelvin_agora(&c, 0) == 3500 && luz_kelvin_agora(&c, 5 * 60 + 59) == 3500);
	int sobe = luz_kelvin_agora(&c, 6 * 60 + 15);                     /* na metade da descida */
	assert(sobe > 3500 && sobe < LUZ_NORMAL);
	assert(luz_kelvin_agora(&c, 6 * 60 + 30) == LUZ_NORMAL);          /* acabou */
	c.modo = LUZ_SEMPRE;
	assert(luz_kelvin_agora(&c, 12 * 60) == 3500);
	puts("horario ok");
	/* um horário no mesmo dia: 09:00 a 17:00 */
	c.modo = LUZ_HORARIO;
	c.inicio = 9 * 60;
	c.fim = 17 * 60;
	assert(luz_kelvin_agora(&c, 12 * 60) == 3500 && luz_kelvin_agora(&c, 8 * 60) == LUZ_NORMAL && luz_kelvin_agora(&c, 18 * 60) == LUZ_NORMAL);
	puts("mesmo dia ok");
	/* as rampas: a normal é reta; a quente tira azul primeiro, depois verde, e o vermelho fica */
	uint16_t r[256], g[256], b[256];
	luz_rampa(LUZ_NORMAL, r, g, b, 256);
	assert(r[255] == 65535 && g[255] == 65535 && b[255] == 65535 && r[0] == 0 && abs((int)r[128] - 32896) < 260);
	luz_rampa(3000, r, g, b, 256);
	assert(r[255] == 65535 && g[255] < r[255] && b[255] < g[255] && b[255] > 0);
	luz_rampa(2500, r, g, b, 256);
	uint16_t b2500 = b[255];
	luz_rampa(4500, r, g, b, 256);
	assert(b[255] > b2500); /* mais quente, menos azul */
	assert(r[0] == 0 && g[0] == 0 && b[0] == 0); /* o preto continua preto */
	puts("rampas ok");
	puts("luz ok");
	return 0;
}
