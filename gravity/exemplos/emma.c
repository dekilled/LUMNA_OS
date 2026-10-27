/*
 * lumna-emma: a Emma no sistema, o tempo todo. Um serviço da sessão (sem janela) que faz a ponte entre as
 * pessoas e o backend da Emma e entre a Emma e o computador:
 *
 *   quem pede      a topbar (ícone), o chat lateral, o painel, o controle do celular, a linha de comando
 *   o que faz      ouve o microfone do sistema, entende comandos curtos sozinho ("pausa", "abre a Netflix",
 *                  "volume 30"), manda o resto para a Emma, executa as ações que ela pede (a Mídia primeiro)
 *                  e fala a resposta
 *
 * Os outros programas falam com ele pelo socket $XDG_RUNTIME_DIR/lumna-emma.sock (ver emmac.h);
 * `lumna-emma <pedido>` faz um pedido na linha de comando (lumna-emma ouvir, lumna-emma enviar "que horas são").
 * Sem argumentos, corre como serviço.
 */
#define _GNU_SOURCE
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emmaa.h"
#include "emmab.h"
#include "emmac.h"
#include "emmav.h"
#include "json.h"
#include "midiac.h"
#include "modos.h"

static GMainLoop *loop;
static bool online;
static bool ouvindo, transcrevendo, pensando;
static char estado_atual[16] = "ocioso";

/* ---- os clientes do socket ---- */

struct cliente {
	GSocketConnection *con;
	GDataInputStream *in;
	GOutputStream *out;
	bool assina;
	bool vivo;
};

static GPtrArray *clientes;

static void escrever(struct cliente *c, const char *linha) {
	if (!c->vivo) {
		return;
	}
	char *l = g_strdup_printf("%s\n", linha);
	if (!g_output_stream_write_all(c->out, l, strlen(l), NULL, NULL, NULL)) {
		c->vivo = false;
	}
	g_free(l);
}

static void emitir(const char *json) {
	for (guint i = 0; i < clientes->len; i++) {
		struct cliente *c = clientes->pdata[i];
		if (c->assina) {
			escrever(c, json);
		}
	}
}

/* de que conversa vêm os eventos de agora: "assist" (o painel flutuante e o que se fala) ou "app" (o app completo, por sessão) */
static const char *emit_canal = "assist";
static char emit_sid[80];

static bool evento_de_conversa(const char *e) {
	return !strcmp(e, "ouvido") || !strcmp(e, "delta") || !strcmp(e, "resposta") || !strcmp(e, "ferramenta") || !strcmp(e, "confirmar") || !strcmp(e, "nova");
}

static void emitir_conv(const char *canal, const char *sid, const char *evento, const char *chaves_json) {
	GString *g = g_string_new("{\"evento\":");
	json_escrever_texto(g, evento);
	if (chaves_json && *chaves_json) {
		g_string_append_printf(g, ",%s", chaves_json);
	}
	if (evento_de_conversa(evento)) {
		g_string_append_printf(g, ",\"canal\":\"%s\",\"sessao\":", canal);
		json_escrever_texto(g, sid);
	}
	g_string_append_c(g, '}');
	emitir(g->str);
	g_string_free(g, TRUE);
}

static void emitir_form(const char *evento, const char *chaves_json) {
	emitir_conv(emit_canal, emit_sid, evento, chaves_json);
}

static char *js(const char *s) {
	GString *g = g_string_new(NULL);
	json_escrever_texto(g, s ? s : "");
	return g_string_free(g, FALSE);
}

/* ---- o estado ---- */

static void recalcular_estado(void) {
	const char *e = ouvindo ? "ouvindo" : transcrevendo ? "transcrevendo" : emmav_falando() ? "falando" : pensando ? "pensando" : "ocioso";
	if (strcmp(e, estado_atual)) {
		g_strlcpy(estado_atual, e, sizeof(estado_atual));
		char *k = g_strdup_printf("\"estado\":\"%s\",\"online\":%s", e, online ? "true" : "false");
		emitir_form("estado", k);
		g_free(k);
	}
}

static void erro_para_todos(const char *msg) {
	char *m = js(msg), *k = g_strdup_printf("\"mensagem\":%s", m);
	emitir_form("erro", k);
	g_free(k);
	g_free(m);
}

static void voz_estado(const char *e, void *d) {
	(void)e;
	(void)d;
	g_idle_add_once((GSourceOnceFunc)recalcular_estado, NULL);
}

/* ---- as ações pedidas pela Emma (a linha da conversa pede, a principal executa) ---- */

struct pedido_acao {
	char nome[80];
	struct json *args;
	struct emmaa_res res;
	GMutex trinco;
	GCond cond;
	bool pronto;
	bool esperando_licenca;
	char id[24];
};

static GHashTable *licencas; /* id -> struct pedido_acao */
static int proximo_id;

