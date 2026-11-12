#define _GNU_SOURCE
#include "seletor.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "gravity.h"

struct req {
	bool salvar, multiplo;
	char *titulo, *nome, *pasta, *filtro_nome;
	char **tipos;
	seletor_cb cb;
	void *dados;
	/* o resultado, preenchido na linha à parte */
	char **caminhos;
	int n;
	char erro[200];
};

static void entregar(void *d) {
	struct req *r = d;
	r->cb(r->caminhos, r->n, r->erro[0] ? r->erro : NULL, r->dados);
	g_free(r->titulo);
	g_free(r->nome);
	g_free(r->pasta);
	g_free(r->filtro_nome);
	g_strfreev(r->tipos);
	g_free(r);
}

/* ---- o gancho dos testes ---- */

static void por_comando(struct req *r, const char *cmd) {
	char *argv[] = { "sh", "-c", (char *)cmd, NULL };
	char *saida = NULL;
	int estado = 0;
	GError *ge = NULL;
	char **env = g_get_environ();
	env = g_environ_setenv(env, "LUMNA_SELETOR_SALVAR", r->salvar ? "1" : "0", TRUE);
	env = g_environ_setenv(env, "LUMNA_SELETOR_NOME", r->nome ? r->nome : "", TRUE);
	if (!g_spawn_sync(NULL, argv, env, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, &estado, &ge)) {
		g_strlcpy(r->erro, ge ? ge->message : "não consegui rodar o seletor", sizeof(r->erro));
		g_clear_error(&ge);
		g_strfreev(env);
		return;
	}
	g_strfreev(env);
	GPtrArray *v = g_ptr_array_new();
	char **linhas = g_strsplit(saida ? saida : "", "\n", -1);
	for (char **l = linhas; *l; l++) {
		if (**l) {
			g_ptr_array_add(v, g_strdup(*l));
		}
	}
	g_strfreev(linhas);
	g_free(saida);
	r->n = (int)v->len;
	g_ptr_array_add(v, NULL);
	r->caminhos = (char **)g_ptr_array_free(v, FALSE);
}

/* ---- o portal ---- */

struct espera {
	GMainLoop *laco;
	struct req *r;
	bool respondeu;
};

static void resposta(GDBusConnection *c, const char *remetente, const char *caminho, const char *iface, const char *sinal, GVariant *par, gpointer d) {
	struct espera *e = d;
	guint32 codigo = 2;
	GVariant *res = NULL;
	g_variant_get(par, "(u@a{sv})", &codigo, &res);
	if (codigo == 0 && res) {
		GVariant *uris = g_variant_lookup_value(res, "uris", G_VARIANT_TYPE("as"));
		if (uris) {
			gsize n = 0;
			const char **v = g_variant_get_strv(uris, &n);
			GPtrArray *a = g_ptr_array_new();
			for (gsize i = 0; i < n; i++) {
				char *p = g_filename_from_uri(v[i], NULL, NULL);
				if (p) {
					g_ptr_array_add(a, p);
				}
			}
			e->r->n = (int)a->len;
			g_ptr_array_add(a, NULL);
			e->r->caminhos = (char **)g_ptr_array_free(a, FALSE);
			g_free(v);
			g_variant_unref(uris);
		}
	} else if (codigo == 2) {
		g_strlcpy(e->r->erro, "o seletor de arquivos terminou com erro", sizeof(e->r->erro));
	} /* 1: a pessoa cancelou (n = 0) */
	if (res) {
		g_variant_unref(res);
	}
	e->respondeu = true;
	g_main_loop_quit(e->laco);
}

