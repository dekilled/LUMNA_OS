/* servicos/sons.c com uma pasta de sons falsa e o pw-play falso. ./testa_sons PASTA */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sons.h"

int main(int argc, char **argv) {
	const char *raiz = argv[1];
	char *sons = g_build_filename(raiz, "sons", NULL), *cfg = g_build_filename(raiz, "cfg", NULL), *log = g_build_filename(raiz, "tocou.log", NULL);
	g_mkdir_with_parents(sons, 0755);
	setenv("LUMNA_SONS_DIR", sons, 1);
	setenv("XDG_CONFIG_HOME", cfg, 1);
	setenv("SONS_LOG", log, 1);
	const char *nomes[] = { "message-new-instant.oga", "dialog-warning.oga", "bell.wav", "leiame.txt" };
	for (int i = 0; i < 4; i++) {
		char *c = g_build_filename(sons, nomes[i], NULL);
		g_file_set_contents(c, "x", -1, NULL);
		g_free(c);
	}
	struct sons_cfg c;
	sons_ler(&c);
	assert(c.avisos && c.volume == 70 && !strcmp(c.aviso, "message-new-instant") && !strcmp(c.critico, "dialog-warning"));
	c.avisos = false;
	c.volume = 130; /* passa de 100: se prende */
	g_strlcpy(c.aviso, "bell", sizeof(c.aviso));
	assert(sons_gravar(&c));
	struct sons_cfg d;
	sons_ler(&d);
	assert(!d.avisos && d.volume == 100 && !strcmp(d.aviso, "bell"));
	puts("ajuste ok");
	char lista[SONS_MAX][40];
	assert(sons_listar(lista, SONS_MAX) == 3 && !strcmp(lista[0], "bell") && !strcmp(lista[2], "message-new-instant"));
	assert(!strcmp(sons_rotulo("bell"), "Sino") && !strcmp(sons_rotulo("meu-som"), "meu-som"));
	puts("listar ok");
	assert(sons_tocar("bell", 50));
	assert(!sons_tocar("nao-existe", 50) && !sons_tocar("../x", 50) && !sons_tocar("", 50));
	char *t = NULL;
	for (int i = 0; i < 30 && !g_file_get_contents(log, &t, NULL, NULL); i++) {
		g_usleep(100000);
	}
	assert(t && strstr(t, "--volume=0.50") && strstr(t, "bell.wav"));
	puts("tocar ok");
	puts("sons ok");
	return 0;
}
