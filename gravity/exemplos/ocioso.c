/*
 * lumna-ocioso: tranca a sessão sozinho. Depois de uns minutos sem mexer (ext-idle-notify; um
 * vídeo a tocar segura, por duas vias: o idle-inhibit do Wayland, que o compositor trata, e o D-Bus
 * org.freedesktop.ScreenSaver / PowerManagement, que o Firefox, o Chromium, o VLC e outros usam e que
 * este programa atende: enquanto houver quem pediu, não tranca) e antes de o computador dormir (logind:
 * PrepareForSleep, com um atraso para a tela de bloqueio aparecer antes). Lê o tempo do
 * ~/.config/lumna/bloqueio.ini, que o Painel grava, e vigia o arquivo:
 *
 *   bloquear_apos = 5          minutos sem mexer (0: nunca)
 *   bloquear_suspender = sim   trancar antes de dormir
 *
 * Trancar é abrir o lumna-bloqueio (que cobre todas as telas). Sobe com a sessão:
 *   lumna-ocioso &
 */
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <wayland-client.h>

#include "ext-idle-notify-v1-client-protocol.h"

static struct wl_display *display;
static struct wl_seat *seat;
static struct ext_idle_notifier_v1 *notificador;
static struct ext_idle_notification_v1 *aviso;
static int minutos = 5;
static bool ao_suspender = true;
static GPid bloqueio_pid;
static char *ini;

static GDBusConnection *sistema;
static int atraso_fd = -1; /* o "espere um pouco" do logind antes de dormir */

/* ---- trancar ---- */

static void bloqueio_saiu(GPid pid, gint estado, gpointer d) {
	g_spawn_close_pid(pid);
	bloqueio_pid = 0;
}

static void trancar(void) {
	if (bloqueio_pid) {
		return; /* já está trancado */
	}
	char *argv[] = { "lumna-bloqueio", NULL };
	GError *erro = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &bloqueio_pid, &erro)) {
		fprintf(stderr, "lumna-ocioso: não abriu o lumna-bloqueio: %s\n", erro->message);
		g_error_free(erro);
		bloqueio_pid = 0;
		return;
	}
	g_child_watch_add(bloqueio_pid, bloqueio_saiu, NULL);
}

static void trancar_agora(void) {
	trancar();
}

/* ---- quem pediu para não trancar (vídeo tocando, apresentação...) ---- */

static GDBusConnection *sessao;
static GHashTable *inibidores;   /* cookie (uint) -> "dono\napp: motivo" */
static guint proximo_cookie = 1;
static bool ficou_ocioso_inibido; /* o tempo passou enquanto havia quem pediu: tranca quando a última pessoa soltar */

static const char *const servico_xml =
	"<node>"
	" <interface name='org.freedesktop.ScreenSaver'>"
	"  <method name='Inhibit'><arg type='s' name='application_name' direction='in'/><arg type='s' name='reason_for_inhibit' direction='in'/><arg type='u' name='cookie' direction='out'/></method>"
	"  <method name='UnInhibit'><arg type='u' name='cookie' direction='in'/></method>"
	"  <method name='GetActive'><arg type='b' direction='out'/></method>"
	"  <method name='SetActive'><arg type='b' direction='in'/><arg type='b' direction='out'/></method>"
	"  <method name='Lock'/>"
	"  <method name='SimulateUserActivity'/>"
	"  <method name='GetActiveTime'><arg type='u' direction='out'/></method>"
	"  <method name='GetSessionIdleTime'><arg type='u' direction='out'/></method>"
	" </interface>"
	" <interface name='org.freedesktop.PowerManagement.Inhibit'>"
	"  <method name='Inhibit'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='u' direction='out'/></method>"
	"  <method name='UnInhibit'><arg type='u' direction='in'/></method>"
	"  <method name='HasInhibit'><arg type='b' direction='out'/></method>"
	" </interface>"
	"</node>";

