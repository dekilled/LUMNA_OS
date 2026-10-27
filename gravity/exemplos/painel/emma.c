/*
 * Emma > Provedores e Roteamento: as telas do harness (Configurações > Providers e Roteamento) aqui no Painel. Provedores de IA compatíveis com
 * OpenAI (Ollama, OpenAI, Gemini, Claude ou um endereço seu) com a chave de API e os modelos, o teste de capacidades, e qual provedor e modelo
 * atende cada tarefa (Chat, Planner, RAG, Coder, Visão).
 *
 * A lista é SEMPRE desta tela: fica guardada em ~/.config/lumna/emma/provedores.json (só do usuário) e aparece com ou sem conexão. O serviço
 * lumna-emma (servicos/emmar.h) manda para o servidor da Emma quando consegue entrar, e traz de lá o que já existir; a chave, depois de enviada,
 * fica só no servidor (aqui sobra a máscara ••••1234). A conexão (endereço e token, ou e-mail e senha) fica recolhida: só aparece se falhar.
 */
#include "painel.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emmac.h"
#include "emmar.h"
#include "json.h"

#define N(lista) (int)(sizeof(lista) / sizeof(lista[0]))
#define MAX_PROV 16
#define MAX_ROTAS 12
#define MAX_APAGAR 16

struct prov {
	char lid[24];            /* o id daqui (não muda); hid é o do servidor (vazio até enviar) */
	char hid[40];
	char nome[96], preset[24], url[200], modelos[1500]; /* modelos separados por vírgula */
	char chave[200];         /* a chave digitada, enquanto não foi enviada */
	char mascara[24];        /* ••••1234, do servidor */
	bool tem_chave;          /* o servidor tem uma chave */
	bool sinc;               /* o servidor já tem isto como está */
	int rev;                 /* sobe a cada mudança (a resposta de um envio antigo não apaga uma mudança nova) */
};

/* o que se edita no cartão de cada provedor */
struct edicao {
	char nome[96], url[200], chave[200], modelos[1500];
	int modelo_teste;
	bool tem_relatorio, chat, streaming, json_mode;
	int latencia;
	char erro[240];
};

struct rota {
	char chave[24], lid[24], modelo[96];
};

static const struct {
	const char *id, *nome, *url, *modelos;
} presets[] = {
	{ "ollama", "Ollama Cloud", "https://ollama.com/v1", "gemma4:31b-cloud, gpt-oss:120b, kimi-k2.6, deepseek-v4.1-flash" },
	{ "openai", "OpenAI", "https://api.openai.com/v1", "gpt-5.4-mini, gpt-5.5" },
	{ "gemini", "Gemini", "https://generativelanguage.googleapis.com/v1beta/openai/", "gemini-3-flash, gemini-3.1-pro" },
	{ "claude", "Claude", "https://api.anthropic.com/v1/", "claude-sonnet-4-6, claude-haiku-4-5" },
	{ "custom", "Endpoint customizado", "http://localhost:11434/v1", "" },
};

static const struct {
	const char *chave, *nome, *dica;
} tarefas[] = {
	{ "chat", "Chat", "Modo Chat livre" },
	{ "planner", "Planner", "Modo Plan + extração de memória" },
	{ "rag", "RAG", "Chat com contexto do projeto" },
	{ "coder", "Coder", "Modos Build e Workspace" },
	{ "vision", "Visão", "Mensagens com imagem (opcional; sem ele usa a tarefa do modo)" },
};

static struct prov provs[MAX_PROV];
static struct edicao eds[MAX_PROV];
static int nprov;
static struct rota rotas[MAX_ROTAS];
static int nrotas;
static bool rotas_sinc = true, rotas_sujas;
static int rotas_rev;
static char apagar[MAX_APAGAR][40];
static int napagar;

static bool lido, ocupado, sincronizou, servico_ok = true, conectado;
static char aviso[300];
static bool aviso_erro;
static char con_msg[240];
static char srv_url[200], srv_token_mascara[64], srv_token[200], srv_email[200], srv_senha[200];
static bool conexao_aberta;
static guint proximo_lid = 1;

enum acao { A_SYNC, A_CONTA, A_MODELOS, A_TESTAR };

struct job {
	enum acao acao;
	int idx;
	char caminho[200], conta[900];
	char *corpo;
	char url[200], token[64], email[200];
	/* A_SYNC: o que foi para o servidor (cópia) e o que voltou */
	struct prov prov[MAX_PROV];
	int n, rev_antes[MAX_PROV];
	char hid_out[MAX_PROV][40];
	bool sinc_out[MAX_PROV];
	char apagar[MAX_APAGAR][40];
	bool apagado[MAX_APAGAR];
	int napagar;
	struct rota rotas[MAX_ROTAS];
	int nrotas, rotas_rev_antes;
	bool rotas_sinc_antes, rotas_ok;
	char erro[240];
	struct emmar r, r2;
};

/* ---- o arquivo local ---- */

static void caminho_local(char *buf, size_t tam) {
	pn_caminho(buf, tam, "emma/provedores.json");
}

