#define _GNU_SOURCE
#include "firefox.h"

#include <gio/gio.h>
#include <signal.h>
#include <libsoup/soup.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "json.h"
#include "midia.h"

#define PORTA_BIDI 47821
#define TENTATIVAS_LIGAR 80      /* x 250 ms: o Firefox frio demora */
#define SALVAR_A_CADA_S 20

static GMutex trinco;
static struct ff_estado estado; /* o que a app lê */
static int fd_aviso = -1;

/* tudo abaixo roda na linha do Firefox */
static GMainContext *ctx;
static GPid pid;
static SoupSession *sessao;
static SoupWebsocketConnection *ws;
static GHashTable *pendentes; /* id -> struct rpc */
static int proximo_id = 1;
static int tentativas;
static bool conectando;
static GPtrArray *fila; /* ações que chegaram antes do controle ligar: struct acao */

struct rpc {
	void (*fim)(const struct json *resultado, gpointer dados);
	gpointer dados;
};

struct acao {
	enum { ACAO_ABRIR, ACAO_ATIVAR, ACAO_FECHAR, ACAO_SALVAR } tipo;
	char servico[32];
	char url[1024];
	bool miniatura;
};

static void avisar(void) {
	uint64_t um = 1;
	if (write(fd_aviso, &um, sizeof(um)) < 0) {
	}
}

static struct ff_aba *aba_do_servico(const char *servico) {
	for (int i = 0; i < estado.n; i++) {
		if (!strcmp(estado.abas[i].servico, servico)) {
			return &estado.abas[i];
		}
	}
	return NULL;
}

static struct ff_aba *aba_do_contexto(const char *contexto) {
	for (int i = 0; i < estado.n; i++) {
		if (!strcmp(estado.abas[i].contexto, contexto)) {
			return &estado.abas[i];
		}
	}
	return NULL;
}

/* ---- o perfil ---- */

static const char *const prefs =
	"// Gerado pelo LUMNA: o Firefox da Mídia. Mudanças aqui se perdem na próxima vez que abrir.\n"
	"user_pref(\"browser.shell.checkDefaultBrowser\", false);\n"
	"user_pref(\"browser.startup.homepage\", \"about:blank\");\n"
	"user_pref(\"browser.startup.page\", 0);\n"
	"user_pref(\"browser.aboutwelcome.enabled\", false);\n"
	"user_pref(\"startup.homepage_welcome_url\", \"\");\n"
	"user_pref(\"browser.tabs.warnOnClose\", false);\n"
	"user_pref(\"browser.warnOnQuit\", false);\n"
	"user_pref(\"browser.sessionstore.resume_from_crash\", false);\n"
	"user_pref(\"browser.tabs.closeWindowWithLastTab\", false);\n"
	"user_pref(\"browser.link.open_newwindow\", 3);\n"
	"user_pref(\"datareporting.policy.dataSubmissionEnabled\", false);\n"
	"user_pref(\"datareporting.healthreport.uploadEnabled\", false);\n"
	"user_pref(\"toolkit.telemetry.enabled\", false);\n"
	"user_pref(\"app.update.auto\", false);\n"
	"user_pref(\"app.update.enabled\", false);\n"
	"user_pref(\"dom.webnotifications.enabled\", false);\n"
	"user_pref(\"dom.push.enabled\", false);\n"
	"user_pref(\"permissions.default.desktop-notification\", 2);\n"
	"user_pref(\"media.autoplay.default\", 0);\n"
	"user_pref(\"media.autoplay.blocking_policy\", 0);\n"
	"user_pref(\"media.eme.enabled\", true);\n"
	"user_pref(\"media.gmp-widevinecdm.visible\", true);\n"
	"user_pref(\"media.gmp-widevinecdm.enabled\", true);\n"
	"user_pref(\"browser.eme.ui.enabled\", false);\n"
	"user_pref(\"media.hardwaremediakeys.enabled\", true);\n"
	"user_pref(\"dom.media.mediasession.enabled\", true);\n"
	"user_pref(\"media.ffmpeg.vaapi.enabled\", true);\n"
	"user_pref(\"full-screen-api.warning.timeout\", 0);\n"
	"user_pref(\"full-screen-api.transition-duration.enter\", \"0 0\");\n"
	"user_pref(\"full-screen-api.transition-duration.leave\", \"0 0\");\n"
	"user_pref(\"ui.key.menuAccessKeyFocuses\", false);\n"
	"user_pref(\"remote.active-protocols\", 1);\n"
	"user_pref(\"remote.log.level\", \"Warn\");\n";

