#include "discos_cfg.h"

#include <glib.h>
#include <glib/gstdio.h>

static char *arquivo(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "discos.ini", NULL);
}

void discos_cfg_ler(discos_cfg *c) {
	c->ao_conectar = false;
	c->ao_entrar = false;
	GKeyFile *kf = g_key_file_new();
	char *a = arquivo();
	if (g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL)) {
		c->ao_conectar = g_key_file_get_boolean(kf, "discos", "montar_ao_conectar", NULL);
		c->ao_entrar = g_key_file_get_boolean(kf, "discos", "montar_ao_entrar", NULL);
	}
	g_free(a);
	g_key_file_free(kf);
}

bool discos_cfg_gravar(const discos_cfg *c) {
	GKeyFile *kf = g_key_file_new();
	g_key_file_set_boolean(kf, "discos", "montar_ao_conectar", c->ao_conectar);
	g_key_file_set_boolean(kf, "discos", "montar_ao_entrar", c->ao_entrar);
	char *a = arquivo();
	char *pasta = g_path_get_dirname(a);
	g_mkdir_with_parents(pasta, 0700);
	bool ok = g_key_file_save_to_file(kf, a, NULL);
	g_free(pasta);
	g_free(a);
	g_key_file_free(kf);
	return ok;
}

bool discos_deve_montar(const discos_cfg *c, enum discos_evento ev, bool removivel, bool montado, bool pode_automontar) {
	if (!removivel || montado || !pode_automontar) {
		return false;
	}
	return ev == DISCOS_CONECTOU ? c->ao_conectar : c->ao_entrar;
}