static void gravar_local(void) {
	GString *s = g_string_new("{\"provedores\":[");
	for (int i = 0; i < nprov; i++) {
		const struct prov *p = &provs[i];
		g_string_append_printf(s, "%s{\"lid\":", i ? "," : "");
		json_escrever_texto(s, p->lid);
		g_string_append(s, ",\"hid\":");
		json_escrever_texto(s, p->hid);
		g_string_append(s, ",\"nome\":");
		json_escrever_texto(s, p->nome);
		g_string_append(s, ",\"preset\":");
		json_escrever_texto(s, p->preset);
		g_string_append(s, ",\"url\":");
		json_escrever_texto(s, p->url);
		g_string_append(s, ",\"modelos\":");
		json_escrever_texto(s, p->modelos);
		g_string_append(s, ",\"chave\":");
		json_escrever_texto(s, p->chave);
		g_string_append(s, ",\"mascara\":");
		json_escrever_texto(s, p->mascara);
		g_string_append_printf(s, ",\"tem_chave\":%s,\"sinc\":%s}", p->tem_chave ? "true" : "false", p->sinc ? "true" : "false");
	}
	g_string_append(s, "],\"rotas\":[");
	for (int i = 0; i < nrotas; i++) {
		g_string_append_printf(s, "%s{\"chave\":", i ? "," : "");
		json_escrever_texto(s, rotas[i].chave);
		g_string_append(s, ",\"lid\":");
		json_escrever_texto(s, rotas[i].lid);
		g_string_append(s, ",\"modelo\":");
		json_escrever_texto(s, rotas[i].modelo);
		g_string_append_c(s, '}');
	}
	g_string_append_printf(s, "],\"rotas_sinc\":%s,\"apagar\":[", rotas_sinc ? "true" : "false");
	for (int i = 0; i < napagar; i++) {
		g_string_append_printf(s, "%s", i ? "," : "");
		json_escrever_texto(s, apagar[i]);
	}
	g_string_append_printf(s, "],\"proximo\":%u}", proximo_lid);
	char p[512], *dir;
	caminho_local(p, sizeof(p));
	dir = g_path_get_dirname(p);
	g_mkdir_with_parents(dir, 0700);
	char *tmp = g_strconcat(p, ".tmp", NULL);
	if (g_file_set_contents(tmp, s->str, -1, NULL)) {
		g_chmod(tmp, 0600); /* tem chaves de API */
		g_rename(tmp, p);
	}
	g_free(tmp);
	g_free(dir);
	g_string_free(s, TRUE);
}

static void csv_de(const struct json *ms, char *dest, size_t tam) {
	GString *csv = g_string_new(NULL);
	for (int k = 0; ms && k < ms->nfilhos; k++) {
		const char *m = json_texto(json_item(ms, k));
		g_string_append_printf(csv, "%s%s", k ? ", " : "", m ? m : "");
	}
	g_strlcpy(dest, csv->str, tam);
	g_string_free(csv, TRUE);
}

static void preparar_edicao(int i) {
	struct edicao *e = &eds[i];
	memset(e, 0, sizeof(*e));
	g_strlcpy(e->nome, provs[i].nome, sizeof(e->nome));
	g_strlcpy(e->url, provs[i].url, sizeof(e->url));
	g_strlcpy(e->modelos, provs[i].modelos, sizeof(e->modelos));
}

static void ler_local(void) {
	char p[512], *t = NULL;
	lido = true;
	caminho_local(p, sizeof(p));
	if (!g_file_get_contents(p, &t, NULL, NULL)) {
		return;
	}
	struct json *j = json_ler(t);
	g_free(t);
	const struct json *ps = json_chave(j, "provedores");
	nprov = 0;
	for (int i = 0; ps && i < ps->nfilhos && nprov < MAX_PROV; i++) {
		const struct json *q = json_item(ps, i);
		struct prov *v = &provs[nprov];
		memset(v, 0, sizeof(*v));
		g_strlcpy(v->lid, json_chave_texto(q, "lid"), sizeof(v->lid));
		g_strlcpy(v->hid, json_chave_texto(q, "hid"), sizeof(v->hid));
		g_strlcpy(v->nome, json_chave_texto(q, "nome"), sizeof(v->nome));
		g_strlcpy(v->preset, json_chave_texto(q, "preset"), sizeof(v->preset));
		g_strlcpy(v->url, json_chave_texto(q, "url"), sizeof(v->url));
		g_strlcpy(v->modelos, json_chave_texto(q, "modelos"), sizeof(v->modelos));
		g_strlcpy(v->chave, json_chave_texto(q, "chave"), sizeof(v->chave));
		g_strlcpy(v->mascara, json_chave_texto(q, "mascara"), sizeof(v->mascara));
		v->tem_chave = json_bool(json_chave(q, "tem_chave"), false);
		v->sinc = json_bool(json_chave(q, "sinc"), false);
		preparar_edicao(nprov);
		nprov++;
	}
	const struct json *rs = json_chave(j, "rotas");
	nrotas = 0;
	for (int i = 0; rs && i < rs->nfilhos && nrotas < MAX_ROTAS; i++) {
		const struct json *q = json_item(rs, i);
		g_strlcpy(rotas[nrotas].chave, json_chave_texto(q, "chave"), sizeof(rotas[0].chave));
		g_strlcpy(rotas[nrotas].lid, json_chave_texto(q, "lid"), sizeof(rotas[0].lid));
		g_strlcpy(rotas[nrotas].modelo, json_chave_texto(q, "modelo"), sizeof(rotas[0].modelo));
		nrotas++;
	}
	rotas_sinc = json_bool(json_chave(j, "rotas_sinc"), true);
	const struct json *ap = json_chave(j, "apagar");
	napagar = 0;
	for (int i = 0; ap && i < ap->nfilhos && napagar < MAX_APAGAR; i++) {
		g_strlcpy(apagar[napagar++], json_texto(json_item(ap, i)) ? json_texto(json_item(ap, i)) : "", 40);
	}
	proximo_lid = (guint)json_numero(json_chave(j, "proximo"), 1);
	json_liberar(j);
}

/* ---- ajudas ---- */

static int separar_modelos(const char *csv, char ***lista) {
	char **partes = g_strsplit(csv, ",", -1);
	GPtrArray *ok = g_ptr_array_new();
	for (int i = 0; partes[i]; i++) {
		g_strstrip(partes[i]);
		if (*partes[i]) {
			g_ptr_array_add(ok, g_strdup(partes[i]));
		}
	}
	g_strfreev(partes);
	int n = (int)ok->len;
	g_ptr_array_add(ok, NULL);
	*lista = (char **)g_ptr_array_free(ok, FALSE);
	return n;
}

