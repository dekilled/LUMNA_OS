#include "privacidade.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "modos.h"

static char *config_pasta(void) {
	const char *c = getenv("XDG_CONFIG_HOME");
	return c && *c ? g_strdup(c) : g_build_filename(g_get_home_dir(), ".config", NULL);
}

static char *dados_pasta(void) {
	const char *c = getenv("XDG_DATA_HOME");
	return c && *c ? g_strdup(c) : g_build_filename(g_get_home_dir(), ".local", "share", NULL);
}

void privacidade_ler(struct privacidade *p) {
	p->lembrar_recentes = true;
	char *cfg = config_pasta();
	char *c = g_build_filename(cfg, "lumna", "privacidade.ini", NULL), *texto = NULL;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (igual && **l != '#') {
				*igual = '\0';
				if (!strcmp(g_strstrip(*l), "lembrar_recentes")) {
					p->lembrar_recentes = strcmp(g_strstrip(igual + 1), "nao") != 0;
				}
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(c);
	g_free(cfg);
}

/* [Settings] gtk-recent-files-enabled = ... num settings.ini do GTK, sem mexer no resto */
static void gtk_recentes(const char *cfg, const char *versao, bool ligar) {
	char *pasta = g_build_filename(cfg, versao, NULL), *c = g_build_filename(pasta, "settings.ini", NULL);
	g_mkdir_with_parents(pasta, 0755);
	GKeyFile *kf = g_key_file_new();
	g_key_file_load_from_file(kf, c, G_KEY_FILE_KEEP_COMMENTS, NULL);
	g_key_file_set_boolean(kf, "Settings", "gtk-recent-files-enabled", ligar);
	g_key_file_save_to_file(kf, c, NULL);
	g_key_file_free(kf);
	g_free(c);
	g_free(pasta);
}

bool privacidade_gravar(const struct privacidade *p) {
	char *cfg = config_pasta();
	char *pasta = g_build_filename(cfg, "lumna", NULL), *c = g_build_filename(pasta, "privacidade.ini", NULL);
	g_mkdir_with_parents(pasta, 0755);
	char *texto = g_strdup_printf("# O que o LUMNA lembra (o Painel de controle grava este arquivo)\nlembrar_recentes = %s\n", p->lembrar_recentes ? "sim" : "nao");
	char *tmp = g_strconcat(c, ".novo", NULL);
	bool ok = g_file_set_contents(tmp, texto, -1, NULL) && g_rename(tmp, c) == 0;
	gtk_recentes(cfg, "gtk-3.0", p->lembrar_recentes);
	gtk_recentes(cfg, "gtk-4.0", p->lembrar_recentes);
	g_free(tmp);
	g_free(texto);
	g_free(c);
	g_free(pasta);
	g_free(cfg);
	return ok;
}

static char *xbel(void) {
	char *d = dados_pasta(), *c = g_build_filename(d, "recently-used.xbel", NULL);
	g_free(d);
	return c;
}

bool privacidade_limpar_recentes(void) {
	char *c = xbel();
	char *d = g_path_get_dirname(c);
	g_mkdir_with_parents(d, 0755);
	bool ok = g_file_set_contents(c, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xbel version=\"1.0\"\n      xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\"\n      xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\">\n</xbel>\n", -1, NULL);
	g_free(d);
	g_free(c);
	return ok;
}

int privacidade_recentes_n(void) {
	char *c = xbel(), *texto = NULL;
	int n = 0;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		for (const char *p = texto; (p = strstr(p, "<bookmark ")); p += 10) {
			n++;
		}
		g_free(texto);
	}
	g_free(c);
	return n;
}

bool privacidade_limpar_copiados(void) {
	char r[256];
	return orbit_pedido("limpar_copiados", r, sizeof(r)) && strstr(r, "\"ok\":true");
}

char *logs_pasta(void) {
	const char *e = getenv("XDG_STATE_HOME");
	return e && *e ? g_build_filename(e, "lumna", NULL) : g_build_filename(g_get_home_dir(), ".local", "state", "lumna", NULL);
}

static int mais_novo(const void *a, const void *b) {
	int64_t x = ((const struct log_arquivo *)a)->quando, y = ((const struct log_arquivo *)b)->quando;
	return x < y ? 1 : x > y ? -1 : 0;
}

int logs_listar(struct log_arquivo *lista, int max) {
	char *pasta = logs_pasta();
	GDir *dir = g_dir_open(pasta, 0, NULL);
	const char *nome;
	int n = 0;
	while (dir && (nome = g_dir_read_name(dir)) && n < max) {
		if (!g_str_has_suffix(nome, ".log")) {
			continue;
		}
		char *c = g_build_filename(pasta, nome, NULL);
		GStatBuf st;
		if (g_stat(c, &st) == 0 && S_ISREG(st.st_mode)) {
			g_strlcpy(lista[n].nome, nome, sizeof(lista[n].nome));
			lista[n].tamanho = st.st_size;
			lista[n].quando = st.st_mtime;
			n++;
		}
		g_free(c);
	}
	if (dir) {
		g_dir_close(dir);
	}
	g_free(pasta);
	qsort(lista, (size_t)n, sizeof(lista[0]), mais_novo);
	return n;
}

bool logs_apagar(const char *nome) {
	if (nome && *nome && (strchr(nome, '/') || !g_str_has_suffix(nome, ".log"))) {
		return false;
	}
	struct log_arquivo l[LOGS_MAX];
	int n = logs_listar(l, LOGS_MAX);
	char *pasta = logs_pasta();
	bool ok = true;
	for (int i = 0; i < n; i++) {
		if (nome && *nome && strcmp(nome, l[i].nome)) {
			continue;
		}
		char *c = g_build_filename(pasta, l[i].nome, NULL);
		ok &= g_remove(c) == 0;
		g_free(c);
	}
	g_free(pasta);
	return ok;
}
