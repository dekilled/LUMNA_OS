/* Os apps que abrem ao entrar (servicos/autostart.c): lista, liga/desliga, adiciona, remove e roda, com pastas falsas.
 * ./testa_autostart PASTA  (o que se roda cria arquivos em PASTA/ran-*) */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autostart.h"

static void escrever(const char *pasta, const char *nome, const char *texto) {
	g_mkdir_with_parents(pasta, 0755);
	char *c = g_build_filename(pasta, nome, NULL);
	g_file_set_contents(c, texto, -1, NULL);
	g_free(c);
}

static const struct autostart_item *achar(const struct autostart_item *l, int n, const char *arq) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(l[i].arquivo, arq)) {
			return &l[i];
		}
	}
	return NULL;
}

int main(int argc, char **argv) {
	const char *raiz = argv[1];
	char *sis = g_build_filename(raiz, "etc", "autostart", NULL), *usu = g_build_filename(raiz, "cfg", "autostart", NULL);
	char *etc = g_build_filename(raiz, "etc", NULL), *cfg = g_build_filename(raiz, "cfg", NULL);
	setenv("XDG_CONFIG_DIRS", etc, 1);
	setenv("XDG_CONFIG_HOME", cfg, 1);
	setenv("XDG_CURRENT_DESKTOP", "LUMNA", 1);
	char cmd[400];
	snprintf(cmd, sizeof(cmd), "[Desktop Entry]\nType=Application\nName=Sistema A\nExec=touch %s/ran-a\n", raiz);
	escrever(sis, "a.desktop", cmd);
	escrever(sis, "b.desktop", "[Desktop Entry]\nType=Application\nName=So do GNOME\nExec=true\nOnlyShowIn=GNOME;\n");
	escrever(sis, "c.desktop", "[Desktop Entry]\nType=Application\nName=Escondido\nExec=true\nHidden=true\n");
	escrever(sis, "d.desktop", "[Desktop Entry]\nType=Application\nName=Sem programa\nExec=true\nTryExec=/nao/existe/mesmo\n");
	snprintf(cmd, sizeof(cmd), "[Desktop Entry]\nType=Application\nName=Meu app\nExec=touch %s/ran-meu\n", raiz);
	escrever(usu, "meu.desktop", cmd);

	struct autostart_item l[AUTOSTART_MAX];
	int n = autostart_listar(l, AUTOSTART_MAX);
	assert(n == 5);
	assert(achar(l, n, "a.desktop") && achar(l, n, "a.desktop")->ligado && achar(l, n, "a.desktop")->do_sistema);
	assert(!achar(l, n, "b.desktop")->ligado); /* é de outro desktop */
	assert(!achar(l, n, "c.desktop")->ligado); /* escondido */
	assert(!achar(l, n, "d.desktop")->ligado); /* o programa não existe */
	assert(achar(l, n, "meu.desktop")->ligado && achar(l, n, "meu.desktop")->do_usuario && !achar(l, n, "meu.desktop")->do_sistema);
	puts("lista ok");

	/* desligar o do sistema: nasce uma cópia no usuário com Hidden */
	assert(autostart_definir("a.desktop", false));
	n = autostart_listar(l, AUTOSTART_MAX);
	assert(n == 5 && !achar(l, n, "a.desktop")->ligado && achar(l, n, "a.desktop")->do_usuario && achar(l, n, "a.desktop")->do_sistema);
	/* ligar o que era de outro desktop */
	assert(autostart_definir("b.desktop", true));
	n = autostart_listar(l, AUTOSTART_MAX);
	assert(achar(l, n, "b.desktop")->ligado);
	puts("liga e desliga ok");

	/* adicionar */
	char arq[200];
	assert(autostart_adicionar("Meu Script Novo", "echo oi", "utilities-terminal", arq, sizeof(arq)));
	assert(!strcmp(arq, "meu-script-novo.desktop"));
	n = autostart_listar(l, AUTOSTART_MAX);
	assert(n == 6 && achar(l, n, arq)->ligado && !strcmp(achar(l, n, arq)->comando, "echo oi"));
	assert(autostart_adicionar("Meu Script Novo", "echo dois", NULL, arq, sizeof(arq)) && !strcmp(arq, "meu-script-novo-2.desktop"));
	/* um nome que sai da pasta não passa */
	assert(!autostart_definir("../x.desktop", true) && !autostart_remover("../x.desktop"));
	puts("adicionar ok");

	/* remover: o do usuário some; o do sistema só desliga */
	assert(autostart_remover("meu.desktop") && autostart_remover("meu-script-novo-2.desktop"));
	assert(autostart_remover("a.desktop"));
	n = autostart_listar(l, AUTOSTART_MAX);
	assert(!achar(l, n, "meu.desktop") && !achar(l, n, "meu-script-novo-2.desktop"));
	assert(achar(l, n, "a.desktop") && !achar(l, n, "a.desktop")->ligado);
	puts("remover ok");

	/* rodar: só os ligados (b e o script novo; o a foi desligado) */
	assert(autostart_rodar() == 2);
	char *ran = g_build_filename(raiz, "ran-a", NULL), *ran_meu = g_build_filename(raiz, "ran-meu", NULL);
	g_usleep(300000);
	assert(!g_file_test(ran, G_FILE_TEST_EXISTS) && !g_file_test(ran_meu, G_FILE_TEST_EXISTS));
	assert(autostart_definir("a.desktop", true) && autostart_rodar() == 3);
	for (int i = 0; i < 20 && !g_file_test(ran, G_FILE_TEST_EXISTS); i++) {
		g_usleep(100000);
	}
	assert(g_file_test(ran, G_FILE_TEST_EXISTS));
	puts("rodar ok");
	puts("autostart ok");
	return 0;
}
