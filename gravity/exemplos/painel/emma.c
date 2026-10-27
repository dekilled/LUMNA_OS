/*
 * Emma > Provedores e modelos: a conta do servidor da Emma (endereço, e-mail e senha), os provedores de IA (Ollama, OpenAI, Gemini,
 * Claude ou um endereço seu), as chaves de API e qual modelo atende a conversa e o plano. Tudo vai e volta pelo serviço lumna-emma
 * (servicos/emmar.h); as chamadas rodam numa linha à parte e o resultado aparece no aviso do topo.
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
#define MAX_ROTAS 8

struct prov {
	char id[40], nome[96], url[200], preset[24], mascara[24];
	bool tem_chave;
	char modelos[1500]; /* separados por vírgula */
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

static struct prov provs[MAX_PROV];
static int nprov;
static struct rota rotas[MAX_ROTAS];
static int nrotas;

static char aviso[300];
static bool aviso_erro, ocupado, carregado, servico_ok = true, servidor_ok;
static char srv_url[200], srv_email[120], srv_senha[120];
static bool conta_aberta;

static bool form_aberto;
static char ed_id[40], ed_nome[96], ed_url[200], ed_chave[200], ed_modelos[1500];
static int ed_preset = 4;

enum acao { A_CARREGAR, A_CONTA, A_ROTA, A_SALVAR, A_EXCLUIR, A_MODELOS, A_TESTAR };