static char *pasta_perfil(void) {
	char *dados = midia_pasta_dados();
	char *p = g_build_filename(dados, "perfil", NULL);
	g_mkdir_with_parents(p, 0700);
	char *u = g_build_filename(p, "user.js", NULL);
	GError *erro = NULL;
	g_file_set_contents(u, prefs, -1, &erro);
	g_clear_error(&erro);
	g_free(u);
	g_free(dados);
	return p;
}

/* ---- BiDi ---- */

static void enviar_rpc(const char *metodo, const char *params, void (*fim)(const struct json *, gpointer), gpointer dados) {
	if (!ws || soup_websocket_connection_get_state(ws) != SOUP_WEBSOCKET_STATE_OPEN) {
		return;
	}
	int id = proximo_id++;
	if (fim) {
		struct rpc *r = g_new0(struct rpc, 1);
		r->fim = fim;
		r->dados = dados;
		g_hash_table_insert(pendentes, GINT_TO_POINTER(id), r);
	}
	char *m = g_strdup_printf("{\"id\":%d,\"method\":\"%s\",\"params\":%s}", id, metodo, params ? params : "{}");
	soup_websocket_connection_send_text(ws, m);
	g_free(m);
}

static char *texto_json(const char *t) {
	GString *s = g_string_new(NULL);
	json_escrever_texto(s, t);
	return g_string_free(s, FALSE);
}

static void ativar_no_estado(const char *servico) {
	g_mutex_lock(&trinco);
	for (int i = 0; i < estado.n; i++) {
		estado.abas[i].ativa = !strcmp(estado.abas[i].servico, servico);
	}
	g_strlcpy(estado.ativo, servico, sizeof(estado.ativo));
	g_mutex_unlock(&trinco);
	avisar();
}

/* a miniatura: a página como está (vídeo protegido sai preto: o Firefox não entrega esses quadros) */
struct salvar {
	char servico[32];
	bool miniatura;
};

static void salvar_gravar(struct salvar *s, const char *arquivo) {
	g_mutex_lock(&trinco);
	struct ff_aba *a = aba_do_servico(s->servico);
	struct midia_recente r;
	memset(&r, 0, sizeof(r));
	bool tem = a && a->url[0] && strcmp(a->url, "about:blank") != 0;
	if (tem) {
		g_strlcpy(r.servico, s->servico, sizeof(r.servico));
		g_strlcpy(r.url, a->url, sizeof(r.url));
		g_strlcpy(r.titulo, a->titulo, sizeof(r.titulo));
	}
	g_mutex_unlock(&trinco);
	if (tem) {
		if (arquivo) {
			g_strlcpy(r.miniatura, arquivo, sizeof(r.miniatura));
		} else {
			struct midia_recente antigos[MIDIA_MAX_RECENTES];
			int n = midia_recentes_ler(antigos, MIDIA_MAX_RECENTES);
			for (int i = 0; i < n; i++) {
				if (!strcmp(antigos[i].servico, s->servico)) {
					g_strlcpy(r.miniatura, antigos[i].miniatura, sizeof(r.miniatura)); /* a de antes continua valendo */
				}
			}
		}
		midia_recente_gravar(&r);
	}
	g_free(s);
}

static void ao_capturar(const struct json *res, gpointer dados) {
	struct salvar *s = dados;
	const char *b64 = json_chave_texto(res, "data");
	char *arquivo = NULL;
	if (*b64) {
		gsize n;
		guchar *bytes = g_base64_decode(b64, &n);
		char *dir = midia_pasta_dados();
		char *mini = g_build_filename(dir, "miniaturas", NULL);
		g_mkdir_with_parents(mini, 0700);
		arquivo = g_strdup_printf("%s/%s.jpg", mini, s->servico);
		GError *erro = NULL;
		if (!g_file_set_contents(arquivo, (const char *)bytes, (gssize)n, &erro)) {
			g_free(arquivo);
			arquivo = NULL;
		}
		g_clear_error(&erro);
		g_free(mini);
		g_free(dir);
		g_free(bytes);
	}
	salvar_gravar(s, arquivo);
	g_free(arquivo);
}

