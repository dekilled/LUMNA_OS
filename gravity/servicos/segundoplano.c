#include "segundoplano.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static char *saida_de(char **argv) {
	char *saida = NULL;
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL)) {
		g_free(saida);
		return NULL;
	}
	return saida;
}

static bool unidade_valida(const char *u) {
	return u && g_str_has_suffix(u, ".service") && !strchr(u, '/') && !strchr(u, ' ') && u[0] != '-';
}

static int por_unidade(const void *a, const void *b) {
	return strcmp(((const struct fundo_servico *)a)->unidade, ((const struct fundo_servico *)b)->unidade);
}

int fundo_listar(struct fundo_servico *lista, int max) {
	char *arquivos_cmd[] = { "systemctl", "--user", "list-unit-files", "--type=service", "--no-legend", "--no-pager", NULL };
	char *arquivos = saida_de(arquivos_cmd);
	if (!arquivos) {
		return 0;
	}
	int n = 0;
	char **linhas = g_strsplit(arquivos, "\n", -1);
	for (char **l = linhas; *l && n < max; l++) {
		char **c = g_strsplit_set(g_strstrip(*l), " \t", -1);
		char *campos[3] = { NULL, NULL, NULL };
		int k = 0;
		for (char **p = c; *p && k < 3; p++) {
			if (**p) {
				campos[k++] = *p;
			}
		}
		if (k >= 2 && unidade_valida(campos[0]) && !strchr(campos[0], '@') && (!strcmp(campos[1], "enabled") || !strcmp(campos[1], "disabled"))) {
			struct fundo_servico *s = &lista[n++];
			memset(s, 0, sizeof(*s));
			g_strlcpy(s->unidade, campos[0], sizeof(s->unidade));
			s->ligado = !strcmp(campos[1], "enabled");
		}
		g_strfreev(c);
	}
	g_strfreev(linhas);
	g_free(arquivos);
	/* o que está carregado: ativo ou não, e a descrição */
	char *carregados_cmd[] = { "systemctl", "--user", "list-units", "--type=service", "--all", "--no-legend", "--no-pager", NULL };
	char *carregados = saida_de(carregados_cmd);
	linhas = g_strsplit(carregados ? carregados : "", "\n", -1);
	for (char **l = linhas; *l; l++) {
		char *t = g_strstrip(*l);
		if (g_str_has_prefix(t, "● ")) {
			t += strlen("● ");
		}
		char **c = g_strsplit_set(t, " \t", 5);
		char *campos[5] = { NULL };
		int k = 0;
		for (char **p = c; *p && k < 4; p++) {
			if (**p) {
				campos[k++] = *p;
			}
		}
		/* a descrição é o resto da linha depois das quatro colunas */
		const char *resto = c[0] ? t : "";
		for (int i = 0; i < 4 && *resto; i++) {
			while (*resto && *resto != ' ' && *resto != '\t') {
				resto++;
			}
			while (*resto == ' ' || *resto == '\t') {
				resto++;
			}
		}
		if (k >= 3) {
			for (int i = 0; i < n; i++) {
				if (!strcmp(lista[i].unidade, campos[0])) {
					lista[i].rodando = !strcmp(campos[2], "active");
					g_strlcpy(lista[i].descricao, resto, sizeof(lista[i].descricao));
				}
			}
		}
		g_strfreev(c);
	}
	g_strfreev(linhas);
	g_free(carregados);
	qsort(lista, (size_t)n, sizeof(lista[0]), por_unidade);
	return n;
}

static bool rodar(char **argv, char *erro, size_t tam) {
	char *err = NULL;
	int estado = 0;
	bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &err, &estado, NULL) &&
		WIFEXITED(estado) && WEXITSTATUS(estado) == 0;
	if (!ok && erro && tam) {
		g_strlcpy(erro, err && *err ? g_strstrip(err) : "o systemctl não conseguiu", tam);
	}
	g_free(err);
	return ok;
}

bool fundo_ligar(const char *unidade, bool ligar, char *erro, size_t tam) {
	if (!unidade_valida(unidade)) {
		return false;
	}
	char *argv[] = { "systemctl", "--user", ligar ? "enable" : "disable", "--now", (char *)unidade, NULL };
	return rodar(argv, erro, tam);
}

bool fundo_rodar(const char *unidade, bool rodar_agora, char *erro, size_t tam) {
	if (!unidade_valida(unidade)) {
		return false;
	}
	char *argv[] = { "systemctl", "--user", rodar_agora ? "start" : "stop", (char *)unidade, NULL };
	return rodar(argv, erro, tam);
}