struct job {
	enum acao acao;
	char metodo[8], caminho[200], conta[700];
	char *corpo;
	struct emmar r, r2;
	char url[200], email[120];
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
			g_strlcpy(j->email, json_chave_texto(c, "email"), sizeof(j->email));
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

static void carregar_dados(struct job *j) {
	servico_ok = j->r.servico;
	servidor_ok = j->r.ok;
	g_strlcpy(srv_url, j->url, sizeof(srv_url));
	g_strlcpy(srv_email, j->email, sizeof(srv_email));
	carregado = true;
	if (!servico_ok) {
		return;
	}
	if (!servidor_ok) {
		g_snprintf(aviso, sizeof(aviso), "%s Confira o endereço e a conta do servidor da Emma.", j->r.msg);
		aviso_erro = true;
		conta_aberta = true;
		return;
	}
	nprov = 0;
	for (int i = 0; j->r.corpo && i < j->r.corpo->nfilhos && nprov < MAX_PROV; i++) {
		const struct json *p = json_item(j->r.corpo, i);
		struct prov *v = &provs[nprov++];
		memset(v, 0, sizeof(*v));
		g_strlcpy(v->id, json_chave_texto(p, "id"), sizeof(v->id));
		g_strlcpy(v->nome, json_chave_texto(p, "name"), sizeof(v->nome));
		g_strlcpy(v->url, json_chave_texto(p, "base_url"), sizeof(v->url));
		g_strlcpy(v->preset, json_chave_texto(p, "preset"), sizeof(v->preset));
		g_strlcpy(v->mascara, json_chave_texto(p, "api_key_masked"), sizeof(v->mascara));
		v->tem_chave = json_bool(json_chave(p, "has_key"), false);
		const struct json *ms = json_chave(p, "models");
		GString *csv = g_string_new(NULL);
		for (int k = 0; ms && k < ms->nfilhos; k++) {
			g_string_append_printf(csv, "%s%s", k ? ", " : "", json_texto(json_item(ms, k)) ? json_texto(json_item(ms, k)) : "");
		}
		g_strlcpy(v->modelos, csv->str, sizeof(v->modelos));
		g_string_free(csv, TRUE);
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
}

static void iniciar(enum acao a, const char *metodo, const char *caminho, char *corpo, const char *modelo);
static struct rota *rota_de(const char *chave);
static int prov_index(const char *id);
static int separar_modelos(const char *csv, char ***lista);
static void guardar_rotas(void);

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
			conta_aberta = false;
			srv_senha[0] = '\0';
		}
		break;
	case A_ROTA:
		g_strlcpy(aviso, j->r.ok ? "Modelo da Emma guardado." : j->r.msg, sizeof(aviso));
		aviso_erro = !j->r.ok;
		break;
	case A_SALVAR:
		if (j->r.ok) {
			g_strlcpy(aviso, "Provedor guardado.", sizeof(aviso));
			aviso_erro = false;
			form_aberto = false;
			ed_chave[0] = '\0';
			recarregar = true;
		} else {
			g_strlcpy(aviso, j->r.msg, sizeof(aviso));
			aviso_erro = true;
		}
		break;
	case A_EXCLUIR:
		g_strlcpy(aviso, j->r.ok ? "Provedor removido." : j->r.msg, sizeof(aviso));
		aviso_erro = !j->r.ok;
		recarregar = j->r.ok;
		break;
	case A_MODELOS: {
		const struct json *ms = json_chave(j->r.corpo, "models");
		if (j->r.ok && json_bool(json_chave(j->r.corpo, "ok"), false) && ms && ms->nfilhos) {
			GString *csv = g_string_new(NULL);
			for (int k = 0; k < ms->nfilhos; k++) {
				g_string_append_printf(csv, "%s%s", k ? ", " : "", json_texto(json_item(ms, k)) ? json_texto(json_item(ms, k)) : "");
			}
			g_strlcpy(ed_modelos, csv->str, sizeof(ed_modelos));
			g_snprintf(aviso, sizeof(aviso), "%d modelos encontrados. Confira a lista e guarde.", ms->nfilhos);
			g_string_free(csv, TRUE);
			aviso_erro = false;
		} else {
			const char *e = json_chave_texto(j->r.corpo, "error");
			g_snprintf(aviso, sizeof(aviso), "Não consegui buscar os modelos: %s", *e ? e : j->r.msg);
			aviso_erro = true;
		}
		break;
	}
	case A_TESTAR: {
		if (!j->r.ok) {
			g_strlcpy(aviso, j->r.msg, sizeof(aviso));
			aviso_erro = true;
		} else if (json_bool(json_chave(j->r.corpo, "chat"), false)) {
			g_snprintf(aviso, sizeof(aviso), "%s respondeu em %d ms%s%s.", j->modelo, (int)json_numero(json_chave(j->r.corpo, "latency_ms"), 0),
				json_bool(json_chave(j->r.corpo, "streaming"), false) ? ", com resposta em tempo real" : "",
				json_bool(json_chave(j->r.corpo, "json_mode"), false) ? " e modo JSON" : "");
			aviso_erro = false;
		} else {
			g_snprintf(aviso, sizeof(aviso), "O teste falhou: %s", json_chave_texto(j->r.corpo, "error"));
			aviso_erro = true;
		}
		break;
	}
	}
	ocupado = false;
	emmar_liberar(&j->r);
	emmar_liberar(&j->r2);
	g_free(j->corpo);
	g_free(j);
	if (recarregar) {
		iniciar(A_CARREGAR, NULL, NULL, NULL, NULL);
		return;
	}
	/* o primeiro provedor já atende a conversa e o plano (senão a Emma continuaria sem responder) */
	if (servidor_ok && nprov && !ocupado) {
		bool mudou = false;
		static const char *chaves[] = { "chat", "planner" };
		for (int k = 0; k < N(chaves); k++) {
			struct rota *r = rota_de(chaves[k]);
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

static void iniciar(enum acao a, const char *metodo, const char *caminho, char *corpo, const char *modelo) {
	struct job *j = g_new0(struct job, 1);
	j->acao = a;
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
	form_aberto = false;
	conta_aberta = false;
	carregado = false;
}

/* ---- as rotas (qual provedor e modelo atende a conversa e o plano) ---- */

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
		g_string_append_printf(c, "%s", i ? "," : "");
		json_escrever_texto(c, rotas[i].chave);
		g_string_append(c, ":{\"provider_id\":");
		json_escrever_texto(c, rotas[i].prov);
		g_string_append(c, ",\"model\":");
		json_escrever_texto(c, rotas[i].modelo);
		g_string_append_c(c, '}');
	}
	g_string_append(c, "}}");
	iniciar(A_ROTA, "PUT", "/api/settings", g_string_free(c, FALSE), NULL);
}

/* os modelos de um provedor, um a um (o texto é do chamador, que libera) */
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