/* a URL e o título de cada aba, depois o que se pedir */
struct atualizacao {
	void (*fim)(gpointer);
	gpointer dados;
	int faltam;
};

static void atualizacao_um(struct atualizacao *a) {
	if (--a->faltam <= 0) {
		if (a->fim) {
			a->fim(a->dados);
		}
		g_free(a);
	}
}

struct titulo_pedido {
	struct atualizacao *a;
	char contexto[64];
};

static void ao_titulo(const struct json *res, gpointer dados) {
	struct titulo_pedido *t = dados;
	const char *valor = json_chave_texto(json_chave(res, "result"), "value");
	g_mutex_lock(&trinco);
	struct ff_aba *aba = aba_do_contexto(t->contexto);
	if (aba && *valor) {
		g_strlcpy(aba->titulo, valor, sizeof(aba->titulo));
	}
	g_mutex_unlock(&trinco);
	atualizacao_um(t->a);
	g_free(t);
}

static void ao_arvore(const struct json *res, gpointer dados) {
	struct atualizacao *a = dados;
	const struct json *ctxs = json_chave(res, "contexts");
	int pedir = 0;
	struct titulo_pedido *ts[FF_MAX_ABAS];
	g_mutex_lock(&trinco);
	for (int i = 0; ctxs && i < ctxs->nfilhos; i++) {
		struct ff_aba *aba = aba_do_contexto(json_chave_texto(ctxs->filhos[i], "context"));
		if (!aba) {
			continue;
		}
		g_strlcpy(aba->url, json_chave_texto(ctxs->filhos[i], "url"), sizeof(aba->url));
		if (pedir < FF_MAX_ABAS) {
			struct titulo_pedido *t = g_new0(struct titulo_pedido, 1);
			t->a = a;
			g_strlcpy(t->contexto, aba->contexto, sizeof(t->contexto));
			ts[pedir++] = t;
		}
	}
	g_mutex_unlock(&trinco);
	a->faltam = pedir + 1; /* +1: este passo */
	for (int i = 0; i < pedir; i++) {
		char *c = texto_json(ts[i]->contexto);
		char *p = g_strdup_printf("{\"expression\":\"document.title\",\"awaitPromise\":false,\"target\":{\"context\":%s}}", c);
		enviar_rpc("script.evaluate", p, ao_titulo, ts[i]);
		g_free(p);
		g_free(c);
	}
	atualizacao_um(a);
}

static void atualizar(void (*fim)(gpointer), gpointer dados) {
	struct atualizacao *a = g_new0(struct atualizacao, 1);
	a->fim = fim;
	a->dados = dados;
	a->faltam = 1;
	enviar_rpc("browsingContext.getTree", "{}", ao_arvore, a);
}

static void salvar_depois_de_atualizar(gpointer dados) {
	struct salvar *s = dados;
	g_mutex_lock(&trinco);
	struct ff_aba *a = aba_do_servico(s->servico);
	char contexto[64] = "";
	if (a) {
		g_strlcpy(contexto, a->contexto, sizeof(contexto));
	}
	g_mutex_unlock(&trinco);
	if (!*contexto) {
		g_free(s);
		return;
	}
	if (s->miniatura) {
		char *c = texto_json(contexto);
		char *p = g_strdup_printf("{\"context\":%s,\"origin\":\"viewport\",\"format\":{\"type\":\"image/jpeg\",\"quality\":0.6}}", c);
		enviar_rpc("browsingContext.captureScreenshot", p, ao_capturar, s);
		g_free(p);
		g_free(c);
	} else {
		salvar_gravar(s, NULL);
	}
}

static void salvar_agora(const char *servico, bool miniatura) {
	struct salvar *s = g_new0(struct salvar, 1);
	g_strlcpy(s->servico, servico, sizeof(s->servico));
	s->miniatura = miniatura;
	atualizar(salvar_depois_de_atualizar, s);
}

static gboolean salvar_periodico(gpointer dados) {
	char ativo[32];
	g_mutex_lock(&trinco);
	g_strlcpy(ativo, estado.ativo, sizeof(ativo));
	g_mutex_unlock(&trinco);
	if (ativo[0] && ws) {
		salvar_agora(ativo, false);
	}
	return G_SOURCE_CONTINUE;
}

/* abrir: a aba nova, depois o endereço */
struct abrir {
	char servico[32];
	char url[1024];
};

