#define _GNU_SOURCE
#include "sessao.h"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "json.h"
#include "modos.h"

static bool do_sistema(const char *app_id) {
	static const char *const nao[] = { "lumna-midia", "lumna-jogos", "lumna-gaming", "lumna-modos", "lumna-carrossel", "lumna-lancador", "lumna-barra", "lumna-dock",
		"lumna-bloqueio", "lumna-painel", "lumna-avisos", "lumna-widgets", "lumna-papel", "lumna-desktop", "lumna-capa", NULL };
	for (int i = 0; nao[i]; i++) {
		if (!strncasecmp(app_id, nao[i], strlen(nao[i]))) {
			return true;
		}
	}
	return false;
}

/* o .desktop de um app_id, ou NULL (g_free) */
static char *desktop_de(const char *app_id) {
	char *achado = NULL;
	GList *todos = g_app_info_get_all();
	for (GList *l = todos; l && !achado; l = l->next) {
		GAppInfo *ai = l->data;
		if (!G_IS_DESKTOP_APP_INFO(ai) || !g_app_info_should_show(ai) || !g_app_info_get_id(ai)) {
			continue;
		}
		const char *id = g_app_info_get_id(ai);
		char *base = g_strndup(id, strlen(id) - (g_str_has_suffix(id, ".desktop") ? 8 : 0));
		const char *wm = g_desktop_app_info_get_startup_wm_class(G_DESKTOP_APP_INFO(ai));
		const char *exe = g_app_info_get_executable(ai);
		char *exe_base = exe ? g_path_get_basename(exe) : NULL;
		if (!strcasecmp(base, app_id) || (wm && !strcasecmp(wm, app_id)) || (exe_base && !strcasecmp(exe_base, app_id))) {
			achado = g_strdup(id);
		}
		g_free(base);
		g_free(exe_base);
	}
	g_list_free_full(todos, g_object_unref);
	return achado;
}

int sessao_de_json(const char *json, char ids[][160], int max) {
	struct json *j = json_ler(json);
	const struct json *lista = json_chave(j, "janelas");
	int n = 0;
	for (int i = 0; lista && i < lista->nfilhos && n < max; i++) {
		const char *app_id = json_chave_texto(lista->filhos[i], "app_id");
		if (!*app_id || do_sistema(app_id)) {
			continue;
		}
		char *d = desktop_de(app_id);
		bool repetido = false;
		for (int k = 0; d && k < n; k++) {
			repetido |= !strcmp(ids[k], d);
		}
		if (d && !repetido) {
			g_strlcpy(ids[n++], d, 160);
		}
		g_free(d);
	}
	json_liberar(j);
	return n;
}

int sessao_agora(char ids[][160], int max) {
	char *r = g_malloc(65536);
	int n = -1;
	if (orbit_pedido("janelas", r, 65536)) {
		n = sessao_de_json(r, ids, max);
	}
	g_free(r);
	return n;
}

char *sessao_arquivo(void) {
	const char *estado = getenv("XDG_STATE_HOME");
	return estado && *estado ? g_build_filename(estado, "lumna", "sessao.list", NULL)
				 : g_build_filename(g_get_home_dir(), ".local", "state", "lumna", "sessao.list", NULL);
}

bool sessao_gravar(const char ids[][160], int n) {
	char *c = sessao_arquivo();
	char *pasta = g_path_get_dirname(c);
	g_mkdir_with_parents(pasta, 0755);
	GString *s = g_string_new(NULL);
	for (int i = 0; i < n; i++) {
		g_string_append_printf(s, "%s\n", ids[i]);
	}
	char *tmp = g_strconcat(c, ".novo", NULL);
	bool ok = g_file_set_contents(tmp, s->str, -1, NULL) && rename(tmp, c) == 0;
	g_free(tmp);
	g_string_free(s, TRUE);
	g_free(pasta);
	g_free(c);
	return ok;
}

int sessao_ler(char ids[][160], int max) {
	char *c = sessao_arquivo(), *texto = NULL;
	int n = 0;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l && n < max; l++) {
			g_strstrip(*l);
			if (**l && g_str_has_suffix(*l, ".desktop")) {
				g_strlcpy(ids[n++], *l, 160);
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(c);
	return n;
}

int sessao_abrir(char ids[][160], int n) {
	int abriu = 0;
	for (int i = 0; i < n; i++) {
		GDesktopAppInfo *info = g_desktop_app_info_new(ids[i]);
		GError *erro = NULL;
		if (info && g_app_info_launch(G_APP_INFO(info), NULL, NULL, &erro)) {
			abriu++;
		} else {
			fprintf(stderr, "lumna-inicio: não reabriu %s: %s\n", ids[i], erro ? erro->message : "não existe mais");
		}
		g_clear_error(&erro);
		if (info) {
			g_object_unref(info);
		}
	}
	return abriu;
}
