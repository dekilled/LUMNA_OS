/*
 * Painel > Emma > Recursos: cada capacidade da Emma se liga e desliga à parte (o sistema é modular): gerar imagens, ver imagens (visão), servidores MCP e o
 * "Oi Emma". A geração de imagens tem o provedor aqui: um servidor na nuvem (OpenAI e compatíveis) ou local (qualquer um que fale /images/generations),
 * com teste. O que está desligado some de verdade: a Emma não vê a ferramenta, o app esconde o botão.
 */
#include "painel.h"

#include <string.h>

#include "emmar.h"
#include "json.h"

struct estado {
	bool lido, servidor;
	bool visao, mcp, gerar, wake, tem_chave;
	char url[300], modelo[96], tam[24], chave_mascara[24], permissao[8];
};

static struct estado est;
static bool ocupado, consultando, editando_gerar;
static char f_url[300], f_modelo[96], f_tam[24], f_chave[200];
static char aviso[400], teste[400];
static bool teste_erro;
static double proxima;

struct job {
	int acao; /* 0 ler, 1 módulo, 2 gerar liga/desliga, 3 aplicar o provedor, 4 testar, 5 permissão, 6 oi emma */
	char nome[24];
	bool valor;
	char corpo[800];
	struct estado est;
	bool ok;
	char erro[300], texto[300];
};

static void ler_estado(const struct json *c, struct estado *e) {
	const struct json *m = json_chave(c, "modules"), *ig = json_chave(c, "image_gen"), *perm = json_chave(json_chave(c, "tools"), "permissions");
	e->servidor = true;
	e->visao = json_bool(json_chave(m, "visao"), true);
	e->mcp = json_bool(json_chave(m, "mcp"), true);
	e->gerar = json_bool(json_chave(ig, "enabled"), false);
	e->tem_chave = json_bool(json_chave(ig, "has_key"), false);
	g_strlcpy(e->url, json_chave_texto(ig, "base_url"), sizeof(e->url));
	g_strlcpy(e->modelo, json_chave_texto(ig, "model"), sizeof(e->modelo));
	g_strlcpy(e->tam, json_chave_texto(ig, "size"), sizeof(e->tam));
	g_strlcpy(e->chave_mascara, json_chave_texto(ig, "api_key_masked"), sizeof(e->chave_mascara));
	g_strlcpy(e->permissao, *json_chave_texto(perm, "gerar_imagem") ? json_chave_texto(perm, "gerar_imagem") : "ask", sizeof(e->permissao));
}

static void trabalho(void *d) {
	struct job *j = d;
	struct emmar r;
	bool ok = true;
	if (j->acao == 1) {
		char *c = g_strdup_printf("{\"modules\":{\"%s\":%s}}", j->nome, j->valor ? "true" : "false");
		ok = emmar_chamar("PUT", "/api/settings", c, 0, &r);
		g_free(c);
		if (!ok) {
			g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		}
		emmar_liberar(&r);
	} else if (j->acao == 2 || j->acao == 3) {
		ok = emmar_chamar("PUT", "/api/settings", j->corpo, 0, &r);
		if (!ok) {
			g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		}
		emmar_liberar(&r);
	} else if (j->acao == 4) {
		ok = emmar_chamar("POST", "/api/settings/image/test", "{}", 0, &r);
		if (ok) {
			if (json_bool(json_chave(r.corpo, "ok"), false)) {
				g_snprintf(j->texto, sizeof(j->texto), "Funcionou: o servidor devolveu uma imagem (%s, %d bytes).", json_chave_texto(r.corpo, "mime"), (int)json_numero(json_chave(r.corpo, "size"), 0));
			} else {
				ok = false;
				g_strlcpy(j->erro, *json_chave_texto(r.corpo, "error") ? json_chave_texto(r.corpo, "error") : "O servidor não devolveu uma imagem.", sizeof(j->erro));
			}
		} else {
			g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		}
		emmar_liberar(&r);
	} else if (j->acao == 5) {
		char *p = g_strdup_printf("/api/tools/gerar_imagem/permission"), *c = g_strdup_printf("{\"permission\":\"%s\"}", j->nome);
		ok = emmar_chamar("PUT", p, c, 0, &r);
		g_free(p);
		g_free(c);
		if (!ok) {
			g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		}
		emmar_liberar(&r);
	} else if (j->acao == 6) {
		ok = emmar_chamar("PUT", "/api/wake", j->valor ? "{\"enabled\":true}" : "{\"enabled\":false}", 0, &r);
		if (!ok) {
			g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		}
		emmar_liberar(&r);
	}
	j->ok = ok;
	if (emmar_chamar("GET", "/api/settings", NULL, 0, &r)) {
		ler_estado(r.corpo, &j->est);
		j->est.lido = true;
		emmar_liberar(&r);
		if (emmar_chamar("GET", "/api/wake", NULL, 0, &r)) {
			j->est.wake = json_bool(json_chave(r.corpo, "enabled"), false);
		}
	} else if (!j->erro[0]) {
		g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		j->ok = false;
	}
	emmar_liberar(&r);
}

