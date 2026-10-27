#include "emmal.h"

#include <libsoup/soup.h>
#include <stdio.h>
#include <string.h>

#include "emmas.h"
#include "json.h"

#define RODADAS_MAX 6   /* quantas vezes a Emma pode agir e voltar ao provedor numa resposta */
#define HISTORICO_MAX 40

static SoupSession *sessao;
static GCancellable *atual;
static GMutex trinco;

static void sessao_iniciar(void) {
	if (!sessao) {
		sessao = soup_session_new_with_options("timeout", 600, "idle-timeout", 120, NULL);
	}
}

/* ---- o que o Painel guardou ---- */

static struct json *provedores_ler(void) {
	char *p = g_build_filename(g_get_user_config_dir(), "lumna", "emma", "provedores.json", NULL), *txt = NULL;
	struct json *j = NULL;
	if (g_file_get_contents(p, &txt, NULL, NULL)) {
		j = json_ler(txt);
	}
	g_free(txt);
	g_free(p);
	return j;
}

static void primeiro_modelo(const char *csv, char *dest, size_t tam) {
	char **partes = g_strsplit(csv, ",", -1);
	dest[0] = '\0';
	for (int i = 0; partes[i] && !dest[0]; i++) {
		g_strstrip(partes[i]);
		g_strlcpy(dest, partes[i], tam);
	}
	g_strfreev(partes);
}

bool emmal_provedor(const char *tarefa, struct emmal_prov *p, char **erro) {
	memset(p, 0, sizeof(*p));
	struct json *j = provedores_ler();
	const struct json *ps = json_chave(j, "provedores"), *rs = json_chave(j, "rotas");
	const struct json *prov = NULL;
	const char *modelo = "";
	for (int pass = 0; pass < 2 && !prov; pass++) {
		const char *quer = pass == 0 ? tarefa : "chat";
		for (int i = 0; rs && i < rs->nfilhos && !prov; i++) {
			const struct json *r = json_item(rs, i);
			if (strcmp(json_chave_texto(r, "chave"), quer) || !*json_chave_texto(r, "lid")) {
				continue;
			}
			for (int k = 0; ps && k < ps->nfilhos; k++) {
				if (!strcmp(json_chave_texto(json_item(ps, k), "lid"), json_chave_texto(r, "lid"))) {
					prov = json_item(ps, k);
					modelo = json_chave_texto(r, "modelo");
				}
			}
		}
	}
	if (!prov && ps && ps->nfilhos) {
		prov = json_item(ps, 0); /* nenhum roteamento: o primeiro provedor */
	}
	if (!prov) {
		if (erro) {
			*erro = g_strdup("Nenhum provedor configurado. Adicione um em Painel > Emma > Provedores (a URL e a chave de API).");
		}
		json_liberar(j);
		return false;
	}
	g_strlcpy(p->nome, json_chave_texto(prov, "nome"), sizeof(p->nome));
	g_strlcpy(p->url, json_chave_texto(prov, "url"), sizeof(p->url));
	g_strlcpy(p->chave, json_chave_texto(prov, "chave"), sizeof(p->chave));
	g_strlcpy(p->modelo, modelo, sizeof(p->modelo));
	if (!p->modelo[0]) {
		primeiro_modelo(json_chave_texto(prov, "modelos"), p->modelo, sizeof(p->modelo));
	}
	json_liberar(j);
	if (!p->url[0] || !p->modelo[0]) {
		if (erro) {
			*erro = g_strdup_printf("O provedor %s está sem %s: complete em Painel > Emma > Provedores.", p->nome, !p->url[0] ? "endereço (URL)" : "modelo");
		}
		return false;
	}
	return true;
}

/* ---- a chamada ao provedor ---- */

struct tcall {
	char *id, *nome;
	GString *args;
};

struct resposta {
	GString *texto;
	GPtrArray *chamadas; /* struct tcall */
	bool stream;         /* o provedor respondeu em pedaços (event-stream) */
};

