/*
 * lumna-ocioso: tranca a sessão sozinho. Depois de uns minutos sem mexer (ext-idle-notify; um
 * vídeo a tocar segura, pelo idle-inhibit do compositor) e antes de o computador dormir (logind:
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

/* ---- sem mexer ---- */

static void ficou_ocioso(void *d, struct ext_idle_notification_v1 *n) {
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

	sistema = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
	if (sistema) {
		g_dbus_connection_signal_subscribe(sistema, "org.freedesktop.login1", "org.freedesktop.login1.Manager", "PrepareForSleep",
			"/org/freedesktop/login1", NULL, G_DBUS_SIGNAL_FLAGS_NONE, vai_dormir, NULL, NULL);
		atraso_pedir();
	}

	g_main_loop_run(laco);
	return 0;
}
