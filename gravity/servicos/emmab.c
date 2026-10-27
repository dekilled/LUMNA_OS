#include "emmab.h"

#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define URL_PADRAO "http://127.0.0.1:8001"
#define TOKEN_VALE_S (50 * 60)

static GMutex trinco;
/* depois de o servidor recusar o login, espera um pouco antes de tentar de novo: insistir só trava a conta ("Muitas tentativas") */
static gint64 pausa_ate;
static char pausa_msg[240];
#define PAUSA_LOGIN_S 120
static char *token;
static gint64 token_ate;                  /* monotônico, em µs */
static SoupSession *sessao_curta, *sessao_longa;
static GCancellable *corte;               /* a resposta que está chegando */
static char sessao_id[EMMAB_NN][80];
static gint64 sessao_usada;


static const char *canal_chave(int c) {
	return c == EMMAB_ASSIST ? "assist_session_id" : "session_id";
}

/* ---- ~/.config/lumna/emma.json ---- */

static char *config_caminho(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "emma.json", NULL);
}

static struct json *config_ler(void) {
	char *p = config_caminho(), *t = NULL;
	struct json *j = NULL;
	if (g_file_get_contents(p, &t, NULL, NULL)) {
		j = json_ler(t);
	}
	g_free(t);
	g_free(p);
	if (!j || j->tipo != JSON_OBJETO) {
		json_liberar(j);
		return NULL;
	}
	return j;
}

char *emmab_config_texto(const char *chave) {
	struct json *j = config_ler();
	char *r = NULL;
	if (j) {
		const char *t = json_texto(json_chave(j, chave));
		r = t && *t ? g_strdup(t) : NULL;
		json_liberar(j);
	}
	return r;
}

bool emmab_config_gravar(const char *chave, const char *valor_json) {
	struct json *j = config_ler();
	GString *s = g_string_new("{");
	bool primeiro = true;
	for (int i = 0; j && i < j->nfilhos; i++) {
		if (!strcmp(j->filhos[i]->chave, chave)) {
			continue;
		}
		g_string_append_printf(s, "%s", primeiro ? "" : ",");
		json_escrever_texto(s, j->filhos[i]->chave);
		g_string_append_c(s, ':');
		json_escrever(s, j->filhos[i]);
		primeiro = false;
	}
	if (valor_json) {
		g_string_append_printf(s, "%s", primeiro ? "" : ",");
		json_escrever_texto(s, chave);
		g_string_append_c(s, ':');
		g_string_append(s, valor_json);
	}
	g_string_append_c(s, '}');
	json_liberar(j);
	char *p = config_caminho(), *dir = g_path_get_dirname(p);
	g_mkdir_with_parents(dir, 0700);
	char *tmp = g_strconcat(p, ".tmp", NULL);
	bool ok = g_file_set_contents(tmp, s->str, -1, NULL);
	if (ok) {
		g_chmod(tmp, 0600);
		ok = g_rename(tmp, p) == 0;
	}
	g_free(tmp);
	g_free(dir);
	g_free(p);
	g_string_free(s, TRUE);
	return ok;
}

GHashTable *emmab_permissoes(void) {
	GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	struct json *j = config_ler();
	const struct json *p = json_chave(j, "permissions");
	for (int i = 0; p && p->tipo == JSON_OBJETO && i < p->nfilhos; i++) {
		const char *v = json_texto(p->filhos[i]);
		if (v) {
			g_hash_table_replace(h, g_strdup(p->filhos[i]->chave), g_strdup(v));
		}
	}
	json_liberar(j);
	return h;
}

const char *emmab_url(void) {
	static char url[200];
	const char *env = g_getenv("LUMNA_EMMA_URL");
	char *c = env && *env ? NULL : emmab_config_texto("url");
	g_strlcpy(url, env && *env ? env : c ? c : URL_PADRAO, sizeof(url));
	g_free(c);
	size_t n = strlen(url);
	while (n > 0 && url[n - 1] == '/') {
		url[--n] = '\0';
	}
	return url;
}