static void acao_terminar(struct pedido_acao *p) {
	char *nome = js(p->nome), *resumo = js(p->res.resumo);
	GString *g = g_string_new(NULL);
	g_string_printf(g, "\"nome\":%s,\"ok\":%s,\"resumo\":%s,\"args\":", nome, p->res.ok ? "true" : "false", resumo);
	if (p->args) {
		json_escrever(g, p->args);
	} else {
		g_string_append(g, "{}");
	}
	emitir_form("ferramenta", g->str);
	g_string_free(g, TRUE);
	g_free(nome);
	g_free(resumo);
	g_mutex_lock(&p->trinco);
	p->pronto = true;
	g_cond_signal(&p->cond);
	g_mutex_unlock(&p->trinco);
}

static gboolean acao_na_principal(gpointer d) {
	struct pedido_acao *p = d;
	const struct emmaa_def *def = emmaa_achar_ferramenta(p->nome);
	if (!def) {
		p->res.ok = false;
		p->res.resumo = g_strdup("Não conheço essa ação.");
		acao_terminar(p);
		return G_SOURCE_REMOVE;
	}
	const char *perm = emmaa_permissao(def);
	if (!strcmp(perm, "off")) {
		p->res.ok = false;
		p->res.resumo = g_strdup("Essa ação está desligada nas permissões da Emma.");
		acao_terminar(p);
	} else if (!strcmp(perm, "ask")) {
		g_snprintf(p->id, sizeof(p->id), "c%d", ++proximo_id);
		p->esperando_licenca = true;
		g_hash_table_insert(licencas, g_strdup(p->id), p);
		char *id = js(p->id), *rot = js(def->rotulo), *desc = js(def->descricao);
		char *k = g_strdup_printf("\"id\":%s,\"nome\":%s,\"descricao\":%s", id, rot, desc);
		emitir_form("confirmar", k);
		g_free(k);
		g_free(id);
		g_free(rot);
		g_free(desc);
	} else {
		emmaa_executar(def, p->args, &p->res);
		acao_terminar(p);
	}
	return G_SOURCE_REMOVE;
}

static void decidir(const char *id, bool sim) {
	struct pedido_acao *p = g_hash_table_lookup(licencas, id);
	if (!p) {
		return;
	}
	g_hash_table_steal(licencas, id);
	p->esperando_licenca = false;
	const struct emmaa_def *def = emmaa_achar_ferramenta(p->nome);
	if (sim && def) {
		emmaa_executar(def, p->args, &p->res);
	} else {
		p->res.ok = false;
		p->res.resumo = g_strdup("Você não deixou.");
	}
	acao_terminar(p);
}

/* ---- uma conversa ---- */

struct conversa {
	char *sid;                 /* a sessão do app (NULL: a do painel) */
	char *extra;               /* pedaço do corpo do chat: "skill_id":"..." */
	bool app;
	char *texto;
	bool voz;
	char *origem;
	struct cliente *espera;    /* o celular que espera a resposta (pode ter caído: confira em clientes) */
	GString *fala;             /* o que ainda não foi falado */
	char *final;
	char *erro;
	bool ok;
};

static GQueue fila;
static bool ocupada;

static char *limpar_fala(const char *t) {
	GString *g = g_string_new(NULL);
	char **ls = g_strsplit(t, "\n", -1);
	for (int i = 0; ls[i]; i++) {
		const char *l = ls[i];
		while (*l == ' ') {
			l++;
		}
		/* os botões de próximo passo do popup não se leem em voz alta */
		if (g_str_has_prefix(l, "[acao:") || g_str_has_prefix(l, "[link:") || g_str_has_prefix(l, "[sugestao:")) {
			continue;
		}
		for (; *l; l++) {
			if (*l != '*' && *l != '#' && *l != '`' && *l != '_' && *l != '>') {
				g_string_append_c(g, *l);
			}
		}
		g_string_append_c(g, '\n');
	}
	g_strfreev(ls);
	char *r = g_strstrip(g_string_free(g, FALSE));
	return r;
}

static void falar_pedacos(struct conversa *c, bool fim) {
	for (;;) {
		char *p = c->fala->str;
		char *corte = NULL;
		for (char *q = p; *q; q++) {
			if ((*q == '.' || *q == '!' || *q == '?' || *q == '\n') && (q[1] == ' ' || q[1] == '\n' || q[1] == '\0') && (q[1] != '\0' || fim || *q == '\n')) {
				/* "3.5" e "a.m." não cortam: só se segue espaço, quebra de linha ou o fim */
				corte = q + 1;
				break;
			}
		}
		if (!corte && fim && *p) {
			corte = p + strlen(p);
		}
		if (!corte) {
			return;
		}
		char *frase = g_strndup(p, (gsize)(corte - p)), *limpa = limpar_fala(frase);
		if (*limpa) {
			emmav_falar(limpa);
		}
		g_free(frase);
		g_free(limpa);
		g_string_erase(c->fala, 0, corte - p);
	}
}

struct evento_ctx {
	char *tipo;
	char *conteudo;
	char *extra;
};

