#define _DEFAULT_SOURCE 1 /* explicit_bzero */
/*
 * lumna-polkit: o agente de autenticação do polkit da sessão. Registra-se no polkit (D-Bus do sistema) e, quando algo pede
 * senha de administrador (o pkexec do Painel, o Flatpak, a Loja...), mostra a janela do LUMNA (lumna-polkit-janela) e
 * confere a senha pelo helper do polkit (servicos/polkitag.h).
 *
 * A janela é um processo à parte: a senha chega por um pipe (nunca pela linha de comando nem pelo ambiente) e, se a
 * janela cair ou for fechada, o pedido é cancelado. Três tentativas e o pedido é recusado.
 *
 * Para os testes: LUMNA_POLKIT_BUS=sessao usa o D-Bus de sessão, LUMNA_POLKIT_JANELA troca o programa da janela e
 * XDG_SESSION_ID dá a sessão (senão pergunta ao logind).
 */
#include <gio/gio.h>
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "polkitag.h"

#define CAMINHO_AGENTE "/os/lumna/polkit/agent"
#define TENTATIVAS 3

static GDBusConnection *barramento;
static GMainLoop *laco;
static GHashTable *pedidos; /* cookie -> struct pedido (só a linha principal mexe) */

static const char *xml_agente =
	"<node><interface name='org.freedesktop.PolicyKit1.AuthenticationAgent'>"
	"<method name='BeginAuthentication'>"
	"<arg type='s' name='action_id' direction='in'/><arg type='s' name='message' direction='in'/>"
	"<arg type='s' name='icon_name' direction='in'/><arg type='a{ss}' name='details' direction='in'/>"
	"<arg type='s' name='cookie' direction='in'/><arg type='a(sa{sv})' name='identities' direction='in'/></method>"
	"<method name='CancelAuthentication'><arg type='s' name='cookie' direction='in'/></method>"
	"</interface></node>";

struct pedido {
	GDBusMethodInvocation *chamada;
	char *cookie, *mensagem, *acao, *usuario;
	GMutex trava;
	GPid janela;      /* o pid da janela de agora (0: não há), sob a trava */
	bool cancelado;   /* sob a trava */
	int resultado;    /* 0 autenticou, 1 cancelou, 2 falhou */
	char erro[300];
};

static void liberar_pedido(struct pedido *p) {
	g_mutex_clear(&p->trava);
	g_free(p->cookie);
	g_free(p->mensagem);
	g_free(p->acao);
	g_free(p->usuario);
	g_free(p);
}

/* mostra a janela e devolve a senha (g_free + explicit_bzero); NULL se cancelou */
static char *perguntar(struct pedido *p, const char *erro) {
	const char *programa = g_getenv("LUMNA_POLKIT_JANELA");
	char *argv[12];
	int n = 0;
	argv[n++] = (char *)(programa && *programa ? programa : "lumna-polkit-janela");
	argv[n++] = "--mensagem";
	argv[n++] = p->mensagem;
	argv[n++] = "--usuario";
	argv[n++] = p->usuario;
	if (erro) {
		argv[n++] = "--erro";
		argv[n++] = (char *)erro;
	}
	argv[n] = NULL;
	GPid pid = 0;
	int saida = -1;
	g_mutex_lock(&p->trava);
	bool ja_cancelado = p->cancelado;
	g_mutex_unlock(&p->trava);
	if (ja_cancelado) {
		return NULL;
	}
	GError *e = NULL;
	if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL, &saida, NULL, &e)) {
		g_snprintf(p->erro, sizeof(p->erro), "não abriu a janela de senha: %s", e->message);
		g_error_free(e);
		return NULL;
	}
	g_mutex_lock(&p->trava);
	p->janela = pid;
	bool cancelou_agora = p->cancelado;
	g_mutex_unlock(&p->trava);
	if (cancelou_agora) {
		kill(pid, SIGTERM);
	}
	GString *senha = g_string_new(NULL);
	char buf[256];
	ssize_t r;
	while ((r = read(saida, buf, sizeof(buf))) > 0) {
		g_string_append_len(senha, buf, r);
		if (senha->len > 1024) { /* senha desse tamanho não é senha */
			break;
		}
	}
	explicit_bzero(buf, sizeof(buf));
	close(saida);
	int estado = 0;
	waitpid(pid, &estado, 0);
	g_spawn_close_pid(pid);
	g_mutex_lock(&p->trava);
	p->janela = 0;
	bool cancelado = p->cancelado;
	g_mutex_unlock(&p->trava);
	/* a janela devolve a senha seguida de uma quebra de linha */
	char *fim = strchr(senha->str, '\n');
	if (fim) {
		*fim = '\0';
	}
	bool ok = !cancelado && WIFEXITED(estado) && WEXITSTATUS(estado) == 0 && senha->str[0];
	char *copia = ok ? g_strdup(senha->str) : NULL;
	explicit_bzero(senha->str, senha->allocated_len);
	g_string_free(senha, TRUE);
	return copia;
}

