/* A rede pelo NetworkManager (rede.h): uma linha só fala com ele, o resto copia o estado. */
#include "rede.h"

#include <arpa/inet.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

#define NM "org.freedesktop.NetworkManager"
#define NM_OBJ "/org/freedesktop/NetworkManager"
#define NM_SET "/org/freedesktop/NetworkManager/Settings"
#define PROPS "org.freedesktop.DBus.Properties"

static GDBusConnection *bus;
static GMutex trinco;
static struct rede_estado estado;
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

/* ---- D-Bus ---- */

static GVariant *chamar(const char *dest, const char *caminho, const char *iface, const char *metodo, GVariant *args,
		const GVariantType *tipo, GError **erro) {
	return g_dbus_connection_call_sync(bus, dest, caminho, iface, metodo, args, tipo, G_DBUS_CALL_FLAGS_NONE, 4000, NULL, erro);
}

/* GetAll de uma interface: um a{sv}, que o chamador liberta (NULL: falhou) */
static GVariant *tudo(const char *caminho, const char *iface) {
	GVariant *r = chamar(NM, caminho, PROPS, "GetAll", g_variant_new("(s)", iface), G_VARIANT_TYPE("(a{sv})"), NULL);
	if (!r) {
		return NULL;
	}
	GVariant *d = g_variant_get_child_value(r, 0);
	g_variant_unref(r);
	return d;
}

static const char *dic_s(GVariant *d, const char *chave) {
	const char *s = NULL;
	if (d && g_variant_lookup(d, chave, "&s", &s)) {
		return s;
	}
	return "";
}

static const char *dic_o(GVariant *d, const char *chave) {
	const char *s = NULL;
	if (d && g_variant_lookup(d, chave, "&o", &s)) {
		return s;
	}
	return "/";
}

static uint32_t dic_u(GVariant *d, const char *chave) {
	uint32_t u = 0;
	if (d && g_variant_lookup(d, chave, "u", &u)) {
		return u;
	}
	return 0;
}

static bool dic_b(GVariant *d, const char *chave, bool padrao) {
	gboolean b;
	if (d && g_variant_lookup(d, chave, "b", &b)) {
		return b;
	}
	return padrao;
}

static void ssid_de(GVariant *bytes, char *dest, size_t tam) {
	dest[0] = '\0';
	if (!bytes) {
		return;
	}
	gsize n;
	const guchar *b = g_variant_get_fixed_array(bytes, &n, 1);
	if (n >= tam) {
		n = tam - 1;
	}
	memcpy(dest, b, n);
	dest[n] = '\0';
}

/* ---- o hardware: PCI (onboard) ou USB (antena), o nome do modelo ---- */

static void hardware(const char *iface, struct rede_disp *d) {
	const char *raiz = getenv("LUMNA_SYSFS_NET");
	char caminho[300], alvo[300];
	snprintf(caminho, sizeof(caminho), "%s/%s/device/subsystem", raiz && *raiz ? raiz : "/sys/class/net", iface);
	ssize_t n = readlink(caminho, alvo, sizeof(alvo) - 1);
	if (n > 0) {
		alvo[n] = '\0';
		const char *base = strrchr(alvo, '/');
		base = base ? base + 1 : alvo;
		if (!strcmp(base, "pci")) {
			copiar(d->barramento, sizeof(d->barramento), "PCI");
		} else if (!strcmp(base, "usb")) {
			copiar(d->barramento, sizeof(d->barramento), "USB");
		}
	}
	/* o modelo: o udev sabe o nome do fabricante e do modelo */
	char cmd[300];
	if (raiz && *raiz) {
		snprintf(cmd, sizeof(cmd), "cat '%s/%s/modelo' 2>/dev/null", raiz, iface);
	} else {
		snprintf(cmd, sizeof(cmd), "udevadm info -q property -p /sys/class/net/%s 2>/dev/null", iface);
	}
	FILE *f = popen(cmd, "r");
	if (!f) {
		return;
	}
	char linha[256], vendor[80] = "", modelo[80] = "", vendor_db[80] = "", modelo_db[80] = "";
	while (fgets(linha, sizeof(linha), f)) {
		linha[strcspn(linha, "\n")] = '\0';
		if (!raiz || !*raiz) {
			if (!strncmp(linha, "ID_VENDOR_FROM_DATABASE=", 24)) {
				copiar(vendor_db, sizeof(vendor_db), linha + 24);
			} else if (!strncmp(linha, "ID_MODEL_FROM_DATABASE=", 23)) {
				copiar(modelo_db, sizeof(modelo_db), linha + 23);
			} else if (!strncmp(linha, "ID_VENDOR=", 10)) {
				copiar(vendor, sizeof(vendor), linha + 10);
			} else if (!strncmp(linha, "ID_MODEL=", 9)) {
				copiar(modelo, sizeof(modelo), linha + 9);
			}
		} else {
			copiar(d->modelo, sizeof(d->modelo), linha);
		}
	}
	pclose(f);
	if (!d->modelo[0]) {
		const char *v = vendor_db[0] ? vendor_db : vendor, *m = modelo_db[0] ? modelo_db : modelo;
		/* "Realtek Semiconductor Corp." fica "Realtek": a primeira palavra do fabricante */
		char curto[40];
		copiar(curto, sizeof(curto), v);
		char *sp = strchr(curto, ' ');
		if (sp) {
			*sp = '\0';
		}
		snprintf(d->modelo, sizeof(d->modelo), "%s%s%s", curto, curto[0] && m[0] ? " " : "", m);
	}
}

