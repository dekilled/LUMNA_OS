/* Um cliente de mentira, como o Firefox tocando vídeo: pede org.freedesktop.ScreenSaver.Inhibit e fica vivo segurando.
 * "inibir.c solta": ao receber SIGUSR1 chama UnInhibit; "inibir.c sai": ao receber SIGUSR1 sai sem soltar (como um Firefox que fechou). */
#include <gio/gio.h>
#include <glib-unix.h>
#include <stdio.h>
#include <string.h>

static GDBusConnection *con;
static guint cookie;
static gboolean solta;
static GMainLoop *laco;

static gboolean usr1(gpointer d) {
	if (solta) {
		g_dbus_connection_call_sync(con, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", "UnInhibit",
			g_variant_new("(u)", cookie), NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
		puts("soltou");
		fflush(stdout);
		return G_SOURCE_CONTINUE;
	}
	g_main_loop_quit(laco);
	return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
	solta = argc > 1 && !strcmp(argv[1], "solta");
	con = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	GVariant *r = g_dbus_connection_call_sync(con, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver",
		"Inhibit", g_variant_new("(ss)", "teste-video", "tocando"), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
	if (!r) {
		puts("erro");
		return 1;
	}
	g_variant_get(r, "(u)", &cookie);
	printf("pediu %u\n", cookie);
	fflush(stdout);
	laco = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGUSR1, usr1, NULL);
	g_main_loop_run(laco);
	return 0;
}
