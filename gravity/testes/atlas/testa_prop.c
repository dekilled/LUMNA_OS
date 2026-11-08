/* As propriedades (arquivos_prop.c): níveis de acesso, grupos, aplicar permissões (com recursão) e o .desktop. */
#include "arquivos_prop.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int falhas;
#define CONFERE(c, ...) do { if (!(c)) { fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); falhas++; } } while (0)

static mode_t modo_de(const char *c) {
	struct stat st;
	stat(c, &st);
	return st.st_mode & 07777;
}

int main(void) {
	/* níveis */
	CONFERE(ar_nivel_pasta_de(7) == AR_NP_TUDO && ar_nivel_pasta_de(5) == AR_NP_LER_ACESSAR && ar_nivel_pasta_de(1) == AR_NP_ACESSAR && ar_nivel_pasta_de(0) == AR_NP_NENHUM, "níveis de pasta");
	CONFERE(ar_nivel_pasta_de(4) == AR_NP_OUTRO && ar_nivel_pasta_de(6) == AR_NP_OUTRO, "uma combinação sem nome é AR_NP_OUTRO");
	for (int n = 0; n < 4; n++) {
		CONFERE(ar_nivel_pasta_de(ar_rwx_da_pasta(n)) == n, "ida e volta do nível de pasta %d", n);
	}
	CONFERE(ar_nivel_arq_de(6) == AR_NA_LER_GRAVAR && ar_nivel_arq_de(4) == AR_NA_LER && ar_nivel_arq_de(0) == AR_NA_NENHUM && ar_nivel_arq_de(7) == AR_NA_LER_GRAVAR, "níveis de arquivo (o x não conta)");
	CONFERE(ar_nivel_arq_de(2) == AR_NA_OUTRO, "só escrever é AR_NA_OUTRO");
	for (int n = 0; n < 3; n++) {
		CONFERE(ar_nivel_arq_de(ar_rwx_do_arq(n)) == n, "ida e volta do nível de arquivo %d", n);
	}

	/* grupos: o do arquivo vem primeiro e não repete */
	ar_grupo g[16];
	int ng = ar_grupos_da_pessoa(g, 16, getegid());
	CONFERE(ng >= 1 && g[0].gid == getegid() && g[0].nome[0], "o grupo atual é o primeiro (%d)", ng);
	for (int i = 0; i < ng; i++) {
		for (int j = i + 1; j < ng; j++) {
			CONFERE(g[i].gid != g[j].gid, "grupo repetido");
		}
	}

	/* aplicar, sem recursão e com */
	char *raiz = g_dir_make_tmp("atlas-prop-XXXXXX", NULL);
	char *sub = g_build_filename(raiz, "pasta", NULL), *dentro = g_build_filename(sub, "dentro", NULL), *a = g_build_filename(sub, "a.txt", NULL), *x = g_build_filename(dentro, "run.sh", NULL);
	g_mkdir_with_parents(dentro, 0755);
	g_file_set_contents(a, "oi", -1, NULL);
	g_file_set_contents(x, "#!/bin/sh\n", -1, NULL);
	chmod(a, 0644);
	chmod(x, 0755);
	ar_prop_resultado r;
	CONFERE(ar_prop_aplicar(sub, 0750, false, 0, false, &r) && r.alterados == 1, "chmod só na pasta (%lld)", (long long)r.alterados);
	CONFERE(modo_de(sub) == 0750 && modo_de(a) == 0644, "sem recursão, o de dentro não muda");
	CONFERE(ar_prop_aplicar(sub, 0750, false, 0, true, &r) && r.erros == 0, "recursivo sem erros");
	CONFERE(modo_de(dentro) == 0750, "a pasta de dentro recebe o modo (%o)", modo_de(dentro));
	CONFERE(modo_de(a) == 0640, "o arquivo recebe só r e w do modo, sem x (%o)", modo_de(a));
	CONFERE(modo_de(x) == 0751, "o arquivo executável continua com os x que tinha (%o)", modo_de(x));
	CONFERE(!ar_prop_aplicar("/nao/existe/mesmo", 0644, false, 0, false, &r) && r.erros == 1, "um caminho que não existe dá erro");
	/* o mesmo grupo de agora: trocar_grupo não falha */
	CONFERE(ar_prop_aplicar(a, 0600, true, getegid(), false, &r) && modo_de(a) == 0600, "trocar para o próprio grupo funciona");
	/* link simbólico não é seguido */
	char *ln = g_build_filename(sub, "ln", NULL);
	CONFERE(symlink(a, ln) == 0, "symlink");
	chmod(a, 0644);
	ar_prop_aplicar(sub, 0700, false, 0, true, &r);
	CONFERE(modo_de(a) == 0600, "o link simbólico não leva a mudança ao alvo (%o)", modo_de(a));

	/* .desktop */
	char *d = g_build_filename(raiz, "app.desktop", NULL);
	g_file_set_contents(d, "[Desktop Entry]\nType=Application\nName=Teste\nName[pt_BR]=Teste\nComment=Um teste\nExec=sh %U\nIcon=utilities-terminal\nVersion=1.2\n", -1, NULL);
	ar_prop_app app;
	CONFERE(ar_prop_desktop(d, &app) && !strcmp(app.nome, "Teste") && !strcmp(app.versao, "1.2") && !strcmp(app.comentario, "Um teste"), "lê o .desktop (%s)", app.nome);
	CONFERE(app.executavel[0] == '/' && g_str_has_suffix(app.executavel, "/sh"), "acha o programa do Exec= no PATH (%s)", app.executavel);
	CONFERE(!ar_prop_desktop(a, &app), "um arquivo que não é .desktop não vale");

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("propriedades: ok\n");
	return 0;
}
