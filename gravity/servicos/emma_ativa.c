#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "emma_ativa.h"

static char *arquivo(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "emma-sistema.ini", NULL);
}

bool emma_ativa(void) {
	GKeyFile *kf = g_key_file_new();
	char *a = arquivo();
	bool ativa = true;
	if (g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL)) {
		char *v = g_key_file_get_string(kf, "emma", "ativa", NULL);
		if (v && (!strcmp(v, "nao") || !strcmp(v, "não") || !strcmp(v, "false") || !strcmp(v, "0"))) {
			ativa = false;
		}
		g_free(v);
	}
	g_free(a);
	g_key_file_free(kf);
	return ativa;
}

bool emma_ativa_gravar(bool ativa) {
	char *a = arquivo(), *dir = g_path_get_dirname(a);
	g_mkdir_with_parents(dir, 0700);
	char *texto = g_strdup_printf("[emma]\nativa=%s\n", ativa ? "sim" : "nao");
	bool ok = g_file_set_contents(a, texto, -1, NULL);
	g_free(texto);
	g_free(dir);
	g_free(a);
	return ok;
}