/* ---- HTTP ---- */

static void sessoes(void) {
	if (!sessao_curta) {
		sessao_curta = soup_session_new_with_options("timeout", 20, "idle-timeout", 20, NULL);
		sessao_longa = soup_session_new_with_options("timeout", 900, "idle-timeout", 900, NULL);
	}
}

static char *detalhe(const char *corpo) {
	struct json *j = corpo ? json_ler(corpo) : NULL;
	char *r = NULL;
	const struct json *d = json_chave(j, "detail");
	if (d && d->tipo == JSON_TEXTO) {
		r = g_strdup(d->s);
	}
	json_liberar(j);
	return r;
}

/* um pedido simples: status, corpo e erro de rede */
static int pedir(SoupSession *s, const char *metodo, const char *caminho, const char *tok, const char *corpo, char **resposta, char **erro) {
	char *url = g_strdup_printf("%s%s", emmab_url(), caminho);
	SoupMessage *m = soup_message_new(metodo, url);
	g_free(url);
	if (!m) {
		if (erro) {
			*erro = g_strdup("endereço da Emma inválido");
		}
		return 0;
	}
	SoupMessageHeaders *h = soup_message_get_request_headers(m);
	if (tok) {
		char *a = g_strdup_printf("Bearer %s", tok);
		soup_message_headers_replace(h, "Authorization", a);
		g_free(a);
	}
	if (corpo) {
		GBytes *b = g_bytes_new(corpo, strlen(corpo));
		soup_message_set_request_body_from_bytes(m, "application/json", b);
		g_bytes_unref(b);
	}
	GError *e = NULL;
	GBytes *r = soup_session_send_and_read(s, m, NULL, &e);
	int status = 0;
	if (r) {
		status = (int)soup_message_get_status(m);
		if (resposta) {
			gsize n;
			const char *d = g_bytes_get_data(r, &n);
			*resposta = g_strndup(d, n);
		}
		g_bytes_unref(r);
	} else if (erro) {
		*erro = g_strdup_printf("a Emma não respondeu em %s", emmab_url());
	}
	g_clear_error(&e);
	g_object_unref(m);
	return status;
}

bool emmab_no_ar(void) {
	sessoes();
	char *r = NULL;
	int st = pedir(sessao_curta, "GET", "/api/", NULL, NULL, &r, NULL);
	g_free(r);
	return st > 0 && st < 500;
}

/* ---- entrar ---- */

static bool conta_local(char **email, char **senha) {
	char *p = g_build_filename(g_get_user_config_dir(), "lumna", "emma", "backend.env", NULL), *t = NULL;
	bool ok = false;
	if (g_file_get_contents(p, &t, NULL, NULL)) {
		char **ls = g_strsplit(t, "\n", -1);
		for (int i = 0; ls[i]; i++) {
			char *l = g_strstrip(ls[i]);
			char *eq = strchr(l, '=');
			if (!eq) {
				continue;
			}
			*eq = '\0';
			char *v = g_strstrip(eq + 1);
			size_t n = strlen(v);
			if (n >= 2 && (v[0] == '"' || v[0] == '\'') && v[n - 1] == v[0]) {
				v[n - 1] = '\0';
				v++;
			}
			if (!strcmp(g_strstrip(l), "ADMIN_EMAIL")) {
				g_free(*email);
				*email = g_strdup(v);
			} else if (!strcmp(l, "ADMIN_PASSWORD")) {
				g_free(*senha);
				*senha = g_strdup(v);
			}
		}
		g_strfreev(ls);
		ok = *email && **email && *senha && **senha;
	}
	g_free(t);
	g_free(p);
	return ok;
}

/* o token de API do servidor da Emma (criado no harness, em Configurações > Conta): $LUMNA_EMMA_TOKEN, ou o guardado em emma.json */
static char *token_api(void) {
	const char *env = g_getenv("LUMNA_EMMA_TOKEN");
	return env && *env ? g_strdup(env) : emmab_config_texto("token");
}

