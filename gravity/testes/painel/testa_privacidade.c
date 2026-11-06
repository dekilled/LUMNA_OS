/* servicos/privacidade.c: recentes, o ajuste do GTK e os logs. ./testa_privacidade PASTA */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "privacidade.h"

int main(int argc, char **argv) {
	const char *raiz = argv[1];
	char *cfg = g_build_filename(raiz, "cfg", NULL), *dados = g_build_filename(raiz, "share", NULL), *estado = g_build_filename(raiz, "state", NULL);
	setenv("XDG_CONFIG_HOME", cfg, 1);
	setenv("XDG_DATA_HOME", dados, 1);
	setenv("XDG_STATE_HOME", estado, 1);
	struct privacidade p;
	privacidade_ler(&p);
	assert(p.lembrar_recentes); /* o padrão é lembrar */
	/* um xbel com dois recentes */
	g_mkdir_with_parents(dados, 0755);
	char *x = g_build_filename(dados, "recently-used.xbel", NULL);
	g_file_set_contents(x, "<xbel><bookmark href=\"file:///a\"/><bookmark href=\"file:///b\"/></xbel>", -1, NULL);
	assert(privacidade_recentes_n() == 2);
	assert(privacidade_limpar_recentes() && privacidade_recentes_n() == 0);
	puts("recentes ok");
	/* não lembrar: o ini e o GTK, sem apagar o resto do settings.ini */
	char *g3 = g_build_filename(cfg, "gtk-3.0", NULL), *s3 = g_build_filename(g3, "settings.ini", NULL);
	g_mkdir_with_parents(g3, 0755);
	g_file_set_contents(s3, "[Settings]\ngtk-theme-name=Adwaita\n", -1, NULL);
	p.lembrar_recentes = false;
	assert(privacidade_gravar(&p));
	struct privacidade q;
	privacidade_ler(&q);
	assert(!q.lembrar_recentes);
	char *t = NULL;
	assert(g_file_get_contents(s3, &t, NULL, NULL) && strstr(t, "gtk-recent-files-enabled=false") && strstr(t, "gtk-theme-name=Adwaita"));
	char *s4 = g_build_filename(cfg, "gtk-4.0", "settings.ini", NULL);
	assert(g_file_get_contents(s4, &t, NULL, NULL) && strstr(t, "gtk-recent-files-enabled=false"));
	p.lembrar_recentes = true;
	assert(privacidade_gravar(&p) && g_file_get_contents(s3, &t, NULL, NULL) && strstr(t, "gtk-recent-files-enabled=true"));
	puts("ajuste ok");
	/* os logs */
	char *lp = g_build_filename(estado, "lumna", NULL);
	g_mkdir_with_parents(lp, 0755);
	char *l1 = g_build_filename(lp, "emma.log", NULL), *l2 = g_build_filename(lp, "outro.log", NULL), *l3 = g_build_filename(lp, "nao-e-log.txt", NULL);
	g_file_set_contents(l1, "abc", -1, NULL);
	g_file_set_contents(l2, "abcdef", -1, NULL);
	g_file_set_contents(l3, "x", -1, NULL);
	struct log_arquivo lista[LOGS_MAX];
	assert(logs_listar(lista, LOGS_MAX) == 2);
	assert(!logs_apagar("../x.log") && !logs_apagar("nao-e-log.txt"));
	assert(logs_apagar("emma.log") && logs_listar(lista, LOGS_MAX) == 1 && !strcmp(lista[0].nome, "outro.log") && lista[0].tamanho == 6);
	assert(logs_apagar("") && logs_listar(lista, LOGS_MAX) == 0 && g_file_test(l3, G_FILE_TEST_EXISTS));
	puts("logs ok");
	puts("privacidade ok");
	return 0;
}