static void tcall_liberar(gpointer p) {
	struct tcall *t = p;
	g_free(t->id);
	g_free(t->nome);
	g_string_free(t->args, TRUE);
	g_free(t);
}

static void resposta_iniciar(struct resposta *r) {
	r->texto = g_string_new(NULL);
	r->chamadas = g_ptr_array_new_with_free_func(tcall_liberar);
}

static void resposta_liberar(struct resposta *r) {
	g_string_free(r->texto, TRUE);
	g_ptr_array_free(r->chamadas, TRUE);
}

static char *url_de(const struct emmal_prov *p, const char *caminho) {
	char *base = g_strdup(p->url);
	size_t n = strlen(base);
	while (n > 0 && base[n - 1] == '/') {
		base[--n] = '\0';
	}
	char *r = g_strdup_printf("%s%s", base, caminho);
	g_free(base);
	return r;
}

/* o que o provedor disse de errado, em português */
static char *erro_do_provedor(const struct emmal_prov *p, int status, const char *corpo) {
	struct json *j = corpo ? json_ler(corpo) : NULL;
	const struct json *e = json_chave(j, "error");
	const char *m = e && e->tipo == JSON_OBJETO ? json_chave_texto(e, "message") : e ? json_texto(e) : NULL;
	if (!m || !*m) {
		m = json_chave_texto(j, "detail");
	}
	char *r;
	if (status == 401 || status == 403) {
		r = g_strdup_printf("%s recusou a chave de API (%d). Confira a chave em Painel > Emma > Provedores.%s%s", p->nome, status, m && *m ? " " : "", m ? m : "");
	} else if (status == 404) {
		r = g_strdup_printf("%s não achou o endereço ou o modelo %s (404). %s", p->nome, p->modelo, m ? m : "");
	} else if (status == 429) {
		r = g_strdup_printf("%s pediu para ir mais devagar (429). Tente de novo daqui a pouco. %s", p->nome, m ? m : "");
	} else if (m && *m) {
		r = g_strdup_printf("%s respondeu %d: %s", p->nome, status, m);
	} else {
		r = g_strdup_printf("%s respondeu %d.", p->nome, status);
	}
	json_liberar(j);
	return g_strstrip(r);
}

static void acumular_chamada(struct resposta *r, const struct json *tc) {
	int idx = (int)json_numero(json_chave(tc, "index"), (double)r->chamadas->len);
	while ((int)r->chamadas->len <= idx && r->chamadas->len < 32) {
		struct tcall *t = g_new0(struct tcall, 1);
		t->args = g_string_new(NULL);
		g_ptr_array_add(r->chamadas, t);
	}
	if (idx >= (int)r->chamadas->len) {
		return;
	}
	struct tcall *t = r->chamadas->pdata[idx];
	const char *id = json_chave_texto(tc, "id");
	const struct json *fn = json_chave(tc, "function");
	const char *nome = json_chave_texto(fn, "name"), *args = json_texto(json_chave(fn, "arguments"));
	if (*id && !t->id) {
		t->id = g_strdup(id);
	}
	if (*nome) {
		char *novo = g_strconcat(t->nome ? t->nome : "", nome, NULL);
		g_free(t->nome);
		t->nome = novo;
	}
	if (args) {
		g_string_append(t->args, args);
	}
}