static int prov_por_lid(const char *lid) {
	for (int i = 0; i < nprov; i++) {
		if (!strcmp(provs[i].lid, lid)) {
			return i;
		}
	}
	return -1;
}

static struct rota *rota_de(const char *chave) {
	for (int i = 0; i < nrotas; i++) {
		if (!strcmp(rotas[i].chave, chave)) {
			return &rotas[i];
		}
	}
	if (nrotas < MAX_ROTAS) {
		struct rota *r = &rotas[nrotas++];
		memset(r, 0, sizeof(*r));
		g_strlcpy(r->chave, chave, sizeof(r->chave));
		return r;
	}
	return &rotas[0];
}

static void rotas_mudaram(void) {
	rotas_sinc = false;
	rotas_rev++;
}

static char *corpo_provedor(const struct prov *p) {
	GString *c = g_string_new("{\"name\":");
	json_escrever_texto(c, p->nome);
	g_string_append(c, ",\"preset\":");
	json_escrever_texto(c, p->preset[0] ? p->preset : "custom");
	g_string_append(c, ",\"base_url\":");
	json_escrever_texto(c, p->url);
	if (p->chave[0]) {
		g_string_append(c, ",\"api_key\":");
		json_escrever_texto(c, p->chave);
	}
	g_string_append(c, ",\"models\":[");
	char **ms;
	int nm = separar_modelos(p->modelos, &ms);
	for (int m = 0; m < nm; m++) {
		g_string_append_printf(c, "%s", m ? "," : "");
		json_escrever_texto(c, ms[m]);
	}
	g_strfreev(ms);
	g_string_append(c, "]}");
	return g_string_free(c, FALSE);
}

/* ---- o envio ao servidor (numa linha à parte) ---- */

static void trabalho(void *d) {
	struct job *j = d;
	if (j->acao == A_CONTA) {
		char resp[300];
		emmac_pedido(j->conta, resp, sizeof(resp));
	}
	if (j->acao == A_MODELOS || j->acao == A_TESTAR) {
		emmar_chamar(j->acao == A_MODELOS ? "GET" : "POST", j->caminho, j->corpo, 0, &j->r);
		return;
	}
	/* A_SYNC e A_CONTA: a conta de agora, e então tudo que estiver pendente */
	char resp[800];
	if (emmac_pedido("conta", resp, sizeof(resp))) {
		struct json *c = json_ler(resp);
		g_strlcpy(j->url, json_chave_texto(c, "url"), sizeof(j->url));
		g_strlcpy(j->token, json_chave_texto(c, "token"), sizeof(j->token));
		g_strlcpy(j->email, json_chave_texto(c, "email"), sizeof(j->email));
		json_liberar(c);
	}
	struct emmar teste;
	emmar_chamar("GET", "/api/providers", NULL, 0, &teste);
	bool entrou = teste.ok;
	emmar_liberar(&teste);
	if (!entrou) {
		j->r = teste;
		memset(&j->r.raiz, 0, sizeof(j->r.raiz));
		emmar_chamar("GET", "/api/providers", NULL, 0, &j->r); /* só para guardar a mensagem do erro */
		return;
	}
	for (int i = 0; i < j->napagar; i++) {
		struct emmar r;
		char *caminho = g_strdup_printf("/api/providers/%s", j->apagar[i]);
		emmar_chamar("DELETE", caminho, NULL, 0, &r);
		j->apagado[i] = r.ok || r.status == 404;
		emmar_liberar(&r);
		g_free(caminho);
	}
	for (int i = 0; i < j->n; i++) {
		struct prov *p = &j->prov[i];
		g_strlcpy(j->hid_out[i], p->hid, sizeof(j->hid_out[i]));
		j->sinc_out[i] = p->sinc;
		if (p->sinc || !p->nome[0] || !p->url[0]) {
			continue;
		}
		struct emmar r;
		char *corpo = corpo_provedor(p), *caminho = p->hid[0] ? g_strdup_printf("/api/providers/%s", p->hid) : g_strdup("/api/providers");
		emmar_chamar(p->hid[0] ? "PATCH" : "POST", caminho, corpo, 0, &r);
		if (r.ok) {
			const char *id = json_chave_texto(r.corpo, "id");
			if (*id) {
				g_strlcpy(j->hid_out[i], id, sizeof(j->hid_out[i]));
			}
			j->sinc_out[i] = true;
		} else {
			g_snprintf(j->erro, sizeof(j->erro), "%s: %s", p->nome, r.msg);
		}
		emmar_liberar(&r);
		g_free(corpo);
		g_free(caminho);
	}
	j->rotas_ok = j->rotas_sinc_antes;
	if (!j->rotas_sinc_antes) {
		GString *c = g_string_new("{\"routing\":{");
		bool completo = true, primeiro = true;
		for (int t = 0; t < j->nrotas; t++) {
			int pi = -1;
			for (int i = 0; i < j->n; i++) {
				if (!strcmp(j->prov[i].lid, j->rotas[t].lid)) {
					pi = i;
				}
			}
			if (pi < 0 || !j->hid_out[pi][0]) {
				if (j->rotas[t].lid[0]) {
					completo = false;
				}
				continue;
			}
			g_string_append_printf(c, "%s", primeiro ? "" : ",");
			primeiro = false;
			json_escrever_texto(c, j->rotas[t].chave);
			g_string_append(c, ":{\"provider_id\":");
			json_escrever_texto(c, j->hid_out[pi]);
			g_string_append(c, ",\"model\":");
			json_escrever_texto(c, j->rotas[t].modelo);
			g_string_append_c(c, '}');
		}
		g_string_append(c, "}}");
		if (completo) {
			struct emmar r;
			emmar_chamar("PUT", "/api/settings", c->str, 0, &r);
			j->rotas_ok = r.ok;
			if (!r.ok) {
				g_strlcpy(j->erro, r.msg, sizeof(j->erro));
			}
			emmar_liberar(&r);
		}
		g_string_free(c, TRUE);
	}
	emmar_chamar("GET", "/api/providers", NULL, 0, &j->r);
	if (j->r.ok) {
		emmar_chamar("GET", "/api/settings", NULL, 0, &j->r2);
	}
}

