/* Os apps padrão (servicos/appspadrao.c), com .desktop falsos. ./testa_appspadrao PASTA */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "appspadrao.h"

static void escrever(const char *pasta, const char *nome, const char *texto) {
	g_mkdir_with_parents(pasta, 0755);
	char *c = g_build_filename(pasta, nome, NULL);
	g_file_set_contents(c, texto, -1, NULL);
	g_free(c);
}

int main(int argc, char **argv) {
	const char *raiz = argv[1];
	char *dados = g_build_filename(raiz, "share", NULL), *apps = g_build_filename(raiz, "share", "applications", NULL), *cfg = g_build_filename(raiz, "cfg", NULL);
	setenv("XDG_DATA_HOME", dados, 1);
	setenv("XDG_DATA_DIRS", dados, 1);
	setenv("XDG_CONFIG_HOME", cfg, 1);
	setenv("XDG_CONFIG_DIRS", cfg, 1);
	escrever(apps, "navegador-a.desktop", "[Desktop Entry]\nType=Application\nName=Navegador A\nExec=true %u\nMimeType=x-scheme-handler/http;x-scheme-handler/https;text/html;\n");
	escrever(apps, "navegador-b.desktop", "[Desktop Entry]\nType=Application\nName=Navegador B\nExec=true %u\nMimeType=x-scheme-handler/http;x-scheme-handler/https;text/html;\n");
	escrever(apps, "player.desktop", "[Desktop Entry]\nType=Application\nName=Player\nExec=true %U\nMimeType=video/mp4;video/webm;audio/mpeg;\n");
	escrever(apps, "escondido.desktop", "[Desktop Entry]\nType=Application\nName=Escondido\nExec=true %u\nNoDisplay=true\nMimeType=x-scheme-handler/http;\n");
	/* o que o update-desktop-database faz: o GIO acha os apps de um tipo por este arquivo */
	escrever(apps, "mimeinfo.cache", "[MIME Cache]\nx-scheme-handler/http=navegador-a.desktop;navegador-b.desktop;escondido.desktop;\n"
		"x-scheme-handler/https=navegador-a.desktop;navegador-b.desktop;\ntext/html=navegador-a.desktop;navegador-b.desktop;\n"
		"video/mp4=player.desktop;\nvideo/webm=player.desktop;\naudio/mpeg=player.desktop;\n");
	/* o padrão de antes, pelo mimeapps.list */
	escrever(cfg, "mimeapps.list", "[Default Applications]\nx-scheme-handler/http=navegador-a.desktop\n");

	int n = padrao_n();
	assert(n == 8);
	struct padrao_categoria c;
	padrao_ler(0, &c);
	assert(!strcmp(c.id, "navegador") && !strcmp(c.atual, "navegador-a.desktop"));
	assert(c.nopcoes == 2); /* o escondido não conta */
	puts("ler ok");
	assert(padrao_definir(0, "navegador-b.desktop"));
	padrao_ler(0, &c);
	assert(!strcmp(c.atual, "navegador-b.desktop"));
	/* vale para todos os tipos da categoria (o mimeapps.list de verdade tem os três) */
	char *texto = NULL;
	char *lista = g_build_filename(cfg, "mimeapps.list", NULL);
	assert(g_file_get_contents(lista, &texto, NULL, NULL));
	assert(strstr(texto, "x-scheme-handler/https=navegador-b.desktop") && strstr(texto, "text/html=navegador-b.desktop"));
	puts("definir ok");
	padrao_ler(3, &c); /* vídeo: só o player sabe */
	assert(!strcmp(c.id, "video") && c.nopcoes == 1 && !strcmp(c.opcoes[0].id, "player.desktop"));
	assert(!padrao_definir(3, "nao-existe.desktop") && !padrao_definir(99, "player.desktop"));
	puts("apps padrao ok");
	return 0;
}