/* ---- ler o estado ---- */

static void ler_ip(const char *ip4, struct rede_disp *d) {
	if (!ip4 || !strcmp(ip4, "/")) {
		return;
	}
	GVariant *c = tudo(ip4, "org.freedesktop.NetworkManager.IP4Config");
	if (!c) {
		return;
	}
	GVariant *ends = g_variant_lookup_value(c, "AddressData", G_VARIANT_TYPE("aa{sv}"));
	if (ends && g_variant_n_children(ends) > 0) {
		GVariant *primeiro = g_variant_get_child_value(ends, 0);
		const char *a = NULL;
		uint32_t prefixo = 0;
		g_variant_lookup(primeiro, "address", "&s", &a);
		g_variant_lookup(primeiro, "prefix", "u", &prefixo);
		if (a) {
			snprintf(d->ip, sizeof(d->ip), "%s/%u", a, prefixo);
		}
		g_variant_unref(primeiro);
	}
	if (ends) {
		g_variant_unref(ends);
	}
	copiar(d->gateway, sizeof(d->gateway), dic_s(c, "Gateway"));
	GVariant *dns = g_variant_lookup_value(c, "NameserverData", G_VARIANT_TYPE("aa{sv}"));
	for (gsize i = 0; dns && i < g_variant_n_children(dns); i++) {
		GVariant *e = g_variant_get_child_value(dns, i);
		const char *a = NULL;
		if (g_variant_lookup(e, "address", "&s", &a) && a) {
			size_t l = strlen(d->dns);
			snprintf(d->dns + l, sizeof(d->dns) - l, "%s%s", l ? ", " : "", a);
		}
		g_variant_unref(e);
	}
	if (dns) {
		g_variant_unref(dns);
	}
	g_variant_unref(c);
}

static void ler_dispositivos(struct rede_estado *e) {
	GVariant *r = chamar(NM, NM_OBJ, NM, "GetAllDevices", NULL, G_VARIANT_TYPE("(ao)"), NULL);
	if (!r) {
		r = chamar(NM, NM_OBJ, NM, "GetDevices", NULL, G_VARIANT_TYPE("(ao)"), NULL);
	}
	if (!r) {
		return;
	}
	GVariant *lista = g_variant_get_child_value(r, 0);
	gsize n = g_variant_n_children(lista);
	for (gsize i = 0; i < n && e->ndisp < REDE_MAX_DISP; i++) {
		const char *caminho;
		GVariant *item = g_variant_get_child_value(lista, i);
		caminho = g_variant_get_string(item, NULL);
		GVariant *p = tudo(caminho, "org.freedesktop.NetworkManager.Device");
		if (!p) {
			g_variant_unref(item);
			continue;
		}
		uint32_t tipo = dic_u(p, "DeviceType");
		if (tipo != 1 && tipo != 2) {
			g_variant_unref(p);
			g_variant_unref(item);
			continue; /* só cabo e Wi-Fi; o resto (loopback, ponte, VPN) fica para depois */
		}
		struct rede_disp *d = &e->disp[e->ndisp++];
		copiar(d->caminho, sizeof(d->caminho), caminho);
		copiar(d->iface, sizeof(d->iface), dic_s(p, "Interface"));
		copiar(d->driver, sizeof(d->driver), dic_s(p, "Driver"));
		d->tipo = tipo == 2 ? REDE_WIFI : REDE_CABO;
		d->estado = (int)dic_u(p, "State");
		d->gerenciado = dic_b(p, "Managed", true);
		hardware(d->iface, d);
		const char *iface_tipo = tipo == 2 ? "org.freedesktop.NetworkManager.Device.Wireless"
						   : "org.freedesktop.NetworkManager.Device.Wired";
		GVariant *t = tudo(caminho, iface_tipo);
		if (t) {
			copiar(d->mac, sizeof(d->mac), dic_s(t, "HwAddress"));
			if (tipo == 1) {
				d->velocidade = dic_u(t, "Speed");
			}
			g_variant_unref(t);
		}
		ler_ip(dic_o(p, "Ip4Config"), d);
		const char *ativa = dic_o(p, "ActiveConnection");
		if (strcmp(ativa, "/")) {
			GVariant *a = tudo(ativa, "org.freedesktop.NetworkManager.Connection.Active");
			if (a) {
				copiar(d->conexao, sizeof(d->conexao), dic_s(a, "Id"));
				copiar(d->conexao_uuid, sizeof(d->conexao_uuid), dic_s(a, "Uuid"));
				g_variant_unref(a);
			}
		}
		g_variant_unref(p);
		g_variant_unref(item);
	}
	g_variant_unref(lista);
	g_variant_unref(r);
}

