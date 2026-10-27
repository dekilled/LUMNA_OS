#define _GNU_SOURCE
#include "firefox.h"

#include <gio/gio.h>
#include <libsoup/soup.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "json.h"
#include "midia.h"

#define PORTA_EXT 47822
#define EXTENSAO_ID "lumna-midia@lumna.os"
#define SALVAR_A_CADA_S 20

static GMutex trinco;
static struct ff_estado estado; /* o que a app lê */
static int fd_aviso = -1;

/* tudo abaixo roda na linha do Firefox */
static GMainContext *ctx;
static GPid pid;
static SoupServer *servidor;
static SoupWebsocketConnection *ws; /* a extensão */
static GHashTable *pendentes;       /* id -> struct rpc */
static int proximo_id = 1;
static char uuid[48];               /* o moz-extension://UUID da nossa extensão: só ela fala com o servidor */
static GPtrArray *fila;             /* ações que chegaram antes da extensão ligar: struct acao */

struct rpc {
	void (*fim)(const struct json *resposta, gpointer dados);
	gpointer dados;
};

struct acao {
	enum { ACAO_ABRIR, ACAO_IR, ACAO_ATIVAR, ACAO_FECHAR, ACAO_SALVAR } tipo;
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

/* ---- o perfil e a extensão ---- */

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
	/* a extensão do perfil liga sozinha (sem perguntar) e não precisa de assinatura no Developer Edition, ESR e Nightly */
	"user_pref(\"extensions.autoDisableScopes\", 0);\n"
	"user_pref(\"extensions.enabledScopes\", 15);\n"
	"user_pref(\"xpinstall.signatures.required\", false);\n"
	"user_pref(\"extensions.update.enabled\", false);\n";

/* a pasta onde está este programa (a extensão do repositório fica ao lado) */
static char *pasta_do_programa(void) {
	char exe[1024];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n <= 0) {
		return g_strdup(".");
	}
	exe[n] = '\0';
	return g_path_get_dirname(exe);
}

/* o UUID fixo da extensão neste perfil: assim o Origin dela (moz-extension://UUID) se sabe de antemão */
static void uuid_da_extensao(const char *dados) {
	char *caminho = g_build_filename(dados, "extensao-uuid", NULL), *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL) && strlen(g_strstrip(texto)) == 36) {
		g_strlcpy(uuid, texto, sizeof(uuid));
	} else {
		char *novo = g_uuid_string_random();
		g_strlcpy(uuid, novo, sizeof(uuid));
		g_free(novo);
		g_file_set_contents(caminho, uuid, -1, NULL);
	}
	g_free(texto);
	g_free(caminho);
}