static void navegar(const char *contexto, const char *url) {
	char *c = texto_json(contexto), *u = texto_json(url);
	char *p = g_strdup_printf("{\"context\":%s,\"url\":%s,\"wait\":\"none\"}", c, u);
	enviar_rpc("browsingContext.navigate", p, NULL, NULL);
	g_free(p);
	g_free(u);
	g_free(c);
}

static void ativar_contexto(const char *contexto) {
	char *c = texto_json(contexto);
	char *p = g_strdup_printf("{\"context\":%s}", c);
	enviar_rpc("browsingContext.activate", p, NULL, NULL);
	g_free(p);
	g_free(c);
}

static void ao_criar(const struct json *res, gpointer dados) {
	struct abrir *ab = dados;
	const char *contexto = json_chave_texto(res, "context");
	if (*contexto && estado.n < FF_MAX_ABAS) {
		g_mutex_lock(&trinco);
		struct ff_aba *a = &estado.abas[estado.n++];
		memset(a, 0, sizeof(*a));
		g_strlcpy(a->servico, ab->servico, sizeof(a->servico));
		g_strlcpy(a->contexto, contexto, sizeof(a->contexto));
		g_strlcpy(a->url, ab->url, sizeof(a->url));
		a->carregando = true;
		g_mutex_unlock(&trinco);
		ativar_no_estado(ab->servico);
		navegar(contexto, ab->url);
	}
	g_free(ab);
}

static void executar(const struct acao *a) {
	struct ff_aba *aba = aba_do_servico(a->servico);
	switch (a->tipo) {
	case ACAO_ABRIR:
	case ACAO_ATIVAR:
		if (aba) {
			ativar_contexto(aba->contexto);
			ativar_no_estado(a->servico);
		} else if (a->tipo == ACAO_ABRIR && estado.n < FF_MAX_ABAS) {
			struct abrir *ab = g_new0(struct abrir, 1);
			g_strlcpy(ab->servico, a->servico, sizeof(ab->servico));
			g_strlcpy(ab->url, a->url, sizeof(ab->url));
			enviar_rpc("browsingContext.create", "{\"type\":\"tab\"}", ao_criar, ab);
		}
		break;
	case ACAO_FECHAR:
		if (aba) {
			char *c = texto_json(aba->contexto);
			char *p = g_strdup_printf("{\"context\":%s}", c);
			enviar_rpc("browsingContext.close", p, NULL, NULL);
			g_free(p);
			g_free(c);
		}
		break;
	case ACAO_SALVAR:
		if (aba) {
			salvar_agora(a->servico, a->miniatura);
		}
		break;
	}
}

static void evento(const char *metodo, const struct json *p) {
	const char *contexto = json_chave_texto(p, "context");
	if (!strcmp(metodo, "browsingContext.navigationStarted") || !strcmp(metodo, "browsingContext.load")) {
		g_mutex_lock(&trinco);
		struct ff_aba *a = aba_do_contexto(contexto);
		if (a) {
			a->carregando = !strcmp(metodo, "browsingContext.navigationStarted");
			const char *url = json_chave_texto(p, "url");
			if (*url) {
				g_strlcpy(a->url, url, sizeof(a->url));
			}
		}
		g_mutex_unlock(&trinco);
		avisar();
	} else if (!strcmp(metodo, "browsingContext.contextDestroyed")) {
		g_mutex_lock(&trinco);
		for (int i = 0; i < estado.n; i++) {
			if (!strcmp(estado.abas[i].contexto, contexto)) {
				if (estado.abas[i].ativa) {
					estado.ativo[0] = '\0';
				}
				memmove(&estado.abas[i], &estado.abas[i + 1], (size_t)(estado.n - i - 1) * sizeof(estado.abas[0]));
				estado.n--;
				break;
			}
		}
		g_mutex_unlock(&trinco);
		avisar();
	}
}

