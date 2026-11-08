#include "teclado_cfg.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "teclado_layout.h"

char *tcfg_caminho(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "teclado.ini", NULL);
}

static const char *um_de(const char *v, const char *a, const char *b, const char *c) {
	return v && (!strcmp(v, a) || !strcmp(v, b) || !strcmp(v, c)) ? v : a;
}

void tcfg_ler(tcfg *cfg) {
	snprintf(cfg->modo, sizeof(cfg->modo), "auto");
	snprintf(cfg->tamanho, sizeof(cfg->tamanho), "medio");
	char *caminho = tcfg_caminho();
	GKeyFile *kf = g_key_file_new();
	if (g_key_file_load_from_file(kf, caminho, G_KEY_FILE_NONE, NULL)) {
		char *modo = g_key_file_get_string(kf, "teclado", "modo", NULL);
		char *tam = g_key_file_get_string(kf, "teclado", "tamanho", NULL);
		snprintf(cfg->modo, sizeof(cfg->modo), "%s", um_de(modo, "auto", "sempre", "nunca"));
		snprintf(cfg->tamanho, sizeof(cfg->tamanho), "%s", um_de(tam, "medio", "pequeno", "grande"));
		g_free(modo);
		g_free(tam);
	}
	g_key_file_free(kf);
	g_free(caminho);
}

bool tcfg_gravar(const tcfg *cfg) {
	char *caminho = tcfg_caminho();
	char *pasta = g_path_get_dirname(caminho);
	g_mkdir_with_parents(pasta, 0700);
	GKeyFile *kf = g_key_file_new();
	g_key_file_load_from_file(kf, caminho, G_KEY_FILE_KEEP_COMMENTS, NULL);
	g_key_file_set_string(kf, "teclado", "modo", um_de(cfg->modo, "auto", "sempre", "nunca"));
	g_key_file_set_string(kf, "teclado", "tamanho", um_de(cfg->tamanho, "medio", "pequeno", "grande"));
	bool ok = g_key_file_save_to_file(kf, caminho, NULL);
	g_key_file_free(kf);
	g_free(pasta);
	g_free(caminho);
	return ok;
}

float tcfg_escala(const tcfg *cfg) {
	return !strcmp(cfg->tamanho, "pequeno") ? 0.8f : !strcmp(cfg->tamanho, "grande") ? 1.2f : 1.0f;
}

static char *recentes_caminho(void) {
	return g_build_filename(g_get_user_data_dir(), "lumna", "teclado", "recentes", NULL);
}

void tcfg_recentes_ler(void) {
	tl_recentes_limpar();
	char *caminho = recentes_caminho();
	char *conteudo = NULL;
	if (g_file_get_contents(caminho, &conteudo, NULL, NULL)) {
		char **linhas = g_strsplit(conteudo, "\n", -1);
		int n = 0;
		while (linhas[n]) {
			n++;
		}
		for (int i = n - 1; i >= 0; i--) { /* o arquivo guarda o mais recente primeiro: entra do fim para o começo */
			if (linhas[i][0] && g_utf8_validate(linhas[i], -1, NULL)) {
				tl_recentes_usar(linhas[i]);
			}
		}
		g_strfreev(linhas);
		g_free(conteudo);
	}
	g_free(caminho);
}

void tcfg_recentes_gravar(void) {
	char *caminho = recentes_caminho();
	char *pasta = g_path_get_dirname(caminho);
	g_mkdir_with_parents(pasta, 0700);
	GString *s = g_string_new(NULL);
	for (int i = 0; i < tl_recentes_n(); i++) {
		g_string_append_printf(s, "%s\n", tl_recentes_get(i));
	}
	g_file_set_contents(caminho, s->str, -1, NULL);
	g_string_free(s, TRUE);
	g_free(pasta);
	g_free(caminho);
}
