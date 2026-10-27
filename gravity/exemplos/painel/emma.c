/*
 * Emma > Provedores e Roteamento: as mesmas telas do harness (Configurações > Providers e Roteamento) aqui no Painel. Provedores de IA
 * compatíveis com OpenAI (Ollama, OpenAI, Gemini, Claude ou um endereço seu) com a chave de API e os modelos, o teste de capacidades, e qual
 * provedor e modelo atende cada tarefa (Chat, Planner, RAG, Coder, Visão).
 *
 * Tudo vai e volta pelo serviço lumna-emma (servicos/emmar.h), que já está ligado ao servidor da Emma: a conexão não aparece na tela, só se
 * falhar (ou em "Conexão com o servidor", lá embaixo). As chamadas rodam numa linha à parte; o resultado aparece no aviso do topo.
 */
#include "painel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emmac.h"
#include "emmar.h"
#include "json.h"

#define N(lista) (int)(sizeof(lista) / sizeof(lista[0]))
#define MAX_PROV 16
#define MAX_ROTAS 12

struct prov {
	char id[40], nome[96], url[200], preset[24], mascara[24];
	bool tem_chave;
	char modelos[1500]; /* separados por vírgula */
};

/* o que se edita no cartão de cada provedor (volta ao valor do servidor quando recarrega) */
struct edicao {
	char nome[96], url[200], chave[200], modelos[1500];
	int modelo_teste;
	bool tem_relatorio;
	bool chat, streaming, json_mode;
	int latencia;
	char erro[240];
};