static gboolean evento_na_principal(gpointer d) {
	struct evento_ctx *e = d;
	if (!strcmp(e->tipo, "delta")) {
		char *t = js(e->conteudo), *k = g_strdup_printf("\"texto\":%s", t);
		emitir_form("delta", k);
		g_free(k);
		g_free(t);
	} else if (!strcmp(e->tipo, "tool_confirm")) {
		/* as ferramentas do próprio backend (internet...) pedem licença: nesta primeira versão ficam negadas na topbar */
	} else if (!strcmp(e->tipo, "error")) {
		erro_para_todos(e->conteudo);
	}
	g_free(e->tipo);
	g_free(e->conteudo);
	g_free(e->extra);
	g_free(e);
	return G_SOURCE_REMOVE;
}

static void ouvinte_evento(const struct json *ev, void *dados) {
	struct conversa *c = dados;
	const char *tipo = json_chave_texto(ev, "type");
	struct evento_ctx *e = g_new0(struct evento_ctx, 1);
	e->tipo = g_strdup(tipo);
	if (!strcmp(tipo, "delta")) {
		e->conteudo = g_strdup(json_chave_texto(ev, "content"));
		if (c->voz) {
			g_string_append(c->fala, e->conteudo);
			falar_pedacos(c, false);
		}
	} else if (!strcmp(tipo, "error")) {
		e->conteudo = g_strdup(json_chave_texto(ev, "message"));
	} else if (!strcmp(tipo, "tool_confirm")) {
		/* as ferramentas do próprio backend (internet, arquivos dele) pedem licença: sem tela para perguntar
		 * ainda, ficam negadas (senão a conversa espera para sempre) */
		char sid[80], *erro = NULL;
		if (emmab_sessao(EMMAB_ASSIST, sid, sizeof(sid), &erro)) {
			emmab_decidir(sid, json_chave_texto(json_chave(ev, "tool"), "id"), false);
		}
		g_free(erro);
	}
	g_main_context_invoke(NULL, evento_na_principal, e);
}

static void ouvinte_acao(const char *nome, const struct json *args, struct emmab_acao *r, void *dados) {
	(void)dados;
	struct pedido_acao *p = g_new0(struct pedido_acao, 1);
	g_mutex_init(&p->trinco);
	g_cond_init(&p->cond);
	g_strlcpy(p->nome, nome, sizeof(p->nome));
	GString *g = g_string_new(NULL);
	if (args) {
		json_escrever(g, args);
	}
	p->args = json_ler(g->len ? g->str : "{}");
	g_string_free(g, TRUE);
	g_main_context_invoke(NULL, acao_na_principal, p);
	gint64 limite = g_get_monotonic_time() + 45 * G_USEC_PER_SEC;
	g_mutex_lock(&p->trinco);
	while (!p->pronto) {
		if (!g_cond_wait_until(&p->cond, &p->trinco, limite)) {
			break;
		}
	}
	bool acabou = p->pronto;
	g_mutex_unlock(&p->trinco);
	if (!acabou) {
		/* ninguém respondeu à licença: nega (a principal solta o pedido quando chegar tarde) */
		r->ok = false;
		r->resumo = g_strdup("Sem resposta a tempo.");
		r->conteudo = g_strdup("A pessoa não respondeu ao pedido de licença.");
		return; /* o pedido fica vazando até a principal decidir: raro, e pequeno */
	}
	r->ok = p->res.ok;
	r->resumo = p->res.resumo;
	r->conteudo = p->res.conteudo ? p->res.conteudo : g_strdup(p->res.resumo);
	json_liberar(p->args);
	g_mutex_clear(&p->trinco);
	g_cond_clear(&p->cond);
	g_free(p);
}

static bool contexto_ativo = true;   /* o chip "Contexto: ..." do painel: a Emma sabe o que a pessoa está vendo */

/* o título e o app da janela em foco, pelo Orbit */
static bool janela_em_foco(char *titulo, size_t tt, char *app, size_t ta) {
	char *r = g_malloc(32 * 1024);
	bool ok = false;
	if (orbit_pedido("janelas", r, 32 * 1024)) {
		struct json *j = json_ler(r);
		const struct json *l = json_chave(j, "janelas");
		for (int i = 0; l && i < l->nfilhos; i++) {
			const struct json *w = json_item(l, i);
			if (json_bool(json_chave(w, "foco"), false)) {
				g_strlcpy(titulo, json_chave_texto(w, "titulo"), tt);
				g_strlcpy(app, json_chave_texto(w, "app_id"), ta);
				ok = true;
				break;
			}
		}
		json_liberar(j);
	}
	g_free(r);
	return ok;
}

static char *contexto_agora(void) {
	GString *s = g_string_new(NULL);
	char titulo[160], app[96];
	if (contexto_ativo && janela_em_foco(titulo, sizeof(titulo), app, sizeof(app)) && *titulo) {
		g_string_append_printf(s, "janela em foco: %s (%s); ", titulo, app);
	}
	struct modos_estado m;
	if (modos_ler(&m) && m.orbit) {
		for (int i = 0; i < m.n; i++) {
			g_string_append_printf(s, "%s no modo %s; ", m.saidas[i].nome, modos_nome(m.saidas[i].modo));
		}
	}
	struct midia_estado e;
	if (midiac_estado(&e) && e.ativo[0]) {
		const struct midia_vivo *v = midiac_achar(&e, e.ativo);
		g_string_append_printf(s, "na frente na Mídia: %s", v ? v->s.nome : e.ativo);
	}
	return g_string_free(s, FALSE);
}