bool emmab_email_ler(char *email, size_t tam) {
	char *e = NULL, *senha = NULL;
	conta_local(&e, &senha);
	g_strlcpy(email, e ? e : "", tam);
	g_free(e);
	g_free(senha);
	return email[0] != '\0';
}

bool emmab_token_ler(char *mascara, size_t tam) {
	char *t = token_api();
	mascara[0] = '\0';
	if (t) {
		size_t n = strlen(t);
		if (n > 14) {
			g_snprintf(mascara, tam, "%.10s…%s", t, t + n - 4);
		} else {
			g_strlcpy(mascara, "••••", tam);
		}
	}
	g_free(t);
	return mascara[0] != '\0';
}

/* ADMIN_EMAIL=... ADMIN_PASSWORD=... entre aspas que o leitor de conta_local entende (sem o mesmo tipo de aspas dentro) */
static char *valor_env(const char *v) {
	if (!strchr(v, '"')) {
		return g_strdup_printf("\"%s\"", v);
	}
	return !strchr(v, '\'') ? g_strdup_printf("'%s'", v) : NULL;
}

bool emmab_conta_gravar(const char *url, const char *token, const char *email, const char *senha, char **erro) {
	if (token && *token && (!g_str_has_prefix(token, "lumna_") || strpbrk(token, " \t\r\n\"\\"))) {
		if (erro) {
			*erro = g_strdup("Isso não parece um token da Emma: ele começa com lumna_ (gere em Configurações > Conta, no harness).");
		}
		return false;
	}
	if (url && *url) {
		GString *v = g_string_new(NULL);
		json_escrever_texto(v, url);
		emmab_config_gravar("url", v->str);
		g_string_free(v, TRUE);
	}
	if (token && *token) {
		GString *v = g_string_new(NULL);
		json_escrever_texto(v, token);
		bool ok = emmab_config_gravar("token", v->str);
		g_string_free(v, TRUE);
		if (!ok) {
			if (erro) {
				*erro = g_strdup("Não consegui guardar o token.");
			}
			return false;
		}
	}
	if ((email && *email) || (senha && *senha)) {
		char *e0 = NULL, *s0 = NULL;
		conta_local(&e0, &s0);
		char *e1 = valor_env(email && *email ? email : e0 ? e0 : ""), *s1 = valor_env(senha && *senha ? senha : s0 ? s0 : "");
		bool ok = false;
		if (e1 && s1 && !strpbrk(email ? email : "", "\n\r") && !strpbrk(senha ? senha : "", "\n\r")) {
			char *dir = g_build_filename(g_get_user_config_dir(), "lumna", "emma", NULL), *p = g_build_filename(dir, "backend.env", NULL);
			char *conteudo = g_strdup_printf("ADMIN_EMAIL=%s\nADMIN_PASSWORD=%s\n", e1, s1), *tmp = g_strconcat(p, ".tmp", NULL);
			g_mkdir_with_parents(dir, 0700);
			ok = g_file_set_contents(tmp, conteudo, -1, NULL) && g_chmod(tmp, 0600) == 0 && g_rename(tmp, p) == 0;
			g_free(tmp);
			g_free(conteudo);
			g_free(p);
			g_free(dir);
		}
		g_free(e1);
		g_free(s1);
		g_free(e0);
		g_free(s0);
		if (!ok) {
			if (erro) {
				*erro = g_strdup("Não consegui guardar o e-mail e a senha (sem quebra de linha nem os dois tipos de aspas).");
			}
			return false;
		}
		emmab_config_gravar("refresh_token", NULL); /* a sessão de antes era de outra conta */
	}
	emmab_reentrar();
	return true;
}

void emmab_reentrar(void) {
	g_mutex_lock(&trinco);
	g_free(token);
	token = NULL;
	pausa_ate = 0;
	g_mutex_unlock(&trinco);
}