/* uma fala do provedor: delta com o texto que chega; r junta o texto e as chamadas de ferramenta. messages_json é o ARRAY completo */
static bool pedir(const struct emmal_prov *p, const char *messages_json, const char *tools_json, const char *extra, void (*delta)(const char *, void *),
	void *dados, struct resposta *r, char **erro) {
	sessao_iniciar();
	GString *c = g_string_new("{\"model\":");
	json_escrever_texto(c, p->modelo);
	g_string_append_printf(c, ",\"stream\":true,\"messages\":%s", messages_json);
	if (tools_json && *tools_json) {
		g_string_append_printf(c, ",\"tools\":%s", tools_json);
	}
	if (extra && *extra) {
		g_string_append_printf(c, ",%s", extra);
	}
	g_string_append_c(c, '}');
	char *url = url_de(p, "/chat/completions");
	SoupMessage *m = soup_message_new("POST", url);
	g_free(url);
	if (!m) {
		g_string_free(c, TRUE);
		*erro = g_strdup_printf("O endereço de %s não parece certo: %s", p->nome, p->url);
		return false;
	}
	SoupMessageHeaders *h = soup_message_get_request_headers(m);
	if (p->chave[0]) {
		char *auth = g_strdup_printf("Bearer %s", p->chave);
		soup_message_headers_replace(h, "Authorization", auth);
		g_free(auth);
	}
	soup_message_headers_replace(h, "Accept", "text/event-stream");
	GBytes *corpo = g_bytes_new(c->str, c->len);
	soup_message_set_request_body_from_bytes(m, "application/json", corpo);
	g_bytes_unref(corpo);
	g_string_free(c, TRUE);

	GCancellable *cancel = g_cancellable_new();
	g_mutex_lock(&trinco);
	atual = cancel;
	g_mutex_unlock(&trinco);
	GError *e = NULL;
	GInputStream *in = soup_session_send(sessao, m, cancel, &e);
	bool ok = false;
	if (!in) {
		if (!g_cancellable_is_cancelled(cancel)) {
			*erro = g_strdup_printf("Não consegui falar com %s (%s): %s", p->nome, p->url, e ? e->message : "sem resposta");
		}
		goto fim;
	}
	int status = (int)soup_message_get_status(m);
	if (status < 200 || status >= 300) {
		char buf[4096];
		gsize n = 0;
		g_input_stream_read_all(in, buf, sizeof(buf) - 1, &n, cancel, NULL);
		buf[n] = '\0';
		*erro = erro_do_provedor(p, status, buf);
		g_object_unref(in);
		goto fim;
	}
	const char *tipo = soup_message_headers_get_content_type(soup_message_get_response_headers(m), NULL);
	if (tipo && strstr(tipo, "event-stream")) {
		r->stream = true;
		GDataInputStream *d = g_data_input_stream_new(in);
		char *linha;
		while ((linha = g_data_input_stream_read_line(d, NULL, cancel, NULL))) {
			if (!g_str_has_prefix(linha, "data:")) {
				g_free(linha);
				continue;
			}
			const char *dado = g_strstrip(linha + 5);
			if (!strcmp(dado, "[DONE]")) {
				g_free(linha);
				break;
			}
			struct json *j = json_ler(dado);
			const struct json *err = json_chave(j, "error");
			if (err) {
				const char *msg = err->tipo == JSON_OBJETO ? json_chave_texto(err, "message") : json_texto(err);
				*erro = g_strdup_printf("%s: %s", p->nome, msg && *msg ? msg : "erro durante a resposta");
				json_liberar(j);
				g_free(linha);
				break;
			}
			const struct json *ch = json_item(json_chave(j, "choices"), 0), *dl = json_chave(ch, "delta");
			const char *txt = json_texto(json_chave(dl, "content"));
			if (txt && *txt) {
				g_string_append(r->texto, txt);
				if (delta) {
					delta(txt, dados);
				}
			}
			const struct json *tcs = json_chave(dl, "tool_calls");
			for (int i = 0; tcs && i < tcs->nfilhos; i++) {
				acumular_chamada(r, json_item(tcs, i));
			}
			json_liberar(j);
			g_free(linha);
		}
		g_object_unref(d);
		g_object_unref(in);
		ok = !*erro; /* cortada de propósito (calar): o que chegou vale */
	} else {
		/* um servidor que ignorou o stream e mandou a resposta inteira */
		GString *tudo = g_string_new(NULL);
		char buf[8192];
		gssize n;
		while ((n = g_input_stream_read(in, buf, sizeof(buf), cancel, NULL)) > 0) {
			g_string_append_len(tudo, buf, (gssize)n);
		}
		g_object_unref(in);
		struct json *j = json_ler(tudo->str);
		const struct json *msg = json_chave(json_item(json_chave(j, "choices"), 0), "message");
		const char *txt = json_texto(json_chave(msg, "content"));
		if (txt && *txt) {
			g_string_append(r->texto, txt);
			if (delta) {
				delta(txt, dados);
			}
		}
		const struct json *tcs = json_chave(msg, "tool_calls");
		for (int i = 0; tcs && i < tcs->nfilhos; i++) {
			acumular_chamada(r, json_item(tcs, i));
		}
		ok = msg != NULL;
		if (!ok) {
			*erro = g_strdup_printf("%s respondeu num formato que eu não entendi.", p->nome);
		}
		json_liberar(j);
		g_string_free(tudo, TRUE);
		goto fim;
	}
fim:
	g_mutex_lock(&trinco);
	atual = NULL;
	g_mutex_unlock(&trinco);
	g_object_unref(cancel);
	g_clear_error(&e);
	g_object_unref(m);
	return ok;
}