static gboolean terminar(gpointer dados) {
	struct pedido *p = dados;
	g_hash_table_remove(pedidos, p->cookie);
	if (p->resultado == 0) {
		g_dbus_method_invocation_return_value(p->chamada, NULL);
	} else if (p->resultado == 1) {
		g_dbus_method_invocation_return_dbus_error(p->chamada, "org.freedesktop.PolicyKit1.Error.Cancelled", "cancelado");
	} else {
		g_dbus_method_invocation_return_dbus_error(p->chamada, "org.freedesktop.PolicyKit1.Error.Failed", p->erro);
	}
	liberar_pedido(p);
	return G_SOURCE_REMOVE;
}

static gpointer trabalho(gpointer dados) {
	struct pedido *p = dados;
	const char *erro = NULL;
	char texto_erro[120];
	for (int t = 0; t < TENTATIVAS; t++) {
		char *senha = perguntar(p, erro);
		if (!senha) {
			p->resultado = p->erro[0] ? 2 : 1;
			g_main_context_invoke(NULL, terminar, p);
			return NULL;
		}
		char falha[300] = "";
		int r = polkitag_conferir(p->usuario, p->cookie, senha, falha, sizeof(falha));
		explicit_bzero(senha, strlen(senha));
		g_free(senha);
		if (r == POLKITAG_OK) {
			p->resultado = 0;
			g_main_context_invoke(NULL, terminar, p);
			return NULL;
		}
		if (r == POLKITAG_FALHA) {
			fprintf(stderr, "lumna-polkit: %s\n", falha);
			g_strlcpy(p->erro, falha, sizeof(p->erro));
			p->resultado = 2;
			g_main_context_invoke(NULL, terminar, p);
			return NULL;
		}
		g_snprintf(texto_erro, sizeof(texto_erro), "Senha incorreta. Tente de novo.");
		erro = texto_erro;
	}
	g_strlcpy(p->erro, "muitas tentativas", sizeof(p->erro));
	p->resultado = 2;
	g_main_context_invoke(NULL, terminar, p);
	return NULL;
}

/* o uid de quem roda (LUMNA_POLKIT_UID troca, para os testes) */
static uint32_t proprio_uid(void) {
	const char *env = g_getenv("LUMNA_POLKIT_UID");
	return env && *env ? (uint32_t)strtoul(env, NULL, 10) : (uint32_t)getuid();
}

static void chamada(GDBusConnection *con, const char *remetente, const char *caminho, const char *interface, const char *metodo,
	GVariant *parametros, GDBusMethodInvocation *inv, gpointer dados) {
	if (!strcmp(metodo, "CancelAuthentication")) {
		const char *cookie;
		g_variant_get(parametros, "(&s)", &cookie);
		struct pedido *p = g_hash_table_lookup(pedidos, cookie);
		if (p) {
			g_mutex_lock(&p->trava);
			p->cancelado = true;
			if (p->janela) {
				kill(p->janela, SIGTERM);
			}
			g_mutex_unlock(&p->trava);
		}
		g_dbus_method_invocation_return_value(inv, NULL);
		return;
	}
	const char *acao, *mensagem, *icone, *cookie;
	GVariantIter *identidades;
	g_variant_get(parametros, "(&s&s&sa{ss}&sa(sa{sv}))", &acao, &mensagem, &icone, NULL, &cookie, &identidades);
	uint32_t uids[16];
	int nuids = 0;
	const char *tipo;
	GVariant *detalhes;
	while (nuids < 16 && g_variant_iter_next(identidades, "(&s@a{sv})", &tipo, &detalhes)) {
		GVariant *v = g_variant_lookup_value(detalhes, "uid", NULL);
		if (!strcmp(tipo, "unix-user") && v && g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32)) {
			uids[nuids++] = g_variant_get_uint32(v);
		}
		if (v) {
			g_variant_unref(v);
		}
		g_variant_unref(detalhes);
	}
	g_variant_iter_free(identidades);
	uint32_t uid = 0;
	char *usuario = NULL;
	if (polkitag_escolher(uids, nuids, proprio_uid(), &uid)) {
		usuario = polkitag_nome(uid);
	}
	if (!usuario) {
		g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.PolicyKit1.Error.Failed", "nenhuma conta para autenticar");
		return;
	}
	struct pedido *p = g_new0(struct pedido, 1);
	g_mutex_init(&p->trava);
	p->chamada = inv;
	p->cookie = g_strdup(cookie);
	p->mensagem = g_strdup(mensagem);
	p->acao = g_strdup(acao);
	p->usuario = usuario;
	g_hash_table_replace(pedidos, p->cookie, p);
	g_thread_unref(g_thread_new("polkit-pedido", trabalho, p));
}