static void ler_aps(struct rede_estado *e) {
	for (int i = 0; i < e->ndisp; i++) {
		struct rede_disp *d = &e->disp[i];
		if (d->tipo != REDE_WIFI || !d->gerenciado) {
			continue;
		}
		GVariant *w = tudo(d->caminho, "org.freedesktop.NetworkManager.Device.Wireless");
		const char *atual = w ? dic_o(w, "ActiveAccessPoint") : "/";
		char atual_copia[96];
		copiar(atual_copia, sizeof(atual_copia), atual);
		if (w) {
			g_variant_unref(w);
		}
		GVariant *r = chamar(NM, d->caminho, "org.freedesktop.NetworkManager.Device.Wireless", "GetAllAccessPoints", NULL,
			G_VARIANT_TYPE("(ao)"), NULL);
		if (!r) {
			continue;
		}
		GVariant *lista = g_variant_get_child_value(r, 0);
		for (gsize k = 0; k < g_variant_n_children(lista); k++) {
			GVariant *item = g_variant_get_child_value(lista, k);
			const char *caminho = g_variant_get_string(item, NULL);
			GVariant *p = tudo(caminho, "org.freedesktop.NetworkManager.AccessPoint");
			if (p) {
				char ssid[40];
				GVariant *bytes = g_variant_lookup_value(p, "Ssid", G_VARIANT_TYPE("ay"));
				ssid_de(bytes, ssid, sizeof(ssid));
				if (bytes) {
					g_variant_unref(bytes);
				}
				uint8_t forca = 0;
				g_variant_lookup(p, "Strength", "y", &forca);
				bool segura = dic_u(p, "Flags") & 1 || dic_u(p, "WpaFlags") || dic_u(p, "RsnFlags");
				bool eh_atual = !strcmp(caminho, atual_copia);
				/* a mesma rede vista duas vezes (2,4 e 5 GHz): fica a mais forte */
				struct rede_ap *igual = NULL;
				for (int j = 0; j < e->naps; j++) {
					if (!strcmp(e->aps[j].ssid, ssid) && !strcmp(e->aps[j].disp, d->caminho)) {
						igual = &e->aps[j];
					}
				}
				if (ssid[0] && igual) {
					if (forca > igual->sinal) {
						igual->sinal = forca;
						copiar(igual->caminho, sizeof(igual->caminho), caminho);
					}
					igual->atual |= eh_atual;
				} else if (ssid[0] && e->naps < REDE_MAX_AP) {
					struct rede_ap *a = &e->aps[e->naps++];
					copiar(a->caminho, sizeof(a->caminho), caminho);
					copiar(a->disp, sizeof(a->disp), d->caminho);
					copiar(a->ssid, sizeof(a->ssid), ssid);
					a->sinal = forca;
					a->segura = segura;
					a->atual = eh_atual;
				}
				g_variant_unref(p);
			}
			g_variant_unref(item);
		}
		g_variant_unref(lista);
		g_variant_unref(r);
	}
}

static void ordenar_aps(struct rede_estado *e) {
	/* a mais forte primeiro; a ligada em cima */
	for (int i = 1; i < e->naps; i++) {
		struct rede_ap x = e->aps[i];
		int j = i;
		while (j > 0 && ((x.atual && !e->aps[j - 1].atual) || (x.atual == e->aps[j - 1].atual && x.sinal > e->aps[j - 1].sinal))) {
			e->aps[j] = e->aps[j - 1];
			j--;
		}
		e->aps[j] = x;
	}
}