void emmal_parar(void) {
	g_mutex_lock(&trinco);
	if (atual) {
		g_cancellable_cancel(atual);
	}
	g_mutex_unlock(&trinco);
}

/* ---- a conversa ---- */

static char *prompt_sistema(bool voz, const char *contexto) {
	GDateTime *t = g_date_time_new_now_local();
	char *agora = g_date_time_format(t, "%A, %d/%m/%Y, %H:%M");
	g_date_time_unref(t);
	GString *g = g_string_new(
		"Você é a Emma, a assistente do LUMNA OS, o sistema (Linux) que a pessoa está usando agora. Fale em português do Brasil, de um jeito próximo, "
		"caloroso e direto, sem enrolar e sem encher de ressalvas.\n\n"
		"Você age no computador com as ferramentas lumna_* (abrir apps e a Mídia, volume, modos, o Painel de controle...). Use a ferramenta quando a "
		"pessoa pedir uma ação; nunca diga que fez algo sem chamar a ferramenta. Para mostrar onde se muda uma configuração, abra o Painel com "
		"lumna_painel_abrir. Se não existe ferramenta para o pedido, diga isso com franqueza e explique como a pessoa faz.\n\n"
		"Você pode escrever e discutir código; coloque o código em blocos com a linguagem (```c). Use Markdown quando ajudar (listas, títulos curtos), "
		"sem exagero.\n\n"
		"No fim da resposta, se ajudar, ofereça até 3 próximos passos em linhas separadas: [acao: texto curto] para o principal, [sugestao: texto "
		"curto] para ideias e [link: texto curto] para abrir algo.");
	if (voz) {
		g_string_append(g, "\n\nEsta resposta será FALADA em voz alta: no máximo duas frases curtas, sem listas, sem Markdown e sem as etiquetas entre colchetes.");
	}
	g_string_append_printf(g, "\n\nAgora: %s.", agora);
	if (contexto && *contexto) {
		g_string_append_printf(g, "\n\nO que a pessoa está vendo agora: %s", contexto);
	}
	g_free(agora);
	return g_string_free(g, FALSE);
}