static void guardar_token(const char *t) {
	g_free(token);
	token = g_strdup(t);
	token_ate = g_get_monotonic_time() + (gint64)TOKEN_VALE_S * G_USEC_PER_SEC;
}

static char *texto_de(const char *json, const char *chave) {
	struct json *j = json_ler(json ? json : "");
	const char *t = json_texto(json_chave(j, chave));
	char *r = t ? g_strdup(t) : NULL;
	json_liberar(j);
	return r;
}

/* o token de acesso: o guardado, renovado pelo refresh token, ou um login com a conta local */
static char *acesso(char **erro) {
	char *api = token_api();
	if (api) {
		return api; /* o token de API vale como está: sem login, sem renovar */
	}
	g_mutex_lock(&trinco);
	if (token && g_get_monotonic_time() < token_ate) {
		char *t = g_strdup(token);
		g_mutex_unlock(&trinco);
		return t;
	}
	if (g_get_monotonic_time() < pausa_ate) {
		if (erro && !*erro) {
			*erro = g_strdup(pausa_msg);
		}
		g_mutex_unlock(&trinco);
		return NULL;
	}
	g_mutex_unlock(&trinco);
	sessoes();
	char *r = NULL, *refresh = emmab_config_texto("refresh_token"), *novo = NULL;
	bool recusado = false;
	if (refresh) {
		GString *c = g_string_new("{\"refresh_token\":");
		json_escrever_texto(c, refresh);
		g_string_append_c(c, '}');
		int st = pedir(sessao_curta, "POST", "/api/auth/refresh", NULL, c->str, &r, erro);
		g_string_free(c, TRUE);
		if (st == 200) {
			novo = texto_de(r, "access_token");
		} else if (st == 0) {
			g_free(r);
			g_free(refresh);
			return NULL;
		}
		g_free(r);
		r = NULL;
	}
	g_free(refresh);
	if (!novo) {
		char *email = NULL, *senha = NULL;
		if (conta_local(&email, &senha)) {
			GString *c = g_string_new("{\"email\":");
			json_escrever_texto(c, email);
			g_string_append(c, ",\"password\":");
			json_escrever_texto(c, senha);
			g_string_append(c, ",\"remember\":true}");
			int st = pedir(sessao_curta, "POST", "/api/auth/login", NULL, c->str, &r, erro);
			g_string_free(c, TRUE);
			if (st == 200) {
				novo = texto_de(r, "access_token");
				char *rf = texto_de(r, "refresh_token");
				GString *v = g_string_new(NULL);
				if (rf) {
					json_escrever_texto(v, rf);
					emmab_config_gravar("refresh_token", v->str);
				}
				g_string_free(v, TRUE);
				g_free(rf);
				GString *e = g_string_new(NULL);
				json_escrever_texto(e, email);
				emmab_config_gravar("email", e->str);
				g_string_free(e, TRUE);
			} else {
				if (erro && !*erro) {
					char *d = detalhe(r);
					*erro = d ? d : g_strdup_printf("A Emma recusou o login (%d).", st);
				}
				recusado = st != 0;
			}
			g_free(r);
		} else if (erro && !*erro) {
			*erro = g_strdup("Coloque o endereço e o token da Emma (Painel > Emma > Provedores e modelos).");
		}
		g_free(email);
		g_free(senha);
	}
	if (novo) {
		g_mutex_lock(&trinco);
		guardar_token(novo);
		pausa_ate = 0;
		g_mutex_unlock(&trinco);
	} else if (recusado) {
		g_mutex_lock(&trinco);
		pausa_ate = g_get_monotonic_time() + (gint64)PAUSA_LOGIN_S * G_USEC_PER_SEC;
		g_snprintf(pausa_msg, sizeof(pausa_msg), "%s", erro && *erro ? *erro : "O servidor recusou o login.");
		g_mutex_unlock(&trinco);
	}
	return novo;
}

static void esquecer_token(void) {
	g_mutex_lock(&trinco);
	g_free(token);
	token = NULL;
	g_mutex_unlock(&trinco);
}