static void ips_texto(GVariant *dns_au, char *dest, size_t tam) {
	dest[0] = '\0';
	if (!dns_au) {
		return;
	}
	gsize n;
	const uint32_t *v = g_variant_get_fixed_array(dns_au, &n, sizeof(uint32_t));
	for (gsize i = 0; i < n; i++) {
		char a[INET_ADDRSTRLEN];
		struct in_addr ia = { .s_addr = v[i] };
		inet_ntop(AF_INET, &ia, a, sizeof(a));
		size_t l = strlen(dest);
		snprintf(dest + l, tam - l, "%s%s", l ? ", " : "", a);
	}
}

static void ler_salvas(struct rede_estado *e) {
	GVariant *r = chamar(NM, NM_SET, "org.freedesktop.NetworkManager.Settings", "ListConnections", NULL, G_VARIANT_TYPE("(ao)"), NULL);
	if (!r) {
		return;
	}
	GVariant *lista = g_variant_get_child_value(r, 0);
	for (gsize i = 0; i < g_variant_n_children(lista) && e->nsalvas < REDE_MAX_SALVAS; i++) {
		GVariant *item = g_variant_get_child_value(lista, i);
		GVariant *s = chamar(NM, g_variant_get_string(item, NULL), "org.freedesktop.NetworkManager.Settings.Connection", "GetSettings",
			NULL, G_VARIANT_TYPE("(a{sa{sv}})"), NULL);
		if (s) {
			GVariant *cfg = g_variant_get_child_value(s, 0);
			GVariant *con = g_variant_lookup_value(cfg, "connection", G_VARIANT_TYPE("a{sv}"));
			const char *tipo = con ? dic_s(con, "type") : "";
			bool wifi = !strcmp(tipo, "802-11-wireless"), cabo = !strcmp(tipo, "802-3-ethernet");
			if (con && (wifi || cabo)) {
				struct rede_salva *x = &e->salvas[e->nsalvas++];
				copiar(x->uuid, sizeof(x->uuid), dic_s(con, "uuid"));
				copiar(x->nome, sizeof(x->nome), dic_s(con, "id"));
				x->tipo = wifi ? REDE_WIFI : REDE_CABO;
				uint64_t quando = 0;
				g_variant_lookup(con, "timestamp", "t", &quando);
				x->ultimo_uso = (int64_t)quando;
				x->automatica = dic_b(con, "autoconnect", true);
				GVariant *rw = g_variant_lookup_value(cfg, "802-11-wireless", G_VARIANT_TYPE("a{sv}"));
				if (rw) {
					GVariant *bytes = g_variant_lookup_value(rw, "ssid", G_VARIANT_TYPE("ay"));
					ssid_de(bytes, x->ssid, sizeof(x->ssid));
					if (bytes) {
						g_variant_unref(bytes);
					}
					g_variant_unref(rw);
				}
				x->metrica = -1;
				GVariant *ip = g_variant_lookup_value(cfg, "ipv4", G_VARIANT_TYPE("a{sv}"));
				if (ip) {
					GVariant *dns = g_variant_lookup_value(ip, "dns", G_VARIANT_TYPE("au"));
					ips_texto(dns, x->dns, sizeof(x->dns));
					if (dns) {
						g_variant_unref(dns);
					}
					int64_t m;
					if (g_variant_lookup(ip, "route-metric", "x", &m)) {
						x->metrica = (int)m;
					}
					g_variant_unref(ip);
				}
				/* ligada agora? */
				for (int k = 0; k < e->ndisp; k++) {
					if (!strcmp(e->disp[k].conexao_uuid, x->uuid)) {
						x->ativa = true;
					}
				}
			}
			if (con) {
				g_variant_unref(con);
			}
			g_variant_unref(cfg);
			g_variant_unref(s);
		}
		g_variant_unref(item);
	}
	g_variant_unref(lista);
	g_variant_unref(r);
	/* a ligada, quando o NM não diz qual ponto de acesso: a que tem o nome da conexão ativa */
	for (int i = 0; i < e->naps; i++) {
		for (int k = 0; k < e->ndisp; k++) {
			if (!strcmp(e->disp[k].caminho, e->aps[i].disp) && e->disp[k].estado >= 100) {
				for (int j = 0; j < e->nsalvas; j++) {
					if (!strcmp(e->salvas[j].uuid, e->disp[k].conexao_uuid) && !strcmp(e->salvas[j].ssid, e->aps[i].ssid)) {
						e->aps[i].atual = true;
					}
				}
			}
		}
	}
	/* as de Wi-Fi à vista que já têm conexão guardada */
	for (int i = 0; i < e->naps; i++) {
		for (int k = 0; k < e->nsalvas; k++) {
			if (e->salvas[k].tipo == REDE_WIFI && !strcmp(e->salvas[k].ssid, e->aps[i].ssid)) {
				e->aps[i].salva = true;
			}
		}
	}
}

