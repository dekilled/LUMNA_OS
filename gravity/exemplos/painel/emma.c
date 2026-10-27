/*
 * Emma > Provedores e Roteamento: as telas do harness (Configurações > Providers e Roteamento) aqui no Painel. Provedores de IA compatíveis com
 * OpenAI (Ollama, OpenAI, Gemini, Claude ou um endereço seu) com a URL, a chave de API e os modelos, o teste de capacidades, e qual provedor e
 * modelo atende cada tarefa (Chat, Planner, RAG, Coder, Visão).
 *
 * Tudo fica guardado em ~/.config/lumna/emma/provedores.json (só do usuário, tem as chaves). A Emma (servicos/emmal.c) lê esse arquivo e
 * conversa direto com o provedor na nuvem: não há servidor no meio, nem login, nem token. Só "/models" e "Testar capacidades" pedem ao
 * serviço lumna-emma (comando "llm") para falar com o provedor.
 */
#include "painel.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emmac.h"
#include "json.h"

#define N(lista) (int)(sizeof(lista) / sizeof(lista[0]))
#define MAX_PROV 16
#define MAX_ROTAS 12
#define MAX_APAGAR 16

struct prov {
	char lid[24];
	char nome[96], preset[24], url[200], chave[200], modelos[1500]; /* modelos separados por vírgula */
};