static void ao_mensagem(SoupWebsocketConnection *c, gint tipo, GBytes *bytes, gpointer dados) {
	if (tipo != SOUP_WEBSOCKET_DATA_TEXT) {
		return;
	}
	gsize n;
	const char *t = g_bytes_get_data(bytes, &n);
	char *copia = g_strndup(t, n);
	struct json *j = json_ler(copia);
	g_free(copia);
	const char *tp = json_chave_texto(j, "type");
	if (!strcmp(tp, "event")) {
		evento(json_chave_texto(j, "method"), json_chave(j, "params"));
	} else if (!strcmp(tp, "success") || !strcmp(tp, "error")) {
		gpointer id = GINT_TO_POINTER((int)json_numero(json_chave(j, "id"), 0));
		struct rpc *r = g_hash_table_lookup(pendentes, id);
		if (r) {
			g_hash_table_steal(pendentes, id);
			if (r->fim && !strcmp(tp, "success")) {
				r->fim(json_chave(j, "result"), r->dados);
			}
			g_free(r);
		}
	}
	json_liberar(j);
}

static void desligar(void) {
	if (ws) {
		g_object_unref(ws);
		ws = NULL;
	}
	g_hash_table_remove_all(pendentes);
	g_mutex_lock(&trinco);
	estado.ligado = false;
	estado.n = 0;
	estado.ativo[0] = '\0';
	g_mutex_unlock(&trinco);
	avisar();
}

static void ao_fechar_ws(SoupWebsocketConnection *c, gpointer dados) {
	if (c == ws) {
		desligar();
	}
}

static void ao_assinar(const struct json *res, gpointer dados) {
	/* o que já estava aberto (a aba em branco do começo) não é de ninguém: fica de reserva */
}

static void ao_sessao(const struct json *res, gpointer dados) {
	enviar_rpc("session.subscribe",
		"{\"events\":[\"browsingContext.navigationStarted\",\"browsingContext.load\",\"browsingContext.contextDestroyed\"]}", ao_assinar, NULL);
	g_mutex_lock(&trinco);
	estado.ligado = true;
	g_mutex_unlock(&trinco);
	avisar();
	/* as ações que esperavam */
	for (guint i = 0; fila && i < fila->len; i++) {
		executar(fila->pdata[i]);
	}
	if (fila) {
		g_ptr_array_set_size(fila, 0);
	}
}

static void ao_conectar(GObject *origem, GAsyncResult *r, gpointer dados);

static gboolean tentar_ligar(gpointer dados) {
	if (ws || conectando) {
		return G_SOURCE_REMOVE;
	}
	char *url = g_strdup_printf("ws://127.0.0.1:%d/session", PORTA_BIDI);
	SoupMessage *m = soup_message_new("GET", url);
	g_free(url);
	conectando = true;
	soup_session_websocket_connect_async(sessao, m, NULL, NULL, G_PRIORITY_DEFAULT, NULL, ao_conectar, NULL);
	g_object_unref(m);
	return G_SOURCE_REMOVE;
}

static void ao_conectar(GObject *origem, GAsyncResult *r, gpointer dados) {
	GError *erro = NULL;
	SoupWebsocketConnection *c = soup_session_websocket_connect_finish(SOUP_SESSION(origem), r, &erro);
	conectando = false;
	g_clear_error(&erro);
	if (!c) {
		bool vivo;
		g_mutex_lock(&trinco);
		vivo = estado.lancado;
		g_mutex_unlock(&trinco);
		if (vivo && ++tentativas < TENTATIVAS_LIGAR) {
			GSource *s = g_timeout_source_new(250);
			g_source_set_callback(s, tentar_ligar, NULL, NULL);
			g_source_attach(s, ctx);
			g_source_unref(s);
		}
		return;
	}
	ws = c;
	soup_websocket_connection_set_max_incoming_payload_size(ws, 32 * 1024 * 1024); /* a miniatura vem em base64 */
	g_signal_connect(ws, "message", G_CALLBACK(ao_mensagem), NULL);
	g_signal_connect(ws, "closed", G_CALLBACK(ao_fechar_ws), NULL);
	enviar_rpc("session.new", "{\"capabilities\":{}}", ao_sessao, NULL);
}

/* ---- o processo ---- */

static void ao_sair(GPid p, gint status, gpointer dados) {
	g_spawn_close_pid(p);
	pid = 0;
	g_mutex_lock(&trinco);
	estado.lancado = false;
	g_mutex_unlock(&trinco);
	if (ws) {
		soup_websocket_connection_close(ws, SOUP_WEBSOCKET_CLOSE_NORMAL, NULL);
	}
	desligar();
}