struct rota {
	char chave[24], prov[40], modelo[96];
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
static struct rota rotas[MAX_ROTAS], rotas_salvas[MAX_ROTAS];
static int nrotas, nrotas_salvas;
static bool rotas_sujas;

static char aviso[300];
static bool aviso_erro, ocupado, carregado, servico_ok = true, servidor_ok;
static char srv_url[200], srv_token_mascara[64], srv_token[200];
static bool conexao_aberta;

enum acao { A_CARREGAR, A_CONTA, A_ROTA, A_SALVAR, A_ADICIONAR, A_EXCLUIR, A_MODELOS, A_TESTAR };

struct job {
	enum acao acao;
	int idx;
	char metodo[8], caminho[200], conta[700];
	char *corpo;
	struct emmar r, r2;
	char url[200], token[64];
	char modelo[96];
};

/* ---- as chamadas ---- */

static void trabalho(void *d) {
	struct job *j = d;
	if (j->acao == A_CONTA) {
		char resp[200];
		emmac_pedido(j->conta, resp, sizeof(resp));
	}
	if (j->acao == A_CARREGAR || j->acao == A_CONTA) {
		char resp[600];
		if (emmac_pedido("conta", resp, sizeof(resp))) {
			struct json *c = json_ler(resp);
			g_strlcpy(j->url, json_chave_texto(c, "url"), sizeof(j->url));
			g_strlcpy(j->token, json_chave_texto(c, "token"), sizeof(j->token));
			json_liberar(c);
		}
		emmar_chamar("GET", "/api/providers", NULL, 0, &j->r);
		if (j->r.ok) {
			emmar_chamar("GET", "/api/settings", NULL, 0, &j->r2);
		}
		return;
	}
	emmar_chamar(j->metodo, j->caminho, j->corpo, 0, &j->r);
}

static int separar_modelos(const char *csv, char ***lista);
static struct rota *rota_de(const char *chave);
static int prov_index(const char *id);
static void guardar_rotas(void);
static void iniciar(enum acao a, int idx, const char *metodo, const char *caminho, char *corpo, const char *modelo);

static void csv_de(const struct json *ms, char *dest, size_t tam) {
	GString *csv = g_string_new(NULL);
	for (int k = 0; ms && k < ms->nfilhos; k++) {
		const char *m = json_texto(json_item(ms, k));
		g_string_append_printf(csv, "%s%s", k ? ", " : "", m ? m : "");
	}
	g_strlcpy(dest, csv->str, tam);
	g_string_free(csv, TRUE);
}

static void carregar_dados(struct job *j) {
	servico_ok = j->r.servico;
	servidor_ok = j->r.ok;
	g_strlcpy(srv_url, j->url, sizeof(srv_url));
	g_strlcpy(srv_token_mascara, j->token, sizeof(srv_token_mascara));
	carregado = true;
	if (!servico_ok) {
		return;
	}
	if (!servidor_ok) {
		g_snprintf(aviso, sizeof(aviso), "Não consegui entrar no servidor da Emma. %s", j->r.msg);
		aviso_erro = true;
		conexao_aberta = true;
		return;
	}
	nprov = 0;
	for (int i = 0; j->r.corpo && i < j->r.corpo->nfilhos && nprov < MAX_PROV; i++) {
		const struct json *p = json_item(j->r.corpo, i);
		struct prov *v = &provs[nprov];
		memset(v, 0, sizeof(*v));
		g_strlcpy(v->id, json_chave_texto(p, "id"), sizeof(v->id));
		g_strlcpy(v->nome, json_chave_texto(p, "name"), sizeof(v->nome));
		g_strlcpy(v->url, json_chave_texto(p, "base_url"), sizeof(v->url));
		g_strlcpy(v->preset, json_chave_texto(p, "preset"), sizeof(v->preset));
		g_strlcpy(v->mascara, json_chave_texto(p, "api_key_masked"), sizeof(v->mascara));
		v->tem_chave = json_bool(json_chave(p, "has_key"), false);
		csv_de(json_chave(p, "models"), v->modelos, sizeof(v->modelos));
		struct edicao *e = &eds[nprov];
		memset(e, 0, sizeof(*e));
		g_strlcpy(e->nome, v->nome, sizeof(e->nome));
		g_strlcpy(e->url, v->url, sizeof(e->url));
		g_strlcpy(e->modelos, v->modelos, sizeof(e->modelos));
		nprov++;
	}
	nrotas = 0;
	const struct json *rt = json_chave(j->r2.corpo, "routing");
	for (int i = 0; rt && i < rt->nfilhos && nrotas < MAX_ROTAS; i++) {
		struct rota *r = &rotas[nrotas++];
		memset(r, 0, sizeof(*r));
		g_strlcpy(r->chave, rt->filhos[i]->chave, sizeof(r->chave));
		g_strlcpy(r->prov, json_chave_texto(rt->filhos[i], "provider_id"), sizeof(r->prov));
		g_strlcpy(r->modelo, json_chave_texto(rt->filhos[i], "model"), sizeof(r->modelo));
	}
	memcpy(rotas_salvas, rotas, sizeof(rotas));
	nrotas_salvas = nrotas;
	rotas_sujas = false;
}

static void pronto(void *d) {
	struct job *j = d;
	bool recarregar = false;
	switch (j->acao) {
	case A_CARREGAR:
	case A_CONTA:
		carregar_dados(j);
		if (j->acao == A_CONTA && servidor_ok) {
			g_strlcpy(aviso, "Conectado ao servidor da Emma.", sizeof(aviso));
			aviso_erro = false;
			conexao_aberta = false;
			srv_token[0] = '\0';
		}
		break;
	case A_ROTA:
		g_strlcpy(aviso, j->r.ok ? "Roteamento salvo." : j->r.msg, sizeof(aviso));
		aviso_erro = !j->r.ok;
		if (j->r.ok) {
			memcpy(rotas_salvas, rotas, sizeof(rotas));
			nrotas_salvas = nrotas;
			rotas_sujas = false;
		}
		break;
	case A_SALVAR:
		g_strlcpy(aviso, j->r.ok ? "Provider salvo." : j->r.msg, sizeof(aviso));
		aviso_erro = !j->r.ok;
		recarregar = j->r.ok;
		break;
	case A_ADICIONAR:
		g_strlcpy(aviso, j->r.ok ? gv_fmt("%s adicionado: configure a API key.", j->modelo).chars : j->r.msg, sizeof(aviso));
		aviso_erro = !j->r.ok;
		recarregar = j->r.ok;
		break;
	case A_EXCLUIR:
		g_strlcpy(aviso, j->r.ok ? "Provider removido." : j->r.msg, sizeof(aviso));
		aviso_erro = !j->r.ok;
		recarregar = j->r.ok;
		break;
	case A_MODELOS: {
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
		break;
	}
	case A_TESTAR:
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
		break;
	}
	ocupado = false;
	emmar_liberar(&j->r);
	emmar_liberar(&j->r2);
	g_free(j->corpo);
	g_free(j);
	if (recarregar) {
		iniciar(A_CARREGAR, 0, NULL, NULL, NULL, NULL);
		return;
	}
	/* o primeiro provedor já atende todas as tarefas que ainda não têm um (senão a Emma continuaria sem responder) */
	if (servidor_ok && nprov && !ocupado) {
		bool mudou = false;
		for (int k = 0; k < N(tarefas); k++) {
			if (!strcmp(tarefas[k].chave, "vision")) {
				continue; /* opcional */
			}
			struct rota *r = rota_de(tarefas[k].chave);
			if (prov_index(r->prov) < 0) {
				char **ms;
				int nm = separar_modelos(provs[0].modelos, &ms);
				g_strlcpy(r->prov, provs[0].id, sizeof(r->prov));
				g_strlcpy(r->modelo, nm ? ms[0] : "", sizeof(r->modelo));
				g_strfreev(ms);
				mudou = true;
			}
		}
		if (mudou) {
			guardar_rotas();
		}
	}
}

static void iniciar(enum acao a, int idx, const char *metodo, const char *caminho, char *corpo, const char *modelo) {
	struct job *j = g_new0(struct job, 1);
	j->acao = a;
	j->idx = idx;
	if (metodo) {
		g_strlcpy(j->metodo, metodo, sizeof(j->metodo));
	}
	if (caminho) {
		g_strlcpy(j->caminho, caminho, sizeof(j->caminho));
	}
	if (modelo) {
		g_strlcpy(j->modelo, modelo, sizeof(j->modelo));
	}
	j->corpo = corpo;
	ocupado = true;
	pn_tarefa(trabalho, pronto, j);
}

/* só marca: o carregamento sai do primeiro desenho (a janela pode nem existir ainda quando se abre direto numa seção) */
void pn_emma_prov_abrir(void) {
	aviso[0] = '\0';
	conexao_aberta = false;
	carregado = false;
}

static void garantir_carregado(void) {
	if (!carregado && !ocupado) {
		iniciar(A_CARREGAR, 0, NULL, NULL, NULL, NULL);
	}
}

/* ---- as rotas ---- */

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

static int prov_index(const char *id) {
	for (int i = 0; i < nprov; i++) {
		if (!strcmp(provs[i].id, id)) {
			return i;
		}
	}
	return -1;
}

static void guardar_rotas(void) {
	GString *c = g_string_new("{\"routing\":{");
	for (int i = 0; i < nrotas; i++) {
		if (!rotas[i].prov[0] && !rotas[i].modelo[0]) {
			continue; /* sem provedor: a tarefa nem entra */
		}
		g_string_append_printf(c, "%s", c->str[c->len - 1] == '{' ? "" : ",");
		json_escrever_texto(c, rotas[i].chave);
		g_string_append(c, ":{\"provider_id\":");
		json_escrever_texto(c, rotas[i].prov);
		g_string_append(c, ",\"model\":");
		json_escrever_texto(c, rotas[i].modelo);
		g_string_append_c(c, '}');
	}
	g_string_append(c, "}}");
	iniciar(A_ROTA, 0, "PUT", "/api/settings", g_string_free(c, FALSE), NULL);
}

/* os modelos de um provedor, um a um (a lista é do chamador, que libera com g_strfreev) */
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

/* ---- avisos e a conexão ---- */

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

/* a conexão com o servidor da Emma: o endereço e o token de API (criado no harness, em Configurações > Conta > Tokens de API) */
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
	CLAY(CLAY_ID("emp-srv-bt"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 0 }, .childGap = 10 } }) {
		if (gv_botao(CLAY_ID("emp-srv-entrar"), "check", "Guardar e conectar", GV_BOTAO_CHEIO) && !ocupado) {
			GString *c = g_string_new("conta {\"url\":");
			json_escrever_texto(c, srv_url);
			g_string_append(c, ",\"token\":");
			json_escrever_texto(c, srv_token);
			g_string_append_c(c, '}');
			struct job *j = g_new0(struct job, 1);
			j->acao = A_CONTA;
			g_strlcpy(j->conta, c->str, sizeof(j->conta));
			g_string_free(c, TRUE);
			ocupado = true;
			pn_tarefa(trabalho, pronto, j);
		}
		if (servidor_ok && gv_botao(CLAY_ID("emp-srv-fechar"), NULL, "Cancelar", GV_BOTAO_SIMPLES)) {
			conexao_aberta = false;
		}
	}
}