static gboolean conversa_pronta(gpointer d);

static gpointer conversa_linha(gpointer d) {
	struct conversa *c = d;
	char *tools = emmaa_ferramentas_json(false), *ctx = contexto_agora();
	struct emmab_ouvinte o = { .evento = ouvinte_evento, .acao = ouvinte_acao, .dados = c };
	c->ok = emmab_conversar(c->app ? EMMAB_APP : EMMAB_ASSIST, c->sid, c->texto, c->voz, tools, c->app && !contexto_ativo ? NULL : ctx, c->extra, &o, &c->final, &c->erro);
	g_free(tools);
	g_free(ctx);
	if (c->voz && c->fala) {
		falar_pedacos(c, true);
	}
	g_main_context_invoke(NULL, conversa_pronta, c);
	return NULL;
}

static struct cliente *cliente_vivo(struct cliente *c) {
	for (guint i = 0; c && i < clientes->len; i++) {
		if (clientes->pdata[i] == c && c->vivo) {
			return c;
		}
	}
	return NULL;
}

static void proxima_conversa(void);

static gboolean conversa_pronta(gpointer d) {
	struct conversa *c = d;
	pensando = false;
	ocupada = false;
	if (c->erro) {
		erro_para_todos(c->erro);
	}
	char *r = js(c->final ? c->final : ""), *o = js(c->origem);
	char *k = g_strdup_printf("\"texto\":%s,\"origem\":%s,\"ok\":%s", r, o, c->ok ? "true" : "false");
	emitir_form("resposta", k);
	g_free(k);
	g_free(o);
	struct cliente *quem = cliente_vivo(c->espera);
	if (quem) {
		char *j = g_strdup_printf("{\"ok\":%s,\"resposta\":%s%s%s}", c->ok ? "true" : "false", r, c->erro ? ",\"erro\":" : "", "");
		if (c->erro) {
			char *e = js(c->erro);
			g_free(j);
			j = g_strdup_printf("{\"ok\":false,\"resposta\":%s,\"erro\":%s}", r, e);
			g_free(e);
		}
		escrever(quem, j);
		g_free(j);
	}
	g_free(r);
	g_free(c->texto);
	g_free(c->origem);
	g_free(c->final);
	g_free(c->erro);
	g_free(c->sid);
	g_free(c->extra);
	if (c->fala) {
		g_string_free(c->fala, TRUE);
	}
	g_free(c);
	recalcular_estado();
	proxima_conversa();
	return G_SOURCE_REMOVE;
}

static void proxima_conversa(void) {
	if (ocupada || g_queue_is_empty(&fila)) {
		return;
	}
	struct conversa *c = g_queue_pop_head(&fila);
	ocupada = true;
	pensando = true;
	emit_canal = c->app ? "app" : "assist";
	g_strlcpy(emit_sid, c->sid ? c->sid : "", sizeof(emit_sid));
	recalcular_estado();
	g_thread_unref(g_thread_new("emma-conversa", conversa_linha, c));
}

/* um texto entrou (digitado, falado ou vindo do celular): comando curto, ou conversa com a Emma */
static void processar_texto(const char *texto, const char *origem, bool voz, struct cliente *espera) {
	char *t = js(texto), *o = js(origem), *k = g_strdup_printf("\"texto\":%s,\"origem\":%s", t, o);
	emitir_conv("assist", "", "ouvido", k);
	g_free(k);
	g_free(t);
	g_free(o);
	struct emmaa_res r = { 0 };
	if (emmaa_intencao(texto, &r)) {
		char *nome = js("comando"), *resumo = js(r.resumo);
		char *k2 = g_strdup_printf("\"nome\":%s,\"ok\":%s,\"resumo\":%s", nome, r.ok ? "true" : "false", resumo);
		emitir_conv("assist", "", "ferramenta", k2);
		char *k3 = g_strdup_printf("\"texto\":%s,\"origem\":\"local\",\"ok\":%s", resumo, r.ok ? "true" : "false");
		emitir_conv("assist", "", "resposta", k3);
		if (cliente_vivo(espera)) {
			char *j = g_strdup_printf("{\"ok\":%s,\"resposta\":%s,\"local\":true}", r.ok ? "true" : "false", resumo);
			escrever(espera, j);
			g_free(j);
		}
		g_free(k2);
		g_free(k3);
		g_free(nome);
		g_free(resumo);
		emmaa_res_limpar(&r);
		return;
	}
	struct conversa *c = g_new0(struct conversa, 1);
	c->texto = g_strdup(texto);
	c->voz = voz;
	c->origem = g_strdup(origem);
	c->espera = espera;
	c->fala = voz ? g_string_new(NULL) : NULL;
	g_queue_push_tail(&fila, c);
	proxima_conversa();
}