/* as ferramentas no formato do provedor: [{"type":"function","function":{name, description, parameters}}] */
static char *ferramentas_openai(const char *lumna_tools_json) {
	struct json *j = lumna_tools_json ? json_ler(lumna_tools_json) : NULL;
	if (!j || j->tipo != JSON_LISTA || !j->nfilhos) {
		json_liberar(j);
		return NULL;
	}
	GString *g = g_string_new("[");
	for (int i = 0; i < j->nfilhos; i++) {
		const struct json *t = json_item(j, i);
		g_string_append_printf(g, "%s{\"type\":\"function\",\"function\":{\"name\":", i ? "," : "");
		json_escrever_texto(g, json_chave_texto(t, "name"));
		g_string_append(g, ",\"description\":");
		json_escrever_texto(g, json_chave_texto(t, "description"));
		g_string_append(g, ",\"parameters\":");
		const struct json *params = json_chave(t, "params");
		if (params && params->tipo == JSON_OBJETO) {
			json_escrever(g, params);
		} else {
			g_string_append(g, "{\"type\":\"object\",\"properties\":{}}");
		}
		g_string_append(g, "}}");
	}
	g_string_append_c(g, ']');
	json_liberar(j);
	return g_string_free(g, FALSE);
}

struct delta_ctx {
	const struct emmab_ouvinte *o;
};

static void delta_para_ouvinte(const char *texto, void *d) {
	struct delta_ctx *c = d;
	GString *g = g_string_new("{\"type\":\"delta\",\"content\":");
	json_escrever_texto(g, texto);
	g_string_append_c(g, '}');
	struct json *ev = json_ler(g->str);
	if (c->o && c->o->evento) {
		c->o->evento(ev, c->o->dados);
	}
	json_liberar(ev);
	g_string_free(g, TRUE);
}

bool emmal_conversar(const char *sid, const char *texto, bool voz, const char *lumna_tools_json, const char *contexto, const struct emmab_ouvinte *o,
	char **resposta, char **erro) {
	struct emmal_prov prov;
	if (!emmal_provedor("chat", &prov, erro)) {
		return false;
	}
	if (!emmas_existe(sid)) {
		*erro = g_strdup("Essa conversa não existe mais.");
		return false;
	}
	char *hist = emmas_historico(sid, HISTORICO_MAX), *sistema = prompt_sistema(voz, contexto), *tools = ferramentas_openai(lumna_tools_json);
	emmas_anexar(sid, "user", texto, NULL);

	GString *msgs = g_string_new("[{\"role\":\"system\",\"content\":");
	json_escrever_texto(msgs, sistema);
	g_string_append_c(msgs, '}');
	size_t hl = strlen(hist);
	if (hl > 2) { /* "[...]" sem os colchetes */
		g_string_append_c(msgs, ',');
		g_string_append_len(msgs, hist + 1, (gssize)(hl - 2));
	}
	g_string_append(msgs, ",{\"role\":\"user\",\"content\":");
	json_escrever_texto(msgs, texto);
	g_string_append(msgs, "}");
	g_free(hist);
	g_free(sistema);

	GString *feitas = g_string_new("["); /* o que a Emma fez, para guardar com a resposta */
	bool ok = false;
	GString *final = g_string_new(NULL);
	struct delta_ctx dc = { .o = o };
	for (int rodada = 0; rodada < RODADAS_MAX; rodada++) {
		struct resposta r;
		resposta_iniciar(&r);
		char *arr = g_strdup_printf("%s]", msgs->str);
		char *e = NULL;
		bool falou = pedir(&prov, arr, tools, NULL, delta_para_ouvinte, &dc, &r, &e);
		g_free(arr);
		if (!falou) {
			*erro = e ? e : g_strdup("A conversa foi cortada.");
			resposta_liberar(&r);
			break;
		}
		g_string_append(final, r.texto->str);
		if (!r.chamadas->len || !o || !o->acao) {
			ok = true;
			resposta_liberar(&r);
			break;
		}
		/* a Emma quer agir: roda as ferramentas e volta ao provedor com o resultado */
		g_string_append(msgs, ",{\"role\":\"assistant\",\"content\":");
		json_escrever_texto(msgs, r.texto->str);
		g_string_append(msgs, ",\"tool_calls\":[");
		for (guint i = 0; i < r.chamadas->len; i++) {
			struct tcall *t = r.chamadas->pdata[i];
			if (!t->id) {
				t->id = g_strdup_printf("call_%u_%u", (guint)rodada, i);
			}
			g_string_append_printf(msgs, "%s{\"id\":", i ? "," : "");
			json_escrever_texto(msgs, t->id);
			g_string_append(msgs, ",\"type\":\"function\",\"function\":{\"name\":");
			json_escrever_texto(msgs, t->nome ? t->nome : "");
			g_string_append(msgs, ",\"arguments\":");
			json_escrever_texto(msgs, t->args->len ? t->args->str : "{}");
			g_string_append(msgs, "}}");
		}
		g_string_append_c(msgs, ']');
		g_string_append_c(msgs, '}');
		for (guint i = 0; i < r.chamadas->len; i++) {
			struct tcall *t = r.chamadas->pdata[i];
			struct json *args = json_ler(t->args->len ? t->args->str : "{}");
			struct emmab_acao res = { 0 };
			if (t->nome) {
				o->acao(t->nome, args ? args : NULL, &res, o->dados);
			}
			if (feitas->len > 1) {
				g_string_append_c(feitas, ',');
			}
			g_string_append(feitas, "{\"name\":");
			json_escrever_texto(feitas, t->nome ? t->nome : "");
			g_string_append(feitas, ",\"args\":");
			if (args) {
				json_escrever(feitas, args);
			} else {
				g_string_append(feitas, "{}");
			}
			g_string_append_printf(feitas, ",\"status\":\"%s\",\"summary\":", res.ok ? "ok" : "error");
			json_escrever_texto(feitas, res.resumo ? res.resumo : "");
			g_string_append_c(feitas, '}');
			g_string_append(msgs, ",{\"role\":\"tool\",\"tool_call_id\":");
			json_escrever_texto(msgs, t->id);
			g_string_append(msgs, ",\"content\":");
			json_escrever_texto(msgs, res.conteudo ? res.conteudo : res.resumo ? res.resumo : "");
			g_string_append_c(msgs, '}');
			g_free(res.resumo);
			g_free(res.conteudo);
			json_liberar(args);
		}
		/* o que a Emma falou antes de agir fica separado do que ela diz depois */
		if (final->len) {
			g_string_append(final, "\n\n");
		}
		resposta_liberar(&r);
		ok = true; /* se acabarem as rodadas, vale o que já foi dito */
	}
	g_string_append_c(feitas, ']');
	if (final->len || ok) {
		char *limpo = g_strstrip(g_strdup(final->str));
		emmas_anexar(sid, "assistant", limpo, feitas->str);
		*resposta = limpo;
	}
	g_string_free(feitas, TRUE);
	g_string_free(final, TRUE);
	g_string_free(msgs, TRUE);
	g_free(tools);
	return ok && !*erro;
}