static bool ha_inibidor(void) {
	return inibidores && g_hash_table_size(inibidores) > 0;
}

static void aviso_refazer(void);

/* o último soltou: se o tempo já tinha passado, a contagem recomeça do zero (não tranca de repente ao terminar o vídeo) */
static void inibidor_mudou(void) {
	if (!ha_inibidor() && ficou_ocioso_inibido) {
		ficou_ocioso_inibido = false;
		aviso_refazer();
	}
}

static void inibidor_soltar_cookie(guint cookie) {
	if (inibidores && g_hash_table_remove(inibidores, GUINT_TO_POINTER(cookie))) {
		inibidor_mudou();
	}
}

static void servico_metodo(GDBusConnection *c, const gchar *dono, const gchar *caminho, const gchar *iface, const gchar *metodo,
		GVariant *params, GDBusMethodInvocation *inv, gpointer d) {
	if (!strcmp(metodo, "Inhibit")) {
		const char *app, *motivo;
		g_variant_get(params, "(&s&s)", &app, &motivo);
		guint cookie = proximo_cookie++;
		g_hash_table_insert(inibidores, GUINT_TO_POINTER(cookie), g_strdup_printf("%s\n%s: %s", dono, app, motivo));
		fprintf(stderr, "lumna-ocioso: %s pediu para não trancar (%s)\n", app, motivo);
		g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", cookie));
	} else if (!strcmp(metodo, "UnInhibit")) {
		guint cookie;
		g_variant_get(params, "(u)", &cookie);
		inibidor_soltar_cookie(cookie);
		g_dbus_method_invocation_return_value(inv, NULL);
	} else if (!strcmp(metodo, "HasInhibit")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ha_inibidor()));
	} else if (!strcmp(metodo, "GetActive")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", bloqueio_pid != 0));
	} else if (!strcmp(metodo, "SetActive")) {
		gboolean ligar;
		g_variant_get(params, "(b)", &ligar);
		if (ligar) {
			trancar_agora();
		}
		g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", TRUE));
	} else if (!strcmp(metodo, "Lock")) {
		trancar_agora();
		g_dbus_method_invocation_return_value(inv, NULL);
	} else if (!strcmp(metodo, "GetActiveTime") || !strcmp(metodo, "GetSessionIdleTime")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", 0));
	} else { /* SimulateUserActivity */
		g_dbus_method_invocation_return_value(inv, NULL);
	}
}

static const GDBusInterfaceVTable servico_vtable = { .method_call = servico_metodo };

/* quem pediu e saiu sem soltar (o Firefox fechou com o vídeo tocando) deixa de contar */
static void nome_mudou(GDBusConnection *c, const gchar *de, const gchar *caminho, const gchar *iface, const gchar *sinal,
		GVariant *params, gpointer d) {
	const char *nome, *antigo, *novo;
	g_variant_get(params, "(&s&s&s)", &nome, &antigo, &novo);
	if (!*novo && *antigo && inibidores) {
		GHashTableIter it;
		gpointer k, v;
		bool mudou = false;
		g_hash_table_iter_init(&it, inibidores);
		while (g_hash_table_iter_next(&it, &k, &v)) {
			if (g_str_has_prefix((const char *)v, antigo) && ((const char *)v)[strlen(antigo)] == '\n') {
				g_hash_table_iter_remove(&it);
				mudou = true;
			}
		}
		if (mudou) {
			inibidor_mudou();
		}
	}
}

static void servico_nome_perdido(GDBusConnection *c, const gchar *nome, gpointer d) {
	fprintf(stderr, "lumna-ocioso: %s já tem outro dono (estamos dentro de outra sessão?); só o idle-inhibit do Wayland vale\n", nome);
}