static void atualizar(void) {
	struct rede_estado *novo = g_new0(struct rede_estado, 1);
	GVariant *p = tudo(NM_OBJ, NM);
	if (p) {
		novo->nm = true;
		novo->wifi_ligado = dic_b(p, "WirelessEnabled", true);
		novo->internet = dic_u(p, "Connectivity") == 4;
		g_variant_unref(p);
		ler_dispositivos(novo);
		ler_aps(novo);
		ler_salvas(novo);
		ordenar_aps(novo);
	}
	g_mutex_lock(&trinco);
	copiar(novo->erro, sizeof(novo->erro), estado.erro);
	novo->geracao = estado.geracao;
	bool mudou = memcmp(novo, &estado, sizeof(estado)) != 0;
	if (mudou) {
		uint32_t g = estado.geracao + 1;
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
	atualizar();
	return G_SOURCE_CONTINUE;
}

/* ---- os comandos: cada um corre na linha do serviço ---- */

struct comando {
	char a[128], b[128], c[160];
	int n, m, o;
	bool x, y;
	void (*faz)(struct comando *);
};

static void erro_guardar(const char *prefixo, GError *e) {
	g_mutex_lock(&trinco);
	/* o NM diz "Secrets were required, but not provided" e afins: aqui vira português */
	const char *m = e ? e->message : "";
	if (strstr(m, "Secrets were required") || strstr(m, "no-secrets") || strstr(m, "Wi-Fi network could not be found")) {
		snprintf(estado.erro, sizeof(estado.erro), "%s: a senha está errada ou a rede saiu do alcance.", prefixo);
	} else if (strstr(m, "NotAuthorized") || strstr(m, "not authorized")) {
		snprintf(estado.erro, sizeof(estado.erro), "%s: o sistema não deixou (falta permissão).", prefixo);
	} else {
		snprintf(estado.erro, sizeof(estado.erro), "%s: %s", prefixo, m);
	}
	estado.geracao++;
	g_mutex_unlock(&trinco);
	avisar();
}

static gboolean executar(gpointer dados) {
	struct comando *c = dados;
	c->faz(c);
	g_free(c);
	atualizar();
	return G_SOURCE_REMOVE;
}

static struct comando *novo_comando(void (*faz)(struct comando *)) {
	struct comando *c = g_new0(struct comando, 1);
	c->faz = faz;
	return c;
}

static void enviar(struct comando *c) {
	if (!contexto) {
		g_free(c);
		return;
	}
	g_main_context_invoke(contexto, executar, c);
}

static void f_varrer(struct comando *c) {
	GVariant *opcoes = g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0);
	/* sem adaptador dito: todos os de Wi-Fi */
	struct rede_estado cp;
	rede_estado_copiar(&cp);
	for (int i = 0; i < cp.ndisp; i++) {
		if (cp.disp[i].tipo == REDE_WIFI && cp.disp[i].gerenciado && (!c->a[0] || !strcmp(cp.disp[i].caminho, c->a))) {
			GError *e = NULL;
			GVariant *r = chamar(NM, cp.disp[i].caminho, "org.freedesktop.NetworkManager.Device.Wireless", "RequestScan",
				g_variant_new("(@a{sv})", g_variant_ref(opcoes)), NULL, &e);
			if (r) {
				g_variant_unref(r);
			} else {
				g_clear_error(&e); /* "varreu há pouco": o NM limita, não é erro de verdade */
			}
		}
	}
	g_variant_unref(opcoes);
}

void rede_varrer(const char *disp) {
	struct comando *c = novo_comando(f_varrer);
	copiar(c->a, sizeof(c->a), disp);
	enviar(c);
}

static void f_wifi_ligar(struct comando *c) {
	GError *e = NULL;
	GVariant *r = chamar(NM, NM_OBJ, PROPS, "Set", g_variant_new("(ssv)", NM, "WirelessEnabled", g_variant_new_boolean(c->x)), NULL, &e);
	if (r) {
		g_variant_unref(r);
	} else {
		erro_guardar("Wi-Fi", e);
		g_clear_error(&e);
	}
}

void rede_wifi_ligar(bool ligar) {
	struct comando *c = novo_comando(f_wifi_ligar);
	c->x = ligar;
	enviar(c);
}

static void f_disp_ativar(struct comando *c) {
	GError *e = NULL;
	GVariant *r = chamar(NM, c->a, PROPS, "Set",
		g_variant_new("(ssv)", "org.freedesktop.NetworkManager.Device", "Managed", g_variant_new_boolean(c->x)), NULL, &e);
	if (r) {
		g_variant_unref(r);
	} else {
		erro_guardar("Adaptador", e);
		g_clear_error(&e);
	}
}