static void pronto(void *d) {
	struct job *j = d;
	ocupado = consultando = false;
	if (j->est.servidor) {
		bool eram_editando = editando_gerar;
		est = j->est;
		est.lido = true;
		if (!eram_editando) { /* o que se digita não é sobrescrito pelo que veio do servidor */
			g_strlcpy(f_url, est.url, sizeof(f_url));
			g_strlcpy(f_modelo, est.modelo, sizeof(f_modelo));
			g_strlcpy(f_tam, est.tam, sizeof(f_tam));
		}
	} else {
		est.lido = true;
		est.servidor = false;
	}
	if (j->acao != 0) {
		g_strlcpy(aviso, j->ok ? "" : j->erro, sizeof(aviso));
	}
	if (j->acao == 4) {
		teste_erro = !j->ok;
		g_strlcpy(teste, j->ok ? j->texto : j->erro, sizeof(teste));
	}
	if (j->acao == 3 && j->ok) {
		f_chave[0] = '\0';
		editando_gerar = false;
	}
	g_free(j);
}

static void tarefa(int acao, const char *nome, bool valor, const char *corpo) {
	if (ocupado || (acao == 0 && consultando)) {
		return;
	}
	struct job *j = g_new0(struct job, 1);
	j->acao = acao;
	g_strlcpy(j->nome, nome ? nome : "", sizeof(j->nome));
	j->valor = valor;
	g_strlcpy(j->corpo, corpo ? corpo : "", sizeof(j->corpo));
	if (acao == 0) {
		consultando = true;
	} else {
		ocupado = true;
	}
	pn_tarefa(trabalho, pronto, j);
}

void pn_emma_rec_abrir(void) {
	est.lido = false;
	aviso[0] = teste[0] = '\0';
	editando_gerar = false;
	proxima = 0;
}

