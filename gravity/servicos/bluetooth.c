/* O Bluetooth pelo BlueZ (bluetooth.h). */
#include "bluetooth.h"

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define BLUEZ "org.bluez"
#define PROPS "org.freedesktop.DBus.Properties"
#define AGENTE "/org/lumna/agente"

static GDBusConnection *bus;
static GMutex trinco;
static struct bt_estado estado;
static int fd_aviso = -1;
static GMainContext *contexto;

static void avisar(void) {
	uint64_t um = 1;
	if (write(fd_aviso, &um, sizeof(um)) < 0) {
	}
}

static void copiar(char *dest, size_t tam, const char *src) {
	g_strlcpy(dest, src ? src : "", tam); /* corta no que cabe */
}

static GVariant *chamar(const char *caminho, const char *iface, const char *metodo, GVariant *args, const GVariantType *tipo,
		GError **erro) {
	return g_dbus_connection_call_sync(bus, BLUEZ, caminho, iface, metodo, args, tipo, G_DBUS_CALL_FLAGS_NONE, 30000, NULL, erro);
}

static const char *prop_s(GVariant *d, const char *k) {
	const char *s = NULL;
	if (d && (g_variant_lookup(d, k, "&s", &s) || g_variant_lookup(d, k, "&o", &s))) {
		return s; /* o Adapter de um aparelho é um caminho (o), não um texto */
	}
	return "";
}

static bool prop_b(GVariant *d, const char *k) {
	gboolean b = FALSE;
	return d && g_variant_lookup(d, k, "b", &b) && b;
}

static void barramento(const char *hci, char *dest, size_t tam) {
	char caminho[200], alvo[200];
	snprintf(caminho, sizeof(caminho), "/sys/class/bluetooth/%s/device/subsystem", hci);
	ssize_t n = readlink(caminho, alvo, sizeof(alvo) - 1);
	dest[0] = '\0';
	if (n > 0) {
		alvo[n] = '\0';
		const char *b = strrchr(alvo, '/');
		b = b ? b + 1 : alvo;
		if (!strcmp(b, "pci")) {
			copiar(dest, tam, "PCI");
		} else if (!strcmp(b, "usb")) {
			copiar(dest, tam, "USB");
		}
	}
}