static void iniciar_sync(enum acao a, const char *conta);

static void pronto(void *d) {
	struct job *j = d;
	bool de_novo = false;
	if (j->acao == A_MODELOS) {
		const struct json *ms = json_chave(j->r.corpo, "models");
		if (j->r.ok && json_bool(json_chave(j->r.corpo, "ok"), false) && ms && ms->nfilhos && j->idx < nprov) {
			csv_de(ms, eds[j->idx].modelos, sizeof(eds[j->idx].modelos));
			g_snprintf(aviso, sizeof(aviso), "%d modelos encontrados: confira a lista e salve.", ms->nfilhos);
			aviso_erro = false;
		} else {
			const char *e = json_chave_texto(j->r.corpo, "error");
			g_strlcpy(aviso, *e ? e : j->r.ok ? "Endpoint não suporta /models" : j->r.msg, sizeof(aviso));
			aviso_erro = true;
		}
	} else if (j->acao == A_TESTAR) {
		if (j->idx < nprov) {
			struct edicao *e = &eds[j->idx];
			e->tem_relatorio = true;
			e->erro[0] = '\0';
			if (!j->r.ok) {
				g_strlcpy(e->erro, j->r.msg, sizeof(e->erro));
				e->chat = e->streaming = e->json_mode = false;
				e->latencia = 0;
			} else {
				e->chat = json_bool(json_chave(j->r.corpo, "chat"), false);
				e->streaming = json_bool(json_chave(j->r.corpo, "streaming"), false);
				e->json_mode = json_bool(json_chave(j->r.corpo, "json_mode"), false);
				e->latencia = (int)json_numero(json_chave(j->r.corpo, "latency_ms"), 0);
				g_strlcpy(e->erro, json_chave_texto(j->r.corpo, "error"), sizeof(e->erro));
			}
		}
	} else {
		sincronizou = true;
		servico_ok = j->r.servico;
		conectado = j->r.ok;
		g_strlcpy(srv_url, j->url, sizeof(srv_url));
		g_strlcpy(srv_token_mascara, j->token, sizeof(srv_token_mascara));
		g_strlcpy(srv_email, j->email, sizeof(srv_email));
		g_strlcpy(con_msg, j->r.msg, sizeof(con_msg));
		if (conectado) {
			/* o que foi enviado */
			for (int i = 0; i < j->n; i++) {
				int pi = prov_por_lid(j->prov[i].lid);
				if (pi < 0 || provs[pi].rev != j->prov[i].rev) {
					continue;
				}
				g_strlcpy(provs[pi].hid, j->hid_out[i], sizeof(provs[pi].hid));
				if (j->sinc_out[i] && !provs[pi].sinc) {
					provs[pi].sinc = true;
					provs[pi].chave[0] = '\0'; /* a chave já está no servidor */
				}
			}
			for (int i = 0; i < j->napagar; i++) {
				if (!j->apagado[i]) {
					continue;
				}
				for (int k = 0; k < napagar; k++) {
					if (!strcmp(apagar[k], j->apagar[i])) {
						memmove(apagar[k], apagar[k + 1], (size_t)(napagar - k - 1) * sizeof(apagar[0]));
						napagar--;
						break;
					}
				}
			}
			if (j->rotas_ok && !j->rotas_sinc_antes && rotas_rev == j->rotas_rev_antes) {
				rotas_sinc = true;
			}
			/* o que está no servidor: atualiza o que já temos e traz o que não temos */
			for (int i = 0; j->r.corpo && i < j->r.corpo->nfilhos; i++) {
				const struct json *sp = json_item(j->r.corpo, i);
				const char *id = json_chave_texto(sp, "id");
				int pi = -1;
				for (int k = 0; k < nprov; k++) {
					if (provs[k].hid[0] && !strcmp(provs[k].hid, id)) {
						pi = k;
					}
				}
				bool apagando = false;
				for (int k = 0; k < napagar; k++) {
					apagando |= !strcmp(apagar[k], id);
				}
				if (apagando) {
					continue;
				}
				if (pi < 0) {
					if (nprov >= MAX_PROV) {
						continue;
					}
					pi = nprov++;
					memset(&provs[pi], 0, sizeof(provs[pi]));
					g_snprintf(provs[pi].lid, sizeof(provs[pi].lid), "p%u", proximo_lid++);
					g_strlcpy(provs[pi].hid, id, sizeof(provs[pi].hid));
					provs[pi].sinc = true;
					pi = nprov - 1;
					g_strlcpy(provs[pi].nome, json_chave_texto(sp, "name"), sizeof(provs[pi].nome));
					g_strlcpy(provs[pi].preset, json_chave_texto(sp, "preset"), sizeof(provs[pi].preset));
					g_strlcpy(provs[pi].url, json_chave_texto(sp, "base_url"), sizeof(provs[pi].url));
					csv_de(json_chave(sp, "models"), provs[pi].modelos, sizeof(provs[pi].modelos));
					preparar_edicao(pi);
				} else if (provs[pi].sinc) {
					g_strlcpy(provs[pi].nome, json_chave_texto(sp, "name"), sizeof(provs[pi].nome));
					g_strlcpy(provs[pi].url, json_chave_texto(sp, "base_url"), sizeof(provs[pi].url));
					csv_de(json_chave(sp, "models"), provs[pi].modelos, sizeof(provs[pi].modelos));
					if (!strcmp(provs[pi].nome, eds[pi].nome) || !eds[pi].nome[0]) {
						preparar_edicao(pi);
					}
				}
				g_strlcpy(provs[pi].mascara, json_chave_texto(sp, "api_key_masked"), sizeof(provs[pi].mascara));
				provs[pi].tem_chave = json_bool(json_chave(sp, "has_key"), false);
			}
			/* o roteamento do servidor, se aqui ainda não há nenhum */
			const struct json *rt = json_chave(j->r2.corpo, "routing");
			if (!nrotas && rt) {
				for (int i = 0; i < rt->nfilhos && nrotas < MAX_ROTAS; i++) {
					const char *hid = json_chave_texto(rt->filhos[i], "provider_id");
					int pi = -1;
					for (int k = 0; k < nprov; k++) {
						if (!strcmp(provs[k].hid, hid)) {
							pi = k;
						}
					}
					if (pi < 0) {
						continue;
					}
					struct rota *r = &rotas[nrotas++];
					memset(r, 0, sizeof(*r));
					g_strlcpy(r->chave, rt->filhos[i]->chave, sizeof(r->chave));
					g_strlcpy(r->lid, provs[pi].lid, sizeof(r->lid));
					g_strlcpy(r->modelo, json_chave_texto(rt->filhos[i], "model"), sizeof(r->modelo));
				}
				rotas_sinc = true;
			}
			if (j->erro[0]) {
				g_strlcpy(aviso, j->erro, sizeof(aviso));
				aviso_erro = true;
			} else if (j->acao == A_CONTA) {
				g_strlcpy(aviso, "Conectado ao servidor da Emma.", sizeof(aviso));
				aviso_erro = false;
				conexao_aberta = false;
				srv_token[0] = srv_senha[0] = '\0';
			}
		} else if (j->acao == A_CONTA) {
			g_snprintf(aviso, sizeof(aviso), "Não consegui entrar no servidor da Emma. %s", j->r.msg);
			aviso_erro = true;
		}
		/* mudou algo enquanto isto ia e voltava: manda de novo */
		bool pendente = false;
		for (int i = 0; i < nprov; i++) {
			pendente |= !provs[i].sinc && provs[i].nome[0] && provs[i].url[0];
		}
		de_novo = conectado && !j->erro[0] && (pendente || !rotas_sinc || napagar);
		gravar_local();
	}
	ocupado = false;
	emmar_liberar(&j->r);
	emmar_liberar(&j->r2);
	g_free(j->corpo);
	g_free(j);
	if (de_novo) {
		iniciar_sync(A_SYNC, NULL);
	}
}