void rede_disp_ativar(const char *disp, bool ativar) {
	struct comando *c = novo_comando(f_disp_ativar);
	copiar(c->a, sizeof(c->a), disp);
	c->x = ativar;
	enviar(c);
}

static GVariant *ssid_bytes(const char *ssid) {
	return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, ssid, strlen(ssid), 1);
}

static GVariant *config_wifi(const char *ssid, int seguranca, const char *senha, bool oculta) {
	GVariantBuilder b, con, sem, seg, ip;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a{sa{sv}}"));
	g_variant_builder_init(&con, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&con, "{sv}", "id", g_variant_new_string(ssid));
	g_variant_builder_add(&con, "{sv}", "type", g_variant_new_string("802-11-wireless"));
	g_variant_builder_add(&b, "{sa{sv}}", "connection", &con);
	g_variant_builder_init(&sem, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&sem, "{sv}", "ssid", ssid_bytes(ssid));
	g_variant_builder_add(&sem, "{sv}", "mode", g_variant_new_string("infrastructure"));
	if (oculta) {
		g_variant_builder_add(&sem, "{sv}", "hidden", g_variant_new_boolean(TRUE));
	}
	g_variant_builder_add(&b, "{sa{sv}}", "802-11-wireless", &sem);
	if (seguranca > 0 && senha && *senha) {
		g_variant_builder_init(&seg, G_VARIANT_TYPE("a{sv}"));
		g_variant_builder_add(&seg, "{sv}", "key-mgmt", g_variant_new_string(seguranca == 2 ? "sae" : "wpa-psk"));
		g_variant_builder_add(&seg, "{sv}", seguranca == 2 ? "psk" : "psk", g_variant_new_string(senha));
		g_variant_builder_add(&b, "{sa{sv}}", "802-11-wireless-security", &seg);
	}
	g_variant_builder_init(&ip, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&ip, "{sv}", "method", g_variant_new_string("auto"));
	g_variant_builder_add(&b, "{sa{sv}}", "ipv4", &ip);
	return g_variant_builder_end(&b);
}

static void conectar_salva(struct comando *c);

static void f_conectar(struct comando *c) {
	GError *e = NULL;
	struct rede_estado cp;
	rede_estado_copiar(&cp);
	const struct rede_ap *ap = NULL;
	for (int i = 0; i < cp.naps; i++) {
		if (!strcmp(cp.aps[i].caminho, c->b)) {
			ap = &cp.aps[i];
		}
	}
	if (!ap) {
		erro_guardar("Conectar", &(GError){ .message = "a rede saiu do alcance" });
		return;
	}
	/* já há uma conexão guardada com este nome e sem senha nova: liga essa */
	if (!c->c[0]) {
		for (int i = 0; i < cp.nsalvas; i++) {
			if (cp.salvas[i].tipo == REDE_WIFI && !strcmp(cp.salvas[i].ssid, ap->ssid)) {
				rede_estado_copiar(&cp);
				struct comando k = { 0 };
				copiar(k.a, sizeof(k.a), cp.salvas[i].uuid);
				copiar(k.b, sizeof(k.b), c->a);
				conectar_salva(&k);
				return;
			}
		}
	}
	GVariant *cfg = config_wifi(ap->ssid, ap->segura ? 1 : 0, c->c, false);
	GVariant *r = chamar(NM, NM_OBJ, NM, "AddAndActivateConnection", g_variant_new("(@a{sa{sv}}oo)", cfg, c->a, c->b),
		G_VARIANT_TYPE("(oo)"), &e);
	if (r) {
		g_variant_unref(r);
	} else {
		erro_guardar("Conectar", e);
		g_clear_error(&e);
	}
}

void rede_conectar(const char *disp, const char *ap, const char *senha) {
	struct comando *c = novo_comando(f_conectar);
	copiar(c->a, sizeof(c->a), disp);
	copiar(c->b, sizeof(c->b), ap);
	copiar(c->c, sizeof(c->c), senha);
	enviar(c);
}

static char *caminho_da_conexao(const char *uuid) {
	GVariant *r = chamar(NM, NM_SET, "org.freedesktop.NetworkManager.Settings", "GetConnectionByUuid", g_variant_new("(s)", uuid),
		G_VARIANT_TYPE("(o)"), NULL);
	if (!r) {
		return NULL;
	}
	const char *p;
	g_variant_get(r, "(&o)", &p);
	char *copia = g_strdup(p);
	g_variant_unref(r);
	return copia;
}