static void atualizar(void) {
	struct bt_estado *novo = g_new0(struct bt_estado, 1);
	GVariant *r = chamar("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"), NULL);
	if (r) {
		novo->bluez = true;
		GVariant *objs = g_variant_get_child_value(r, 0);
		GVariantIter it;
		g_variant_iter_init(&it, objs);
		const char *caminho;
		GVariant *ifaces;
		while (g_variant_iter_next(&it, "{&o@a{sa{sv}}}", &caminho, &ifaces)) {
			GVariant *a = g_variant_lookup_value(ifaces, "org.bluez.Adapter1", G_VARIANT_TYPE("a{sv}"));
			GVariant *d = g_variant_lookup_value(ifaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
			if (a && novo->nadapt < BT_MAX_ADAPT) {
				struct bt_adapt *x = &novo->adapt[novo->nadapt++];
				copiar(x->caminho, sizeof(x->caminho), caminho);
				copiar(x->nome, sizeof(x->nome), prop_s(a, "Alias")[0] ? prop_s(a, "Alias") : prop_s(a, "Name"));
				copiar(x->endereco, sizeof(x->endereco), prop_s(a, "Address"));
				x->ligado = prop_b(a, "Powered");
				x->procurando = prop_b(a, "Discovering");
				x->visivel = prop_b(a, "Discoverable");
				const char *hci = strrchr(caminho, '/');
				barramento(hci ? hci + 1 : caminho, x->barramento, sizeof(x->barramento));
			}
			if (d && novo->ndisp < BT_MAX_DISP) {
				struct bt_disp *x = &novo->disp[novo->ndisp++];
				copiar(x->caminho, sizeof(x->caminho), caminho);
				copiar(x->adapt, sizeof(x->adapt), prop_s(d, "Adapter"));
				copiar(x->nome, sizeof(x->nome), prop_s(d, "Alias")[0] ? prop_s(d, "Alias") : prop_s(d, "Name"));
				copiar(x->endereco, sizeof(x->endereco), prop_s(d, "Address"));
				copiar(x->icone, sizeof(x->icone), prop_s(d, "Icon"));
				x->pareado = prop_b(d, "Paired");
				x->conectado = prop_b(d, "Connected");
				x->confiavel = prop_b(d, "Trusted");
				int16_t rssi = 0;
				g_variant_lookup(d, "RSSI", "n", &rssi);
				x->sinal = rssi;
				x->bateria = -1;
				GVariant *bat = g_variant_lookup_value(ifaces, "org.bluez.Battery1", G_VARIANT_TYPE("a{sv}"));
				if (bat) {
					uint8_t p = 0;
					if (g_variant_lookup(bat, "Percentage", "y", &p)) {
						x->bateria = p;
					}
					g_variant_unref(bat);
				}
			}
			if (a) {
				g_variant_unref(a);
			}
			if (d) {
				g_variant_unref(d);
			}
			g_variant_unref(ifaces);
		}
		g_variant_unref(objs);
		g_variant_unref(r);
	}
	/* os pareados e conectados em cima, depois os de sinal mais forte */
	for (int i = 1; i < novo->ndisp; i++) {
		struct bt_disp x = novo->disp[i];
		int j = i;
		while (j > 0) {
			struct bt_disp *p = &novo->disp[j - 1];
			int pa = p->conectado * 4 + p->pareado * 2, xa = x.conectado * 4 + x.pareado * 2;
			if (xa > pa || (xa == pa && x.sinal > p->sinal)) {
				novo->disp[j] = *p;
				j--;
			} else {
				break;
			}
		}
		novo->disp[j] = x;
	}
	g_mutex_lock(&trinco);
	copiar(novo->erro, sizeof(novo->erro), estado.erro);
	novo->geracao = estado.geracao;
	bool mudou = memcmp(novo, &estado, sizeof(estado)) != 0;
	if (mudou) {
		unsigned g = estado.geracao + 1;
		memcpy(&estado, novo, sizeof(estado));
		estado.geracao = g;
	}
	g_mutex_unlock(&trinco);
	g_free(novo);
	if (mudou) {
		avisar();
	}
}

static gboolean tic(gpointer d) {
	if (!bus) {
		return G_SOURCE_CONTINUE; /* sem D-Bus do sistema (um teste, um contêiner) */
	}
	atualizar();
	return G_SOURCE_CONTINUE;
}

/* ---- o agente: parear sem digitar ---- */

static const char *agente_xml =
	"<node><interface name='org.bluez.Agent1'>"
	"<method name='Release'/>"
	"<method name='RequestPinCode'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>"
	"<method name='DisplayPinCode'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
	"<method name='RequestPasskey'><arg type='o' direction='in'/><arg type='u' direction='out'/></method>"
	"<method name='DisplayPasskey'><arg type='o' direction='in'/><arg type='u' direction='in'/><arg type='q' direction='in'/></method>"
	"<method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>"
	"<method name='RequestAuthorization'><arg type='o' direction='in'/></method>"
	"<method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
	"<method name='Cancel'/></interface></node>";

static void agente_chamada(GDBusConnection *c, const gchar *remetente, const gchar *caminho, const gchar *iface, const gchar *metodo,
		GVariant *args, GDBusMethodInvocation *inv, gpointer d) {
	if (!strcmp(metodo, "RequestPinCode")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", "0000"));
	} else if (!strcmp(metodo, "RequestPasskey")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", 0));
	} else {
		g_dbus_method_invocation_return_value(inv, NULL);
	}
}

static const GDBusInterfaceVTable agente_vtable = { .method_call = agente_chamada };

