/* A sessão (servicos/sessao.c): das janelas do Orbit aos .desktop, gravar, ler e reabrir. ./testa_sessao PASTA */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sessao.h"

static void escrever(const char *pasta, const char *nome, const char *texto) {
	g_mkdir_with_parents(pasta, 0755);
	char *c = g_build_filename(pasta, nome, NULL);
	g_file_set_contents(c, texto, -1, NULL);
	g_free(c);
}

int main(int argc, char **argv) {
	const char *raiz = argv[1];
	char *dados = g_build_filename(raiz, "share", NULL), *apps = g_build_filename(raiz, "share", "applications", NULL);
	char *estado = g_build_filename(raiz, "state", NULL);
	setenv("XDG_DATA_HOME", dados, 1);
	setenv("XDG_DATA_DIRS", dados, 1);
	setenv("XDG_STATE_HOME", estado, 1);
	char cmd[600];
	/* o app_id é o nome do .desktop */
	escrever(apps, "org.exemplo.Notas.desktop", "[Desktop Entry]\nType=Application\nName=Notas\nExec=true\n");
	/* o app_id é o StartupWMClass */
	escrever(apps, "navegador.desktop", "[Desktop Entry]\nType=Application\nName=Navegador\nExec=true\nStartupWMClass=Navegador-Janela\n");
	/* o app_id é o programa */
	snprintf(cmd, sizeof(cmd), "[Desktop Entry]\nType=Application\nName=Editor\nExec=%s/editorzinho %%F\n", raiz);
	escrever(apps, "editor.desktop", cmd);
	snprintf(cmd, sizeof(cmd), "%s/editorzinho", raiz);
	g_file_set_contents(cmd, "#!/bin/sh\nexit 0\n", -1, NULL);
	g_chmod(cmd, 0755); /* o GIO só lista o app se o programa existe */
	/* escondido: não conta */
	escrever(apps, "oculto.desktop", "[Desktop Entry]\nType=Application\nName=Oculto\nExec=true\nNoDisplay=true\nStartupWMClass=oculto\n");
	const char *json = "{\"ok\":true,\"janelas\":["
		"{\"id\":1,\"app_id\":\"org.exemplo.Notas\"},{\"id\":2,\"app_id\":\"Navegador-Janela\"},{\"id\":3,\"app_id\":\"editorzinho\"},"
		"{\"id\":4,\"app_id\":\"org.exemplo.Notas\"},{\"id\":5,\"app_id\":\"lumna-midia-web\"},{\"id\":6,\"app_id\":\"lumna-painel\"},"
		"{\"id\":7,\"app_id\":\"oculto\"},{\"id\":8,\"app_id\":\"desconhecido\"},{\"id\":9,\"app_id\":\"\"}]}";
	char ids[SESSAO_MAX][160];
	int n = sessao_de_json(json, ids, SESSAO_MAX);
	assert(n == 3);
	assert(!strcmp(ids[0], "org.exemplo.Notas.desktop") && !strcmp(ids[1], "navegador.desktop") && !strcmp(ids[2], "editor.desktop"));
	puts("janelas para apps ok");
	assert(sessao_gravar((const char(*)[160])ids, n));
	char lidos[SESSAO_MAX][160];
	assert(sessao_ler(lidos, SESSAO_MAX) == 3 && !strcmp(lidos[2], "editor.desktop"));
	puts("gravar e ler ok");
	/* reabrir: o Exec do Notas é "true" (abre); os outros também; um que sumiu não derruba */
	strcpy(lidos[3], "sumiu.desktop");
	assert(sessao_abrir(lidos, 4) >= 2);
	puts("sessao ok");
	return 0;
}