static void conectar_salva(struct comando *c) {
	char *conexao = caminho_da_conexao(c->a);
	if (!conexao) {
		erro_guardar("Conectar", &(GError){ .message = "a conexão não existe mais" });
		return;
	}
	GError *e = NULL;
	GVariant *r = chamar(NM, NM_OBJ, NM, "ActivateConnection", g_variant_new("(ooo)", conexao, c->b[0] ? c->b : "/", "/"),
		G_VARIANT_TYPE("(o)"), &e);
	if (r) {
		g_variant_unref(r);
	} else {
		erro_guardar("Conectar", e);
		g_clear_error(&e);
	}
	g_free(conexao);
}

void rede_conectar_salva(const char *uuid, const char *disp) {
	struct comando *c = novo_comando(conectar_salva);
	copiar(c->a, sizeof(c->a), uuid);
	copiar(c->b, sizeof(c->b), disp);
	enviar(c);
}

static void f_desconectar(struct comando *c) {
	GError *e = NULL;
	GVariant *r = chamar(NM, c->a, "org.freedesktop.NetworkManager.Device", "Disconnect", NULL, NULL, &e);
	if (r) {
		g_variant_unref(r);
	} else {
		erro_guardar("Desconectar", e);
		g_clear_error(&e);
	}
}

void rede_desconectar(const char *disp) {
	struct comando *c = novo_comando(f_desconectar);
	copiar(c->a, sizeof(c->a), disp);
	enviar(c);
}

static void f_esquecer(struct comando *c) {
	char *conexao = caminho_da_conexao(c->a);
	if (!conexao) {
		return;
	}
	GError *e = NULL;
	GVariant *r = chamar(NM, conexao, "org.freedesktop.NetworkManager.Settings.Connection", "Delete", NULL, NULL, &e);
	if (r) {
		g_variant_unref(r);
	} else {
		erro_guardar("Esquecer", e);
		g_clear_error(&e);
	}
	g_free(conexao);
}

void rede_esquecer(const char *uuid) {
	struct comando *c = novo_comando(f_esquecer);
	copiar(c->a, sizeof(c->a), uuid);
	enviar(c);
}

static void f_adicionar(struct comando *c) {
	GError *e = NULL;
	GVariant *cfg = config_wifi(c->b, c->n, c->c, c->x);
	/* "/" no adaptador: o NM escolhe; no ponto, "/" é "qualquer um" (e na oculta o único jeito) */
	GVariant *r = chamar(NM, NM_OBJ, NM, "AddAndActivateConnection", g_variant_new("(@a{sa{sv}}oo)", cfg, c->a[0] ? c->a : "/", "/"),
		G_VARIANT_TYPE("(oo)"), &e);
	if (r) {
		g_variant_unref(r);
	} else {
		erro_guardar("Adicionar rede", e);
		g_clear_error(&e);
	}
}

void rede_adicionar(const char *disp, const char *ssid, int seguranca, const char *senha, bool oculta) {
	struct comando *c = novo_comando(f_adicionar);
	copiar(c->a, sizeof(c->a), disp);
	copiar(c->b, sizeof(c->b), ssid);
	copiar(c->c, sizeof(c->c), senha);
	c->n = seguranca;
	c->x = oculta;
	enviar(c);
}

static GVariant *dns_au(const char *texto) {
	GArray *v = g_array_new(FALSE, FALSE, sizeof(uint32_t));
	char *copia = g_strdup(texto);
	for (char *tok = strtok(copia, ", ;"); tok; tok = strtok(NULL, ", ;")) {
		struct in_addr ia;
		if (inet_pton(AF_INET, tok, &ia) == 1) {
			g_array_append_val(v, ia.s_addr);
		}
	}
	g_free(copia);
	GVariant *r = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, v->data, v->len, sizeof(uint32_t));
	g_array_free(v, TRUE);
	return r;
}

