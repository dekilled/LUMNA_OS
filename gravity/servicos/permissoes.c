#include "permissoes.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOME "org.freedesktop.impl.portal.PermissionStore"
#define CAMINHO "/org/freedesktop/impl/portal/PermissionStore"

struct tipo {
	const char *tabela, *id, *nome, *icone;
	bool muda;
};

static const struct tipo tipos[] = {
	{ "devices", "camera", "Câmera", "camera", true },
	{ "devices", "microphone", "Microfone", "mic", true },
	{ "devices", "speakers", "Alto-falantes", "speaker", true },
	{ "location", "location", "Localização", "map", false },
	{ "screenshot", "screenshot", "Capturar a tela", "image", true },
	{ "notifications", "notification", "Avisos", "bell", true },
	{ "background", "background", "Rodar em segundo plano", "stack", true },
};

static GDBusConnection *sessao(void) {
	static GDBusConnection *c;
	if (!c) {
		c = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	}
	return c;
}

static GVariant *chamar(const char *metodo, GVariant *params, const GVariantType *tipo) {
	GDBusConnection *c = sessao();
	if (!c) {
		if (params) {
			g_variant_unref(g_variant_ref_sink(params));
		}
		return NULL;
	}
	GError *erro = NULL;
	GVariant *r = g_dbus_connection_call_sync(c, NOME, CAMINHO, NOME, metodo, params, tipo, G_DBUS_CALL_FLAGS_NONE, 1500, NULL, &erro);
	g_clear_error(&erro);
	return r;
}

static int por_app_e_nome(const void *a, const void *b) {
	const struct perm_entrada *x = a, *y = b;
	int c = strcmp(x->app, y->app);
	return c ? c : strcmp(x->id, y->id);
}

int perm_listar(struct perm_entrada *lista, int max) {
	int n = 0;
	bool respondeu = false;
	for (size_t t = 0; t < sizeof(tipos) / sizeof(tipos[0]) && n < max; t++) {
		GVariant *r = chamar("Lookup", g_variant_new("(ss)", tipos[t].tabela, tipos[t].id), G_VARIANT_TYPE("(a{sas}v)"));
		if (!r) {
			continue; /* essa tabela ainda não existe (ninguém pediu nada) ou o serviço não está lá */
		}
		respondeu = true;
		GVariant *perms = g_variant_get_child_value(r, 0);
		GVariantIter *it;
		g_variant_get(perms, "a{sas}", &it);
		char *app;
		GVariant *lista_v;
		while (n < max && g_variant_iter_next(it, "{&s@as}", &app, &lista_v)) {
			struct perm_entrada *e = &lista[n++];
			memset(e, 0, sizeof(*e));
			g_strlcpy(e->tabela, tipos[t].tabela, sizeof(e->tabela));
			g_strlcpy(e->id, tipos[t].id, sizeof(e->id));
			g_strlcpy(e->app, app, sizeof(e->app));
			const char **valores = g_variant_get_strv(lista_v, NULL);
			g_strlcpy(e->valor, valores && valores[0] ? valores[0] : "", sizeof(e->valor));
			g_free(valores);
			e->pode = !g_ascii_strcasecmp(e->valor, "yes") || !g_ascii_strcasecmp(e->valor, "exact") || !g_ascii_strcasecmp(e->valor, "city") ||
				!g_ascii_strcasecmp(e->valor, "true");
			e->muda = tipos[t].muda;
			e->nome = tipos[t].nome;
			e->icone = tipos[t].icone;
			g_variant_unref(lista_v);
		}
		g_variant_iter_free(it);
		g_variant_unref(perms);
		g_variant_unref(r);
	}
	if (!respondeu) {
		/* o serviço pode estar parado e ainda não ter uma tabela: pergunta se ele existe */
		GVariant *l = chamar("List", g_variant_new("(s)", "devices"), G_VARIANT_TYPE("(as)"));
		if (!l) {
			return -1;
		}
		g_variant_unref(l);
	}
	qsort(lista, (size_t)n, sizeof(lista[0]), por_app_e_nome);
	return n;
}

bool perm_definir(const struct perm_entrada *e, bool permitir) {
	if (!e->muda) {
		return false;
	}
	const char *v[] = { permitir ? "yes" : "no", NULL };
	GVariant *r = chamar("SetPermission", g_variant_new("(sbss^as)", e->tabela, FALSE, e->id, e->app, v), NULL);
	if (r) {
		g_variant_unref(r);
	}
	return r != NULL;
}

bool perm_tirar(const struct perm_entrada *e) {
	GVariant *r = chamar("DeletePermission", g_variant_new("(sss)", e->tabela, e->id, e->app), NULL);
	if (r) {
		g_variant_unref(r);
	}
	return r != NULL;
}