/* ---- o microfone ---- */

struct nivel_ctx {
	double nivel;
	bool fala;
};

static gboolean nivel_na_principal(gpointer d) {
	struct nivel_ctx *n = d;
	char *k = g_strdup_printf("\"nivel\":%.2f,\"fala\":%s", n->nivel, n->fala ? "true" : "false");
	emitir_form("nivel", k);
	g_free(k);
	g_free(n);
	return G_SOURCE_REMOVE;
}

static void nivel_cb(double nivel, bool fala, void *d) {
	(void)d;
	struct nivel_ctx *n = g_new(struct nivel_ctx, 1);
	n->nivel = nivel;
	n->fala = fala;
	g_main_context_invoke(NULL, nivel_na_principal, n);
}

struct ouvido_res {
	char *texto, *erro;
};

static gboolean ouvido_pronto(gpointer d) {
	struct ouvido_res *r = d;
	ouvindo = transcrevendo = false;
	if (r->erro) {
		erro_para_todos(r->erro);
	}
	if (r->texto && *r->texto) {
		processar_texto(r->texto, "mic", true, NULL);
	}
	g_free(r->texto);
	g_free(r->erro);
	g_free(r);
	recalcular_estado();
	return G_SOURCE_REMOVE;
}

static gboolean transcrevendo_ligar(gpointer d) {
	(void)d;
	ouvindo = false;
	transcrevendo = true;
	recalcular_estado();
	return G_SOURCE_REMOVE;
}

static gpointer ouvir_linha(gpointer d) {
	(void)d;
	struct ouvido_res *r = g_new0(struct ouvido_res, 1);
	char *erro = NULL;
	char *wav = emmav_ouvir(nivel_cb, NULL, &erro);
	if (wav) {
		g_main_context_invoke(NULL, transcrevendo_ligar, NULL);
		char *texto = NULL;
		if (emmab_stt(wav, &texto, &r->erro)) {
			r->texto = texto;
		}
		g_remove(wav);
		g_free(wav);
	}
	if (!r->erro) {
		r->erro = erro;
	} else {
		g_free(erro);
	}
	g_main_context_invoke(NULL, ouvido_pronto, r);
	return NULL;
}

static void ouvir_alternar(void) {
	if (ouvindo) {
		emmav_ouvir_parar(false); /* termina a fala e transcreve o que já tem */
		return;
	}
	if (transcrevendo) {
		return;
	}
	emmav_calar();
	ouvindo = true;
	recalcular_estado();
	g_thread_unref(g_thread_new("emma-ouvir", ouvir_linha, NULL));
}

/* ---- os pedidos ---- */

static void responder_ok(struct cliente *c) {
	escrever(c, "{\"ok\":true}");
}

static void responder_erro(struct cliente *c, const char *m) {
	char *e = js(m), *j = g_strdup_printf("{\"ok\":false,\"erro\":%s}", e);
	escrever(c, j);
	g_free(j);
	g_free(e);
}

static void pedido_acoes(struct cliente *c) {
	GString *g = g_string_new("{\"ok\":true,\"acoes\":[");
	int n;
	const struct emmaa_def *cat = emmaa_catalogo(&n);
	for (int i = 0; i < n; i++) {
		char *nome = js(cat[i].nome), *rot = js(cat[i].rotulo), *desc = js(cat[i].descricao);
		g_string_append_printf(g, "%s{\"nome\":%s,\"rotulo\":%s,\"descricao\":%s,\"risco\":%d,\"permissao\":\"%s\"}", i ? "," : "", nome, rot, desc, cat[i].risco,
			emmaa_permissao(&cat[i]));
		g_free(nome);
		g_free(rot);
		g_free(desc);
	}
	g_string_append(g, "]}");
	escrever(c, g->str);
	g_string_free(g, TRUE);
}

/* o histórico da conversa do painel: buscado numa linha à parte e respondido quando chegar */
struct hist_ctx {
	struct cliente *c;
	char *json;
};

static char *hora_local(const char *iso) {
	GDateTime *t = g_date_time_new_from_iso8601(iso, NULL);
	char *r = NULL;
	if (t) {
		GDateTime *l = g_date_time_to_local(t);
		r = g_date_time_format(l, "%H:%M");
		g_date_time_unref(l);
		g_date_time_unref(t);
	}
	return r ? r : g_strdup("");
}

static gboolean hist_pronto(gpointer d) {
	struct hist_ctx *h = d;
	if (cliente_vivo(h->c)) {
		escrever(h->c, h->json);
	}
	g_free(h->json);
	g_free(h);
	return G_SOURCE_REMOVE;
}