static void servico_ligar(void) {
	sessao = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	if (!sessao) {
		return;
	}
	inibidores = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
	GDBusNodeInfo *no = g_dbus_node_info_new_for_xml(servico_xml, NULL);
	const char *caminhos[] = { "/org/freedesktop/ScreenSaver", "/ScreenSaver", "/org/freedesktop/PowerManagement/Inhibit",
		"/org/freedesktop/PowerManagement" };
	for (int i = 0; no && i < 4; i++) {
		GDBusInterfaceInfo *info = g_dbus_node_info_lookup_interface(no, i < 2 ? "org.freedesktop.ScreenSaver" : "org.freedesktop.PowerManagement.Inhibit");
		if (info && !g_dbus_connection_register_object(sessao, caminhos[i], info, &servico_vtable, NULL, NULL, NULL)) {
			fprintf(stderr, "lumna-ocioso: não registrou %s\n", caminhos[i]);
		}
	}
	g_dbus_connection_signal_subscribe(sessao, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus",
		NULL, G_DBUS_SIGNAL_FLAGS_NONE, nome_mudou, NULL, NULL);
	g_bus_own_name_on_connection(sessao, "org.freedesktop.ScreenSaver", G_BUS_NAME_OWNER_FLAGS_NONE, NULL, servico_nome_perdido, NULL, NULL);
	g_bus_own_name_on_connection(sessao, "org.freedesktop.PowerManagement", G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
}

/* ---- sem mexer ---- */

static void ficou_ocioso(void *d, struct ext_idle_notification_v1 *n) {
	if (ha_inibidor()) {
		/* alguém (um vídeo) pediu para não trancar: espera; ao soltar, a contagem recomeça */
		ficou_ocioso_inibido = true;
		return;
	}
	trancar();
}

static void voltou(void *d, struct ext_idle_notification_v1 *n) {}

static const struct ext_idle_notification_v1_listener aviso_ouvinte = { .idled = ficou_ocioso, .resumed = voltou };

static void aviso_refazer(void) {
	if (aviso) {
		ext_idle_notification_v1_destroy(aviso);
		aviso = NULL;
	}
	if (minutos > 0 && notificador && seat) {
		aviso = ext_idle_notifier_v1_get_idle_notification(notificador, (uint32_t)minutos * 60 * 1000, seat);
		ext_idle_notification_v1_add_listener(aviso, &aviso_ouvinte, NULL);
	}
	wl_display_flush(display);
}

static void ler_ajustes(void) {
	minutos = 5;
	ao_suspender = true;
	char *texto = NULL;
	if (g_file_get_contents(ini, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#') {
				continue;
			}
			*igual = '\0';
			char *chave = g_strstrip(*l), *valor = g_strstrip(igual + 1);
			if (!strcmp(chave, "bloquear_apos")) {
				minutos = atoi(valor);
			} else if (!strcmp(chave, "bloquear_suspender")) {
				ao_suspender = strcmp(valor, "nao") != 0;
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	if (minutos < 0) {
		minutos = 0;
	}
}

static void registo_global(void *d, struct wl_registry *r, uint32_t nome, const char *iface, uint32_t versao) {
	if (!strcmp(iface, wl_seat_interface.name) && !seat) {
		seat = wl_registry_bind(r, nome, &wl_seat_interface, 1);
	} else if (!strcmp(iface, ext_idle_notifier_v1_interface.name)) {
		notificador = wl_registry_bind(r, nome, &ext_idle_notifier_v1_interface, 1);
	}
}

static void registo_saiu(void *d, struct wl_registry *r, uint32_t nome) {}

static const struct wl_registry_listener registo_ouvinte = { .global = registo_global, .global_remove = registo_saiu };

static gboolean wayland_pronto(gint fd, GIOCondition cond, gpointer d) {
	if (cond & (G_IO_ERR | G_IO_HUP) || wl_display_dispatch(display) < 0) {
		g_main_loop_quit(d); /* o compositor fechou: a sessão acabou */
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

/* ---- o arquivo mudou (o Painel gravou) ---- */

static gboolean ini_mudou(gint fd, GIOCondition cond, gpointer d) {
	char buf[4096];
	if (read(fd, buf, sizeof(buf)) <= 0) {
		return G_SOURCE_CONTINUE;
	}
	int antes = minutos;
	ler_ajustes();
	if (minutos != antes) {
		aviso_refazer();
	}
	return G_SOURCE_CONTINUE;
}

/* ---- antes de dormir ---- */

static void atraso_pedir(void) {
	if (!sistema || atraso_fd >= 0) {
		return;
	}
	GUnixFDList *fds = NULL;
	GError *erro = NULL;
	GVariant *r = g_dbus_connection_call_with_unix_fd_list_sync(sistema, "org.freedesktop.login1", "/org/freedesktop/login1",
		"org.freedesktop.login1.Manager", "Inhibit", g_variant_new("(ssss)", "sleep", "LUMNA", "Trancar a tela antes de dormir", "delay"),
		G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &fds, NULL, &erro);
	if (!r) {
		g_error_free(erro);
		return;
	}
	gint32 i;
	g_variant_get(r, "(h)", &i);
	atraso_fd = g_unix_fd_list_get(fds, i, NULL);
	g_variant_unref(r);
	g_object_unref(fds);
}

static gboolean atraso_soltar(gpointer d) {
	if (atraso_fd >= 0) {
		close(atraso_fd);
		atraso_fd = -1;
	}
	return G_SOURCE_REMOVE;
}

static void vai_dormir(GDBusConnection *c, const gchar *de, const gchar *caminho, const gchar *iface, const gchar *sinal,
		GVariant *params, gpointer d) {
	gboolean antes;
	g_variant_get(params, "(b)", &antes);
	if (antes) {
		if (ao_suspender) {
			trancar();
		}
		/* um instante para a tela de bloqueio cobrir tudo, e deixa dormir */
		g_timeout_add(700, atraso_soltar, NULL);
	} else {
		atraso_pedir(); /* acordou: o próximo sono também espera */
	}
}

int main(void) {
	const char *config = getenv("XDG_CONFIG_HOME");
	ini = config && *config ? g_build_filename(config, "lumna", "bloqueio.ini", NULL)
				: g_build_filename(g_get_home_dir(), ".config", "lumna", "bloqueio.ini", NULL);
	ler_ajustes();

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "lumna-ocioso: sem compositor Wayland\n");
		return 1;
	}
	struct wl_registry *registo = wl_display_get_registry(display);
	wl_registry_add_listener(registo, &registo_ouvinte, NULL);
	wl_display_roundtrip(display);
	if (!notificador || !seat) {
		fprintf(stderr, "lumna-ocioso: o compositor não tem ext-idle-notify; não tranco sozinho por tempo\n");
	}
	aviso_refazer();

	GMainLoop *laco = g_main_loop_new(NULL, FALSE);
	g_unix_fd_add(wl_display_get_fd(display), G_IO_IN | G_IO_ERR | G_IO_HUP, wayland_pronto, laco);

	/* vigia a pasta (o Painel grava num temporário e troca) */
	int in = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	char *pasta = g_path_get_dirname(ini);
	g_mkdir_with_parents(pasta, 0700);
	if (in >= 0 && inotify_add_watch(in, pasta, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE) >= 0) {
		g_unix_fd_add(in, G_IO_IN, ini_mudou, NULL);
	}
	g_free(pasta);

	servico_ligar();
	sistema = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
	if (sistema) {
		g_dbus_connection_signal_subscribe(sistema, "org.freedesktop.login1", "org.freedesktop.login1.Manager", "PrepareForSleep",
			"/org/freedesktop/login1", NULL, G_DBUS_SIGNAL_FLAGS_NONE, vai_dormir, NULL, NULL);
		atraso_pedir();
	}

	g_main_loop_run(laco);
	return 0;
}