static void iniciar_sync(enum acao a, const char *conta) {
	struct job *j = g_new0(struct job, 1);
	j->acao = a;
	if (conta) {
		g_strlcpy(j->conta, conta, sizeof(j->conta));
	}
	j->n = nprov;
	for (int i = 0; i < nprov; i++) {
		j->prov[i] = provs[i];
	}
	j->napagar = napagar;
	memcpy(j->apagar, apagar, sizeof(apagar));
	j->nrotas = nrotas;
	memcpy(j->rotas, rotas, sizeof(rotas));
	j->rotas_sinc_antes = rotas_sinc;
	j->rotas_rev_antes = rotas_rev;
	ocupado = true;
	pn_tarefa(trabalho, pronto, j);
}

static void sincronizar(void) {
	if (!ocupado) {
		iniciar_sync(A_SYNC, NULL);
	}
}

static void iniciar_unico(enum acao a, int idx, const char *caminho, char *corpo) {
	struct job *j = g_new0(struct job, 1);
	j->acao = a;
	j->idx = idx;
	g_strlcpy(j->caminho, caminho, sizeof(j->caminho));
	j->corpo = corpo;
	ocupado = true;
	pn_tarefa(trabalho, pronto, j);
}

/* só marca: tudo sai do primeiro desenho (a janela pode nem existir ainda quando se abre direto numa seção) */
void pn_emma_prov_abrir(void) {
	aviso[0] = '\0';
	conexao_aberta = false;
	sincronizou = false;
}

static void ao_desenhar(void) {
	if (!lido) {
		ler_local();
	}
	if (!sincronizou && !ocupado) {
		iniciar_sync(A_SYNC, NULL);
	}
}

/* o primeiro provedor já atende as tarefas que ainda não têm um (senão a Emma continuaria sem responder) */
static void rotas_padrao(void) {
	if (!nprov) {
		return;
	}
	bool mudou = false;
	for (int k = 0; k < N(tarefas); k++) {
		if (!strcmp(tarefas[k].chave, "vision")) {
			continue; /* opcional */
		}
		struct rota *r = rota_de(tarefas[k].chave);
		if (prov_por_lid(r->lid) < 0) {
			char **ms;
			int nm = separar_modelos(provs[0].modelos, &ms);
			g_strlcpy(r->lid, provs[0].lid, sizeof(r->lid));
			g_strlcpy(r->modelo, nm ? ms[0] : "", sizeof(r->modelo));
			g_strfreev(ms);
			mudou = true;
		}
	}
	if (mudou) {
		rotas_mudaram();
	}
}

static void salvar_tudo(void) {
	gravar_local();
	sincronizar();
}

/* ---- o topo: o aviso e o estado da conexão ---- */

static void aviso_topo(void) {
	if (!aviso[0]) {
		return;
	}
	Clay_Color cor = aviso_erro ? gv_t.perigo : gv_t.sucesso;
	CLAY(CLAY_ID("emp-aviso"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 8, 10, 10 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_alfa(cor, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_alfa(cor, 0.5f), .width = GV_BORDA(1) } }) {
		gv_icone(aviso_erro ? "info" : "check", 18, cor);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(aviso), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		if (gv_botao(CLAY_ID("emp-aviso-x"), "close", NULL, GV_BOTAO_SIMPLES)) {
			aviso[0] = '\0';
		}
	}
}