static gpointer hist_linha(gpointer d) {
	struct hist_ctx *h = d;
	char sid[80], *erro = NULL, *corpo = NULL;
	GString *g = g_string_new("{\"ok\":true,\"mensagens\":[");
	if (emmab_sessao(EMMAB_ASSIST, sid, sizeof(sid), &erro)) {
		char *caminho = g_strdup_printf("/api/sessions/%s/messages", sid);
		if (emmab_chamar("GET", caminho, NULL, &corpo, &erro) == 200) {
			struct json *j = json_ler(corpo);
			bool primeiro = true;
			for (int i = 0; j && j->tipo == JSON_LISTA && i < j->nfilhos; i++) {
				const struct json *m = json_item(j, i);
				const char *papel = json_chave_texto(m, "role");
				if (strcmp(papel, "user") && strcmp(papel, "assistant")) {
					continue;
				}
				char *hora = hora_local(json_chave_texto(m, "created_at"));
				g_string_append_printf(g, "%s{\"papel\":\"%s\",\"texto\":", primeiro ? "" : ",", papel);
				json_escrever_texto(g, json_chave_texto(m, "content"));
				g_string_append(g, ",\"hora\":");
				json_escrever_texto(g, hora);
				g_string_append(g, ",\"ferramentas\":[");
				const struct json *ts = json_chave(m, "tools");
				bool pt = true;
				for (int k = 0; ts && k < ts->nfilhos; k++) {
					const struct json *t = json_item(ts, k);
					g_string_append_printf(g, "%s{\"nome\":", pt ? "" : ",");
					json_escrever_texto(g, json_chave_texto(t, "name"));
					g_string_append(g, ",\"resumo\":");
					json_escrever_texto(g, json_chave_texto(t, "summary"));
					g_string_append_printf(g, ",\"ok\":%s,\"args\":", !strcmp(json_chave_texto(t, "status"), "ok") ? "true" : "false");
					const struct json *a = json_chave(t, "args");
					if (a) {
						json_escrever(g, a);
					} else {
						g_string_append(g, "{}");
					}
					g_string_append_c(g, '}');
					pt = false;
				}
				g_string_append(g, "]}");
				g_free(hora);
				primeiro = false;
			}
			json_liberar(j);
		}
		g_free(caminho);
	}
	g_string_append_c(g, ']');
	if (erro) {
		char *e = js(erro);
		g_string_append_printf(g, ",\"erro\":%s", e);
		g_free(e);
	}
	g_string_append_c(g, '}');
	h->json = g_string_free(g, FALSE);
	g_free(corpo);
	g_free(erro);
	g_main_context_invoke(NULL, hist_pronto, h);
	return NULL;
}

static gboolean sair(gpointer d);
static gboolean reiniciar_agora(gpointer d);

/* uma chamada REST ao backend, repassada pelo serviço (o app em C não precisa de HTTP nem de token): rest METODO CAMINHO [CORPO] */
struct rest_ctx {
	struct cliente *c;
	char *metodo, *caminho, *corpo, *json;
};

static gboolean rest_pronto(gpointer d) {
	struct rest_ctx *r = d;
	if (cliente_vivo(r->c)) {
		escrever(r->c, r->json);
	}
	g_free(r->metodo);
	g_free(r->caminho);
	g_free(r->corpo);
	g_free(r->json);
	g_free(r);
	return G_SOURCE_REMOVE;
}

static gpointer rest_linha(gpointer d) {
	struct rest_ctx *r = d;
	char *resp = NULL, *erro = NULL;
	int st = emmab_chamar(r->metodo, r->caminho, r->corpo && *r->corpo ? r->corpo : NULL, &resp, &erro);
	GString *g = g_string_new(NULL);
	g_string_printf(g, "{\"ok\":%s,\"status\":%d,\"corpo\":", st >= 200 && st < 300 ? "true" : "false", st);
	struct json *j = resp && *resp ? json_ler(resp) : NULL;
	if (j) {
		g_string_append(g, resp);
		json_liberar(j);
	} else if (resp && *resp) {
		json_escrever_texto(g, resp);
	} else {
		g_string_append(g, "null");
	}
	if (erro) {
		g_string_append(g, ",\"erro\":");
		json_escrever_texto(g, erro);
	}
	g_string_append_c(g, '}');
	r->json = g_string_free(g, FALSE);
	g_free(resp);
	g_free(erro);
	g_main_context_invoke(NULL, rest_pronto, r);
	return NULL;
}

/* conversar {"sid":"...","texto":"...","skill":"...","contexto":true}: uma mensagem numa sessão do app */
static void conversar_app(struct cliente *c, const char *corpo) {
	struct json *j = json_ler(corpo);
	const char *sid = json_chave_texto(j, "sid"), *texto = json_chave_texto(j, "texto"), *skill = json_chave_texto(j, "skill");
	if (!*sid || !*texto) {
		json_liberar(j);
		responder_erro(c, "falta sid ou texto");
		return;
	}
	struct conversa *v = g_new0(struct conversa, 1);
	v->app = true;
	v->sid = g_strdup(sid);
	v->texto = g_strdup(texto);
	v->origem = g_strdup("app");
	if (*skill) {
		GString *e = g_string_new("\"skill_id\":");
		json_escrever_texto(e, skill);
		v->extra = g_string_free(e, FALSE);
	}
	char *t = js(texto), *k = g_strdup_printf("\"texto\":%s,\"origem\":\"app\"", t);
	emitir_conv("app", sid, "ouvido", k);
	g_free(k);
	g_free(t);
	json_liberar(j);
	g_queue_push_tail(&fila, v);
	proxima_conversa();
	responder_ok(c);
}