/* ---- para o Painel ---- */

static char *json_de_erro(const char *msg) {
	GString *g = g_string_new("{\"ok\":false,\"error\":");
	json_escrever_texto(g, msg);
	g_string_append_c(g, '}');
	return g_string_free(g, FALSE);
}

static int comparar_texto(gconstpointer a, gconstpointer b) {
	return g_strcmp0(*(const char *const *)a, *(const char *const *)b);
}

char *emmal_modelos_json(const char *url, const char *chave) {
	sessao_iniciar();
	struct emmal_prov p = { 0 };
	g_strlcpy(p.nome, "O provedor", sizeof(p.nome));
	g_strlcpy(p.url, url, sizeof(p.url));
	g_strlcpy(p.chave, chave, sizeof(p.chave));
	char *u = url_de(&p, "/models");
	SoupMessage *m = soup_message_new("GET", u);
	g_free(u);
	if (!m) {
		return json_de_erro("O endereço não parece certo.");
	}
	if (p.chave[0]) {
		char *auth = g_strdup_printf("Bearer %s", p.chave);
		soup_message_headers_replace(soup_message_get_request_headers(m), "Authorization", auth);
		g_free(auth);
	}
	GError *e = NULL;
	GBytes *b = soup_session_send_and_read(sessao, m, NULL, &e);
	char *r = NULL;
	if (!b) {
		char *msg = g_strdup_printf("Não consegui falar com o provedor: %s", e ? e->message : "sem resposta");
		r = json_de_erro(msg);
		g_free(msg);
	} else {
		gsize n;
		const char *d = g_bytes_get_data(b, &n);
		char *txt = g_strndup(d, n);
		int st = (int)soup_message_get_status(m);
		if (st < 200 || st >= 300) {
			char *msg = erro_do_provedor(&p, st, txt);
			r = json_de_erro(msg);
			g_free(msg);
		} else {
			struct json *j = json_ler(txt);
			const struct json *lista = json_chave(j, "data");
			if (!lista) {
				lista = json_chave(j, "models"); /* Ollama nativo */
			}
			GPtrArray *ids = g_ptr_array_new_with_free_func(g_free);
			for (int i = 0; lista && i < lista->nfilhos; i++) {
				const struct json *x = json_item(lista, i);
				const char *id = json_chave_texto(x, "id");
				if (!*id) {
					id = json_chave_texto(x, "name");
				}
				if (*id) {
					g_ptr_array_add(ids, g_strdup(g_str_has_prefix(id, "models/") ? id + 7 : id));
				}
			}
			g_ptr_array_sort(ids, comparar_texto);
			GString *g = g_string_new("{\"ok\":true,\"models\":[");
			for (guint i = 0; i < ids->len; i++) {
				g_string_append_printf(g, "%s", i ? "," : "");
				json_escrever_texto(g, ids->pdata[i]);
			}
			g_string_append(g, "]}");
			r = g_string_free(g, FALSE);
			g_ptr_array_free(ids, TRUE);
			json_liberar(j);
		}
		g_free(txt);
		g_bytes_unref(b);
	}
	g_clear_error(&e);
	g_object_unref(m);
	return r;
}