int emmab_chamar(const char *metodo, const char *caminho, const char *corpo, char **resposta, char **erro) {
	sessoes();
	for (int vez = 0; vez < 2; vez++) {
		char *t = acesso(erro);
		if (!t) {
			return 0;
		}
		char *r = NULL;
		int st = pedir(sessao_curta, metodo, caminho, t, corpo, &r, erro);
		g_free(t);
		if (st == 401 && vez == 0) {
			g_free(r);
			esquecer_token();
			continue;
		}
		if (resposta) {
			*resposta = r;
		} else {
			g_free(r);
		}
		return st;
	}
	return 0;
}

/* ---- conversas ---- */

bool emmab_sessao(int canal, char *sid, size_t n, char **erro) {
	g_mutex_lock(&trinco);
	/* a do assistente recomeça depois de 10 min parada, como no popup do shell antigo */
	bool velha = canal == EMMAB_ASSIST && sessao_usada && g_get_monotonic_time() - sessao_usada > 10 * 60 * G_USEC_PER_SEC;
	if (canal == EMMAB_ASSIST) {
		sessao_usada = g_get_monotonic_time();
	}
	char guardada[80];
	g_strlcpy(guardada, sessao_id[canal], sizeof(guardada));
	g_mutex_unlock(&trinco);
	char *salva = guardada[0] ? NULL : emmab_config_texto(canal_chave(canal));
	const char *atual = guardada[0] ? guardada : salva;
	if (atual && !velha) {
		char *caminho = g_strdup_printf("/api/sessions/%s/messages", atual);
		int st = emmab_chamar("GET", caminho, NULL, NULL, erro);
		g_free(caminho);
		if (st == 200) {
			g_strlcpy(sid, atual, n);
			g_mutex_lock(&trinco);
			g_strlcpy(sessao_id[canal], atual, sizeof(sessao_id[canal]));
			g_mutex_unlock(&trinco);
			g_free(salva);
			return true;
		}
		if (st == 0) {
			g_free(salva);
			return false;
		}
	}
	g_free(salva);
	char *r = NULL;
	int st = emmab_chamar("POST", "/api/sessions",
		canal == EMMAB_APP ? "{\"title\":\"Nova sessão\"}" : "{\"title\":\"LUMNA Assistente\",\"assist_log\":true}", &r, erro);
	char *novo = st == 200 ? texto_de(r, "id") : NULL;
	g_free(r);
	if (!novo) {
		if (erro && !*erro) {
			*erro = g_strdup_printf("não deu para abrir a conversa (%d)", st);
		}
		return false;
	}
	g_strlcpy(sid, novo, n);
	g_mutex_lock(&trinco);
	g_strlcpy(sessao_id[canal], novo, sizeof(sessao_id[canal]));
	g_mutex_unlock(&trinco);
	GString *v = g_string_new(NULL);
	json_escrever_texto(v, novo);
	emmab_config_gravar(canal_chave(canal), v->str);
	g_string_free(v, TRUE);
	g_free(novo);
	return true;
}

void emmab_nova_conversa(int canal) {
	g_mutex_lock(&trinco);
	sessao_id[canal][0] = '\0';
	g_mutex_unlock(&trinco);
	emmab_config_gravar(canal_chave(canal), NULL);
}

bool emmab_decidir(const char *sid, const char *call_id, bool permitir) {
	char *caminho = g_strdup_printf("/api/sessions/%s/tool-decision", sid);
	GString *c = g_string_new("{\"call_id\":");
	json_escrever_texto(c, call_id);
	g_string_append_printf(c, ",\"allow\":%s}", permitir ? "true" : "false");
	int st = emmab_chamar("POST", caminho, c->str, NULL, NULL);
	g_string_free(c, TRUE);
	g_free(caminho);
	return st == 200;
}