static void tratar(struct cliente *c, char *linha) {
	linha = g_strstrip(linha);
	char *resto = strchr(linha, ' ');
	if (resto) {
		*resto++ = '\0';
		resto = g_strstrip(resto);
	}
	const char *cmd = linha;
	if (!strcmp(cmd, "estado")) {
		char *j = g_strdup_printf("{\"ok\":true,\"estado\":\"%s\",\"online\":%s}", estado_atual, online ? "true" : "false");
		escrever(c, j);
		g_free(j);
	} else if (!strcmp(cmd, "assinar")) {
		c->assina = true;
		char *j = g_strdup_printf("{\"evento\":\"estado\",\"estado\":\"%s\",\"online\":%s}", estado_atual, online ? "true" : "false");
		escrever(c, j);
		g_free(j);
	} else if (!strcmp(cmd, "ouvir")) {
		ouvir_alternar();
		responder_ok(c);
	} else if (!strcmp(cmd, "ouvir_cancelar")) {
		emmav_ouvir_parar(true);
		responder_ok(c);
	} else if (!strcmp(cmd, "calar")) {
		emmav_calar();
		emmab_parar();
		g_queue_clear(&fila);
		responder_ok(c);
	} else if (!strcmp(cmd, "enviar") || !strcmp(cmd, "enviar_voz")) {
		if (!resto || !*resto) {
			responder_erro(c, "falta o texto");
		} else {
			processar_texto(resto, "texto", !strcmp(cmd, "enviar_voz"), NULL);
			responder_ok(c);
		}
	} else if (!strcmp(cmd, "celular")) {
		if (!resto || !*resto) {
			responder_erro(c, "falta o texto");
		} else {
			/* a resposta sai quando a Emma acabar (ou na hora, num comando curto) */
			processar_texto(resto, "celular", false, c);
		}
	} else if (!strcmp(cmd, "confirmar")) {
		char *sp = resto ? strchr(resto, ' ') : NULL;
		if (!sp) {
			responder_erro(c, "confirmar ID sim|nao");
		} else {
			*sp++ = '\0';
			decidir(resto, !strcmp(g_strstrip(sp), "sim"));
			responder_ok(c);
		}
	} else if (!strcmp(cmd, "nova")) {
		emmab_nova_conversa(EMMAB_ASSIST);
		emitir_form("nova", "");
		responder_ok(c);
	} else if (!strcmp(cmd, "historico")) {
		struct hist_ctx *h = g_new0(struct hist_ctx, 1);
		h->c = c;
		g_thread_unref(g_thread_new("emma-historico", hist_linha, h));
	} else if (!strcmp(cmd, "rest")) {
		/* rest METODO CAMINHO [CORPO] */
		char *sp1 = resto ? strchr(resto, ' ') : NULL;
		if (!sp1) {
			responder_erro(c, "rest METODO CAMINHO [CORPO]");
		} else {
			*sp1++ = '\0';
			char *corpo = strchr(sp1, ' ');
			if (corpo) {
				*corpo++ = '\0';
			}
			if (!g_str_has_prefix(sp1, "/api/")) {
				responder_erro(c, "só caminhos /api/");
			} else {
				struct rest_ctx *r = g_new0(struct rest_ctx, 1);
				r->c = c;
				r->metodo = g_strdup(resto);
				r->caminho = g_strdup(sp1);
				r->corpo = g_strdup(corpo ? corpo : "");
				g_thread_unref(g_thread_new("emma-rest", rest_linha, r));
			}
		}
	} else if (!strcmp(cmd, "conversar")) {
		conversar_app(c, resto ? resto : "");
	} else if (!strcmp(cmd, "contexto")) {
		if (resto && (!strcmp(resto, "sim") || !strcmp(resto, "nao"))) {
			contexto_ativo = !strcmp(resto, "sim");
		}
		char titulo[160] = "", app[96] = "";
		janela_em_foco(titulo, sizeof(titulo), app, sizeof(app));
		char *t = js(titulo), *a = js(app);
		char *j = g_strdup_printf("{\"ok\":true,\"ativo\":%s,\"titulo\":%s,\"app\":%s}", contexto_ativo ? "true" : "false", t, a);
		escrever(c, j);
		g_free(j);
		g_free(t);
		g_free(a);
	} else if (!strcmp(cmd, "acoes")) {
		pedido_acoes(c);
	} else if (!strcmp(cmd, "reiniciar")) {
		responder_ok(c);
		g_timeout_add(200, reiniciar_agora, NULL);
	} else if (!strcmp(cmd, "encerrar")) {
		responder_ok(c);
		g_timeout_add(200, sair, NULL);
	} else {
		responder_erro(c, "pedido desconhecido");
	}
}

