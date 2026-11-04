/*
 * Painel > Celular Connect: o dia a dia entre o PC e o celular (o serviço é o lumna-controle, o mesmo protocolo do celular LUMNA e do app Android).
 * Quatro páginas sobre o mesmo estado: Pareamento (o interruptor geral, o código, os celulares), Área de transferência, Avisos e Arquivos.
 * Cada recurso liga e desliga por celular; desligado, nem sai nem entra.
 */
#include "painel.h"

#include <string.h>

#include "controlec.h"
#include "json.h"

static struct json *estado;       /* a resposta do "connect" */
static bool lido, ocupado, servico_ok = true;
static double proxima;
static char aviso[300];
static char codigo[8], enderecos[200], codigo_nome[64];
static double codigo_ate;
static char esquecer_id[48];
static char f_pasta[300], f_arquivo[400];
static bool editando_pasta;
static char escolhido[40]; /* o celular para onde o "Enviar arquivo" vai */

struct job {
	char pedido[700];
	char *resposta;
	bool ok;
	bool ler;
};

static void trabalho(void *d) {
	struct job *j = d;
	char *buf = g_malloc(256 * 1024);
	if (j->pedido[0]) {
		char r[4096];
		if (controlec_pedido(j->pedido, r, sizeof(r))) {
			j->resposta = g_strdup(r);
		}
	}
	if (controlec_pedido("connect", buf, 256 * 1024)) {
		j->ler = true;
		j->ok = true;
		g_free(buf);
		/* a resposta do pedido anterior fica em j->resposta; o estado novo vai junto, depois de um separador */
		char *novo = g_malloc(256 * 1024);
		controlec_pedido("connect", novo, 256 * 1024);
		g_free(j->resposta ? NULL : NULL);
		char *resp = j->resposta;
		j->resposta = g_strdup_printf("%s\n%s", resp ? resp : "", novo);
		g_free(resp);
		g_free(novo);
		return;
	}
	g_free(buf);
}

static void pronto(void *d) {
	struct job *j = d;
	ocupado = false;
	lido = true;
	servico_ok = j->ok;
	if (j->ok && j->resposta) {
		char *sep = strchr(j->resposta, '\n');
		if (sep) {
			*sep = '\0';
			struct json *r = *j->resposta ? json_ler(j->resposta) : NULL;
			if (r && !json_bool(json_chave(r, "ok"), true)) {
				g_strlcpy(aviso, json_chave_texto(r, "erro"), sizeof(aviso));
			} else if (r && *json_chave_texto(r, "codigo")) {
				g_strlcpy(codigo, json_chave_texto(r, "codigo"), sizeof(codigo));
				g_strlcpy(codigo_nome, json_chave_texto(r, "nome"), sizeof(codigo_nome));
				codigo_ate = gv_agora() + json_numero(json_chave(r, "expira"), 300);
				GString *e = g_string_new(NULL);
				const struct json *en = json_chave(r, "enderecos");
				for (int i = 0; en && i < en->nfilhos; i++) {
					g_string_append_printf(e, "%s%s:%d", i ? "  ·  " : "", json_item(en, i)->s, (int)json_numero(json_chave(r, "porta"), 47800));
				}
				g_strlcpy(enderecos, e->str, sizeof(enderecos));
				g_string_free(e, TRUE);
			}
			json_liberar(r);
			json_liberar(estado);
			estado = json_ler(sep + 1);
		}
	} else if (!j->ok) {
		json_liberar(estado);
		estado = NULL;
	}
	g_free(j->resposta);
	g_free(j);
}

static void pedir(const char *pedido) {
	if (ocupado) {
		return;
	}
	ocupado = true;
	aviso[0] = '\0';
	struct job *j = g_new0(struct job, 1);
	g_strlcpy(j->pedido, pedido, sizeof(j->pedido));
	pn_tarefa(trabalho, pronto, j);
}

void pn_cc_abrir(void) {
	lido = false;
	proxima = 0;
	aviso[0] = '\0';
	esquecer_id[0] = '\0';
	editando_pasta = false;
}

static void atualizar(void) {
	double agora = gv_agora();
	if (!ocupado && (!lido || agora >= proxima)) {
		proxima = agora + 2.0; /* aberta, acompanha os celulares e as transferências */
		pedir("");
	}
	gv_acordar_em(2.0);
}

