#include "perfil.h"

#include <glib.h>
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

const char *perfil_nome(const char *id) {
	return !strcmp(id, "power-saver") ? "Economia" : !strcmp(id, "balanced") ? "Equilibrado" : !strcmp(id, "performance") ? "Desempenho" : id;
}

bool perfil_ler(struct perfil_info *p) {
	memset(p, 0, sizeof(*p));
	char *argv_list[] = { "powerprofilesctl", "list", NULL };
	char *lista = saida_de(argv_list);
	if (!lista) {
		return false;
	}
	char **linhas = g_strsplit(lista, "\n", -1);
	for (char **l = linhas; *l; l++) {
		char *t = *l;
		bool ativo = *t == '*';
		if (*t == '*' || *t == ' ') {
			t++;
			while (*t == ' ') {
				t++;
			}
			size_t n = strlen(t);
			/* uma linha "nome:" no começo (sem recuo grande): é um perfil */
			if (n > 1 && t[n - 1] == ':' && !strchr(t, '\t') && p->n < 4 && ((*l)[0] == '*' || (*l)[1] == ' ' || (*l)[1] != ' ')) {
				char nome[24];
				g_strlcpy(nome, t, sizeof(nome) < n ? sizeof(nome) : n);
				if (!strcmp(nome, "performance") || !strcmp(nome, "balanced") || !strcmp(nome, "power-saver")) {
					g_strlcpy(p->lista[p->n++], nome, 24);
					if (ativo) {
						g_strlcpy(p->atual, nome, sizeof(p->atual));
					}
				}
			}
		}
		if (strstr(*l, "Degraded:") && !strstr(*l, ": no") && !strstr(*l, ":\tno")) {
			p->degradado |= strstr(*l, "no") == NULL;
		}
	}
	g_strfreev(linhas);
	g_free(lista);
	char *argv_get[] = { "powerprofilesctl", "get", NULL };
	char *atual = saida_de(argv_get);
	if (atual && *g_strstrip(atual)) {
		g_strlcpy(p->atual, atual, sizeof(p->atual));
	}
	g_free(atual);
	p->existe = p->n > 0;
	return p->existe;
}

bool perfil_definir(const char *perfil, char *erro, size_t tam) {
	if (!perfil || (strcmp(perfil, "performance") && strcmp(perfil, "balanced") && strcmp(perfil, "power-saver"))) {
		return false;
	}
	char *argv[] = { "powerprofilesctl", "set", (char *)perfil, NULL };
	char *err = NULL;
	int estado = 0;
	bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &err, &estado, NULL) && WIFEXITED(estado) &&
		WEXITSTATUS(estado) == 0;
	if (!ok && erro && tam) {
		g_strlcpy(erro, err && *err ? g_strstrip(err) : "o powerprofilesctl não conseguiu", tam);
	}
	g_free(err);
	return ok;
}