/* põe a extensão no perfil: o .xpi assinado, ou (Developer Edition, ESR, Nightly) a pasta, por um arquivo-ponteiro */
static bool instalar_extensao(const char *perfil) {
	char *exe = pasta_do_programa();
	const char *env = g_getenv("LUMNA_EXTENSAO");
	char *candidatos[6] = { NULL };
	int n = 0;
	if (env && *env) {
		candidatos[n++] = g_strdup(env);
	}
	candidatos[n++] = g_strdup("/usr/share/lumna/midia/lumna-midia.xpi");
	candidatos[n++] = g_build_filename(exe, "extensao-firefox", "lumna-midia.xpi", NULL);
	candidatos[n++] = g_build_filename(exe, "extensao-firefox", NULL);
	g_free(exe);
	char *pasta_ext = g_build_filename(perfil, "extensions", NULL);
	g_mkdir_with_parents(pasta_ext, 0700);
	bool ok = false;
	for (int i = 0; i < n && !ok; i++) {
		if (g_file_test(candidatos[i], G_FILE_TEST_IS_DIR)) {
			char *manifesto = g_build_filename(candidatos[i], "manifest.json", NULL);
			if (g_file_test(manifesto, G_FILE_TEST_IS_REGULAR)) {
				char *alvo = g_build_filename(pasta_ext, EXTENSAO_ID, NULL);
				char *real = realpath(candidatos[i], NULL);
				unlink(alvo);
				ok = real && g_file_set_contents(alvo, real, -1, NULL); /* o arquivo-ponteiro: o texto é o caminho da pasta */
				free(real);
				g_free(alvo);
			}
			g_free(manifesto);
		} else if (g_file_test(candidatos[i], G_FILE_TEST_IS_REGULAR)) {
			char *alvo = g_strdup_printf("%s/%s.xpi", pasta_ext, EXTENSAO_ID);
			GFile *de = g_file_new_for_path(candidatos[i]), *para = g_file_new_for_path(alvo);
			unlink(alvo);
			ok = g_file_copy(de, para, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
			g_object_unref(de);
			g_object_unref(para);
			g_free(alvo);
		}
	}
	for (int i = 0; i < n; i++) {
		g_free(candidatos[i]);
	}
	g_free(pasta_ext);
	return ok;
}

static char *pasta_perfil(void) {
	char *dados = midia_pasta_dados();
	char *p = g_build_filename(dados, "perfil", NULL);
	g_mkdir_with_parents(p, 0700);
	uuid_da_extensao(dados);
	GString *u = g_string_new(prefs);
	g_string_append_printf(u, "user_pref(\"extensions.webextensions.uuids\", \"{\\\"%s\\\":\\\"%s\\\"}\");\n", EXTENSAO_ID, uuid);
	char *arquivo = g_build_filename(p, "user.js", NULL);
	g_file_set_contents(arquivo, u->str, (gssize)u->len, NULL);
	g_free(arquivo);
	g_string_free(u, TRUE);
	bool achou = instalar_extensao(p);
	g_mutex_lock(&trinco);
	estado.sem_extensao = !achou;
	g_mutex_unlock(&trinco);
	if (!achou) {
		fprintf(stderr, "lumna-midia: não achei a extensão do Firefox (extensao-firefox/ ou $LUMNA_EXTENSAO): o Firefox abre, mas sem controle\n");
	}
	g_free(dados);
	return p;
}

/* ---- a conversa com a extensão ---- */

static void enviar_cmd(const char *cmd, const char *servico, const char *url, void (*fim)(const struct json *, gpointer), gpointer dados) {
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
	GString *m = g_string_new(NULL);
	g_string_append_printf(m, "{\"id\":%d,\"cmd\":\"%s\",\"servico\":", id, cmd);
	json_escrever_texto(m, servico);
	if (url) {
		g_string_append(m, ",\"url\":");
		json_escrever_texto(m, url);
	}
	g_string_append_c(m, '}');
	soup_websocket_connection_send_text(ws, m->str);
	g_string_free(m, TRUE);
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

static void ao_capturar(const struct json *resp, gpointer dados) {
	struct salvar *s = dados;
	const char *url_dados = json_chave_texto(resp, "dados"); /* "data:image/jpeg;base64,...." */
	const char *b64 = strchr(url_dados, ',');
	char *arquivo = NULL;
	if (b64 && json_bool(json_chave(resp, "ok"), false)) {
		gsize n;
		guchar *bytes = g_base64_decode(b64 + 1, &n);
		char *dir = midia_pasta_dados();
		char *mini = g_build_filename(dir, "miniaturas", NULL);
		g_mkdir_with_parents(mini, 0700);
		arquivo = g_strdup_printf("%s/%s.jpg", mini, s->servico);
		if (!g_file_set_contents(arquivo, (const char *)bytes, (gssize)n, NULL)) {
			g_free(arquivo);
			arquivo = NULL;
		}
		g_free(mini);
		g_free(dir);
		g_free(bytes);
	}
	salvar_gravar(s, arquivo);
	g_free(arquivo);
}

static void salvar_agora(const char *servico, bool miniatura) {
	struct salvar *s = g_new0(struct salvar, 1);
	g_strlcpy(s->servico, servico, sizeof(s->servico));
	if (miniatura) {
		enviar_cmd("capturar", servico, NULL, ao_capturar, s);
	} else {
		salvar_gravar(s, NULL); /* o endereço e o título já vêm atualizados pelos avisos da extensão */
	}
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

static void executar(const struct acao *a) {
	g_mutex_lock(&trinco);
	bool existe = aba_do_servico(a->servico) != NULL;
	g_mutex_unlock(&trinco);
	switch (a->tipo) {
	case ACAO_ABRIR:
	case ACAO_IR:
		if (!existe) {
			enviar_cmd("abrir", a->servico, a->url, NULL, NULL);
		} else if (a->tipo == ACAO_IR) {
			enviar_cmd("ir", a->servico, a->url, NULL, NULL); /* a aba vai a outro endereço */
		} else {
			enviar_cmd("ativar", a->servico, NULL, NULL, NULL); /* já está viva: só vem para a frente */
		}
		ativar_no_estado(a->servico);
		break;
	case ACAO_ATIVAR:
		if (existe) {
			enviar_cmd("ativar", a->servico, NULL, NULL, NULL);
			ativar_no_estado(a->servico);
		}
		break;
	case ACAO_FECHAR:
		if (existe) {
			enviar_cmd("fechar", a->servico, NULL, NULL, NULL);
		}
		break;
	case ACAO_SALVAR:
		if (existe) {
			salvar_agora(a->servico, a->miniatura);
		}
		break;
	}
}

/* ---- os avisos da extensão ---- */

static void aviso_aba(const struct json *m) {
	const char *servico = json_chave_texto(m, "servico");
	if (!*servico) {
		return;
	}
	g_mutex_lock(&trinco);
	struct ff_aba *a = aba_do_servico(servico);
	if (!a && estado.n < FF_MAX_ABAS) {
		a = &estado.abas[estado.n++];
		memset(a, 0, sizeof(*a));
		g_strlcpy(a->servico, servico, sizeof(a->servico));
		a->ativa = !strcmp(estado.ativo, servico);
	}
	if (a) {
		snprintf(a->contexto, sizeof(a->contexto), "%d", (int)json_numero(json_chave(m, "tab"), 0));
		const char *url = json_chave_texto(m, "url");
		if (*url) {
			g_strlcpy(a->url, url, sizeof(a->url));
		}
		const char *titulo = json_chave_texto(m, "titulo");
		if (*titulo) {
			g_strlcpy(a->titulo, titulo, sizeof(a->titulo));
		}
		a->carregando = json_bool(json_chave(m, "carregando"), false);
	}
	g_mutex_unlock(&trinco);
	avisar();
}

static void aviso_fechada(const struct json *m) {
	const char *servico = json_chave_texto(m, "servico");
	g_mutex_lock(&trinco);
	for (int i = 0; i < estado.n; i++) {
		if (!strcmp(estado.abas[i].servico, servico)) {
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

static void ao_mensagem(SoupWebsocketConnection *c, gint tipo, GBytes *bytes, gpointer dados) {
	if (tipo != SOUP_WEBSOCKET_DATA_TEXT) {
		return;
	}
	gsize n;
	const char *t = g_bytes_get_data(bytes, &n);
	char *copia = g_strndup(t, n);
	struct json *j = json_ler(copia);
	g_free(copia);
	const char *ev = json_chave_texto(j, "evento");
	if (!strcmp(ev, "ola")) {
		g_mutex_lock(&trinco);
		estado.ligado = true;
		g_mutex_unlock(&trinco);
		avisar();
		for (guint i = 0; fila && i < fila->len; i++) {
			executar(fila->pdata[i]);
		}
		if (fila) {
			g_ptr_array_set_size(fila, 0);
		}
	} else if (!strcmp(ev, "aba")) {
		aviso_aba(j);
	} else if (!strcmp(ev, "fechada")) {
		aviso_fechada(j);
	} else if (json_chave(j, "id")) {
		gpointer id = GINT_TO_POINTER((int)json_numero(json_chave(j, "id"), 0));
		struct rpc *r = g_hash_table_lookup(pendentes, id);
		if (r) {
			g_hash_table_steal(pendentes, id);
			r->fim(j, r->dados);
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

/* quem se conecta? só a nossa extensão: o Origin é o moz-extension://UUID que fixamos no perfil, e uma página da web não consegue forjá-lo */
static void ao_conectar(SoupServer *s, SoupServerMessage *msg, const char *caminho, SoupWebsocketConnection *c, gpointer dados) {
	char *esperado = g_strdup_printf("moz-extension://%s", uuid);
	const char *origem = soup_message_headers_get_one(soup_server_message_get_request_headers(msg), "Origin");
	bool vale = origem && !strcmp(origem, esperado);
	g_free(esperado);
	if (!vale) {
		soup_websocket_connection_close(c, SOUP_WEBSOCKET_CLOSE_POLICY_VIOLATION, "só a extensão do LUMNA");
		return;
	}
	if (ws) {
		soup_websocket_connection_close(ws, SOUP_WEBSOCKET_CLOSE_GOING_AWAY, "outra extensão tomou o lugar");
		desligar();
	}
	ws = g_object_ref(c);
	soup_websocket_connection_set_max_incoming_payload_size(ws, 32 * 1024 * 1024); /* a miniatura vem em base64 */
	g_signal_connect(ws, "message", G_CALLBACK(ao_mensagem), NULL);
	g_signal_connect(ws, "closed", G_CALLBACK(ao_fechar_ws), NULL);
}

/* ---- o processo ---- */

static gboolean na_linha(gpointer dados);

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
	g_ptr_array_set_size(fila, 0);
}

static bool lancar(void) {
	if (pid) {
		return true;
	}
	char *perfil = pasta_perfil();
	const char *programa = g_getenv("LUMNA_FIREFOX");
	char *argv[] = { (char *)(programa && *programa ? programa : "firefox"), "--kiosk", "--no-remote", "--profile", perfil, "--name", FF_APP_ID,
		"--class", FF_APP_ID, "about:blank", NULL };
	GError *erro = NULL;
	bool ok = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
		NULL, NULL, &pid, &erro);
	g_clear_error(&erro);
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
		g_ptr_array_add(fila, a); /* a extensão ainda está subindo */
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
	if (tipo != ACAO_ABRIR && tipo != ACAO_IR) {
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

void ff_ir(const char *servico, const char *url) {
	pedir(ACAO_IR, servico, url, false);
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
	pendentes = g_hash_table_new(g_direct_hash, g_direct_equal);
	fila = g_ptr_array_new_with_free_func(g_free);
	char *dir = midia_pasta_dados();
	uuid_da_extensao(dir);
	g_free(dir);
	/* o servidor da extensão: só em 127.0.0.1 */
	servidor = soup_server_new("server-header", "lumna-midia", NULL);
	soup_server_add_websocket_handler(servidor, "/extensao", NULL, NULL, ao_conectar, NULL, NULL);
	GError *erro = NULL;
	if (!soup_server_listen_local(servidor, PORTA_EXT, 0, &erro)) {
		fprintf(stderr, "lumna-midia: a porta %d da extensão do Firefox: %s\n", PORTA_EXT, erro ? erro->message : "?");
		g_clear_error(&erro);
	}
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
