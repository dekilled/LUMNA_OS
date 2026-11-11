/*
 * lumna-portal: o que o sistema pede ao gerenciador de arquivos, respondido pelo Atlas.
 *
 *  - org.freedesktop.FileManager1 (ShowFolders, ShowItems, ShowItemProperties): é o "Mostrar na pasta" dos downloads do Firefox e do Chromium,
 *    do Steam e de qualquer app. Cada pedido abre uma janela do Atlas na pasta (e, num arquivo, com ele selecionado).
 *
 * O serviço sobe sozinho pelo D-Bus (dbus/org.freedesktop.FileManager1.service) e sai depois de um minuto parado. LUMNA_ATLAS_EXE troca o
 * programa que abre a janela (os testes põem um de mentira).
 */
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#define OCIOSO_SEG 60

static GMainLoop *laco;
static guint ocioso_id;

static gboolean ocioso_acabou(gpointer d) {
	g_main_loop_quit(laco);
	return G_SOURCE_REMOVE;
}

static void ocioso_reiniciar(void) {
	if (ocioso_id) {
		g_source_remove(ocioso_id);
	}
	ocioso_id = g_timeout_add_seconds(OCIOSO_SEG, ocioso_acabou, NULL);
}

static const char *atlas_exe(void) {
	const char *e = g_getenv("LUMNA_ATLAS_EXE");
	return e && *e ? e : "lumna-atlas";
}

/* abre uma janela do Atlas em um caminho (pasta, ou arquivo: o Atlas abre a pasta dele com o arquivo selecionado) */
static void atlas_abrir(const char *caminho) {
	char *argv[] = { (char *)atlas_exe(), (char *)caminho, NULL };
	GError *err = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &err)) {
		g_printerr("lumna-portal: não abri o Atlas: %s\n", err->message);
		g_clear_error(&err);
	}
}

static const char XML[] =
	"<node><interface name='org.freedesktop.FileManager1'>"
	"<method name='ShowFolders'><arg type='as' name='URIs' direction='in'/><arg type='s' name='StartupId' direction='in'/></method>"
	"<method name='ShowItems'><arg type='as' name='URIs' direction='in'/><arg type='s' name='StartupId' direction='in'/></method>"
	"<method name='ShowItemProperties'><arg type='as' name='URIs' direction='in'/><arg type='s' name='StartupId' direction='in'/></method>"
	"</interface></node>";

static void chamada(GDBusConnection *c, const char *remetente, const char *caminho_obj, const char *interface, const char *metodo, GVariant *params,
		GDBusMethodInvocation *inv, gpointer dados) {
	ocioso_reiniciar();
	GVariantIter *it;
	const char *startup;
	g_variant_get(params, "(as&s)", &it, &startup);
	char *uri;
	int n = 0;
	while (g_variant_iter_next(it, "s", &uri)) {
		char *caminho = g_filename_from_uri(uri, NULL, NULL);
		if (caminho && g_path_is_absolute(caminho) && n < 16) { /* quantas janelas de uma vez: não abre dezenas se pedirem dezenas */
			atlas_abrir(caminho);
			n++;
		}
		g_free(caminho);
		g_free(uri);
	}
	g_variant_iter_free(it);
	g_dbus_method_invocation_return_value(inv, NULL);
}

static const GDBusInterfaceVTable VTABLE = { .method_call = chamada };

static void ao_ter_barramento(GDBusConnection *c, const char *nome, gpointer dados) {
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(XML, NULL);
	GError *err = NULL;
	if (!g_dbus_connection_register_object(c, "/org/freedesktop/FileManager1", info->interfaces[0], &VTABLE, NULL, NULL, &err)) {
		g_printerr("lumna-portal: %s\n", err->message);
		g_main_loop_quit(laco);
	}
	g_dbus_node_info_unref(info);
}

static gboolean nome_adquirido;

static void nome_ganho(GDBusConnection *c, const char *nome, gpointer dados) {
	nome_adquirido = TRUE;
}

static void nome_perdido(GDBusConnection *c, const char *nome, gpointer dados) {
	if (!nome_adquirido) { /* nunca chegou a ter o nome: outro gerenciador de arquivos (o Thunar) já o tem. Perder depois, com a sessão acabando, não é problema */
		g_printerr("lumna-portal: o nome %s já tem dono (outro gerenciador de arquivos): saindo\n", nome);
	}
	g_main_loop_quit(laco);
}

int main(void) {
	laco = g_main_loop_new(NULL, FALSE);
	guint dono = g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.FileManager1", G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT, ao_ter_barramento, nome_ganho, nome_perdido, NULL, NULL);
	ocioso_reiniciar();
	g_main_loop_run(laco);
	g_bus_unown_name(dono);
	return 0;
}