/* o que mostrar quando o serviço ou o servidor não estão de pé; devolve verdadeiro se já desenhou tudo */
static bool sem_servidor(void) {
	garantir_carregado();
	if (!carregado) {
		gv_texto(CLAY_STRING("Falando com o serviço da Emma…"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		return true;
	}
	if (!servico_ok) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 8 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("O serviço da Emma não está rodando"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Ele fica na barra do topo e cuida da conversa, da voz e dos provedores. Inicie para configurar."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 6, 0 }, .childGap = 10 } }) {
				if (gv_botao(CLAY_ID("emp-iniciar"), "power", "Iniciar a Emma", GV_BOTAO_CHEIO)) {
					pn_executar("lumna-emma");
					pn_emma_prov_abrir();
				}
				if (gv_botao(CLAY_ID("emp-tentar"), "refresh", "Tentar de novo", GV_BOTAO_ELEVADO)) {
					pn_emma_prov_abrir();
				}
			}
		}
		return true;
	}
	aviso_topo();
	if (!servidor_ok) {
		pn_subtitulo(CLAY_ID("emp-t-srv0"), "Conexão com o servidor da Emma");
		bloco_conexao();
		if (gv_botao(CLAY_ID("emp-tentar2"), "refresh", "Tentar de novo", GV_BOTAO_ELEVADO)) {
			pn_emma_prov_abrir();
		}
		return true;
	}
	return false;
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
			gv_texto(gv_str(p->tem_chave ? gv_fmt("chave %s", p->mascara).chars : "sem chave"), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			if (pn_confirmar(CLAY_IDI("emp-x", k), "trash", NULL, gv_fmt("Remover %s?", p->nome).chars, true) && !ocupado) {
				char *caminho = g_strdup_printf("/api/providers/%s", p->id);
				iniciar(A_EXCLUIR, i, "DELETE", caminho, NULL, NULL);
				g_free(caminho);
			}
		}
		CLAY(CLAY_IDI("emp-l1", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
			campo_cartao(CLAY_IDI("emp-nome", k), e->nome, sizeof(e->nome), "Nome", GV_CAMPO_TEXTO);
			campo_cartao(CLAY_IDI("emp-url", k), e->url, sizeof(e->url), "Base URL", GV_CAMPO_URL);
		}
		CLAY(CLAY_IDI("emp-l2", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
			campo_cartao(CLAY_IDI("emp-chave", k), e->chave, sizeof(e->chave), p->tem_chave ? "manter chave atual" : "API key", GV_CAMPO_SENHA);
			CLAY(CLAY_IDI("emp-modc", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
				campo_cartao(CLAY_IDI("emp-mod", k), e->modelos, sizeof(e->modelos), "modelos, separados, por vírgula", GV_CAMPO_TEXTO);
				if (gv_botao(CLAY_IDI("emp-busca", k), NULL, "/models", GV_BOTAO_SIMPLES) && !ocupado) {
					char *caminho = g_strdup_printf("/api/providers/%s/models", p->id);
					iniciar(A_MODELOS, i, "GET", caminho, NULL, NULL);
					g_free(caminho);
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
					GString *c = g_string_new("{\"model\":");
					json_escrever_texto(c, ms[e->modelo_teste]);
					g_string_append_c(c, '}');
					char *caminho = g_strdup_printf("/api/providers/%s/test", p->id);
					iniciar(A_TESTAR, i, "POST", caminho, g_string_free(c, FALSE), NULL);
					g_free(caminho);
				}
			} else {
				gv_texto(CLAY_STRING("Salve com alguns modelos (ou use /models) para poder testar."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			g_strfreev(ms);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			if (gv_botao(CLAY_IDI("emp-salvar", k), NULL, "Salvar", GV_BOTAO_CHEIO) && !ocupado && e->nome[0] && e->url[0]) {
				GString *c = g_string_new("{\"name\":");
				json_escrever_texto(c, e->nome);
				g_string_append(c, ",\"preset\":");
				json_escrever_texto(c, p->preset[0] ? p->preset : "custom");
				g_string_append(c, ",\"base_url\":");
				json_escrever_texto(c, e->url);
				if (e->chave[0]) {
					g_string_append(c, ",\"api_key\":");
					json_escrever_texto(c, e->chave);
				}
				g_string_append(c, ",\"models\":[");
				char **lista;
				int nl = separar_modelos(e->modelos, &lista);
				for (int m = 0; m < nl; m++) {
					g_string_append_printf(c, "%s", m ? "," : "");
					json_escrever_texto(c, lista[m]);
				}
				g_strfreev(lista);
				g_string_append(c, "]}");
				char *caminho = g_strdup_printf("/api/providers/%s", p->id);
				iniciar(A_SALVAR, i, "PATCH", caminho, g_string_free(c, FALSE), NULL);
				g_free(caminho);
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

void pn_emma_prov_desenhar(void) {
	if (sem_servidor()) {
		return;
	}
	if (ocupado) {
		gv_texto(CLAY_STRING("Falando com o servidor da Emma…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	/* os botões de adicionar, um por tipo */
	CLAY(CLAY_ID("emp-presets"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
		for (int i = 0; i < N(presets); i++) {
			if (gv_botao(CLAY_IDI("emp-add", (uint32_t)i), "plus", presets[i].nome, GV_BOTAO_CONTORNO) && !ocupado) {
				GString *c = g_string_new("{\"name\":");
				json_escrever_texto(c, presets[i].nome);
				g_string_append(c, ",\"preset\":");
				json_escrever_texto(c, presets[i].id);
				g_string_append(c, ",\"base_url\":");
				json_escrever_texto(c, presets[i].url);
				g_string_append(c, ",\"models\":[");
				char **ms;
				int nm = separar_modelos(presets[i].modelos, &ms);
				for (int m = 0; m < nm; m++) {
					g_string_append_printf(c, "%s", m ? "," : "");
					json_escrever_texto(c, ms[m]);
				}
				g_strfreev(ms);
				g_string_append(c, "]}");
				iniciar(A_ADICIONAR, i, "POST", "/api/providers", g_string_free(c, FALSE), presets[i].nome);
			}
		}
	}
	if (!nprov) {
		gv_texto(CLAY_STRING("Nenhum provedor ainda. Adicione um acima e coloque a API key para a Emma poder conversar."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	for (int i = 0; i < nprov; i++) {
		cartao_provedor(i);
	}
	/* a conexão com o servidor fica recolhida: só interessa a quem usa outro servidor */
	CLAY(CLAY_ID("emp-con-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 18, 0 } } }) {
		if (gv_botao(CLAY_ID("emp-con-b"), conexao_aberta ? "chevron-up" : "chevron-down", "Conexão com o servidor", GV_BOTAO_SIMPLES)) {
			conexao_aberta = !conexao_aberta;
		}
	}
	if (conexao_aberta) {
		bloco_conexao();
	}
}

/* ---- Roteamento: qual provedor e modelo atende cada tarefa ---- */

bool pn_emma_rota_pendente(void) {
	return rotas_sujas;
}

void pn_emma_rota_aplicar(void) {
	if (!ocupado) {
		guardar_rotas();
	}
}

void pn_emma_rota_descartar(void) {
	memcpy(rotas, rotas_salvas, sizeof(rotas));
	nrotas = nrotas_salvas;
	rotas_sujas = false;
}

void pn_emma_rota_desenhar(void) {
	if (sem_servidor()) {
		return;
	}
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
			int pi = prov_index(r->prov), esc = pi + 1;
			CLAY(CLAY_IDI("emr-pc", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(210) } } }) {
				if (gv_lista_suspensa(CLAY_IDI("emr-p", k), nomes, nprov + 1, &esc)) {
					g_strlcpy(r->prov, esc > 0 ? provs[esc - 1].id : "", sizeof(r->prov));
					char **ms;
					int nm = esc > 0 ? separar_modelos(provs[esc - 1].modelos, &ms) : 0;
					g_strlcpy(r->modelo, nm ? ms[0] : "", sizeof(r->modelo));
					if (nm) {
						g_strfreev(ms);
					}
					rotas_sujas = true;
				}
			}
			pi = prov_index(r->prov);
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