static void cliente_fechar(struct cliente *c) {
	c->vivo = false;
	c->assina = false;
	g_ptr_array_remove(clientes, c);
	g_io_stream_close(G_IO_STREAM(c->con), NULL, NULL);
	g_object_unref(c->in);
	g_object_unref(c->con);
	g_free(c);
}

static void cliente_linha(GObject *o, GAsyncResult *r, gpointer d) {
	struct cliente *c = d;
	char *linha = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(o), r, NULL, NULL);
	if (!linha) {
		cliente_fechar(c);
		return;
	}
	tratar(c, linha);
	g_free(linha);
	if (!c->vivo) {
		cliente_fechar(c);
		return;
	}
	g_data_input_stream_read_line_async(c->in, G_PRIORITY_DEFAULT, NULL, cliente_linha, c);
}

static gboolean chegou(GSocketService *s, GSocketConnection *con, GObject *src, gpointer d) {
	(void)s;
	(void)src;
	(void)d;
	struct cliente *c = g_new0(struct cliente, 1);
	c->con = g_object_ref(con);
	c->in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(con)));
	c->out = g_io_stream_get_output_stream(G_IO_STREAM(con));
	c->vivo = true;
	/* sem limite de tempo no socket: quem assina (a topbar, o painel) fica parado esperando eventos por horas */
	g_ptr_array_add(clientes, c);
	g_data_input_stream_read_line_async(c->in, G_PRIORITY_DEFAULT, NULL, cliente_linha, c);
	return TRUE;
}

/* ---- o backend está no ar? ---- */

static gboolean online_pronto(gpointer d) {
	bool agora = GPOINTER_TO_INT(d);
	if (agora != online) {
		online = agora;
		estado_atual[0] = '\0'; /* força o evento de estado, que leva o online junto */
		recalcular_estado();
	}
	return G_SOURCE_REMOVE;
}

static gpointer online_linha(gpointer d) {
	(void)d;
	g_main_context_invoke(NULL, online_pronto, GINT_TO_POINTER(emmab_no_ar()));
	return NULL;
}

static gboolean verificar_online(gpointer d) {
	(void)d;
	g_thread_unref(g_thread_new("emma-online", online_linha, NULL));
	return G_SOURCE_CONTINUE;
}

/* ---- main ---- */

static int como_cliente(int argc, char **argv) {
	GString *p = g_string_new(NULL);
	for (int i = 1; i < argc; i++) {
		g_string_append_printf(p, "%s%s", i > 1 ? " " : "", argv[i]);
	}
	char r[16384];
	if (!emmac_pedido(p->str, r, sizeof(r))) {
		fprintf(stderr, "lumna-emma: o serviço não responde (lumna-emma sem argumentos o liga)\n");
		g_string_free(p, TRUE);
		return 1;
	}
	puts(r);
	g_string_free(p, TRUE);
	return strstr(r, "\"ok\":false") ? 2 : 0;
}

static gboolean sair(gpointer d) {
	(void)d;
	g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}

static char **argv_original;

/* o serviço recomeça do zero (a conversa e as contas ficam no backend; o que se perde é só o que estava em andamento) */
static gboolean reiniciar_agora(gpointer d) {
	(void)d;
	emmav_calar();
	emmab_parar();
	char *caminho = emmac_socket_caminho();
	g_unlink(caminho);
	g_free(caminho);
	execv("/proc/self/exe", argv_original);
	return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
	if (argc > 1) {
		return como_cliente(argc, argv);
	}
	signal(SIGPIPE, SIG_IGN);
	argv_original = argv;
	/* um só serviço: se já responde, este não faz nada (senão tomava o socket do outro) */
	char resp[256];
	if (emmac_pedido("estado", resp, sizeof(resp))) {
		fprintf(stderr, "lumna-emma: já está rodando\n");
		return 0;
	}
	clientes = g_ptr_array_new();
	licencas = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_queue_init(&fila);
	emmaa_iniciar();
	emmav_falar_iniciar(voz_estado, NULL);

	char *caminho = emmac_socket_caminho();
	g_unlink(caminho);
	GSocketService *servico = g_socket_service_new();
	GError *e = NULL;
	GSocketAddress *addr = g_unix_socket_address_new(caminho);
	if (!g_socket_listener_add_address(G_SOCKET_LISTENER(servico), addr, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, NULL, NULL, &e)) {
		fprintf(stderr, "lumna-emma: não abri %s: %s\n", caminho, e->message);
		return 1;
	}
	chmod(caminho, 0600);
	g_signal_connect(servico, "incoming", G_CALLBACK(chegou), NULL);
	g_socket_service_start(servico);

	loop = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGTERM, sair, NULL);
	g_unix_signal_add(SIGINT, sair, NULL);
	verificar_online(NULL);
	g_timeout_add_seconds(20, verificar_online, NULL);
	g_main_loop_run(loop);
	g_unlink(caminho);
	return 0;
}