static void linha_rota(const char *chave, const char *titulo, const char *sub) {
	struct rota *r = rota_de(chave);
	const char *nomes[MAX_PROV + 1] = { "Escolher…" };
	for (int i = 0; i < nprov; i++) {
		nomes[i + 1] = provs[i].nome;
	}
	int pi = prov_index(r->prov), esc = pi + 1;
	if (pn_ajuste_lista(CLAY_SID(gv_fmt("emp-rp-%s", chave)), "globe", titulo, sub, nomes, nprov + 1, &esc)) {
		g_strlcpy(r->prov, esc > 0 ? provs[esc - 1].id : "", sizeof(r->prov));
		/* o modelo de antes não vale para outro provedor: o primeiro dele */
		char **ms;
		int nm = esc > 0 ? separar_modelos(provs[esc - 1].modelos, &ms) : 0;
		g_strlcpy(r->modelo, nm ? ms[0] : "", sizeof(r->modelo));
		if (nm) {
			g_strfreev(ms);
		}
		guardar_rotas();
	}
	pi = prov_index(r->prov);
	if (pi < 0) {
		return;
	}
	char **ms;
	int nm = separar_modelos(provs[pi].modelos, &ms);
	if (!nm) {
		g_strfreev(ms);
		pn_linha_comecar(CLAY_SID(gv_fmt("emp-rm0-%s", chave)), "sparkle", "Modelo", "Este provedor ainda não tem modelos: adicione nele.");
		pn_linha_acabar();
		return;
	}
	const char *opcoes[64];
	int n = nm > 64 ? 64 : nm, atual = 0;
	for (int i = 0; i < n; i++) {
		opcoes[i] = ms[i];
		if (!strcmp(ms[i], r->modelo)) {
			atual = i;
		}
	}
	if (pn_ajuste_lista(CLAY_SID(gv_fmt("emp-rm-%s", chave)), "sparkle", "Modelo", NULL, opcoes, n, &atual)) {
		g_strlcpy(r->modelo, ms[atual], sizeof(r->modelo));
		guardar_rotas();
	}
	g_strfreev(ms);
}

/* ---- o formulário de um provedor ---- */

static void abrir_form(int i) {
	form_aberto = true;
	ed_chave[0] = '\0';
	if (i < 0) {
		ed_id[0] = '\0';
		ed_nome[0] = ed_url[0] = ed_modelos[0] = '\0';
		ed_preset = 4;
		g_strlcpy(ed_url, presets[4].url, sizeof(ed_url));
	} else {
		g_strlcpy(ed_id, provs[i].id, sizeof(ed_id));
		g_strlcpy(ed_nome, provs[i].nome, sizeof(ed_nome));
		g_strlcpy(ed_url, provs[i].url, sizeof(ed_url));
		g_strlcpy(ed_modelos, provs[i].modelos, sizeof(ed_modelos));
		ed_preset = 4;
		for (int k = 0; k < N(presets); k++) {
			if (!strcmp(presets[k].id, provs[i].preset)) {
				ed_preset = k;
			}
		}
	}
}

static void guardar_provedor(void) {
	GString *c = g_string_new("{\"name\":");
	json_escrever_texto(c, ed_nome);
	g_string_append(c, ",\"preset\":");
	json_escrever_texto(c, presets[ed_preset].id);
	g_string_append(c, ",\"base_url\":");
	json_escrever_texto(c, ed_url);
	if (ed_chave[0]) {
		g_string_append(c, ",\"api_key\":");
		json_escrever_texto(c, ed_chave);
	}
	g_string_append(c, ",\"models\":[");
	char **ms;
	int nm = separar_modelos(ed_modelos, &ms);
	for (int i = 0; i < nm; i++) {
		g_string_append_printf(c, "%s", i ? "," : "");
		json_escrever_texto(c, ms[i]);
	}
	g_strfreev(ms);
	g_string_append(c, "]}");
	if (ed_id[0]) {
		char *p = g_strdup_printf("/api/providers/%s", ed_id);
		iniciar(A_SALVAR, "PATCH", p, g_string_free(c, FALSE), NULL);
		g_free(p);
	} else {
		iniciar(A_SALVAR, "POST", "/api/providers", g_string_free(c, FALSE), NULL);
	}
}