/* o que se edita no cartão de cada provedor */
struct edicao {
	char nome[96], url[200], chave[200], modelos[1500];
	int modelo_teste;
	bool tem_relatorio, chat, streaming, json_mode;
	int latencia;
	char erro[300];
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
static bool rotas_sujas;
static bool lido, ocupado;
static char aviso[400];
static bool aviso_erro;
static guint proximo_lid = 1;

/* ---- o arquivo ---- */

static void caminho_arquivo(char *buf, size_t tam) {
	pn_caminho(buf, tam, "emma/provedores.json");
}

static void gravar_arquivo(void) {
	GString *s = g_string_new("{\"provedores\":[");
	for (int i = 0; i < nprov; i++) {
		const struct prov *p = &provs[i];
		g_string_append_printf(s, "%s{\"lid\":", i ? "," : "");
		json_escrever_texto(s, p->lid);
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
		g_string_append_c(s, '}');
	}
	g_string_append(s, "],\"rotas\":[");
	bool primeiro = true;
	for (int i = 0; i < nrotas; i++) {
		if (!rotas[i].lid[0]) {
			continue;
		}
		g_string_append_printf(s, "%s{\"chave\":", primeiro ? "" : ",");
		primeiro = false;
		json_escrever_texto(s, rotas[i].chave);
		g_string_append(s, ",\"lid\":");
		json_escrever_texto(s, rotas[i].lid);
		g_string_append(s, ",\"modelo\":");
		json_escrever_texto(s, rotas[i].modelo);
		g_string_append_c(s, '}');
	}
	g_string_append_printf(s, "],\"proximo\":%u}", proximo_lid);
	char p[512], *dir;
	caminho_arquivo(p, sizeof(p));
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

static void preparar_edicao(int i) {
	struct edicao *e = &eds[i];
	memset(e, 0, sizeof(*e));
	g_strlcpy(e->nome, provs[i].nome, sizeof(e->nome));
	g_strlcpy(e->url, provs[i].url, sizeof(e->url));
	g_strlcpy(e->modelos, provs[i].modelos, sizeof(e->modelos));
}

static void ler_arquivo(void) {
	char p[512], *t = NULL;
	lido = true;
	nprov = nrotas = 0;
	rotas_sujas = false;
	caminho_arquivo(p, sizeof(p));
	if (!g_file_get_contents(p, &t, NULL, NULL)) {
		return;
	}
	struct json *j = json_ler(t);
	g_free(t);
	const struct json *ps = json_chave(j, "provedores");
	for (int i = 0; ps && i < ps->nfilhos && nprov < MAX_PROV; i++) {
		const struct json *q = json_item(ps, i);
		struct prov *v = &provs[nprov];
		memset(v, 0, sizeof(*v));
		g_strlcpy(v->lid, json_chave_texto(q, "lid"), sizeof(v->lid));
		g_strlcpy(v->nome, json_chave_texto(q, "nome"), sizeof(v->nome));
		g_strlcpy(v->preset, json_chave_texto(q, "preset"), sizeof(v->preset));
		g_strlcpy(v->url, json_chave_texto(q, "url"), sizeof(v->url));
		g_strlcpy(v->modelos, json_chave_texto(q, "modelos"), sizeof(v->modelos));
		g_strlcpy(v->chave, json_chave_texto(q, "chave"), sizeof(v->chave));
		preparar_edicao(nprov);
		nprov++;
	}
	const struct json *rs = json_chave(j, "rotas");
	for (int i = 0; rs && i < rs->nfilhos && nrotas < MAX_ROTAS; i++) {
		const struct json *q = json_item(rs, i);
		g_strlcpy(rotas[nrotas].chave, json_chave_texto(q, "chave"), sizeof(rotas[0].chave));
		g_strlcpy(rotas[nrotas].lid, json_chave_texto(q, "lid"), sizeof(rotas[0].lid));
		g_strlcpy(rotas[nrotas].modelo, json_chave_texto(q, "modelo"), sizeof(rotas[0].modelo));
		nrotas++;
	}
	proximo_lid = (guint)json_numero(json_chave(j, "proximo"), 1);
	json_liberar(j);
}

/* só marca: o arquivo é lido no primeiro desenho */
void pn_emma_prov_abrir(void) {
	aviso[0] = '\0';
	lido = false;
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

/* o primeiro provedor já atende as tarefas que ainda não têm um (senão a Emma continuaria sem responder) */
static void rotas_padrao(void) {
	for (int k = 0; nprov && k < N(tarefas); k++) {
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
		}
	}
}

static const char *situacao(const struct prov *p) {
	size_t n = strlen(p->chave);
	if (!n) {
		return "sem chave";
	}
	return n > 4 ? gv_fmt("chave ••••%s", p->chave + n - 4).chars : "chave guardada";
}

/* ---- /models e Testar capacidades: o serviço fala com o provedor ---- */

enum acao { A_MODELOS, A_TESTAR };

struct job {
	enum acao acao;
	int idx;
	char pedido[1000];
	char resp[16384];
	bool falou;
};

static void trabalho(void *d) {
	struct job *j = d;
	j->falou = emmac_pedido(j->pedido, j->resp, sizeof(j->resp));
}

static void pronto(void *d) {
	struct job *j = d;
	struct json *r = j->falou ? json_ler(j->resp) : NULL;
	if (!j->falou) {
		g_strlcpy(aviso, "O serviço da Emma não está rodando: inicie pela barra do topo e tente de novo.", sizeof(aviso));
		aviso_erro = true;
	} else if (j->acao == A_MODELOS) {
		const struct json *ms = json_chave(r, "models");
		if (json_bool(json_chave(r, "ok"), false) && ms && ms->nfilhos && j->idx < nprov) {
			GString *csv = g_string_new(NULL);
			for (int k = 0; k < ms->nfilhos; k++) {
				const char *m = json_texto(json_item(ms, k));
				g_string_append_printf(csv, "%s%s", k ? ", " : "", m ? m : "");
			}
			g_strlcpy(eds[j->idx].modelos, csv->str, sizeof(eds[j->idx].modelos));
			g_string_free(csv, TRUE);
			g_snprintf(aviso, sizeof(aviso), "%d modelos encontrados: confira a lista e salve.", ms->nfilhos);
			aviso_erro = false;
		} else {
			const char *e = json_chave_texto(r, "error");
			g_strlcpy(aviso, *e ? e : "Endpoint não suporta /models", sizeof(aviso));
			aviso_erro = true;
		}
	} else if (j->idx < nprov) {
		struct edicao *e = &eds[j->idx];
		e->tem_relatorio = true;
		e->chat = json_bool(json_chave(r, "chat"), false);
		e->streaming = json_bool(json_chave(r, "streaming"), false);
		e->json_mode = json_bool(json_chave(r, "json_mode"), false);
		e->latencia = (int)json_numero(json_chave(r, "latency_ms"), 0);
		g_strlcpy(e->erro, json_chave_texto(r, "error"), sizeof(e->erro));
	}
	json_liberar(r);
	ocupado = false;
	g_free(j);
}

static void chamar_provedor(enum acao a, int idx, const char *modelo) {
	struct job *j = g_new0(struct job, 1);
	j->acao = a;
	j->idx = idx;
	GString *c = g_string_new("llm {\"acao\":");
	json_escrever_texto(c, a == A_MODELOS ? "modelos" : "testar");
	g_string_append(c, ",\"url\":");
	json_escrever_texto(c, provs[idx].url);
	g_string_append(c, ",\"chave\":");
	json_escrever_texto(c, provs[idx].chave);
	if (modelo) {
		g_string_append(c, ",\"modelo\":");
		json_escrever_texto(c, modelo);
	}
	g_string_append_c(c, '}');
	g_strlcpy(j->pedido, c->str, sizeof(j->pedido));
	g_string_free(c, TRUE);
	ocupado = true;
	pn_tarefa(trabalho, pronto, j);
}

/* ---- avisos ---- */

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
			gv_texto(gv_str(situacao(p)), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			if (pn_confirmar(CLAY_IDI("emp-x", k), "trash", NULL, gv_fmt("Remover %s?", p->nome).chars, true)) {
				for (int t = 0; t < nrotas; t++) {
					if (!strcmp(rotas[t].lid, p->lid)) {
						rotas[t].lid[0] = rotas[t].modelo[0] = '\0';
					}
				}
				memmove(&provs[i], &provs[i + 1], (size_t)(nprov - i - 1) * sizeof(provs[0]));
				memmove(&eds[i], &eds[i + 1], (size_t)(nprov - i - 1) * sizeof(eds[0]));
				nprov--;
				rotas_padrao();
				gravar_arquivo();
				g_strlcpy(aviso, "Provedor removido.", sizeof(aviso));
				aviso_erro = false;
				return;
			}
		}
		CLAY(CLAY_IDI("emp-l1", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
			campo_cartao(CLAY_IDI("emp-nome", k), e->nome, sizeof(e->nome), "Nome", GV_CAMPO_TEXTO);
			campo_cartao(CLAY_IDI("emp-url", k), e->url, sizeof(e->url), "Base URL", GV_CAMPO_URL);
		}
		CLAY(CLAY_IDI("emp-l2", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
			campo_cartao(CLAY_IDI("emp-chave", k), e->chave, sizeof(e->chave), p->chave[0] ? "manter chave atual" : "API key", GV_CAMPO_SENHA);
			CLAY(CLAY_IDI("emp-modc", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
				campo_cartao(CLAY_IDI("emp-mod", k), e->modelos, sizeof(e->modelos), "modelos, separados, por vírgula", GV_CAMPO_TEXTO);
				if (gv_botao(CLAY_IDI("emp-busca", k), NULL, "/models", GV_BOTAO_SIMPLES) && !ocupado) {
					/* busca com o que está digitado agora, sem precisar salvar antes */
					g_strlcpy(p->url, e->url, sizeof(p->url));
					if (e->chave[0]) {
						g_strlcpy(p->chave, e->chave, sizeof(p->chave));
					}
					chamar_provedor(A_MODELOS, i, NULL);
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
					g_strlcpy(p->url, e->url, sizeof(p->url));
					if (e->chave[0]) {
						g_strlcpy(p->chave, e->chave, sizeof(p->chave));
					}
					e->tem_relatorio = false;
					chamar_provedor(A_TESTAR, i, ms[e->modelo_teste]);
				}
			} else {
				gv_texto(CLAY_STRING("Coloque alguns modelos (ou use /models) para poder testar."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
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
				rotas_padrao();
				gravar_arquivo();
				g_strlcpy(aviso, "Provedor salvo.", sizeof(aviso));
				aviso_erro = false;
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
				gv_texto(CLAY_STRING("Compatibilidade parcial: este provedor não tem o modo JSON, mas a conversa funciona."), gv_t.tamanho_corpo - 2, gv_t.aviso, GV_PESO_NORMAL);
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
	preparar_edicao(nprov);
	nprov++;
	rotas_padrao();
	gravar_arquivo();
	g_strlcpy(aviso, gv_fmt("%s adicionado: coloque a API key e salve.", presets[i].nome).chars, sizeof(aviso));
	aviso_erro = false;
}

void pn_emma_prov_desenhar(void) {
	if (!lido) {
		ler_arquivo();
	}
	aviso_topo();
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
}

/* ---- Roteamento: qual provedor e modelo atende cada tarefa ---- */

bool pn_emma_rota_pendente(void) {
	return rotas_sujas;
}

void pn_emma_rota_aplicar(void) {
	rotas_sujas = false;
	gravar_arquivo();
	g_strlcpy(aviso, "Roteamento salvo.", sizeof(aviso));
	aviso_erro = false;
}

void pn_emma_rota_descartar(void) {
	ler_arquivo();
}

void pn_emma_rota_desenhar(void) {
	if (!lido) {
		ler_arquivo();
	}
	aviso_topo();
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
