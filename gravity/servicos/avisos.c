/*
 * O servidor dos avisos (org.freedesktop.Notifications, versão 1.2 da especificação) numa linha
 * com o seu próprio ciclo do GLib: o GDBus chama-nos lá, e o que chega passa para a app por uma
 * fila (GAsyncQueue) e um eventfd que o ciclo do Gravity vigia. Os sinais de volta
 * (NotificationClosed, ActionInvoked) podem sair de qualquer linha.
 *
 * Também responde em org.lumna.Avisos (AlternarCentro), que é como a topbar abre a central e
 * como um segundo lumna-avisos percebe que já há um.
 */
#include "avisos.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define NOME_FDO "org.freedesktop.Notifications"
#define CAMINHO_FDO "/org/freedesktop/Notifications"
#define NOME_LUMNA "org.lumna.Avisos"
#define CAMINHO_LUMNA "/org/lumna/Avisos"

static const char xml_fdo[] =
	"<node><interface name='org.freedesktop.Notifications'>"
	"<method name='Notify'><arg type='s' direction='in'/><arg type='u' direction='in'/>"
	"<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/>"
	"<arg type='as' direction='in'/><arg type='a{sv}' direction='in'/><arg type='i' direction='in'/>"
	"<arg type='u' direction='out'/></method>"
	"<method name='CloseNotification'><arg type='u' direction='in'/></method>"
	"<method name='GetCapabilities'><arg type='as' direction='out'/></method>"
	"<method name='GetServerInformation'><arg type='s' direction='out'/><arg type='s' direction='out'/>"
	"<arg type='s' direction='out'/><arg type='s' direction='out'/></method>"
	"<signal name='NotificationClosed'><arg type='u'/><arg type='u'/></signal>"
	"<signal name='ActionInvoked'><arg type='u'/><arg type='s'/></signal>"
	"</interface></node>";

static const char xml_lumna[] =
	"<node><interface name='org.lumna.Avisos'><method name='AlternarCentro'/></interface></node>";

static GDBusConnection *ligacao;
static GAsyncQueue *fila;
static int fd_aviso = -1;
static uint32_t proximo_id = 1;
static uint32_t imagens; /* um nome novo por imagem: o cache do Gravity vai pelo caminho */

/* o arranque espera a linha saber se ficou com o nome */
static GMutex trinco;
static GCond pronto;
static int estado_nome; /* 0: à espera; 1: nosso; -1: de outro (ou sem barramento) */

static void avisar(struct aviso_evento *ev) {
	g_async_queue_push(fila, ev);
	uint64_t um = 1;
	if (write(fd_aviso, &um, sizeof(um)) < 0) {
		/* o contador só enche com 2^64 avisos por ler */
	}
}

/* ---- o texto ---- */

