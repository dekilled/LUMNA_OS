/*
 * Painel > Emma > Servidores MCP: ferramentas de fora (Model Context Protocol) que a Emma pode chamar. O servidor da Emma conecta cada servidor
 * (um comando local, falando por stdin/stdout, ou um endereço http); aqui se adiciona, reinicia e remove, e se escolhe o que cada ferramenta pode:
 * Automático (roda), Perguntar (cartão de aprovação a cada uso: o padrão) ou Desligada (a Emma nem a vê). O que elas devolvem é conteúdo de fora:
 * depois de usar uma, as ações seguintes da Emma pedem a sua aprovação.
 */
#include "painel.h"

#include <string.h>

#include "emmar.h"
#include "json.h"

#define MAX_SERV 16

static struct json *lista;          /* GET /api/mcp/servers */
static bool lido, ocupado, servidor_ok = true;
static double proxima;
static char aviso[300];
static bool aberto_[MAX_SERV];      /* a lista de ferramentas do servidor está aberta */
static char remover_id[48];         /* o servidor que espera a segunda confirmação de remoção */
static char f_nome[64], f_cmd[600], f_env[800];
static const char *preset_ativo = "";

struct job {
	char metodo[8], caminho[200], corpo[1800];
	char *resultado; /* a lista nova, serializada */
	bool ok;
	char erro[240];
};

static void trabalho(void *d) {
	struct job *j = d;
	struct emmar r;
	if (j->metodo[0]) {
		bool ok = emmar_chamar(j->metodo, j->caminho, j->corpo[0] ? j->corpo : "{}", 0, &r);
		if (!ok) {
			g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		}
		emmar_liberar(&r);
	}
	if (emmar_chamar("GET", "/api/mcp/servers", NULL, 0, &r)) {
		GString *g = g_string_new(NULL);
		json_escrever(g, r.corpo);
		j->resultado = g_string_free(g, FALSE);
		j->ok = true;
	} else if (!j->erro[0]) {
		g_strlcpy(j->erro, r.msg, sizeof(j->erro));
	}
	emmar_liberar(&r);
}

static void pronto(void *d) {
	struct job *j = d;
	ocupado = false;
	lido = true;
	servidor_ok = j->ok;
	if (j->ok) {
		json_liberar(lista);
		lista = json_ler(j->resultado);
	}
	g_strlcpy(aviso, j->erro, sizeof(aviso));
	g_free(j->resultado);
	g_free(j);
}

static void pedir(const char *metodo, const char *caminho, const char *corpo) {
	if (ocupado) {
		return;
	}
	ocupado = true;
	struct job *j = g_new0(struct job, 1);
	g_strlcpy(j->metodo, metodo, sizeof(j->metodo));
	g_strlcpy(j->caminho, caminho, sizeof(j->caminho));
	g_strlcpy(j->corpo, corpo, sizeof(j->corpo));
	pn_tarefa(trabalho, pronto, j);
}

void pn_emma_mcp_abrir(void) {
	lido = false;
	proxima = 0;
	aviso[0] = '\0';
	remover_id[0] = '\0';
}

/* os atalhos: o que preenche o formulário */
static void preset(const char *qual) {
	preset_ativo = qual;
	f_env[0] = '\0';
	if (!strcmp(qual, "arquivos")) {
		g_strlcpy(f_nome, "Arquivos", sizeof(f_nome));
		char *pasta = g_build_filename(g_get_home_dir(), "Emma", NULL);
		g_snprintf(f_cmd, sizeof(f_cmd), "npx -y @modelcontextprotocol/server-filesystem %s", pasta);
		g_free(pasta);
	} else if (!strcmp(qual, "web")) {
		g_strlcpy(f_nome, "Web", sizeof(f_nome));
		g_strlcpy(f_cmd, "uvx mcp-server-fetch", sizeof(f_cmd));
	} else if (!strcmp(qual, "github")) {
		g_strlcpy(f_nome, "GitHub", sizeof(f_nome));
		g_strlcpy(f_cmd, "npx -y @modelcontextprotocol/server-github", sizeof(f_cmd));
		g_strlcpy(f_env, "GITHUB_PERSONAL_ACCESS_TOKEN=", sizeof(f_env));
	} else {
		f_nome[0] = f_cmd[0] = '\0';
	}
}

/* "CHAVE=valor" por linha -> {"CHAVE":"valor"} */
static void env_para_json(GString *g) {
	g_string_append_c(g, '{');
	char **ls = g_strsplit(f_env, "\n", -1);
	bool primeiro = true;
	for (int i = 0; ls[i]; i++) {
		char *l = g_strstrip(ls[i]), *ig = strchr(l, '=');
		if (!ig || ig == l) {
			continue;
		}
		char *k = g_strndup(l, (gsize)(ig - l));
		g_string_append(g, primeiro ? "" : ",");
		json_escrever_texto(g, g_strstrip(k));
		g_string_append_c(g, ':');
		json_escrever_texto(g, g_strstrip(ig + 1));
		primeiro = false;
		g_free(k);
	}
	g_strfreev(ls);
	g_string_append_c(g, '}');
}