static void enviar_resultado(const char *sid, const char *call_id, const struct emmab_acao *a) {
	char *caminho = g_strdup_printf("/api/sessions/%s/tool-result", sid);
	GString *c = g_string_new("{\"call_id\":");
	json_escrever_texto(c, call_id);
	g_string_append_printf(c, ",\"ok\":%s,\"summary\":", a->ok ? "true" : "false");
	char *resumo = g_utf8_substring(a->resumo ? a->resumo : "", 0, MIN(300, (glong)g_utf8_strlen(a->resumo ? a->resumo : "", -1)));
	json_escrever_texto(c, resumo);
	g_free(resumo);
	g_string_append(c, ",\"content\":");
	char *conteudo = g_utf8_substring(a->conteudo ? a->conteudo : "", 0, MIN(4000, (glong)g_utf8_strlen(a->conteudo ? a->conteudo : "", -1)));
	json_escrever_texto(c, conteudo);
	g_free(conteudo);
	g_string_append_c(c, '}');
	emmab_chamar("POST", caminho, c->str, NULL, NULL);
	g_string_free(c, TRUE);
	g_free(caminho);
}

static bool conversar_uma(int canal, const char *sid_fixo, const char *texto, bool voz, const char *tools, const char *contexto, const char *extra,
	const struct emmab_ouvinte *o, char **resposta, char **erro, bool *auth_de_novo) {
	char sid[80];
	if (sid_fixo && *sid_fixo) {
		g_strlcpy(sid, sid_fixo, sizeof(sid));
	} else if (!emmab_sessao(canal, sid, sizeof(sid), erro)) {
		return false;
	}
	char *t = acesso(erro);
	if (!t) {
		return false;
	}
	if (canal != EMMAB_APP && contexto && *contexto) {
		GString *c = g_string_new("{\"system_prompt\":");
		GString *p = g_string_new("MODO ASSISTENTE (LUMNA, por cima do que a pessoa está fazendo): responda curto e direto, em 1 a 3 frases. "
			"AGIR VEM PRIMEIRO: se pedirem para fazer algo e uma ferramenta lumna_* faz isso, CHAME a ferramenta, sem perguntar. "
			"Nunca diga que fez sem ter chamado.\n\nA pessoa está vendo agora: ");
		g_string_append(p, contexto);
		json_escrever_texto(c, p->str);
		g_string_free(p, TRUE);
		g_string_append_c(c, '}');
		char *caminho = g_strdup_printf("/api/sessions/%s", sid);
		emmab_chamar("PATCH", caminho, c->str, NULL, NULL);
		g_free(caminho);
		g_string_free(c, TRUE);
	}
	char *agora = NULL;
	g_spawn_command_line_sync("date \"+%Y-%m-%d %H:%M (%A) %z\"", &agora, NULL, NULL, NULL);
	GString *c = g_string_new("{\"content\":");
	json_escrever_texto(c, texto);
	g_string_append_printf(c, ",\"mode\":\"chat\",\"voice\":%s,\"client_now\":", voz ? "true" : "false");
	json_escrever_texto(c, agora ? g_strstrip(agora) : "");
	g_free(agora);
	g_string_append_printf(c, ",\"lumna_tools\":%s%s%s}", tools && *tools ? tools : "[]", extra && *extra ? "," : "", extra && *extra ? extra : "");

	sessoes();
	char *url = g_strdup_printf("%s/api/sessions/%s/chat", emmab_url(), sid);
	SoupMessage *m = soup_message_new("POST", url);
	g_free(url);
	char *a = g_strdup_printf("Bearer %s", t);
	soup_message_headers_replace(soup_message_get_request_headers(m), "Authorization", a);
	g_free(a);
	g_free(t);
	GBytes *b = g_bytes_new(c->str, c->len);
	soup_message_set_request_body_from_bytes(m, "application/json", b);
	g_bytes_unref(b);
	g_string_free(c, TRUE);

	GCancellable *cancel = g_cancellable_new();
	g_mutex_lock(&trinco);
	if (corte) {
		g_object_unref(corte);
	}
	corte = g_object_ref(cancel);
	g_mutex_unlock(&trinco);
	GError *e = NULL;
	GInputStream *in = soup_session_send(sessao_longa, m, cancel, &e);
	bool ok = false;
	if (!in) {
		if (!g_cancellable_is_cancelled(cancel) && erro) {
			*erro = g_strdup_printf("a Emma não respondeu em %s", emmab_url());
		}
		ok = g_cancellable_is_cancelled(cancel);
	} else if (soup_message_get_status(m) == 401) {
		*auth_de_novo = true;
		g_object_unref(in);
	} else if (soup_message_get_status(m) >= 400) {
		GDataInputStream *d = g_data_input_stream_new(in);
		char *corpo = g_data_input_stream_read_upto(d, "\0", 1, NULL, NULL, NULL);
		char *det = detalhe(corpo);
		if (erro) {
			*erro = det ? det : g_strdup_printf("A Emma recusou (%d).", soup_message_get_status(m));
		} else {
			g_free(det);
		}
		g_free(corpo);
		g_object_unref(d);
		g_object_unref(in);
	} else {
		GDataInputStream *d = g_data_input_stream_new(in);
		char *linha;
		while ((linha = g_data_input_stream_read_line(d, NULL, cancel, NULL))) {
			if (!g_str_has_prefix(linha, "data:")) {
				g_free(linha);
				continue;
			}
			struct json *ev = json_ler(g_strstrip(linha + 5));
			g_free(linha);
			if (!ev) {
				continue;
			}
			const char *tipo = json_chave_texto(ev, "type");
			if (o && o->evento) {
				o->evento(ev, o->dados);
			}
			if (!strcmp(tipo, "done") && resposta) {
				g_free(*resposta);
				const char *msg = json_chave_texto(json_chave(ev, "message"), "content");
				*resposta = g_strdup(msg);
			} else if (!strcmp(tipo, "tool_client") && o && o->acao) {
				const struct json *tool = json_chave(ev, "tool");
				const char *cid = json_chave_texto(tool, "id"), *nome = json_chave_texto(tool, "name");
				struct emmab_acao r = { 0 };
				o->acao(nome, json_chave(tool, "args"), &r, o->dados);
				enviar_resultado(sid, cid, &r);
				g_free(r.resumo);
				g_free(r.conteudo);
			}
			json_liberar(ev);
		}
		ok = true;
		g_object_unref(d);
		g_object_unref(in);
	}
	g_clear_error(&e);
	g_object_unref(m);
	g_mutex_lock(&trinco);
	if (corte == cancel) {
		g_object_unref(corte);
		corte = NULL;
	}
	g_mutex_unlock(&trinco);
	g_object_unref(cancel);
	return ok;
}