/* Tira as tags (<b>, <a href>, <img>) e troca as entidades pelo carácter. */
static void sem_marcacao(char *dst, size_t cap, const char *src) {
	size_t n = 0;
	for (const char *p = src; *p && n + 4 < cap;) {
		if (*p == '<') {
			const char *fim = strchr(p, '>');
			if (fim) {
				/* uma quebra de linha HTML vira uma quebra de verdade */
				if (!g_ascii_strncasecmp(p, "<br", 3)) {
					dst[n++] = '\n';
				}
				p = fim + 1;
				continue;
			}
		}
		if (*p == '&') {
			static const struct { const char *ent; char c; } ents[] = {
				{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' },
			};
			bool trocou = false;
			for (size_t i = 0; i < G_N_ELEMENTS(ents); i++) {
				size_t l = strlen(ents[i].ent);
				if (!strncmp(p, ents[i].ent, l)) {
					dst[n++] = ents[i].c;
					p += l;
					trocou = true;
					break;
				}
			}
			if (!trocou && p[1] == '#') {
				char *fim;
				gunichar c = p[2] == 'x' ? (gunichar)strtoul(p + 3, &fim, 16) : (gunichar)strtoul(p + 2, &fim, 10);
				if (*fim == ';' && g_unichar_validate(c)) {
					n += g_unichar_to_utf8(c, dst + n);
					p = fim + 1;
					trocou = true;
				}
			}
			if (trocou) {
				continue;
			}
		}
		dst[n++] = *p++;
	}
	dst[n] = '\0';
	/* um corte no meio de um carácter de vários bytes não pode chegar ao pango */
	const char *fim_valido;
	if (!g_utf8_validate(dst, -1, &fim_valido)) {
		dst[fim_valido - dst] = '\0';
	}
	g_strstrip(dst);
}

/* "file:///x.png" vira "/x.png"; um caminho fica; um nome de ícone fica. */
static void caminho_ou_nome(char *dst, size_t cap, const char *src) {
	if (g_str_has_prefix(src, "file://")) {
		char *c = g_filename_from_uri(src, NULL, NULL);
		snprintf(dst, cap, "%s", c ? c : "");
		g_free(c);
	} else {
		snprintf(dst, cap, "%s", src);
	}
}

/* image-data: (iiibiiay) largura, altura, passo, alfa, bits, canais, bytes → um PNG */
static void gravar_imagem(GVariant *v, char *dst, size_t cap) {
	if (!g_variant_is_of_type(v, G_VARIANT_TYPE("(iiibiiay)"))) {
		return;
	}
	int largura, altura, passo, bits, canais;
	gboolean alfa;
	GVariant *dados;
	g_variant_get(v, "(iiibii@ay)", &largura, &altura, &passo, &alfa, &bits, &canais, &dados);
	gsize tamanho;
	const guchar *bytes = g_variant_get_fixed_array(dados, &tamanho, 1);
	if (bits == 8 && largura > 0 && altura > 0 && passo > 0 && (gsize)passo * (altura - 1) + largura * canais <= tamanho &&
		canais == (alfa ? 4 : 3)) {
		GdkPixbuf *pb = gdk_pixbuf_new_from_data(bytes, GDK_COLORSPACE_RGB, alfa, 8, largura, altura, passo, NULL, NULL);
		const char *run = g_getenv("XDG_RUNTIME_DIR");
		char *pasta = g_build_filename(run ? run : g_get_tmp_dir(), "lumna-avisos", NULL);
		g_mkdir_with_parents(pasta, 0700);
		char nome[64];
		snprintf(nome, sizeof(nome), "%u.png", ++imagens);
		char *caminho = g_build_filename(pasta, nome, NULL);
		if (gdk_pixbuf_save(pb, caminho, "png", NULL, NULL)) {
			snprintf(dst, cap, "%s", caminho);
		}
		g_free(caminho);
		g_free(pasta);
		g_object_unref(pb);
	}
	g_variant_unref(dados);
}

/* ---- os métodos ---- */

static uint32_t notificar(GVariant *p) {
	const char *app, *icone, *titulo, *corpo;
	uint32_t substitui;
	int32_t expira;
	GVariantIter *acoes;
	GVariant *dicas;
	g_variant_get(p, "(&su&s&s&sas@a{sv}i)", &app, &substitui, &icone, &titulo, &corpo, &acoes, &dicas, &expira);

	struct aviso_evento *ev = g_new0(struct aviso_evento, 1);
	struct aviso *a = &ev->aviso;
	ev->tipo = AVISO_NOVO;
	if (substitui) {
		a->id = substitui;
		if (substitui >= proximo_id) {
			proximo_id = substitui + 1;
		}
	} else {
		a->id = proximo_id++;
	}
	ev->id = a->id;
	snprintf(a->app, sizeof(a->app), "%s", app);
	caminho_ou_nome(a->icone, sizeof(a->icone), icone);
	sem_marcacao(a->titulo, sizeof(a->titulo), titulo);
	sem_marcacao(a->corpo, sizeof(a->corpo), corpo);
	a->expira_ms = expira;
	a->urgencia = AVISO_NORMAL;
	a->quando = g_get_real_time() / G_USEC_PER_SEC;

	/* as ações vêm aos pares: chave, rótulo */
	const char *chave, *rotulo;
	while (g_variant_iter_next(acoes, "&s", &chave) && g_variant_iter_next(acoes, "&s", &rotulo)) {
		if (a->nacoes < AVISO_MAX_ACOES) {
			snprintf(a->acoes[a->nacoes][0], 64, "%s", chave);
			snprintf(a->acoes[a->nacoes][1], 64, "%s", rotulo);
			a->nacoes++;
		}
	}
	g_variant_iter_free(acoes);

	GVariantIter it;
	const char *dica;
	GVariant *valor;
	g_variant_iter_init(&it, dicas);
	while (g_variant_iter_next(&it, "{&sv}", &dica, &valor)) {
		if (!strcmp(dica, "urgency") && g_variant_is_of_type(valor, G_VARIANT_TYPE_BYTE)) {
			int u = g_variant_get_byte(valor);
			a->urgencia = u <= 0 ? AVISO_BAIXA : u >= 2 ? AVISO_CRITICA : AVISO_NORMAL;
		} else if (!strcmp(dica, "desktop-entry") && g_variant_is_of_type(valor, G_VARIANT_TYPE_STRING)) {
			snprintf(a->entrada, sizeof(a->entrada), "%s", g_variant_get_string(valor, NULL));
			char *ponto = strstr(a->entrada, ".desktop");
			if (ponto && !ponto[8]) {
				*ponto = '\0';
			}
		} else if (!strcmp(dica, "resident") && g_variant_is_of_type(valor, G_VARIANT_TYPE_BOOLEAN)) {
			a->residente = g_variant_get_boolean(valor);
		} else if (!strcmp(dica, "transient") && g_variant_is_of_type(valor, G_VARIANT_TYPE_BOOLEAN)) {
			a->transitorio = g_variant_get_boolean(valor);
		} else if ((!strcmp(dica, "image-path") || !strcmp(dica, "image_path")) &&
			g_variant_is_of_type(valor, G_VARIANT_TYPE_STRING)) {
			const char *s = g_variant_get_string(valor, NULL);
			if (*s == '/' || g_str_has_prefix(s, "file://")) {
				caminho_ou_nome(a->imagem, sizeof(a->imagem), s);
			} else if (!a->icone[0]) {
				snprintf(a->icone, sizeof(a->icone), "%s", s);
			}
		} else if ((!strcmp(dica, "image-data") || !strcmp(dica, "image_data") || !strcmp(dica, "icon_data")) && !a->imagem[0]) {
			gravar_imagem(valor, a->imagem, sizeof(a->imagem));
		}
		g_variant_unref(valor);
	}
	g_variant_unref(dicas);
	/* sem ícone mas com o .desktop: o ícone da app */
	if (!a->icone[0] && a->entrada[0]) {
		snprintf(a->icone, sizeof(a->icone), "%s", a->entrada);
	}
	uint32_t id = a->id;
	avisar(ev);
	return id;
}

static void chamada(GDBusConnection *c, const char *remetente, const char *caminho, const char *iface, const char *metodo,
	GVariant *p, GDBusMethodInvocation *inv, gpointer dados) {
	if (!strcmp(metodo, "Notify")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", notificar(p)));
	} else if (!strcmp(metodo, "CloseNotification")) {
		struct aviso_evento *ev = g_new0(struct aviso_evento, 1);
		ev->tipo = AVISO_FECHAR;
		g_variant_get(p, "(u)", &ev->id);
		avisar(ev);
		g_dbus_method_invocation_return_value(inv, NULL);
	} else if (!strcmp(metodo, "GetCapabilities")) {
		const char *caps[] = { "actions", "body", "body-markup", "icon-static", "persistence", NULL };
		g_dbus_method_invocation_return_value(inv, g_variant_new("(^as)", caps));
	} else if (!strcmp(metodo, "GetServerInformation")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(ssss)", "LUMNA", "LUMNA OS", "1.0", "1.2"));
	} else if (!strcmp(metodo, "AlternarCentro")) {
		struct aviso_evento *ev = g_new0(struct aviso_evento, 1);
		ev->tipo = AVISO_CENTRO;
		avisar(ev);
		g_dbus_method_invocation_return_value(inv, NULL);
	} else {
		g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod", metodo);
	}
}

static const GDBusInterfaceVTable vtable = { .method_call = chamada };

static void registar(GDBusConnection *c, const char *xml, const char *caminho) {
	GDBusNodeInfo *no = g_dbus_node_info_new_for_xml(xml, NULL);
	g_dbus_connection_register_object(c, caminho, no->interfaces[0], &vtable, NULL, NULL, NULL);
	g_dbus_node_info_unref(no);
}

static void decidir(int estado) {
	g_mutex_lock(&trinco);
	if (estado_nome == 0) {
		estado_nome = estado;
		g_cond_signal(&pronto);
	}
	g_mutex_unlock(&trinco);
}

static void ficou_lumna(GDBusConnection *c, const char *nome, gpointer d) {
	decidir(1);
}

static void perdeu_lumna(GDBusConnection *c, const char *nome, gpointer d) {
	decidir(-1);
}

static void ficou_fdo(GDBusConnection *c, const char *nome, gpointer d) {
	fprintf(stderr, "lumna-avisos: sou o servidor dos avisos\n");
}

static void perdeu_fdo(GDBusConnection *c, const char *nome, gpointer d) {
	fprintf(stderr, "lumna-avisos: outro programa responde pelos avisos (%s)\n", NOME_FDO);
}

static gpointer linha(gpointer d) {
	GMainContext *ctx = g_main_context_new();
	g_main_context_push_thread_default(ctx);
	GError *erro = NULL;
	ligacao = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &erro);
	if (!ligacao) {
		fprintf(stderr, "lumna-avisos: sem o barramento da sessão: %s\n", erro->message);
		g_error_free(erro);
		decidir(-1);
		return NULL;
	}
	registar(ligacao, xml_lumna, CAMINHO_LUMNA);
	registar(ligacao, xml_fdo, CAMINHO_FDO);
	/* o nosso primeiro: se já é de alguém, é outro lumna-avisos */
	g_bus_own_name_on_connection(ligacao, NOME_LUMNA, G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE, ficou_lumna, perdeu_lumna, NULL, NULL);
	/* o dos avisos: tira-o de quem deixar (um servidor de outro shell) */
	g_bus_own_name_on_connection(ligacao, NOME_FDO, G_BUS_NAME_OWNER_FLAGS_REPLACE | G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
		ficou_fdo, perdeu_fdo, NULL, NULL);
	GMainLoop *ciclo = g_main_loop_new(ctx, FALSE);
	g_main_loop_run(ciclo);
	return NULL;
}