static void adicionar(void) {
	bool http = g_str_has_prefix(f_cmd, "http://") || g_str_has_prefix(f_cmd, "https://");
	GString *g = g_string_new("{\"name\":");
	json_escrever_texto(g, f_nome);
	g_string_append_printf(g, ",\"transport\":\"%s\",%s:", http ? "http" : "stdio", http ? "\"url\"" : "\"command\"");
	json_escrever_texto(g, f_cmd);
	g_string_append(g, ",\"env\":");
	env_para_json(g);
	g_string_append_c(g, '}');
	pedir("POST", "/api/mcp/servers", g->str);
	g_string_free(g, TRUE);
	f_nome[0] = f_cmd[0] = f_env[0] = '\0';
	preset_ativo = "";
}

/* liga/desliga: o PUT leva o servidor inteiro (as variáveis secretas vão mascaradas e o servidor as mantém) */
static void ligar_desligar(const struct json *s, bool ligar) {
	GString *g = g_string_new("{\"name\":");
	json_escrever_texto(g, json_chave_texto(s, "name"));
	g_string_append_printf(g, ",\"transport\":\"%s\",\"command\":", json_chave_texto(s, "transport"));
	json_escrever_texto(g, json_chave_texto(s, "command"));
	g_string_append(g, ",\"url\":");
	json_escrever_texto(g, json_chave_texto(s, "url"));
	g_string_append(g, ",\"env\":{");
	const struct json *ks = json_chave(s, "env_keys");
	for (int i = 0; ks && i < ks->nfilhos; i++) {
		const struct json *k = json_item(ks, i);
		g_string_append(g, i ? "," : "");
		json_escrever_texto(g, k->s);
		g_string_append(g, ":\"••••\"");
	}
	g_string_append_printf(g, "},\"enabled\":%s}", ligar ? "true" : "false");
	char *c = g_strdup_printf("/api/mcp/servers/%s", json_chave_texto(s, "id"));
	pedir("PUT", c, g->str);
	g_free(c);
	g_string_free(g, TRUE);
}

static void escolha(uint32_t k, const char *nome_tool, const char *atual, const char *valor, const char *rotulo) {
	bool sel = !strcmp(atual, valor);
	if (gv_botao_ext(CLAY_IDI("mcp-perm", k), &(gv_botao_opcoes){ .texto = rotulo, .estilo = sel ? GV_BOTAO_CHEIO : GV_BOTAO_SIMPLES, .altura = 28, .desativado = ocupado })
		&& !sel) {
		char *c = g_strdup_printf("/api/mcp/tools/%s/permission", nome_tool), *b = g_strdup_printf("{\"permission\":\"%s\"}", valor);
		pedir("PUT", c, b);
		g_free(c);
		g_free(b);
	}
}

static const char *cor_status_txt(const char *s) {
	return !strcmp(s, "ok") ? "Conectado" : !strcmp(s, "connecting") ? "Conectando…" : !strcmp(s, "error") ? "Com erro" : "Parado";
}