char *emmal_testar_json(const char *url, const char *chave, const char *modelo) {
	struct emmal_prov p = { 0 };
	g_strlcpy(p.nome, "O provedor", sizeof(p.nome));
	g_strlcpy(p.url, url, sizeof(p.url));
	g_strlcpy(p.chave, chave, sizeof(p.chave));
	g_strlcpy(p.modelo, modelo, sizeof(p.modelo));
	struct resposta r;
	resposta_iniciar(&r);
	char *erro = NULL;
	gint64 t0 = g_get_monotonic_time();
	bool chat = pedir(&p, "[{\"role\":\"user\",\"content\":\"Responda apenas: ok\"}]", NULL, NULL, NULL, NULL, &r, &erro);
	int ms = (int)((g_get_monotonic_time() - t0) / 1000);
	bool stream = r.stream, json_mode = false;
	GString *g = g_string_new(NULL);
	if (!chat) {
		g_string_append(g, "{\"ok\":true,\"chat\":false,\"streaming\":false,\"json_mode\":false,\"error\":");
		json_escrever_texto(g, erro ? erro : "falhou");
		g_string_append_c(g, '}');
	} else {
		struct resposta r2;
		resposta_iniciar(&r2);
		char *e2 = NULL;
		if (pedir(&p, "[{\"role\":\"user\",\"content\":\"Retorne o JSON {\\\"ok\\\": true}\"}]", NULL, "\"response_format\":{\"type\":\"json_object\"}", NULL, NULL, &r2, &e2)) {
			json_mode = strstr(r2.texto->str, "ok") != NULL;
		}
		g_free(e2);
		resposta_liberar(&r2);
		g_string_append_printf(g, "{\"ok\":true,\"chat\":true,\"streaming\":%s,\"json_mode\":%s,\"latency_ms\":%d,\"error\":\"\"}", stream ? "true" : "false",
			json_mode ? "true" : "false", ms);
	}
	g_free(erro);
	resposta_liberar(&r);
	return g_string_free(g, FALSE);
}
