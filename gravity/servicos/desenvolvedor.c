#include "desenvolvedor.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

static char *caminho(void) {
	const char *config = getenv("XDG_CONFIG_HOME");
	return config && *config ? g_build_filename(config, "lumna", "desenvolvedor.ini", NULL)
				 : g_build_filename(g_get_home_dir(), ".config", "lumna", "desenvolvedor.ini", NULL);
}

struct lumna_dev dev_ler_cru(void) {
	struct lumna_dev d = { 0 };
	char *c = caminho(), *texto = NULL;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#') {
				continue;
			}
			*igual = '\0';
			char *chave = g_strstrip(*l);
			bool sim = !strcmp(g_strstrip(igual + 1), "sim");
			if (!strcmp(chave, "ativo")) {
				d.ativo = sim;
			} else if (!strcmp(chave, "toque")) {
				d.toque = sim;
			} else if (!strcmp(chave, "ao_vivo")) {
				d.ao_vivo = sim;
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(c);
	return d;
}

struct lumna_dev dev_ler(void) {
	struct lumna_dev d = dev_ler_cru();
	if (!d.ativo) {
		d.toque = d.ao_vivo = false;
	}
	return d;
}

bool dev_gravar(struct lumna_dev d) {
	char *c = caminho(), *pasta = g_path_get_dirname(c);
	g_mkdir_with_parents(pasta, 0700);
	char *texto = g_strdup_printf("# O modo desenvolvedor (o Painel de controle grava este arquivo)\n"
				      "ativo = %s\ntoque = %s\nao_vivo = %s\n",
		d.ativo ? "sim" : "nao", d.toque ? "sim" : "nao", d.ao_vivo ? "sim" : "nao");
	bool ok = g_file_set_contents(c, texto, -1, NULL);
	g_free(texto);
	g_free(pasta);
	g_free(c);
	return ok;
}