static void form_provedor(void) {
	CLAY(CLAY_ID("emp-form"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 4 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_texto(gv_str(ed_id[0] ? "Editar provedor" : "Novo provedor"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		const char *nomes[N(presets)];
		for (int i = 0; i < N(presets); i++) {
			nomes[i] = presets[i].nome;
		}
		if (pn_ajuste_lista(CLAY_ID("emp-preset"), "globe", "Tipo", "Preenche o endereço e os modelos mais comuns.", nomes, N(presets), &ed_preset)) {
			g_strlcpy(ed_url, presets[ed_preset].url, sizeof(ed_url));
			if (!ed_id[0] || !ed_modelos[0]) {
				g_strlcpy(ed_modelos, presets[ed_preset].modelos, sizeof(ed_modelos));
			}
			if (!ed_id[0]) {
				g_strlcpy(ed_nome, presets[ed_preset].nome, sizeof(ed_nome));
			}
		}
		pn_linha_comecar(CLAY_ID("emp-f-nome"), "text", "Nome", "Como aparece nas listas.");
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(360) } } }) {
			gv_campo(CLAY_ID("emp-f-nome-c"), ed_nome, sizeof(ed_nome), "Meu provedor", GV_CAMPO_TEXTO);
		}
		pn_linha_acabar();
		pn_linha_comecar(CLAY_ID("emp-f-url"), "link", "Endereço", "A API compatível com OpenAI (termina em /v1).");
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(360) } } }) {
			gv_campo(CLAY_ID("emp-f-url-c"), ed_url, sizeof(ed_url), "https://…/v1", GV_CAMPO_URL);
		}
		pn_linha_acabar();
		int pi = ed_id[0] ? prov_index(ed_id) : -1;
		pn_linha_comecar(CLAY_ID("emp-f-chave"), "lock", "Chave de API",
			pi >= 0 && provs[pi].tem_chave ? gv_fmt("Guardada (%s). Deixe vazio para manter.", provs[pi].mascara).chars : "Fica guardada só no servidor da Emma.");
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(360) } } }) {
			gv_campo(CLAY_ID("emp-f-chave-c"), ed_chave, sizeof(ed_chave), "sk-…", GV_CAMPO_SENHA);
		}
		pn_linha_acabar();
		pn_linha_comecar(CLAY_ID("emp-f-mod"), "sparkle", "Modelos", "Separados por vírgula.");
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(360) } } }) {
			gv_campo(CLAY_ID("emp-f-mod-c"), ed_modelos, sizeof(ed_modelos), "modelo-a, modelo-b", GV_CAMPO_TEXTO);
		}
		pn_linha_acabar();
		CLAY(CLAY_ID("emp-f-bt"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 10, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao(CLAY_ID("emp-f-salvar"), "check", "Guardar", GV_BOTAO_CHEIO) && !ocupado && ed_nome[0] && ed_url[0]) {
				guardar_provedor();
			}
			if (ed_id[0] && gv_botao(CLAY_ID("emp-f-buscar"), "refresh", "Buscar modelos no provedor", GV_BOTAO_ELEVADO) && !ocupado) {
				char *p = g_strdup_printf("/api/providers/%s/models", ed_id);
				iniciar(A_MODELOS, "GET", p, NULL, NULL);
				g_free(p);
			}
			if (!ed_id[0]) {
				gv_texto(CLAY_STRING("Guarde primeiro para poder buscar os modelos."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			if (gv_botao(CLAY_ID("emp-f-cancelar"), NULL, "Cancelar", GV_BOTAO_SIMPLES)) {
				form_aberto = false;
			}
		}
	}
}

/* ---- a página ---- */

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

static void card_servidor(void) {
	pn_subtitulo(CLAY_ID("emp-t-srv"), "Servidor da Emma");
	if (!conta_aberta) {
		pn_linha_comecar(CLAY_ID("emp-srv"), "computer", srv_url[0] ? srv_url : "Sem endereço", servidor_ok ? (srv_email[0] ? gv_fmt("Conectado como %s", srv_email).chars : "Conectado") : "Sem conexão");
		if (gv_botao(CLAY_ID("emp-srv-alt"), "edit", "Alterar", GV_BOTAO_ELEVADO)) {
			conta_aberta = true;
		}
		pn_linha_acabar();
		return;
	}
	pn_linha_comecar(CLAY_ID("emp-srv-u"), "link", "Endereço", "Onde o servidor da Emma está (o padrão é este computador).");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(360) } } }) {
		gv_campo(CLAY_ID("emp-srv-u-c"), srv_url, sizeof(srv_url), "http://127.0.0.1:8001", GV_CAMPO_URL);
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("emp-srv-e"), "user", "E-mail", "A conta da Emma.");
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
		if (gv_botao(CLAY_ID("emp-srv-entrar"), "check", "Guardar e entrar", GV_BOTAO_CHEIO) && !ocupado) {
			GString *c = g_string_new("conta {\"url\":");
			json_escrever_texto(c, srv_url);
			g_string_append(c, ",\"email\":");
			json_escrever_texto(c, srv_email);
			g_string_append(c, ",\"senha\":");
			json_escrever_texto(c, srv_senha);
			g_string_append_c(c, '}');
			struct job *j = g_new0(struct job, 1);
			j->acao = A_CONTA;
			g_strlcpy(j->conta, c->str, sizeof(j->conta));
			g_string_free(c, TRUE);
			ocupado = true;
			pn_tarefa(trabalho, pronto, j);
		}
		if (servidor_ok && gv_botao(CLAY_ID("emp-srv-fechar"), NULL, "Cancelar", GV_BOTAO_SIMPLES)) {
			conta_aberta = false;
		}
	}
}