static bool lancar(void) {
	if (pid) {
		return true;
	}
	char *perfil = pasta_perfil();
	const char *programa = g_getenv("LUMNA_FIREFOX");
	char *porta = g_strdup_printf("--remote-debugging-port=%d", PORTA_BIDI);
	char *argv[] = { (char *)(programa && *programa ? programa : "firefox"), "--kiosk", "--no-remote", "--profile", perfil, "--name", FF_APP_ID,
		"--class", FF_APP_ID, porta, "about:blank", NULL };
	GError *erro = NULL;
	bool ok = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
		NULL, NULL, &pid, &erro);
	g_clear_error(&erro);
	g_free(porta);
	g_free(perfil);
	if (!ok) {
		pid = 0;
		return false;
	}
	GSource *w = g_child_watch_source_new(pid);
	g_source_set_callback(w, (GSourceFunc)(void (*)(void))ao_sair, NULL, NULL);
	g_source_attach(w, ctx);
	g_source_unref(w);
	g_mutex_lock(&trinco);
	estado.lancado = true;
	g_mutex_unlock(&trinco);
	tentativas = 0;
	GSource *s = g_timeout_source_new(250);
	g_source_set_callback(s, tentar_ligar, NULL, NULL);
	g_source_attach(s, ctx);
	g_source_unref(s);
	avisar();
	return true;
}

/* ---- as chamadas da app ---- */

static gboolean na_linha(gpointer dados) {
	struct acao *a = dados;
	if (!lancar()) {
		g_free(a);
		return G_SOURCE_REMOVE;
	}
	bool ligado;
	g_mutex_lock(&trinco);
	ligado = estado.ligado;
	g_mutex_unlock(&trinco);
	if (ligado) {
		executar(a);
		g_free(a);
	} else {
		g_ptr_array_add(fila, a); /* o controle ainda está subindo */
	}
	return G_SOURCE_REMOVE;
}

static void pedir(int tipo, const char *servico, const char *url, bool miniatura) {
	if (!ctx) {
		return;
	}
	struct acao *a = g_new0(struct acao, 1);
	a->tipo = tipo;
	g_strlcpy(a->servico, servico, sizeof(a->servico));
	if (url) {
		g_strlcpy(a->url, url, sizeof(a->url));
	}
	a->miniatura = miniatura;
	/* salvar, ativar e fechar sem Firefox não fazem nada: não vale subir um só para isso */
	if (tipo != ACAO_ABRIR) {
		bool vivo;
		g_mutex_lock(&trinco);
		vivo = estado.lancado;
		g_mutex_unlock(&trinco);
		if (!vivo) {
			g_free(a);
			return;
		}
	}
	g_main_context_invoke(ctx, na_linha, a);
}

void ff_abrir(const char *servico, const char *url) {
	pedir(ACAO_ABRIR, servico, url, false);
}

void ff_ativar(const char *servico) {
	pedir(ACAO_ATIVAR, servico, NULL, false);
}

void ff_fechar(const char *servico) {
	pedir(ACAO_FECHAR, servico, NULL, false);
}

void ff_salvar(const char *servico, bool miniatura) {
	pedir(ACAO_SALVAR, servico, NULL, miniatura);
}

void ff_ler(struct ff_estado *e) {
	uint64_t n;
	if (read(fd_aviso, &n, sizeof(n)) < 0) {
	}
	g_mutex_lock(&trinco);
	*e = estado;
	g_mutex_unlock(&trinco);
}

void ff_encerrar(void) {
	if (pid) {
		kill(pid, SIGTERM);
	}
}

static gpointer linha(gpointer dados) {
	g_main_context_push_thread_default(ctx);
	sessao = soup_session_new();
	pendentes = g_hash_table_new(g_direct_hash, g_direct_equal);
	fila = g_ptr_array_new_with_free_func(g_free);
	GSource *s = g_timeout_source_new_seconds(SALVAR_A_CADA_S);
	g_source_set_callback(s, salvar_periodico, NULL, NULL);
	g_source_attach(s, ctx);
	g_source_unref(s);
	g_main_loop_run(g_main_loop_new(ctx, FALSE));
	return NULL;
}

int ff_iniciar(void) {
	if (ctx) {
		return fd_aviso;
	}
	fd_aviso = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (fd_aviso < 0) {
		return -1;
	}
	ctx = g_main_context_new();
	g_thread_unref(g_thread_new("firefox", linha, NULL));
	return fd_aviso;
}
