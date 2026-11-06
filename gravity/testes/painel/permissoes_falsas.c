/* Um PermissionStore de mentira (org.freedesktop.impl.portal.PermissionStore): guarda tabela/id/app -> permissões em memória.
 * Começa com: devices/camera: org.exemplo.Video=yes, org.exemplo.Chat=no; devices/microphone: org.exemplo.Chat=yes; location/location: org.exemplo.Mapa=EXACT */
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

static const char *xml =
	"<node><interface name='org.freedesktop.impl.portal.PermissionStore'>"
	"<method name='List'><arg type='s' direction='in'/><arg type='as' direction='out'/></method>"
	"<method name='Lookup'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='a{sas}' direction='out'/><arg type='v' direction='out'/></method>"
	"<method name='SetPermission'><arg type='s' direction='in'/><arg type='b' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='as' direction='in'/></method>"
	"<method name='DeletePermission'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/></method>"
	"</interface></node>";

static GHashTable *dados; /* "tabela/id" -> GHashTable app -> valor */

static GHashTable *tabela_de(const char *chave, gboolean criar) {
	GHashTable *t = g_hash_table_lookup(dados, chave);
	if (!t && criar) {
		t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		g_hash_table_insert(dados, g_strdup(chave), t);
	}
	return t;
}

static void guardar(const char *tabela, const char *id, const char *app, const char *valor) {
	char *chave = g_strdup_printf("%s/%s", tabela, id);
	g_hash_table_insert(tabela_de(chave, TRUE), g_strdup(app), g_strdup(valor));
	g_free(chave);
}

static void metodo(GDBusConnection *c, const gchar *s, const gchar *o, const gchar *i, const gchar *m, GVariant *p, GDBusMethodInvocation *inv, gpointer d) {
	if (!strcmp(m, "List")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(^as)", (const char *[]){ NULL }));
	} else if (!strcmp(m, "Lookup")) {
		const char *tabela, *id;
		g_variant_get(p, "(&s&s)", &tabela, &id);
		char *chave = g_strdup_printf("%s/%s", tabela, id);
		GHashTable *t = tabela_de(chave, FALSE);
		g_free(chave);
		if (!t) {
			g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.portal.Error.NotFound", "sem esta entrada");
			return;
		}
		GVariantBuilder b;
		g_variant_builder_init(&b, G_VARIANT_TYPE("a{sas}"));
		GHashTableIter it;
		gpointer k, v;
		g_hash_table_iter_init(&it, t);
		while (g_hash_table_iter_next(&it, &k, &v)) {
			const char *vs[] = { v, NULL };
			g_variant_builder_add(&b, "{s^as}", k, vs);
		}
		g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sas}v)", &b, g_variant_new_string("")));
	} else if (!strcmp(m, "SetPermission")) {
		const char *tabela, *id, *app;
		gboolean criar;
		char **valores;
		g_variant_get(p, "(&sb&s&s^as)", &tabela, &criar, &id, &app, &valores);
		guardar(tabela, id, app, valores && valores[0] ? valores[0] : "");
		g_strfreev(valores);
		g_dbus_method_invocation_return_value(inv, NULL);
	} else if (!strcmp(m, "DeletePermission")) {
		const char *tabela, *id, *app;
		g_variant_get(p, "(&s&s&s)", &tabela, &id, &app);
		char *chave = g_strdup_printf("%s/%s", tabela, id);
		GHashTable *t = tabela_de(chave, FALSE);
		g_free(chave);
		if (t) {
			g_hash_table_remove(t, app);
		}
		g_dbus_method_invocation_return_value(inv, NULL);
	}
}

static const GDBusInterfaceVTable vtable = { .method_call = metodo };

static void nome_ok(GDBusConnection *c, const gchar *nome, gpointer d) {
	puts("pronto");
	fflush(stdout);
}

int main(void) {
	dados = g_hash_table_new(g_str_hash, g_str_equal);
	guardar("devices", "camera", "org.exemplo.Video", "yes");
	guardar("devices", "camera", "org.exemplo.Chat", "no");
	guardar("devices", "microphone", "org.exemplo.Chat", "yes");
	guardar("location", "location", "org.exemplo.Mapa", "EXACT");
	GDBusConnection *con = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	GDBusNodeInfo *no = g_dbus_node_info_new_for_xml(xml, NULL);
	g_dbus_connection_register_object(con, "/org/freedesktop/impl/portal/PermissionStore", no->interfaces[0], &vtable, NULL, NULL, NULL);
	g_bus_own_name_on_connection(con, "org.freedesktop.impl.portal.PermissionStore", G_BUS_NAME_OWNER_FLAGS_NONE, nome_ok, NULL, NULL, NULL);
	g_main_loop_run(g_main_loop_new(NULL, FALSE));
	return 0;
}