int avisos_iniciar(void) {
	fila = g_async_queue_new_full(g_free);
	fd_aviso = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (fd_aviso < 0) {
		return -1;
	}
	g_thread_unref(g_thread_new("avisos", linha, NULL));
	g_mutex_lock(&trinco);
	while (estado_nome == 0) {
		g_cond_wait(&pronto, &trinco);
	}
	int estado = estado_nome;
	g_mutex_unlock(&trinco);
	return estado > 0 ? fd_aviso : -1;
}

int avisos_ler(struct aviso_evento *eventos, int max) {
	uint64_t n;
	if (read(fd_aviso, &n, sizeof(n)) < 0) {
		/* nada por ler (não bloqueia) */
	}
	int i = 0;
	struct aviso_evento *ev;
	while (i < max && (ev = g_async_queue_try_pop(fila))) {
		eventos[i++] = *ev;
		g_free(ev);
	}
	if (g_async_queue_length(fila) > 0) {
		/* sobrou: que o ciclo volte cá */
		uint64_t um = 1;
		if (write(fd_aviso, &um, sizeof(um)) < 0) {
		}
	}
	return i;
}

void avisos_fechou(uint32_t id, uint32_t motivo) {
	if (ligacao) {
		g_dbus_connection_emit_signal(ligacao, NULL, CAMINHO_FDO, NOME_FDO, "NotificationClosed",
			g_variant_new("(uu)", id, motivo), NULL);
	}
}

void avisos_acao(uint32_t id, const char *chave) {
	if (ligacao) {
		g_dbus_connection_emit_signal(ligacao, NULL, CAMINHO_FDO, NOME_FDO, "ActionInvoked",
			g_variant_new("(us)", id, chave), NULL);
	}
}

bool avisos_pedir_centro(void) {
	GDBusConnection *c = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	if (!c) {
		return false;
	}
	GVariant *r = g_dbus_connection_call_sync(c, NOME_LUMNA, CAMINHO_LUMNA, NOME_LUMNA, "AlternarCentro", NULL, NULL,
		G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, NULL, NULL);
	if (r) {
		g_variant_unref(r);
	}
	g_object_unref(c);
	return r != NULL;
}