/* a conexão com o servidor da Emma: o endereço e o token de API (harness > Configurações > Conta), ou o e-mail e a senha do harness */
static void bloco_conexao(void) {
	pn_linha_comecar(CLAY_ID("emp-srv-u"), "link", "Endereço do servidor", "Onde o servidor da Emma está (o padrão é este computador).");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(360) } } }) {
		gv_campo(CLAY_ID("emp-srv-u-c"), srv_url, sizeof(srv_url), "http://127.0.0.1:8001", GV_CAMPO_URL);
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("emp-srv-t"), "lock", "Token de API",
		srv_token_mascara[0] ? gv_fmt("Guardado (%s). Deixe vazio para manter.", srv_token_mascara).chars : "Crie no harness: Configurações > Conta > Tokens de API.");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(360) } } }) {
		gv_campo(CLAY_ID("emp-srv-t-c"), srv_token, sizeof(srv_token), "lumna_…", GV_CAMPO_SENHA);
	}
	pn_linha_acabar();
	gv_texto(CLAY_STRING("Ou entre com a conta do harness:"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	pn_linha_comecar(CLAY_ID("emp-srv-e"), "user", "E-mail", "O mesmo que você usa para entrar no harness.");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(360) } } }) {
		gv_campo(CLAY_ID("emp-srv-e-c"), srv_email, sizeof(srv_email), "voce@exemplo.com", GV_CAMPO_EMAIL);
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("emp-srv-s"), "lock", "Senha", "Fica guardada só no seu usuário (~/.config/lumna/emma).");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(360) } } }) {
		gv_campo(CLAY_ID("emp-srv-s-c"), srv_senha, sizeof(srv_senha), "Deixe vazio para manter a de agora", GV_CAMPO_SENHA);
	}
	pn_linha_acabar();
	CLAY(CLAY_ID("emp-srv-bt"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 0 }, .childGap = 10 } }) {
		if (gv_botao(CLAY_ID("emp-srv-entrar"), "check", "Guardar e conectar", GV_BOTAO_CHEIO) && !ocupado) {
			GString *c = g_string_new("conta {\"url\":");
			json_escrever_texto(c, srv_url);
			g_string_append(c, ",\"token\":");
			json_escrever_texto(c, srv_token);
			g_string_append(c, ",\"email\":");
			json_escrever_texto(c, srv_email);
			g_string_append(c, ",\"senha\":");
			json_escrever_texto(c, srv_senha);
			g_string_append_c(c, '}');
			iniciar_sync(A_CONTA, c->str);
			g_string_free(c, TRUE);
		}
		if (conectado && gv_botao(CLAY_ID("emp-srv-fechar"), NULL, "Cancelar", GV_BOTAO_SIMPLES)) {
			conexao_aberta = false;
		}
	}
}

static void estado_conexao(void) {
	aviso_topo();
	if (!sincronizou) {
		gv_texto(CLAY_STRING("Falando com o serviço da Emma…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		return;
	}
	if (!servico_ok) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 10, 10, 10 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_t.aviso, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_alfa(gv_t.aviso, 0.5f), .width = GV_BORDA(1) } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("O serviço da Emma não está rodando. O que você configurar fica guardado aqui e vai quando ele ligar."), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao(CLAY_ID("emp-iniciar"), "power", "Iniciar a Emma", GV_BOTAO_CHEIO)) {
				pn_executar("lumna-emma");
				sincronizou = false;
			}
		}
		return;
	}
	if (!conectado) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 10, 10, 10 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_t.aviso, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_alfa(gv_t.aviso, 0.5f), .width = GV_BORDA(1) } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(gv_fmt("Sem conexão com o servidor da Emma%s%s. O que você configurar fica guardado aqui e vai quando conectar.", con_msg[0] ? ": " : "", con_msg).chars),
					gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao(CLAY_ID("emp-conectar"), "link", "Conexão", GV_BOTAO_ELEVADO)) {
				conexao_aberta = !conexao_aberta;
			}
			if (gv_botao(CLAY_ID("emp-tentar"), "refresh", "Tentar de novo", GV_BOTAO_ELEVADO)) {
				sincronizou = false;
			}
		}
		if (conexao_aberta) {
			bloco_conexao();
		}
	}
}

/* ---- Provedores: um cartão por provedor, como no harness ---- */

static void campo_cartao(Clay_ElementId id, char *texto, size_t tam, const char *dica, enum gv_campo_tipo tipo) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
		gv_campo(id, texto, tam, dica, tipo);
	}
}