static void registar_agente(void) {
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(agente_xml, NULL);
	if (!info) {
		return;
	}
	g_dbus_connection_register_object(bus, AGENTE, info->interfaces[0], &agente_vtable, NULL, NULL, NULL);
	g_dbus_node_info_unref(info);
	GVariant *r = chamar("/org/bluez", "org.bluez.AgentManager1", "RegisterAgent", g_variant_new("(os)", AGENTE, "NoInputNoOutput"), NULL, NULL);
	if (r) {
		g_variant_unref(r);
		r = chamar("/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent", g_variant_new("(o)", AGENTE), NULL, NULL);
		if (r) {
			g_variant_unref(r);
		}
	}
}

/* ---- comandos ---- */

struct comando {
	char a[128], b[96];
	bool x;
	int tipo;
};

static void erro_guardar(const char *prefixo, GError *e) {
	const char *m = e ? e->message : "";
	g_mutex_lock(&trinco);
	if (strstr(m, "AuthenticationFailed") || strstr(m, "Authentication Failed")) {
		snprintf(estado.erro, sizeof(estado.erro), "%s: o aparelho recusou o pareamento.", prefixo);
	} else if (strstr(m, "InProgress")) {
		snprintf(estado.erro, sizeof(estado.erro), "%s: já tem uma operação em andamento.", prefixo);
	} else if (strstr(m, "Timeout") || strstr(m, "timeout") || strstr(m, "Page Timeout")) {
		snprintf(estado.erro, sizeof(estado.erro), "%s: o aparelho não respondeu (está ligado e por perto?).", prefixo);
	} else if (strstr(m, "NotReady") || strstr(m, "Not Ready")) {
		snprintf(estado.erro, sizeof(estado.erro), "%s: o Bluetooth está desligado.", prefixo);
	} else {
		snprintf(estado.erro, sizeof(estado.erro), "%s: %s", prefixo, m);
	}
	estado.geracao++;
	g_mutex_unlock(&trinco);
	avisar();
}

