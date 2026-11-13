#include "enviar.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "arquivos_rede.h"
#include "connectc.h"
#include "json.h"

static void adiciona(env_destino *v, int *n, int max, enum env_tipo tipo, const char *rotulo, const char *caminho) {
	if (*n >= max) {
		return;
	}
	v[*n].tipo = tipo;
	g_strlcpy(v[*n].rotulo, rotulo, sizeof(v[*n].rotulo));
	g_strlcpy(v[*n].caminho, caminho, sizeof(v[*n].caminho));
	(*n)++;
}

int env_destinos(env_destino *v, int max) {
	int n = 0;
	char *desktop = g_build_filename(g_get_home_dir(), "Desktop", NULL); /* a área de trabalho do LUMNA é ~/Desktop (DESKTOP.md) */
	adiciona(v, &n, max, ENV_LOCAL, "Área de trabalho", desktop);
	g_free(desktop);
	const char *docs = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
	if (docs && strcmp(docs, g_get_home_dir()) != 0) {
		adiciona(v, &n, max, ENV_LOCAL, "Documentos", docs);
	}
	ar_rede *r = g_new0(ar_rede, 1);
	ar_rede_ler(r);
	for (int i = 0; i < r->n_contas; i++) {
		if (r->contas[i].pasta_local[0] && g_file_test(r->contas[i].pasta_local, G_FILE_TEST_IS_DIR)) {
			adiciona(v, &n, max, ENV_NUVEM, r->contas[i].nome, r->contas[i].pasta_local);
		}
	}
	for (int i = 0; i < r->n_shares; i++) {
		char caminho[1024] = "";
		if (ar_share_montado(r->shares[i].uri, caminho, sizeof(caminho)) && caminho[0] && g_file_test(caminho, G_FILE_TEST_IS_DIR)) {
			adiciona(v, &n, max, ENV_REDE, r->shares[i].nome, caminho);
		}
	}
	g_free(r);
	return n;
}

bool env_preparar(const env_destino *d) {
	if (d->tipo == ENV_LOCAL) {
		g_mkdir_with_parents(d->caminho, 0755); /* só a Área de trabalho pode faltar */
	}
	return g_file_test(d->caminho, G_FILE_TEST_IS_DIR);
}

bool env_email(const char *const *arquivos, int n) {
	if (n <= 0) {
		return false;
	}
	GPtrArray *argv = g_ptr_array_new();
	g_ptr_array_add(argv, (gpointer) "xdg-email");
	for (int i = 0; i < n; i++) {
		g_ptr_array_add(argv, (gpointer) "--attach");
		g_ptr_array_add(argv, (gpointer)arquivos[i]);
	}
	g_ptr_array_add(argv, NULL);
	GError *err = NULL;
	bool ok = g_spawn_async(NULL, (char **)argv->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &err);
	g_clear_error(&err);
	g_ptr_array_free(argv, TRUE);
	return ok;
}

int env_celulares(env_celular *v, int max) {
	int n = 0;
	char *buf = g_malloc(64 * 1024);
	if (connectc_pedido("connect", buf, 64 * 1024)) {
		struct json *j = json_ler(buf);
		const struct json *ds = json_chave(j, "dispositivos");
		bool ativo = json_bool(json_chave(j, "ativo"), true);
		for (int i = 0; ativo && ds && i < ds->nfilhos && n < max; i++) {
			const struct json *d = json_item(ds, i);
			if (json_bool(json_chave(d, "conectado"), false) && json_bool(json_chave(json_chave(d, "recursos"), "files"), true)) {
				g_strlcpy(v[n].id, json_chave_texto(d, "id"), sizeof(v[n].id));
				g_strlcpy(v[n].nome, json_chave_texto(d, "nome"), sizeof(v[n].nome));
				n++;
			}
		}
		json_liberar(j);
	}
	g_free(buf);
	return n;
}

bool env_enviar_celular(const char *id, const char *const *arquivos, int n, char *erro, size_t tam_erro) {
	erro[0] = '\0';
	int enviados = 0;
	for (int i = 0; i < n; i++) {
		char *pedido = g_strdup_printf("enviar %s %s", id, arquivos[i]), r[1024] = "";
		if (!connectc_pedido(pedido, r, sizeof(r))) {
			g_strlcpy(erro, "O serviço do celular não respondeu", tam_erro);
		} else {
			struct json *j = json_ler(r);
			if (json_bool(json_chave(j, "ok"), false)) {
				enviados++;
			} else if (!erro[0]) {
				g_strlcpy(erro, *json_chave_texto(j, "erro") ? json_chave_texto(j, "erro") : "Não deu para enviar", tam_erro);
			}
			json_liberar(j);
		}
		g_free(pedido);
	}
	return enviados == n && !erro[0];
}