static void por_portal(struct req *r) {
	GError *ge = NULL;
	GDBusConnection *c = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &ge);
	if (!c) {
		g_snprintf(r->erro, sizeof(r->erro), "sem o barramento de sessão: %s", ge ? ge->message : "?");
		g_clear_error(&ge);
		return;
	}
	char *token = g_strdup_printf("signals%u", g_random_int());
	char *remetente = g_strdelimit(g_strdup(g_dbus_connection_get_unique_name(c) + 1), ".", '_');
	char *caminho_req = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s", remetente, token);
	GMainContext *ctx = g_main_context_new();
	g_main_context_push_thread_default(ctx);
	struct espera e = { .laco = g_main_loop_new(ctx, FALSE), .r = r };
	guint sub = g_dbus_connection_signal_subscribe(c, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request", "Response", caminho_req, NULL, G_DBUS_SIGNAL_FLAGS_NONE, resposta, &e, NULL);
	GVariantBuilder opc;
	g_variant_builder_init(&opc, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&opc, "{sv}", "handle_token", g_variant_new_string(token));
	g_variant_builder_add(&opc, "{sv}", "modal", g_variant_new_boolean(TRUE));
	if (!r->salvar) {
		g_variant_builder_add(&opc, "{sv}", "multiple", g_variant_new_boolean(r->multiplo));
	}
	/* os filtros: o de texto e "Todos os arquivos" (a lista de tipos do pedido, a1(sa(us))) */
	GVariantBuilder filtros, tipos_t, tipos_a;
	g_variant_builder_init(&filtros, G_VARIANT_TYPE("a(sa(us))"));
	if (r->tipos && r->tipos[0]) {
		g_variant_builder_init(&tipos_t, G_VARIANT_TYPE("a(us)"));
		for (char **t = r->tipos; *t; t++) {
			g_variant_builder_add(&tipos_t, "(us)", 1u, *t);
		}
		g_variant_builder_add(&filtros, "(s@a(us))", r->filtro_nome ? r->filtro_nome : "Texto", g_variant_builder_end(&tipos_t));
	}
	g_variant_builder_init(&tipos_a, G_VARIANT_TYPE("a(us)"));
	g_variant_builder_add(&tipos_a, "(us)", 0u, "*");
	g_variant_builder_add(&filtros, "(s@a(us))", "Todos os arquivos", g_variant_builder_end(&tipos_a));
	g_variant_builder_add(&opc, "{sv}", "filters", g_variant_builder_end(&filtros));
	if (r->salvar && r->nome && *r->nome) {
		g_variant_builder_add(&opc, "{sv}", "current_name", g_variant_new_string(r->nome));
	}
	if (r->pasta && *r->pasta) {
		g_variant_builder_add(&opc, "{sv}", "current_folder", g_variant_new_bytestring(r->pasta));
	}
	GVariant *ret = g_dbus_connection_call_sync(c, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.FileChooser", r->salvar ? "SaveFile" : "OpenFile",
		g_variant_new("(ss@a{sv})", "", r->titulo ? r->titulo : (r->salvar ? "Salvar" : "Abrir"), g_variant_builder_end(&opc)), NULL, G_DBUS_CALL_FLAGS_NONE, 15000, NULL, &ge);
	if (!ret) {
		g_snprintf(r->erro, sizeof(r->erro), "o seletor de arquivos do sistema não respondeu: %s", ge ? ge->message : "?");
		g_clear_error(&ge);
	} else {
		g_variant_unref(ret);
		g_main_loop_run(e.laco); /* até a pessoa escolher ou cancelar (a resposta chega como sinal) */
	}
	g_dbus_connection_signal_unsubscribe(c, sub);
	g_main_loop_unref(e.laco);
	g_main_context_pop_thread_default(ctx);
	g_main_context_unref(ctx);
	g_free(token);
	g_free(remetente);
	g_free(caminho_req);
	g_object_unref(c);
}

static gpointer trabalho(gpointer d) {
	struct req *r = d;
	const char *cmd = g_getenv("LUMNA_SELETOR");
	if (g_getenv("LUMNA_SELETOR_FALHA")) {
		g_strlcpy(r->erro, "o seletor de arquivos do sistema não respondeu: (simulado)", sizeof(r->erro));
	} else if (cmd && *cmd) {
		por_comando(r, cmd);
	} else {
		por_portal(r);
	}
	gv_na_linha_principal(entregar, r);
	return NULL;
}

void seletor_pedir(const struct seletor_pedido *p, seletor_cb cb, void *dados) {
	struct req *r = g_new0(struct req, 1);
	r->salvar = p->salvar;
	r->multiplo = p->multiplo;
	r->titulo = g_strdup(p->titulo);
	r->nome = g_strdup(p->nome_sugerido);
	r->pasta = g_strdup(p->pasta);
	r->filtro_nome = g_strdup(p->filtro_nome);
	r->tipos = g_strdupv((char **)p->tipos);
	r->cb = cb;
	r->dados = dados;
	g_thread_unref(g_thread_new("signals-seletor", trabalho, r));
}