static void f_ajustar(struct comando *c) {
	char *conexao = caminho_da_conexao(c->a);
	if (!conexao) {
		return;
	}
	GError *e = NULL;
	GVariant *r = chamar(NM, conexao, "org.freedesktop.NetworkManager.Settings.Connection", "GetSettings", NULL,
		G_VARIANT_TYPE("(a{sa{sv}})"), &e);
	if (!r) {
		erro_guardar("Ajustar", e);
		g_clear_error(&e);
		g_free(conexao);
		return;
	}
	GVariant *cfg = g_variant_get_child_value(r, 0);
	GVariantBuilder b;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a{sa{sv}}"));
	GVariantIter it;
	g_variant_iter_init(&it, cfg);
	const char *nome;
	GVariant *sec;
	bool tinha_ipv4 = false;
	while (g_variant_iter_next(&it, "{&s@a{sv}}", &nome, &sec)) {
		GVariantBuilder s;
		g_variant_builder_init(&s, G_VARIANT_TYPE("a{sv}"));
		bool eh_ip = !strcmp(nome, "ipv4"), eh_con = !strcmp(nome, "connection");
		tinha_ipv4 |= eh_ip;
		GVariantIter si;
		g_variant_iter_init(&si, sec);
		const char *k;
		GVariant *v;
		while (g_variant_iter_next(&si, "{&sv}", &k, &v)) {
			bool troca = (eh_ip && (!strcmp(k, "dns") || !strcmp(k, "ignore-auto-dns") || (!strcmp(k, "route-metric") && c->o)))
				|| (eh_con && !strcmp(k, "autoconnect") && c->m >= 0);
			if (!troca) {
				g_variant_builder_add(&s, "{sv}", k, v);
			}
			g_variant_unref(v);
		}
		if (eh_ip) {
			if (c->b[0]) {
				g_variant_builder_add(&s, "{sv}", "dns", dns_au(c->b));
				g_variant_builder_add(&s, "{sv}", "ignore-auto-dns", g_variant_new_boolean(TRUE));
			}
			if (c->o) {
				if (c->n >= 0) {
					g_variant_builder_add(&s, "{sv}", "route-metric", g_variant_new_int64(c->n));
				}
			}
		}
		if (eh_con && c->m >= 0) {
			g_variant_builder_add(&s, "{sv}", "autoconnect", g_variant_new_boolean(c->m == 1));
		}
		g_variant_builder_add(&b, "{sa{sv}}", nome, &s);
		g_variant_unref(sec);
	}
	if (!tinha_ipv4 && (c->b[0] || c->o)) {
		GVariantBuilder s;
		g_variant_builder_init(&s, G_VARIANT_TYPE("a{sv}"));
		g_variant_builder_add(&s, "{sv}", "method", g_variant_new_string("auto"));
		if (c->b[0]) {
			g_variant_builder_add(&s, "{sv}", "dns", dns_au(c->b));
			g_variant_builder_add(&s, "{sv}", "ignore-auto-dns", g_variant_new_boolean(TRUE));
		}
		if (c->o && c->n >= 0) {
			g_variant_builder_add(&s, "{sv}", "route-metric", g_variant_new_int64(c->n));
		}
		g_variant_builder_add(&b, "{sa{sv}}", "ipv4", &s);
	}
	GVariant *novo = chamar(NM, conexao, "org.freedesktop.NetworkManager.Settings.Connection", "Update",
		g_variant_new("(@a{sa{sv}})", g_variant_builder_end(&b)), NULL, &e);
	if (novo) {
		g_variant_unref(novo);
	} else {
		erro_guardar("Ajustar", e);
		g_clear_error(&e);
	}
	g_variant_unref(cfg);
	g_variant_unref(r);
	g_free(conexao);
}

void rede_conexao_ajustar(const char *uuid, const char *dns, int metrica, int automatica) {
	struct comando *c = novo_comando(f_ajustar);
	copiar(c->a, sizeof(c->a), uuid);
	copiar(c->b, sizeof(c->b), dns);
	c->n = metrica;
	c->o = metrica != -2; /* -2: não mexe na métrica */
	c->m = automatica;
	enviar(c);
}

/* ---- a linha do serviço ---- */

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
		atualizar();
	}
	g_main_loop_run(laco);
	return NULL;
}

int rede_iniciar(void) {
	if (fd_aviso >= 0) {
		return fd_aviso;
	}
	fd_aviso = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	g_thread_new("lumna-rede", linha, NULL);
	/* os comandos só entram quando a linha já tem o seu contexto */
	for (int i = 0; i < 100 && !g_atomic_pointer_get(&contexto); i++) {
		g_usleep(2000);
	}
	return fd_aviso;
}

void rede_estado_copiar(struct rede_estado *dest) {
	g_mutex_lock(&trinco);
	memcpy(dest, &estado, sizeof(*dest));
	g_mutex_unlock(&trinco);
}

void rede_erro_limpar(void) {
	g_mutex_lock(&trinco);
	estado.erro[0] = '\0';
	g_mutex_unlock(&trinco);
}

const char *rede_barramento_nome(const char *barramento) {
	if (!strcmp(barramento, "PCI")) {
		return "Onboard (PCI)";
	}
	if (!strcmp(barramento, "USB")) {
		return "Antena USB";
	}
	return "Origem desconhecida";
}

const char *rede_estado_nome(int estado) {
	if (estado >= 100) {
		return "Conectado";
	}
	if (estado >= 40) {
		return "Conectando...";
	}
	if (estado >= 30) {
		return "Desconectado";
	}
	if (estado >= 20) {
		return "Indisponível";
	}
	return "Desligado";
}
