/* Um player MPRIS de mentira (org.mpris.MediaPlayer2.falso): Play, Pause e PlayPause trocam o estado; imprime cada pedido que recebe.
 * Com o argumento "so-playpause" o Play e o Pause são recusados (como um player que só aceita PlayPause). */
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

static const char *xml =
	"<node><interface name='org.mpris.MediaPlayer2.Player'>"
	"<method name='Play'/><method name='Pause'/><method name='PlayPause'/><method name='Next'/><method name='Previous'/>"
	"<method name='Seek'><arg type='x' direction='in'/></method>"
	"<property name='PlaybackStatus' type='s' access='read'/><property name='Metadata' type='a{sv}' access='read'/>"
	"<property name='Position' type='x' access='read'/><property name='CanGoNext' type='b' access='read'/>"
	"<property name='CanGoPrevious' type='b' access='read'/></interface></node>";

static gboolean tocando = TRUE, so_playpause;
static GDBusConnection *con;

static void mudou(void) {
	GVariantBuilder b;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&b, "{sv}", "PlaybackStatus", g_variant_new_string(tocando ? "Playing" : "Paused"));
	g_dbus_connection_emit_signal(con, NULL, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "PropertiesChanged",
		g_variant_new("(sa{sv}as)", "org.mpris.MediaPlayer2.Player", &b, NULL), NULL);
}

static void metodo(GDBusConnection *c, const gchar *s, const gchar *o, const gchar *i, const gchar *m, GVariant *p, GDBusMethodInvocation *inv,
		gpointer d) {
	printf("pedido %s\n", m);
	fflush(stdout);
	if (so_playpause && (!strcmp(m, "Play") || !strcmp(m, "Pause"))) {
		g_dbus_method_invocation_return_dbus_error(inv, "org.mpris.MediaPlayer2.Error", "só PlayPause");
		return;
	}
	if (!strcmp(m, "Play")) {
		tocando = TRUE;
	} else if (!strcmp(m, "Pause")) {
		tocando = FALSE;
	} else if (!strcmp(m, "PlayPause")) {
		tocando = !tocando;
	}
	g_dbus_method_invocation_return_value(inv, NULL);
	mudou();
}

static GVariant *propriedade(GDBusConnection *c, const gchar *s, const gchar *o, const gchar *i, const gchar *n, GError **e, gpointer d) {
	if (!strcmp(n, "PlaybackStatus")) {
		return g_variant_new_string(tocando ? "Playing" : "Paused");
	} else if (!strcmp(n, "Metadata")) {
		GVariantBuilder b;
		g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
		g_variant_builder_add(&b, "{sv}", "xesam:title", g_variant_new_string("Video de teste"));
		g_variant_builder_add(&b, "{sv}", "mpris:length", g_variant_new_int64(600 * 1000000LL));
		return g_variant_builder_end(&b);
	} else if (!strcmp(n, "Position")) {
		return g_variant_new_int64(5 * 1000000LL);
	}
	return g_variant_new_boolean(TRUE);
}

static const GDBusInterfaceVTable vtable = { .method_call = metodo, .get_property = propriedade };

static void nome_ok(GDBusConnection *c, const gchar *nome, gpointer d) {
	puts("pronto");
	fflush(stdout);
}

int main(int argc, char **argv) {
	so_playpause = argc > 1 && !strcmp(argv[1], "so-playpause");
	con = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	GDBusNodeInfo *no = g_dbus_node_info_new_for_xml(xml, NULL);
	g_dbus_connection_register_object(con, "/org/mpris/MediaPlayer2", no->interfaces[0], &vtable, NULL, NULL, NULL);
	g_bus_own_name_on_connection(con, "org.mpris.MediaPlayer2.falso", G_BUS_NAME_OWNER_FLAGS_NONE, nome_ok, NULL, NULL, NULL);
	g_main_loop_run(g_main_loop_new(NULL, FALSE));
	return 0;
}