bool emmab_conversar(int canal, const char *sid, const char *texto, bool voz, const char *lumna_tools_json, const char *contexto, const char *extra,
	const struct emmab_ouvinte *o, char **resposta, char **erro) {
	bool de_novo = false;
	bool ok = conversar_uma(canal, sid, texto, voz, lumna_tools_json, contexto, extra, o, resposta, erro, &de_novo);
	if (de_novo) {
		esquecer_token();
		ok = conversar_uma(canal, sid, texto, voz, lumna_tools_json, contexto, extra, o, resposta, erro, &de_novo);
		if (de_novo && erro && !*erro) {
			*erro = g_strdup("A Emma recusou o login: entre de novo no app da Emma.");
		}
	}
	return ok && !de_novo;
}

void emmab_parar(void) {
	g_mutex_lock(&trinco);
	if (corte) {
		g_cancellable_cancel(corte);
	}
	g_mutex_unlock(&trinco);
}

/* ---- voz ---- */

bool emmab_stt(const char *wav, char **texto, char **erro) {
	sessoes();
	gchar *dados = NULL;
	gsize n = 0;
	if (!g_file_get_contents(wav, &dados, &n, NULL)) {
		if (erro) {
			*erro = g_strdup("não achei o áudio gravado");
		}
		return false;
	}
	for (int vez = 0; vez < 2; vez++) {
		char *t = acesso(erro);
		if (!t) {
			g_free(dados);
			return false;
		}
		SoupMultipart *mp = soup_multipart_new("multipart/form-data");
		GBytes *bytes = g_bytes_new(dados, n);
		soup_multipart_append_form_file(mp, "audio", "fala.wav", "audio/wav", bytes);
		g_bytes_unref(bytes);
		char *url = g_strdup_printf("%s/api/stt", emmab_url());
		SoupMessage *m = soup_message_new_from_multipart(url, mp);
		g_free(url);
		soup_multipart_free(mp);
		char *a = g_strdup_printf("Bearer %s", t);
		soup_message_headers_replace(soup_message_get_request_headers(m), "Authorization", a);
		g_free(a);
		g_free(t);
		GError *e = NULL;
		SoupSession *s = soup_session_new_with_options("timeout", 60, "idle-timeout", 60, NULL);
		GBytes *r = soup_session_send_and_read(s, m, NULL, &e);
		int st = r ? (int)soup_message_get_status(m) : 0;
		char *corpo = NULL;
		if (r) {
			gsize rn;
			const char *d = g_bytes_get_data(r, &rn);
			corpo = g_strndup(d, rn);
			g_bytes_unref(r);
		}
		g_object_unref(s);
		g_object_unref(m);
		g_clear_error(&e);
		if (st == 401 && vez == 0) {
			esquecer_token();
			g_free(corpo);
			continue;
		}
		if (st == 200) {
			char *tx = texto_de(corpo, "text");
			*texto = tx ? g_strstrip(tx) : g_strdup("");
			g_free(corpo);
			g_free(dados);
			return true;
		}
		if (erro) {
			char *d = detalhe(corpo);
			*erro = d ? d : st == 0 ? g_strdup_printf("a Emma não respondeu em %s", emmab_url()) : g_strdup_printf("A transcrição falhou (%d). Confira a voz no app da Emma.", st);
		}
		g_free(corpo);
		break;
	}
	g_free(dados);
	return false;
}