static void modulo(const char *id, const char *titulo, const char *dica, bool ligado, void (*acao)(bool)) {
	bool v = ligado;
	CLAY(CLAY_SID(gv_fmt("rec-%s", id)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(12), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(dica), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (gv_interruptor(CLAY_SID(gv_fmt("rec-i-%s", id)), &v) && !ocupado) {
			acao(v);
		}
	}
}

static void liga_visao(bool v) { tarefa(1, "visao", v, NULL); }
static void liga_mcp(bool v) { tarefa(1, "mcp", v, NULL); }
static void liga_gerar(bool v) { tarefa(2, "", v, v ? "{\"image_gen\":{\"enabled\":true}}" : "{\"image_gen\":{\"enabled\":false}}"); }
static void liga_wake(bool v) { tarefa(6, "", v, NULL); }

static void aplicar_provedor(void) {
	GString *g = g_string_new("{\"image_gen\":{\"base_url\":");
	json_escrever_texto(g, f_url);
	g_string_append(g, ",\"model\":");
	json_escrever_texto(g, f_modelo);
	g_string_append(g, ",\"size\":");
	json_escrever_texto(g, f_tam);
	if (f_chave[0]) {
		g_string_append(g, ",\"api_key\":");
		json_escrever_texto(g, f_chave);
	}
	g_string_append(g, "}}");
	tarefa(3, "", false, g->str);
	g_string_free(g, TRUE);
}

static void preset(bool local) {
	editando_gerar = true;
	if (local) {
		g_strlcpy(f_url, "http://127.0.0.1:8188/v1", sizeof(f_url));
		f_modelo[0] = '\0';
		g_strlcpy(f_tam, "512x512", sizeof(f_tam));
	} else {
		g_strlcpy(f_url, "https://api.openai.com/v1", sizeof(f_url));
		g_strlcpy(f_modelo, "gpt-image-1", sizeof(f_modelo));
		g_strlcpy(f_tam, "1024x1024", sizeof(f_tam));
	}
}

void pn_emma_rec_desenhar(void) {
	double agora = gv_agora();
	if (!ocupado && !consultando && (!est.lido || agora >= proxima) && !editando_gerar) {
		proxima = agora + 4.0;
		tarefa(0, "", false, NULL);
	}
	gv_acordar_em(4.0);
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		gv_texto(CLAY_STRING("Cada recurso da Emma liga e desliga à parte. O que está desligado some de verdade: ela não vê a ferramenta e o app esconde o botão."),
			gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (aviso[0]) {
			gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
		}
		if (est.lido && !est.servidor) {
			gv_texto(CLAY_STRING("O servidor da Emma não está respondendo."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		} else if (!est.lido) {
			gv_texto(CLAY_STRING("Consultando…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		if (est.lido && est.servidor) {
			modulo("gerar", "Gerar imagens", "A Emma cria imagens quando você pede, ou com “/imagem descrição” no app. Precisa de um provedor, abaixo.", est.gerar, liga_gerar);
			if (est.gerar) {
				CLAY(CLAY_ID("rec-gerar-cfg"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 0, 0, 6 }, .childGap = 8 } }) {
					CLAY_AUTO_ID({ .layout = { .childGap = 8 } }) {
						if (gv_botao_ext(CLAY_ID("rec-pre-nuvem"), &(gv_botao_opcoes){ .texto = "Nuvem (OpenAI)", .estilo = GV_BOTAO_ELEVADO, .altura = 32 })) {
							preset(false);
						}
						if (gv_botao_ext(CLAY_ID("rec-pre-local"), &(gv_botao_opcoes){ .texto = "Local (sua máquina)", .estilo = GV_BOTAO_ELEVADO, .altura = 32 })) {
							preset(true);
						}
					}
					if (gv_campo(CLAY_ID("rec-url"), f_url, sizeof(f_url), "Endereço (compatível com /images/generations)", GV_CAMPO_URL) & GV_CAMPO_MUDOU) {
						editando_gerar = true;
					}
					if (gv_campo(CLAY_ID("rec-modelo"), f_modelo, sizeof(f_modelo), "Modelo", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
						editando_gerar = true;
					}
					if (gv_campo(CLAY_ID("rec-tam"), f_tam, sizeof(f_tam), "Tamanho (ex.: 1024x1024)", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
						editando_gerar = true;
					}
					if (gv_campo(CLAY_ID("rec-chave"), f_chave, sizeof(f_chave), est.tem_chave ? gv_fmt("Chave de API (guardada %s; deixe vazio para manter)", est.chave_mascara).chars : "Chave de API (um servidor local não precisa)", GV_CAMPO_SENHA) & GV_CAMPO_MUDOU) {
						editando_gerar = true;
					}
					CLAY_AUTO_ID({ .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						if (gv_botao_ext(CLAY_ID("rec-aplicar"), &(gv_botao_opcoes){ .icone = "check", .texto = "Aplicar", .estilo = GV_BOTAO_CHEIO, .altura = 36, .desativado = ocupado || !editando_gerar })) {
							aplicar_provedor();
						}
						if (gv_botao_ext(CLAY_ID("rec-testar"), &(gv_botao_opcoes){ .icone = "image", .texto = "Testar (gera uma imagem)", .estilo = GV_BOTAO_ELEVADO, .altura = 36, .desativado = ocupado || editando_gerar })) {
							tarefa(4, "", false, NULL);
							g_strlcpy(teste, "Testando… (pode levar um minuto)", sizeof(teste));
							teste_erro = false;
						}
					}
					if (teste[0]) {
						gv_texto(gv_str(teste), gv_t.tamanho_corpo - 2, teste_erro ? gv_t.perigo : gv_t.texto_secundario, GV_PESO_NORMAL);
					}
					CLAY_AUTO_ID({ .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						gv_texto(CLAY_STRING("A Emma gera sozinha:"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
						static const char *vals[] = { "ask", "auto" }, *rot[] = { "Perguntando antes", "Direto" };
						for (int i = 0; i < 2; i++) {
							bool sel = !strcmp(est.permissao, vals[i]) || (i == 0 && strcmp(est.permissao, "auto"));
							if (gv_botao_ext(CLAY_IDI("rec-perm", (uint32_t)i), &(gv_botao_opcoes){ .texto = rot[i], .estilo = sel ? GV_BOTAO_CHEIO : GV_BOTAO_SIMPLES, .altura = 30, .desativado = ocupado }) && !sel) {
								tarefa(5, vals[i], false, NULL);
							}
						}
					}
				}
			}
			modulo("visao", "Ver imagens (visão)", "Anexar imagens na conversa e a Emma olhar para elas. Desligado, o botão de anexar some e ela não recebe imagens.", est.visao, liga_visao);
			modulo("mcp", "Servidores MCP", "Ferramentas de fora que a Emma pode usar. Desligado, nenhum servidor conecta (a lista fica guardada em Servidores MCP).", est.mcp, liga_mcp);
			modulo("wake", "Oi Emma", "Chamar a Emma pela voz. Precisa do modelo de voz baixado (veja a página Oi Emma).", est.wake, liga_wake);
		}
	}
}