void pn_emma_prov_desenhar(void) {
	if (!carregado && !ocupado) {
		iniciar(A_CARREGAR, NULL, NULL, NULL, NULL);
	}
	if (!carregado) {
		gv_texto(CLAY_STRING("Falando com o serviço da Emma…"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
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
		return;
	}
	aviso_topo();
	if (ocupado) {
		gv_texto(CLAY_STRING("Falando com o servidor da Emma…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	card_servidor();
	if (!servidor_ok) {
		return;
	}
	if (nprov) {
		pn_subtitulo(CLAY_ID("emp-t-rotas"), "Quem responde");
		linha_rota("chat", "Conversa", "O provedor da conversa de todo dia.");
		linha_rota("planner", "Plano", "Quem monta o plano antes de a Emma criar uma macro.");
	}
	pn_subtitulo(CLAY_ID("emp-t-provs"), "Provedores");
	if (!nprov) {
		gv_texto(CLAY_STRING("Nenhum provedor ainda. Adicione um (e a chave de API) para a Emma poder conversar."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	for (int i = 0; i < nprov; i++) {
		const struct prov *p = &provs[i];
		char **ms;
		int nm = separar_modelos(p->modelos, &ms);
		g_strfreev(ms);
		pn_linha_comecar(CLAY_IDI("emp-p", (uint32_t)i), "globe", p->nome,
			gv_fmt("%s · %s · %d modelo%s", p->url, p->tem_chave ? gv_fmt("chave %s", p->mascara).chars : "sem chave", nm, nm == 1 ? "" : "s").chars);
		if (gv_botao(CLAY_IDI("emp-p-t", (uint32_t)i), "zap", "Testar", GV_BOTAO_ELEVADO) && !ocupado) {
			struct rota *r = rota_de("chat");
			char **lista;
			int n = separar_modelos(p->modelos, &lista);
			const char *modelo = !strcmp(r->prov, p->id) && r->modelo[0] ? r->modelo : n ? lista[0] : "";
			if (*modelo) {
				GString *c = g_string_new("{\"model\":");
				json_escrever_texto(c, modelo);
				g_string_append_c(c, '}');
				char *caminho = g_strdup_printf("/api/providers/%s/test", p->id);
				iniciar(A_TESTAR, "POST", caminho, g_string_free(c, FALSE), modelo);
				g_free(caminho);
			} else {
				g_strlcpy(aviso, "Este provedor não tem modelos para testar: edite e adicione.", sizeof(aviso));
				aviso_erro = true;
			}
			g_strfreev(lista);
		}
		if (gv_botao(CLAY_IDI("emp-p-e", (uint32_t)i), "edit", "Editar", GV_BOTAO_ELEVADO)) {
			abrir_form(i);
		}
		if (pn_confirmar(CLAY_IDI("emp-p-x", (uint32_t)i), "trash", NULL, gv_fmt("Remover %s?", p->nome).chars, true) && !ocupado) {
			char *caminho = g_strdup_printf("/api/providers/%s", p->id);
			iniciar(A_EXCLUIR, "DELETE", caminho, NULL, NULL);
			g_free(caminho);
		}
		pn_linha_acabar();
	}
	if (form_aberto) {
		form_provedor();
	} else {
		CLAY(CLAY_ID("emp-novo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 8, 0 } } }) {
			if (gv_botao(CLAY_ID("emp-novo-b"), "plus-circle", "Adicionar provedor", GV_BOTAO_CONTORNO)) {
				abrir_form(-1);
			}
		}
	}
}