static char *sessao_do_logind(void) {
	const char *env = g_getenv("XDG_SESSION_ID");
	if (env && *env) {
		return g_strdup(env);
	}
	GError *e = NULL;
	GVariant *r = g_dbus_connection_call_sync(barramento, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
		"GetSessionByPID", g_variant_new("(u)", (guint32)getpid()), G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &e);
	if (!r) {
		fprintf(stderr, "lumna-polkit: logind: %s\n", e->message);
		g_error_free(e);
		return NULL;
	}
	const char *caminho;
	g_variant_get(r, "(&o)", &caminho);
	GVariant *id = g_dbus_connection_call_sync(barramento, "org.freedesktop.login1", caminho, "org.freedesktop.DBus.Properties", "Get",
		g_variant_new("(ss)", "org.freedesktop.login1.Session", "Id"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
	g_variant_unref(r);
	if (!id) {
		return NULL;
	}
	GVariant *v;
	g_variant_get(id, "(v)", &v);
	char *resultado = g_variant_dup_string(v, NULL);
	g_variant_unref(v);
	g_variant_unref(id);
	return resultado;
}

static void registrar(void) {
	char *sessao = sessao_do_logind();
	if (!sessao) {
		fprintf(stderr, "lumna-polkit: sem sessão do logind: o agente não se registra\n");
		return;
	}
	const char *lang = g_getenv("LANG");
	GVariantBuilder detalhes;
	g_variant_builder_init(&detalhes, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&detalhes, "{sv}", "session-id", g_variant_new_string(sessao));
	GError *e = NULL;
	GVariant *r = g_dbus_connection_call_sync(barramento, "org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority",
		"org.freedesktop.PolicyKit1.Authority", "RegisterAuthenticationAgent",
		g_variant_new("((sa{sv})ss)", "unix-session", &detalhes, lang && *lang ? lang : "pt_BR.UTF-8", CAMINHO_AGENTE), NULL,
		G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &e);
	if (r) {
		g_variant_unref(r);
		fprintf(stderr, "lumna-polkit: agente registrado (sessão %s)\n", sessao);
	} else {
		fprintf(stderr, "lumna-polkit: polkit: %s\n", e->message);
		g_error_free(e);
	}
	g_free(sessao);
}

/* o polkit pode subir depois, ou reiniciar: registra sempre que o nome dele aparecer */
static void polkit_apareceu(GDBusConnection *con, const char *nome, const char *dono, gpointer dados) {
	registrar();
}

static void polkit_sumiu(GDBusConnection *con, const char *nome, gpointer dados) {
	fprintf(stderr, "lumna-polkit: o polkit não está no ar; registro de novo quando voltar\n");
}

static void ao_terminar(int sinal) {
	if (laco) {
		g_main_loop_quit(laco);
	}
}

int main(void) {
	GError *e = NULL;
	const char *barra = g_getenv("LUMNA_POLKIT_BUS");
	barramento = g_bus_get_sync(barra && !strcmp(barra, "sessao") ? G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM, NULL, &e);
	if (!barramento) {
		fprintf(stderr, "lumna-polkit: D-Bus: %s\n", e->message);
		return 1;
	}
	pedidos = g_hash_table_new(g_str_hash, g_str_equal);
	GDBusNodeInfo *no = g_dbus_node_info_new_for_xml(xml_agente, NULL);
	static const GDBusInterfaceVTable vtable = { .method_call = chamada };
	if (!g_dbus_connection_register_object(barramento, CAMINHO_AGENTE, no->interfaces[0], &vtable, NULL, NULL, &e)) {
		fprintf(stderr, "lumna-polkit: %s\n", e->message);
		return 1;
	}
	g_bus_watch_name_on_connection(barramento, "org.freedesktop.PolicyKit1", G_BUS_NAME_WATCHER_FLAGS_NONE, polkit_apareceu, polkit_sumiu, NULL, NULL);
	laco = g_main_loop_new(NULL, FALSE);
	signal(SIGTERM, ao_terminar);
	signal(SIGINT, ao_terminar);
	g_main_loop_run(laco);
	return 0;
}