static gboolean executar(gpointer dados) {
	struct comando *c = dados;
	if (!bus) {
		g_free(c);
		return G_SOURCE_REMOVE;
	}
	GError *e = NULL;
	GVariant *r = NULL;
	const char *prefixo = "Bluetooth";
	switch (c->tipo) {
	case 0: /* ligar/desligar o adaptador */
		r = chamar(c->a, PROPS, "Set", g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered", g_variant_new_boolean(c->x)), NULL, &e);
		break;
	case 1:
		r = chamar(c->a, "org.bluez.Adapter1", c->x ? "StartDiscovery" : "StopDiscovery", NULL, NULL, &e);
		break;
	case 2:
		r = chamar(c->a, PROPS, "Set", g_variant_new("(ssv)", "org.bluez.Adapter1", "Discoverable", g_variant_new_boolean(c->x)), NULL, &e);
		break;
	case 3:
		r = chamar(c->a, PROPS, "Set", g_variant_new("(ssv)", "org.bluez.Adapter1", "Alias", g_variant_new_string(c->b)), NULL, &e);
		break;
	case 4:
		prefixo = "Parear";
		r = chamar(c->a, "org.bluez.Device1", "Pair", NULL, NULL, &e);
		if (r) {
			g_variant_unref(r);
			/* depois de parear, confiar (o aparelho volta a conectar sozinho) e conectar */
			r = chamar(c->a, PROPS, "Set", g_variant_new("(ssv)", "org.bluez.Device1", "Trusted", g_variant_new_boolean(TRUE)), NULL, NULL);
			if (r) {
				g_variant_unref(r);
			}
			r = chamar(c->a, "org.bluez.Device1", "Connect", NULL, NULL, &e);
		}
		break;
	case 5:
		prefixo = "Conectar";
		r = chamar(c->a, "org.bluez.Device1", "Connect", NULL, NULL, &e);
		break;
	case 6:
		prefixo = "Desconectar";
		r = chamar(c->a, "org.bluez.Device1", "Disconnect", NULL, NULL, &e);
		break;
	case 7:
		prefixo = "Esquecer";
		r = chamar(c->b, "org.bluez.Adapter1", "RemoveDevice", g_variant_new("(o)", c->a), NULL, &e);
		break;
	case 8:
		r = chamar(c->a, PROPS, "Set", g_variant_new("(ssv)", "org.bluez.Device1", "Trusted", g_variant_new_boolean(c->x)), NULL, &e);
		break;
	}
	if (r) {
		g_variant_unref(r);
	} else if (e) {
		erro_guardar(prefixo, e);
		g_clear_error(&e);
	}
	g_free(c);
	atualizar();
	return G_SOURCE_REMOVE;
}

static void enviar(int tipo, const char *a, const char *b, bool x) {
	if (!contexto) {
		return;
	}
	struct comando *c = g_new0(struct comando, 1);
	c->tipo = tipo;
	copiar(c->a, sizeof(c->a), a);
	copiar(c->b, sizeof(c->b), b);
	c->x = x;
	g_main_context_invoke(contexto, executar, c);
}

void bt_ligar(const char *adapt, bool ligar) { enviar(0, adapt, "", ligar); }
void bt_procurar(const char *adapt, bool procurar) { enviar(1, adapt, "", procurar); }
void bt_visivel(const char *adapt, bool visivel) { enviar(2, adapt, "", visivel); }
void bt_renomear(const char *adapt, const char *nome) { enviar(3, adapt, nome, false); }
void bt_parear(const char *disp) { enviar(4, disp, "", false); }
void bt_conectar(const char *disp) { enviar(5, disp, "", false); }
void bt_desconectar(const char *disp) { enviar(6, disp, "", false); }
void bt_confiar(const char *disp, bool confiar) { enviar(8, disp, "", confiar); }

void bt_esquecer(const char *disp) {
	/* o RemoveDevice é do adaptador: o caminho dele é o do aparelho sem o /dev_XX */
	char adapt[96];
	copiar(adapt, sizeof(adapt), disp);
	char *barra = strrchr(adapt, '/');
	if (barra) {
		*barra = '\0';
	}
	enviar(7, disp, adapt, false);
}

static gpointer linha(gpointer d) {
	GMainContext *ctx = g_main_context_new();
	g_main_context_push_thread_default(ctx);
	contexto = ctx;
	GError *e = NULL;
	bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &e);
	if (!bus) {
		g_clear_error(&e);
	}
	GMainLoop *laco = g_main_loop_new(ctx, FALSE);
	GSource *t = g_timeout_source_new(2000);
	g_source_set_callback(t, tic, NULL, NULL);
	g_source_attach(t, ctx);
	if (bus) {
		registar_agente();
		atualizar();
	}
	g_main_loop_run(laco);
	return NULL;
}

int bt_iniciar(void) {
	if (fd_aviso >= 0) {
		return fd_aviso;
	}
	fd_aviso = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	g_thread_new("lumna-bt", linha, NULL);
	for (int i = 0; i < 100 && !g_atomic_pointer_get(&contexto); i++) {
		g_usleep(2000);
	}
	return fd_aviso;
}

void bt_estado_copiar(struct bt_estado *dest) {
	g_mutex_lock(&trinco);
	memcpy(dest, &estado, sizeof(*dest));
	g_mutex_unlock(&trinco);
}

void bt_erro_limpar(void) {
	g_mutex_lock(&trinco);
	estado.erro[0] = '\0';
	g_mutex_unlock(&trinco);
}

const char *bt_tipo_nome(const char *icone) {
	if (!strncmp(icone, "audio-headset", 13) || !strncmp(icone, "audio-headphones", 16)) {
		return "Fone de ouvido";
	}
	if (!strncmp(icone, "audio", 5)) {
		return "Áudio";
	}
	if (!strncmp(icone, "input-keyboard", 14)) {
		return "Teclado";
	}
	if (!strncmp(icone, "input-mouse", 11)) {
		return "Mouse";
	}
	if (!strncmp(icone, "input-gaming", 12)) {
		return "Controle";
	}
	if (!strncmp(icone, "phone", 5)) {
		return "Celular";
	}
	if (!strncmp(icone, "computer", 8)) {
		return "Computador";
	}
	return "Aparelho";
}