bool emmab_tts(const char *texto, const char *saida, char **erro) {
	sessoes();
	GString *c = g_string_new("{\"text\":");
	json_escrever_texto(c, texto);
	g_string_append_c(c, '}');
	for (int vez = 0; vez < 2; vez++) {
		char *t = acesso(erro);
		if (!t) {
			g_string_free(c, TRUE);
			return false;
		}
		char *url = g_strdup_printf("%s/api/tts", emmab_url());
		SoupMessage *m = soup_message_new("POST", url);
		g_free(url);
		char *a = g_strdup_printf("Bearer %s", t);
		soup_message_headers_replace(soup_message_get_request_headers(m), "Authorization", a);
		g_free(a);
		g_free(t);
		GBytes *b = g_bytes_new(c->str, c->len);
		soup_message_set_request_body_from_bytes(m, "application/json", b);
		g_bytes_unref(b);
		GError *e = NULL;
		SoupSession *s = soup_session_new_with_options("timeout", 60, "idle-timeout", 60, NULL);
		GBytes *r = soup_session_send_and_read(s, m, NULL, &e);
		int st = r ? (int)soup_message_get_status(m) : 0;
		bool ok = false;
		if (r && st == 200) {
			gsize rn;
			const char *d = g_bytes_get_data(r, &rn);
			ok = g_file_set_contents(saida, d, (gssize)rn, NULL);
		}
		if (r) {
			g_bytes_unref(r);
		}
		g_object_unref(s);
		g_object_unref(m);
		g_clear_error(&e);
		if (st == 401 && vez == 0) {
			esquecer_token();
			continue;
		}
		if (!ok && erro) {
			*erro = st == 0 ? g_strdup_printf("a Emma não respondeu em %s", emmab_url()) : g_strdup_printf("A voz falhou (%d). Confira a voz no app da Emma.", st);
		}
		g_string_free(c, TRUE);
		return ok;
	}
	g_string_free(c, TRUE);
	return false;
}