void pn_emma_mcp_desenhar(void) {
	double agora = gv_agora();
	if (!ocupado && (!lido || agora >= proxima)) {
		proxima = agora + 2.0; /* aberta, acompanha a conexão dos servidores */
		pedir("", "", "");
	}
	gv_acordar_em(2.0);
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
		gv_texto(CLAY_STRING("Servidores MCP dão ferramentas de fora à Emma (arquivos, web, GitHub...). Cada ferramenta nasce em “Perguntar”: você vê o que ela vai fazer "
				     "antes de cada uso. O que elas devolvem é tratado como conteúdo de fora: depois de usar uma, as ações seguintes pedem a sua aprovação."),
			gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (aviso[0]) {
			gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
		}
		if (lido && !servidor_ok) {
			gv_texto(CLAY_STRING("O servidor da Emma não está respondendo."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		}
		int n = lista ? lista->nfilhos : 0;
		for (int i = 0; i < n && i < MAX_SERV; i++) {
			const struct json *s = json_item(lista, i);
			const char *status = json_chave_texto(s, "status"), *id = json_chave_texto(s, "id");
			bool ok = !strcmp(status, "ok"), ligado = json_bool(json_chave(s, "enabled"), true);
			const struct json *ts = json_chave(s, "tools");
			int nt = ts ? ts->nfilhos : 0;
			CLAY(CLAY_IDI("mcp-srv", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 8 },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY(CLAY_IDI("mcp-pt", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10) } },
						.backgroundColor = ok ? gv_t.sucesso : !strcmp(status, "error") ? gv_t.perigo : gv_t.texto_suave, .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
					CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
						gv_texto(gv_str(json_chave_texto(s, "name")), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
						gv_texto(gv_fmt("%s%s", ligado ? cor_status_txt(status) : "Desligado", ok ? gv_fmt(" · %d ferramenta(s)", nt).chars : ""), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
					bool lig = ligado;
					if (gv_interruptor(CLAY_IDI("mcp-lig", (uint32_t)i), &lig) && !ocupado) {
						ligar_desligar(s, lig);
					}
					if (gv_botao_ext(CLAY_IDI("mcp-rein", (uint32_t)i), &(gv_botao_opcoes){ .icone = "refresh", .estilo = GV_BOTAO_SIMPLES, .altura = 32, .desativado = ocupado || !ligado })) {
						char *c = g_strdup_printf("/api/mcp/servers/%s/restart", id);
						pedir("POST", c, "{}");
						g_free(c);
					}
					bool confirma = !strcmp(remover_id, id);
					if (gv_botao_ext(CLAY_IDI("mcp-rem", (uint32_t)i), &(gv_botao_opcoes){ .icone = "trash", .texto = confirma ? "Remover?" : NULL, .estilo = confirma ? GV_BOTAO_PERIGO : GV_BOTAO_SIMPLES,
						    .altura = 32, .desativado = ocupado })) {
						if (confirma) {
							char *c = g_strdup_printf("/api/mcp/servers/%s", id);
							pedir("DELETE", c, "{}");
							g_free(c);
							remover_id[0] = '\0';
						} else {
							g_strlcpy(remover_id, id, sizeof(remover_id));
						}
					}
				}
				gv_texto(gv_str(json_chave_texto(s, *json_chave_texto(s, "url") ? "url" : "command")), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
				if (!strcmp(status, "error") && *json_chave_texto(s, "error")) {
					gv_texto(gv_str(json_chave_texto(s, "error")), gv_t.tamanho_corpo - 2, gv_t.perigo, GV_PESO_NORMAL);
				}
				if (ok && nt > 0) {
					if (gv_botao_ext(CLAY_IDI("mcp-abre", (uint32_t)i), &(gv_botao_opcoes){ .icone = aberto_[i] ? "chevron-down" : "chevron-right", .texto = aberto_[i] ? "Ocultar ferramentas" : "Ver e ajustar as ferramentas",
						    .estilo = GV_BOTAO_SIMPLES, .altura = 30 })) {
						aberto_[i] = !aberto_[i];
					}
					for (int t = 0; aberto_[i] && t < nt; t++) {
						const struct json *f = json_item(ts, t);
						const char *perm = json_chave_texto(f, "permission"), *desc = json_chave_texto(f, "description");
						char curta[140];
						g_strlcpy(curta, desc, sizeof(curta));
						uint32_t k = (uint32_t)(i * 100 + t) * 4;
						CLAY(CLAY_IDI("mcp-t", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 6, 6 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
							CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
								gv_texto(gv_str(json_chave_texto(f, "label")), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
								gv_texto(gv_str(curta), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
							}
							escolha(k + 1, json_chave_texto(f, "name"), perm, "auto", "Automático");
							escolha(k + 2, json_chave_texto(f, "name"), perm, "ask", "Perguntar");
							escolha(k + 3, json_chave_texto(f, "name"), perm, "off", "Desligada");
						}
					}
				}
			}
		}
		if (lido && servidor_ok && n == 0) {
			gv_texto(CLAY_STRING("Nenhum servidor ainda. Adicione um abaixo."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		gv_separador(false, 0);
		gv_texto(CLAY_STRING("Adicionar um servidor"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
		CLAY(CLAY_ID("mcp-presets"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
			static const char *ids[] = { "arquivos", "web", "github", "outro" }, *nomes[] = { "Arquivos", "Web (fetch)", "GitHub", "Outro" };
			for (int p = 0; p < 4; p++) {
				if (gv_botao_ext(CLAY_IDI("mcp-pre", (uint32_t)p), &(gv_botao_opcoes){ .texto = nomes[p], .estilo = !strcmp(preset_ativo, ids[p]) ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO, .altura = 32 })) {
					preset(ids[p]);
				}
			}
		}
		gv_texto(CLAY_STRING("Precisam de Node.js (npx) ou uv (uvx) instalados. O primeiro uso baixa o servidor e pode demorar um pouco."), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
		gv_campo(CLAY_ID("mcp-nome"), f_nome, sizeof(f_nome), "Nome (ex.: Arquivos)", GV_CAMPO_TEXTO);
		gv_campo(CLAY_ID("mcp-cmd"), f_cmd, sizeof(f_cmd), "Comando (ou endereço http://...)", GV_CAMPO_TEXTO);
		gv_campo_area(CLAY_ID("mcp-env"), f_env, sizeof(f_env), "Variáveis, uma por linha: CHAVE=valor (chaves e tokens ficam só no servidor da Emma)", 2, 4);
		if (gv_botao_ext(CLAY_ID("mcp-adicionar"), &(gv_botao_opcoes){ .icone = "plus", .texto = "Adicionar e conectar", .estilo = GV_BOTAO_CHEIO, .altura = 38,
			    .desativado = ocupado || !f_nome[0] || !f_cmd[0] })) {
			adicionar();
		}
		gv_texto(CLAY_STRING("Um servidor MCP local roda um programa na sua máquina com os seus poderes: adicione só os que você conhece e confia."), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}