/* o que as quatro páginas repetem: o aviso, o serviço fora do ar, e se o Connect está desligado */
static bool cabecalho(const char *explica) {
	gv_texto(gv_str(explica), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	if (aviso[0]) {
		gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
	}
	if (lido && (!servico_ok || !estado)) {
		gv_texto(CLAY_STRING("O serviço do celular (lumna-controle) não está rodando."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		return false;
	}
	if (!estado) {
		gv_texto(CLAY_STRING("Consultando…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		return false;
	}
	if (!json_bool(json_chave(estado, "ativo"), true)) {
		gv_texto(CLAY_STRING("O Connect está desligado (na página Pareamento): nada sai nem entra."), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
	}
	return true;
}

static void sem_celular(void) {
	gv_texto(CLAY_STRING("Nenhum celular pareado ainda. Pareie um na página Pareamento."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
}

/* uma linha por celular com o interruptor de um recurso */
static void linha_recurso(int i, const struct json *d, const char *recurso, const char *rotulo) {
	bool ligado = json_bool(json_chave(json_chave(d, "recursos"), recurso), true), on = json_bool(json_chave(d, "conectado"), false);
	CLAY(CLAY_SID(gv_fmt("cc-%s-%d", recurso, i)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(12), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		CLAY(CLAY_SID(gv_fmt("cc-pt-%s-%d", recurso, i)), { .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10) } }, .backgroundColor = on ? gv_t.sucesso : gv_t.texto_suave, .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
			gv_texto(gv_str(json_chave_texto(d, "nome")), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_fmt("%s · %s", rotulo, on ? "conectado agora" : "fora da rede"), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		bool v = ligado;
		if (gv_interruptor(CLAY_SID(gv_fmt("cc-i-%s-%d", recurso, i)), &v) && !ocupado) {
			char *p = g_strdup_printf("recurso %s %s %s", json_chave_texto(d, "id"), recurso, v ? "sim" : "nao");
			pedir(p);
			g_free(p);
		}
	}
}

/* ---- Pareamento ---- */

void pn_cc_parear_desenhar(void) {
	atualizar();
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		bool ok = cabecalho("Ligue o celular a este computador, na mesma rede (ou pelo Tailscale). Funciona com o LUMNA no celular e com o app LUMNA Connect do Android.");
		if (ok) {
			bool ativo = json_bool(json_chave(estado, "ativo"), true);
			CLAY(CLAY_ID("cc-geral"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(12), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
					gv_texto(CLAY_STRING("Connect"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(CLAY_STRING("O interruptor geral: desligado, nenhum recurso sai nem entra (o controle da Mídia segue)."), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				bool v = ativo;
				if (gv_interruptor(CLAY_ID("cc-geral-i"), &v) && !ocupado) {
					pedir(v ? "connect_ativo sim" : "connect_ativo nao");
				}
			}
			bool mostrando = codigo[0] && gv_agora() < codigo_ate;
			if (mostrando) {
				CLAY(CLAY_ID("cc-codigo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 6 },
					.backgroundColor = gv_alfa(gv_t.acento, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_alfa(gv_t.acento, 0.5f), .width = GV_BORDA(1) } }) {
					gv_texto(CLAY_STRING("No celular, em Celular Connect, digite este código:"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
					gv_texto(gv_str(codigo), 40, gv_t.acento, GV_PESO_MEDIO);
					gv_texto(gv_fmt("Vale mais %d min e só uma vez. Se o celular não achar este computador sozinho, digite o endereço: %s", (int)((codigo_ate - gv_agora()) / 60) + 1, enderecos[0] ? enderecos : "(sem rede)"),
						gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
					if (gv_botao_ext(CLAY_ID("cc-cod-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_SIMPLES, .altura = 32 })) {
						codigo[0] = '\0';
						pedir("parear_cancelar");
					}
				}
			} else if (gv_botao_ext(CLAY_ID("cc-parear"), &(gv_botao_opcoes){ .icone = "plus", .texto = "Parear um celular", .estilo = GV_BOTAO_CHEIO, .altura = 38, .desativado = ocupado || !ativo })) {
				pedir("parear");
			}
			gv_texto(CLAY_STRING("Celulares pareados"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			const struct json *ds = json_chave(estado, "dispositivos");
			int n = ds ? ds->nfilhos : 0;
			for (int i = 0; i < n; i++) {
				const struct json *d = json_item(ds, i);
				const char *id = json_chave_texto(d, "id");
				bool on = json_bool(json_chave(d, "conectado"), false), conf = !strcmp(esquecer_id, id);
				CLAY(CLAY_IDI("cc-dev", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(12), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
					CLAY(CLAY_IDI("cc-dev-pt", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10) } }, .backgroundColor = on ? gv_t.sucesso : gv_t.texto_suave, .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
					CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
						gv_texto(gv_str(json_chave_texto(d, "nome")), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
						gv_texto(gv_str(on ? "Conectado agora" : "Fora da rede"), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
					if (gv_botao_ext(CLAY_IDI("cc-esq", (uint32_t)i), &(gv_botao_opcoes){ .icone = "trash", .texto = conf ? "Esquecer?" : "Esquecer", .estilo = conf ? GV_BOTAO_PERIGO : GV_BOTAO_SIMPLES, .altura = 32, .desativado = ocupado })) {
						if (conf) {
							char *p = g_strdup_printf("esquecer %s", id);
							pedir(p);
							g_free(p);
							esquecer_id[0] = '\0';
						} else {
							g_strlcpy(esquecer_id, id, sizeof(esquecer_id));
						}
					}
				}
			}
			if (n == 0) {
				sem_celular();
			}
		}
	}
}

/* ---- Área de transferência ---- */

void pn_cc_transf_desenhar(void) {
	atualizar();
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		if (cabecalho("O que você copia aqui pode ser colado no celular, e o contrário. Só texto (até 256 KB); o que já estava copiado ao ligar não sai.")) {
			const struct json *ds = json_chave(estado, "dispositivos");
			for (int i = 0; ds && i < ds->nfilhos; i++) {
				const struct json *d = json_item(ds, i);
				linha_recurso(i, d, "clipboard", "Sincronizar a área de transferência");
				if (json_bool(json_chave(d, "conectado"), false) && json_bool(json_chave(json_chave(d, "recursos"), "clipboard"), true)) {
					if (gv_botao_ext(CLAY_IDI("cc-mandar", (uint32_t)i), &(gv_botao_opcoes){ .icone = "send", .texto = "Mandar o que está copiado agora", .estilo = GV_BOTAO_SIMPLES, .altura = 30, .desativado = ocupado })) {
						char *p = g_strdup_printf("copiar_para %s", json_chave_texto(d, "id"));
						pedir(p);
						g_free(p);
					}
				}
			}
			if (!ds || !ds->nfilhos) {
				sem_celular();
			}
		}
	}
}

/* ---- Avisos ---- */

void pn_cc_avisos_desenhar(void) {
	atualizar();
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		if (cabecalho("Os avisos do celular (e as ligações) aparecem aqui, e os daqui aparecem no celular. Cada sentido liga e desliga à parte, por celular.")) {
			const struct json *ds = json_chave(estado, "dispositivos");
			for (int i = 0; ds && i < ds->nfilhos; i++) {
				const struct json *d = json_item(ds, i);
				linha_recurso(i, d, "phone_notifications", "Avisos do celular aqui no PC");
				linha_recurso(i + 100, d, "notifications", "Avisos do PC no celular");
			}
			if (!ds || !ds->nfilhos) {
				sem_celular();
			}
		}
	}
}

/* ---- Arquivos ---- */

static void barra_progresso(uint32_t k, double frac, bool erro) {
	CLAY(CLAY_IDI("cc-bar", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(6) } }, .backgroundColor = gv_t.borda_controle, .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
		CLAY(CLAY_IDI("cc-bar-i", k), { .layout = { .sizing = { CLAY_SIZING_PERCENT((float)frac), CLAY_SIZING_GROW(0) } }, .backgroundColor = erro ? gv_t.perigo : gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(3) }) {}
	}
}

void pn_cc_arquivos_desenhar(void) {
	atualizar();
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		if (cabecalho("Mande arquivos para o celular e receba os dele. O celular puxa o que você oferece; o que chega cai na pasta abaixo, sem nunca sobrescrever.")) {
			if (!editando_pasta) {
				g_strlcpy(f_pasta, json_chave_texto(estado, "pasta_escolhida"), sizeof(f_pasta));
			}
			gv_texto(gv_fmt("Pasta dos recebidos (agora: %s)", json_chave_texto(estado, "pasta")), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					if (gv_campo(CLAY_ID("cc-pasta"), f_pasta, sizeof(f_pasta), "Vazio: a pasta Downloads", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
						editando_pasta = true;
					}
				}
				if (gv_botao_ext(CLAY_ID("cc-pasta-ok"), &(gv_botao_opcoes){ .texto = "Aplicar", .estilo = GV_BOTAO_CHEIO, .altura = 36, .desativado = ocupado || !editando_pasta })) {
					char *p = g_strdup_printf("connect_pasta %s", f_pasta);
					pedir(p);
					g_free(p);
					editando_pasta = false;
				}
			}
			const struct json *ds = json_chave(estado, "dispositivos");
			for (int i = 0; ds && i < ds->nfilhos; i++) {
				linha_recurso(i, json_item(ds, i), "files", "Enviar e receber arquivos");
			}
			if (!ds || !ds->nfilhos) {
				sem_celular();
			} else {
				gv_texto(CLAY_STRING("Enviar um arquivo ao celular"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				if (!escolhido[0] || !json_chave(ds, "x")) {
					bool achou = false;
					for (int i = 0; i < ds->nfilhos; i++) {
						achou |= !strcmp(json_chave_texto(json_item(ds, i), "id"), escolhido);
					}
					if (!achou) {
						g_strlcpy(escolhido, json_chave_texto(json_item(ds, 0), "id"), sizeof(escolhido));
					}
				}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					for (int i = 0; i < ds->nfilhos && ds->nfilhos > 1; i++) {
						const struct json *d = json_item(ds, i);
						bool sel = !strcmp(escolhido, json_chave_texto(d, "id"));
						if (gv_botao_ext(CLAY_IDI("cc-para", (uint32_t)i), &(gv_botao_opcoes){ .texto = json_chave_texto(d, "nome"), .estilo = sel ? GV_BOTAO_CHEIO : GV_BOTAO_SIMPLES, .altura = 30 })) {
							g_strlcpy(escolhido, json_chave_texto(d, "id"), sizeof(escolhido));
						}
					}
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
						gv_campo(CLAY_ID("cc-arq"), f_arquivo, sizeof(f_arquivo), "Caminho do arquivo (ex.: ~/Imagens/foto.png)", GV_CAMPO_TEXTO);
					}
					if (gv_botao_ext(CLAY_ID("cc-enviar"), &(gv_botao_opcoes){ .icone = "send", .texto = "Enviar", .estilo = GV_BOTAO_CHEIO, .altura = 36, .desativado = ocupado || !f_arquivo[0] })) {
						char *caminho = f_arquivo[0] == '~' ? g_build_filename(g_get_home_dir(), f_arquivo + (f_arquivo[1] == '/' ? 2 : 1), NULL) : g_strdup(f_arquivo);
						char *p = g_strdup_printf("enviar %s %s", escolhido, caminho);
						pedir(p);
						g_free(p);
						g_free(caminho);
						f_arquivo[0] = '\0';
					}
				}
			}
			const struct json *ts = json_chave(estado, "transferencias");
			if (ts && ts->nfilhos) {
				gv_texto(CLAY_STRING("Transferências"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			}
			for (int i = 0; ts && i < ts->nfilhos && i < 8; i++) {
				const struct json *t = json_item(ts, i);
				double tam = json_numero(json_chave(t, "tamanho"), 1), feito = json_numero(json_chave(t, "feito"), 0);
				bool entra = !strcmp(json_chave_texto(t, "direcao"), "in"), err = !strcmp(json_chave_texto(t, "estado"), "error"), fim = !strcmp(json_chave_texto(t, "estado"), "done");
				CLAY(CLAY_IDI("cc-tr", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(10), .childGap = 6 },
					.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
					gv_texto(gv_fmt("%s %s · %s", entra ? "↓ de" : "↑ para", json_chave_texto(t, "par"), json_chave_texto(t, "nome")), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
					barra_progresso((uint32_t)i, tam > 0 ? (fim ? 1.0 : feito / tam) : 1.0, err);
					gv_texto(gv_fmt("%s", err ? json_chave_texto(t, "erro") : fim ? "Concluído" : gv_fmt("%.1f de %.1f MB", feito / 1048576.0, tam / 1048576.0).chars), gv_t.tamanho_corpo - 3, err ? gv_t.perigo : gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
		}
	}
}