static void marca_capacidade(const char *rotulo, bool ok) {
	CLAY_AUTO_ID({ .layout = { .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone(ok ? "check" : "close", 14, ok ? gv_t.sucesso : gv_t.perigo);
		gv_texto(gv_str(rotulo), gv_t.tamanho_corpo - 2, ok ? gv_t.sucesso : gv_t.perigo, GV_PESO_NORMAL);
	}
}

static const char *situacao(const struct prov *p) {
	if (p->sinc) {
		return p->tem_chave ? gv_fmt("chave %s", p->mascara).chars : "sem chave";
	}
	if (p->chave[0]) {
		return conectado ? "enviando…" : "chave guardada aqui (ainda não enviada)";
	}
	return conectado ? "enviando…" : "só aqui, ainda não enviado";
}

static void cartao_provedor(int i) {
	struct prov *p = &provs[i];
	struct edicao *e = &eds[i];
	uint32_t k = (uint32_t)i;
	CLAY(CLAY_IDI("emp-card", k), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 10 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		CLAY(CLAY_IDI("emp-cab", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_IDI("emp-tag", k), { .layout = { .padding = { 8, 8, 3, 3 } }, .backgroundColor = gv_t.selecionado, .cornerRadius = CLAY_CORNER_RADIUS(5) }) {
				char *alto = g_ascii_strup(p->preset[0] ? p->preset : "custom", -1);
				gv_texto(gv_str(alto), gv_t.tamanho_corpo - 3, gv_t.texto_secundario, GV_PESO_MEDIO);
				g_free(alto);
			}
			gv_texto(gv_str(situacao(p)), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			if (pn_confirmar(CLAY_IDI("emp-x", k), "trash", NULL, gv_fmt("Remover %s?", p->nome).chars, true)) {
				if (p->hid[0] && napagar < MAX_APAGAR) {
					g_strlcpy(apagar[napagar++], p->hid, sizeof(apagar[0]));
				}
				for (int t = 0; t < nrotas; t++) {
					if (!strcmp(rotas[t].lid, p->lid)) {
						rotas[t].lid[0] = rotas[t].modelo[0] = '\0';
						rotas_mudaram();
					}
				}
				memmove(&provs[i], &provs[i + 1], (size_t)(nprov - i - 1) * sizeof(provs[0]));
				memmove(&eds[i], &eds[i + 1], (size_t)(nprov - i - 1) * sizeof(eds[0]));
				nprov--;
				rotas_padrao();
				salvar_tudo();
				return;
			}
		}
		CLAY(CLAY_IDI("emp-l1", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
			campo_cartao(CLAY_IDI("emp-nome", k), e->nome, sizeof(e->nome), "Nome", GV_CAMPO_TEXTO);
			campo_cartao(CLAY_IDI("emp-url", k), e->url, sizeof(e->url), "Base URL", GV_CAMPO_URL);
		}
		CLAY(CLAY_IDI("emp-l2", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
			campo_cartao(CLAY_IDI("emp-chave", k), e->chave, sizeof(e->chave), p->tem_chave || p->chave[0] ? "manter chave atual" : "API key", GV_CAMPO_SENHA);
			CLAY(CLAY_IDI("emp-modc", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
				campo_cartao(CLAY_IDI("emp-mod", k), e->modelos, sizeof(e->modelos), "modelos, separados, por vírgula", GV_CAMPO_TEXTO);
				if (gv_botao(CLAY_IDI("emp-busca", k), NULL, "/models", GV_BOTAO_SIMPLES) && !ocupado) {
					if (p->hid[0] && p->sinc && conectado) {
						char *caminho = g_strdup_printf("/api/providers/%s/models", p->hid);
						iniciar_unico(A_MODELOS, i, caminho, NULL);
						g_free(caminho);
					} else {
						g_strlcpy(aviso, "Para buscar os modelos o provedor precisa estar salvo no servidor: salve e conecte.", sizeof(aviso));
						aviso_erro = true;
					}
				}
			}
		}
		CLAY(CLAY_IDI("emp-l3", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			char **ms;
			int nm = separar_modelos(p->modelos, &ms);
			if (nm) {
				const char *opcoes[64];
				int n = nm > 64 ? 64 : nm;
				for (int m = 0; m < n; m++) {
					opcoes[m] = ms[m];
				}
				if (e->modelo_teste >= n) {
					e->modelo_teste = 0;
				}
				CLAY(CLAY_IDI("emp-tm", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(260) } } }) {
					gv_lista_suspensa(CLAY_IDI("emp-tml", k), opcoes, n, &e->modelo_teste);
				}
				if (gv_botao(CLAY_IDI("emp-testar", k), "zap", "Testar capacidades", GV_BOTAO_SIMPLES) && !ocupado) {
					if (p->hid[0] && p->sinc && conectado) {
						GString *c = g_string_new("{\"model\":");
						json_escrever_texto(c, ms[e->modelo_teste]);
						g_string_append_c(c, '}');
						char *caminho = g_strdup_printf("/api/providers/%s/test", p->hid);
						iniciar_unico(A_TESTAR, i, caminho, g_string_free(c, FALSE));
						g_free(caminho);
					} else {
						g_strlcpy(aviso, "Para testar, o provedor precisa estar salvo no servidor: salve e conecte.", sizeof(aviso));
						aviso_erro = true;
					}
				}
			} else {
				gv_texto(CLAY_STRING("Salve com alguns modelos (ou use /models) para poder testar."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			g_strfreev(ms);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			if (gv_botao(CLAY_IDI("emp-salvar", k), NULL, "Salvar", GV_BOTAO_CHEIO) && e->nome[0] && e->url[0]) {
				g_strlcpy(p->nome, e->nome, sizeof(p->nome));
				g_strlcpy(p->url, e->url, sizeof(p->url));
				g_strlcpy(p->modelos, e->modelos, sizeof(p->modelos));
				if (e->chave[0]) {
					g_strlcpy(p->chave, e->chave, sizeof(p->chave));
					e->chave[0] = '\0';
				}
				p->sinc = false;
				p->rev++;
				g_strlcpy(aviso, conectado ? "Provedor salvo." : "Provedor salvo aqui: vai para o servidor quando conectar.", sizeof(aviso));
				aviso_erro = false;
				salvar_tudo();
			}
		}
		if (e->tem_relatorio) {
			CLAY(CLAY_IDI("emp-rel", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 8, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
				marca_capacidade("chat", e->chat);
				marca_capacidade("streaming", e->streaming);
				marca_capacidade("JSON mode", e->json_mode);
				if (e->latencia) {
					gv_texto(gv_fmt("%dms", e->latencia), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			if (e->erro[0]) {
				gv_texto(gv_str(e->erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
			}
			if (e->chat && !e->json_mode) {
				gv_texto(CLAY_STRING("Compatibilidade parcial: o LUMNA usa blocos de texto (não tool calling), então Build continua funcionando."), gv_t.tamanho_corpo - 2, gv_t.aviso, GV_PESO_NORMAL);
			}
		}
	}
}

static void adicionar_preset(int i) {
	if (nprov >= MAX_PROV) {
		return;
	}
	struct prov *p = &provs[nprov];
	memset(p, 0, sizeof(*p));
	g_snprintf(p->lid, sizeof(p->lid), "p%u", proximo_lid++);
	g_strlcpy(p->nome, presets[i].nome, sizeof(p->nome));
	g_strlcpy(p->preset, presets[i].id, sizeof(p->preset));
	g_strlcpy(p->url, presets[i].url, sizeof(p->url));
	g_strlcpy(p->modelos, presets[i].modelos, sizeof(p->modelos));
	p->rev = 1;
	preparar_edicao(nprov);
	nprov++;
	rotas_padrao();
	g_strlcpy(aviso, gv_fmt("%s adicionado: coloque a API key e salve.", presets[i].nome).chars, sizeof(aviso));
	aviso_erro = false;
	salvar_tudo();
}

void pn_emma_prov_desenhar(void) {
	ao_desenhar();
	estado_conexao();
	/* os botões de adicionar, um por tipo */
	CLAY(CLAY_ID("emp-presets"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
		for (int i = 0; i < N(presets); i++) {
			if (gv_botao(CLAY_IDI("emp-add", (uint32_t)i), "plus", presets[i].nome, GV_BOTAO_CONTORNO)) {
				adicionar_preset(i);
			}
		}
	}
	if (!nprov) {
		gv_texto(CLAY_STRING("Nenhum provedor ainda. Adicione um acima e coloque a API key para a Emma poder conversar."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	for (int i = 0; i < nprov; i++) {
		cartao_provedor(i);
	}
	/* a conexão com o servidor fica recolhida quando está tudo certo */
	if (conectado) {
		CLAY(CLAY_ID("emp-con-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 18, 0 } } }) {
			if (gv_botao(CLAY_ID("emp-con-b"), conexao_aberta ? "chevron-up" : "chevron-down", "Conexão com o servidor", GV_BOTAO_SIMPLES)) {
				conexao_aberta = !conexao_aberta;
			}
		}
		if (conexao_aberta) {
			bloco_conexao();
		}
	}
}

/* ---- Roteamento: qual provedor e modelo atende cada tarefa ---- */

bool pn_emma_rota_pendente(void) {
	return rotas_sujas;
}

void pn_emma_rota_aplicar(void) {
	rotas_sujas = false;
	g_strlcpy(aviso, conectado ? "Roteamento salvo." : "Roteamento salvo aqui: vai para o servidor quando conectar.", sizeof(aviso));
	aviso_erro = false;
	salvar_tudo();
}

void pn_emma_rota_descartar(void) {
	rotas_sujas = false;
	lido = false;
	ler_local();
}

void pn_emma_rota_desenhar(void) {
	ao_desenhar();
	estado_conexao();
	if (!nprov) {
		gv_texto(CLAY_STRING("Antes de escolher, adicione um provedor em Provedores."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	for (int t = 0; t < N(tarefas); t++) {
		struct rota *r = rota_de(tarefas[t].chave);
		uint32_t k = (uint32_t)t;
		CLAY(CLAY_IDI("emr-fora", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 0, 8 } } }) {
			CLAY(CLAY_IDI("emr-lin", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(12), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
				CLAY(CLAY_IDI("emr-tit", k), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(190) }, .childGap = 2 } }) {
					gv_texto(gv_str(tarefas[t].nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(tarefas[t].dica), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
				}
				const char *nomes[MAX_PROV + 1] = { "— provedor —" };
				for (int i = 0; i < nprov; i++) {
					nomes[i + 1] = provs[i].nome;
				}
				int pi = prov_por_lid(r->lid), esc = pi + 1;
				CLAY(CLAY_IDI("emr-pc", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(210) } } }) {
					if (gv_lista_suspensa(CLAY_IDI("emr-p", k), nomes, nprov + 1, &esc)) {
						g_strlcpy(r->lid, esc > 0 ? provs[esc - 1].lid : "", sizeof(r->lid));
						char **ms;
						int nm = esc > 0 ? separar_modelos(provs[esc - 1].modelos, &ms) : 0;
						g_strlcpy(r->modelo, nm ? ms[0] : "", sizeof(r->modelo));
						if (nm) {
							g_strfreev(ms);
						}
						rotas_mudaram();
						rotas_sujas = true;
					}
				}
				pi = prov_por_lid(r->lid);
				CLAY(CLAY_IDI("emr-mc", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					if (pi < 0) {
						gv_texto(CLAY_STRING("—"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
					} else {
						char **ms;
						int nm = separar_modelos(provs[pi].modelos, &ms);
						const char *opcoes[66];
						int n = 0, atual = -1;
						for (int m = 0; m < nm && n < 64; m++) {
							opcoes[n] = ms[m];
							if (!strcmp(ms[m], r->modelo)) {
								atual = n;
							}
							n++;
						}
						if (atual < 0 && r->modelo[0]) { /* um modelo que não está na lista (escrito à mão no harness) continua valendo */
							opcoes[n] = r->modelo;
							atual = n++;
						}
						if (atual < 0) {
							atual = 0;
						}
						if (n && gv_lista_suspensa(CLAY_IDI("emr-m", k), opcoes, n, &atual)) {
							g_strlcpy(r->modelo, opcoes[atual], sizeof(r->modelo));
							rotas_mudaram();
							rotas_sujas = true;
						}
						if (!n) {
							gv_texto(CLAY_STRING("Este provedor não tem modelos."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
						}
						g_strfreev(ms);
					}
				}
			}
		}
	}
}
