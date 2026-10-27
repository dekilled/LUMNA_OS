/*
 * lumna-emma-chat: o painel flutuante da Emma, à direita da tela. Um chat rápido por cima do que a pessoa
 * está fazendo: a conversa, os botões de próximo passo que a Emma sugere, o cartão que leva a uma configuração,
 * o contexto (a janela que está em foco) e o microfone. Rodar de novo fecha (como o lançador).
 *
 * Quem pensa e age é o serviço lumna-emma (servicos/emmac.h); aqui só se mostra e se pede. Pedido rápido
 * ("pausa", "abre a Netflix"): a resposta aparece, e o painel (se não estiver fixado) fecha sozinho.
 */
#define _GNU_SOURCE
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "emma-ui.h"
#include "emmac.h"
#include "gravity.h"
#include "json.h"
#include "modos.h"

static GPtrArray *msgs;
static char estado[16] = "offline";
static bool servidor;
static double nivel;
static char erro[300];
static char campo[512];
static char conf_id[24], conf_nome[96], conf_desc[256];  /* uma ação que pede licença */
static char ctx_titulo[160];
static bool ctx_ativo = true;
static bool fixado;
static bool rolar_fim = true;
static double fechar_em;
static gv_app *app;
static int emma_fd = -1;

/* ---- o estado ---- */

static void atualizar_contexto(void) {
	char r[512];
	if (emmac_pedido(ctx_ativo ? "contexto" : "contexto", r, sizeof(r))) {
		struct json *j = json_ler(r);
		if (j) {
			g_strlcpy(ctx_titulo, json_chave_texto(j, "titulo"), sizeof(ctx_titulo));
			ctx_ativo = json_bool(json_chave(j, "ativo"), true);
			json_liberar(j);
		}
	}
}

static void carregar_historico(void) {
	char *r = g_malloc(256 * 1024);
	g_ptr_array_set_size(msgs, 0);
	if (emmac_pedido("historico", r, 256 * 1024)) {
		struct json *j = json_ler(r);
		const struct json *l = json_chave(j, "mensagens");
		for (int i = 0; l && i < l->nfilhos; i++) {
			const struct json *m = json_item(l, i);
			struct eu_msg *n = eu_msg_nova(msgs, !strcmp(json_chave_texto(m, "papel"), "user"), json_chave_texto(m, "texto"), json_chave_texto(m, "hora"));
			const struct json *ts = json_chave(m, "ferramentas");
			for (int k = 0; ts && k < ts->nfilhos; k++) {
				eu_juntar_ferramenta(n, json_item(ts, k));
			}
		}
		json_liberar(j);
	}
	g_free(r);
	rolar_fim = true;
}

static void evento(const struct json *j) {
	const char *ev = json_chave_texto(j, "evento");
	if (!strcmp(json_chave_texto(j, "canal"), "app")) {
		return;   /* as conversas do app completo são dele, não do painel */
	}
	if (!strcmp(ev, "estado")) {
		g_strlcpy(estado, json_chave_texto(j, "estado"), sizeof(estado));
		servidor = json_bool(json_chave(j, "online"), false);
		if (strcmp(estado, "ouvindo")) {
			nivel = 0;
		}
	} else if (!strcmp(ev, "nivel")) {
		nivel = json_numero(json_chave(j, "nivel"), 0);
	} else if (!strcmp(ev, "ouvido")) {
		erro[0] = '\0';
		eu_msg_nova(msgs, true, json_chave_texto(j, "texto"), NULL);
		fechar_em = 0;
	} else if (!strcmp(ev, "delta")) {
		struct eu_msg *m = eu_msg_parcial(msgs);
		if (!m) {
			m = eu_msg_nova(msgs, false, "", NULL);
			m->parcial = true;
		}
		char *t = g_strconcat(m->texto, json_chave_texto(j, "texto"), NULL);
		g_free(m->texto);
		m->texto = t;
		rolar_fim = true;
	} else if (!strcmp(ev, "ferramenta")) {
		struct eu_msg *m = eu_msg_parcial(msgs);
		if (!m) {
			m = eu_msg_nova(msgs, false, "", NULL);
			m->parcial = true;
		}
		eu_juntar_ferramenta(m, j);
		conf_id[0] = '\0';
	} else if (!strcmp(ev, "resposta")) {
		struct eu_msg *m = eu_msg_parcial(msgs);
		const char *t = json_chave_texto(j, "texto");
		if (!m && *t) {
			m = eu_msg_nova(msgs, false, "", NULL);
		}
		if (m) {
			if (*t) {
				g_free(m->texto);
				m->texto = g_strdup(t);
			}
			m->parcial = false;
		}
		atualizar_contexto();
		/* um pedido rápido ("pausa", "abre a Netflix"): o painel some sozinho, a não ser que esteja fixado */
		if (!fixado && !strcmp(json_chave_texto(j, "origem"), "local")) {
			fechar_em = gv_agora() + 2.5;
		}
	} else if (!strcmp(ev, "confirmar")) {
		g_strlcpy(conf_id, json_chave_texto(j, "id"), sizeof(conf_id));
		g_strlcpy(conf_nome, json_chave_texto(j, "nome"), sizeof(conf_nome));
		g_strlcpy(conf_desc, json_chave_texto(j, "descricao"), sizeof(conf_desc));
		rolar_fim = true;
	} else if (!strcmp(ev, "erro")) {
		g_strlcpy(erro, json_chave_texto(j, "mensagem"), sizeof(erro));
	} else if (!strcmp(ev, "nova")) {
		g_ptr_array_set_size(msgs, 0);
	}
}

static void emma_mudou(gv_app *a, int fd, void *dados) {
	bool caiu = false;
	char *linha;
	while ((linha = emmac_assinar_ler(fd, &caiu))) {
		struct json *j = json_ler(linha);
		if (j) {
			evento(j);
			json_liberar(j);
		}
		g_free(linha);
	}
	if (caiu) {
		gv_app_desvigiar(a, fd);
		close(fd);
		emma_fd = -1;
		g_strlcpy(estado, "offline", sizeof(estado));
	}
	gv_app_redesenhar(a);
}

/* ---- o desenho ---- */

static void pedir(const char *p) {
	char r[256];
	emmac_pedido(p, r, sizeof(r));
}

static void enviar(const char *texto) {
	if (!*texto) {
		return;
	}
	char *p = g_strdup_printf("enviar %s", texto);
	pedir(p);
	g_free(p);
}

static void abrir_app_completo(void) {
	eu_lancar("lumna-emma-app");
	gv_app_sair(app);
}

static const char *frase_estado(void) {
	return !strcmp(estado, "offline") ? "Serviço parado" : !strcmp(estado, "ouvindo") ? "Ouvindo…" : !strcmp(estado, "transcrevendo") ? "Entendendo…" :
		!strcmp(estado, "pensando") ? "Pensando…" : !strcmp(estado, "falando") ? "Falando…" : servidor ? "Disponível" : "Só comandos curtos";
}

static Clay_Color cor_estado(void) {
	return !strcmp(estado, "offline") || (!servidor && !strcmp(estado, "ocioso")) ? gv_t.texto_suave : !strcmp(estado, "ouvindo") ? gv_t.perigo :
		!strcmp(estado, "ocioso") ? gv_t.sucesso : gv_t.acento;
}

static void desenhar(gv_app *a, void *dados) {
	(void)dados;
	if (gv_tecla() == XKB_KEY_Escape) {
		gv_app_sair(a);
		return;
	}
	if (fechar_em > 0) {
		if (gv_agora() >= fechar_em) {
			gv_app_sair(a);
			return;
		}
		gv_acordar_em(fechar_em - gv_agora() + 0.05);
	}
	if (emma_fd < 0) {
		emma_fd = emmac_assinar();
		if (emma_fd >= 0) {
			gv_app_vigiar(a, emma_fd, emma_mudou, NULL);
			g_strlcpy(estado, "ocioso", sizeof(estado));
		} else {
			gv_acordar_em(3);
		}
	}
	bool ouvindo = !strcmp(estado, "ouvindo");
	CLAY(CLAY_ID("em-painel"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 16, 16, 16, 12 }, .childGap = 10 },
		.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		/* o cabeçalho */
		CLAY(CLAY_ID("em-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			eu_avatar(CLAY_ID("em-av"), 64);
			CLAY(CLAY_ID("em-nome"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
				gv_texto(CLAY_STRING("Emma"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
				CLAY_AUTO_ID({ .layout = { .childGap = 7, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY(CLAY_ID("em-ponto"), { .layout = { .sizing = { CLAY_SIZING_FIXED(9), CLAY_SIZING_FIXED(9) } }, .backgroundColor = cor_estado(), .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
					gv_texto(gv_str(frase_estado()), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				struct modos_estado me;
				const char *modo = modos_ler(&me) && me.orbit ? modos_nome(modos_em_foco(&me)) : "";
				gv_texto(gv_fmt("Ajudando no modo %s", modo), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			if (eu_botao_icone(CLAY_ID("em-app"), "external", false)) {
				abrir_app_completo();
			}
			if (eu_botao_icone(CLAY_ID("em-pin"), "pin", fixado)) {
				fixado = !fixado;
				if (fixado) {
					fechar_em = 0;
				}
			}
			if (eu_botao_icone(CLAY_ID("em-fechar"), "close", false)) {
				gv_app_sair(a);
			}
		}
		gv_separador(false, 0);
		/* a conversa */
		Clay_ElementId lista = CLAY_ID("em-lista");
		CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
			.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 14, .padding = { 0, 6, 4, 8 } } })) {
			if (!msgs->len) {
				CLAY(CLAY_ID("em-vazio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 30, 10 }, .childGap = 8 } }) {
					gv_texto(CLAY_STRING("Oi! Peça algo ou pergunte."), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(CLAY_STRING("Ex.: \"abre a Netflix\", \"volume 30\", \"como adiciono outro layout de teclado?\""), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
			for (guint i = 0; i < msgs->len; i++) {
				eu_bolha(i, msgs->pdata[i], enviar);
			}
			if (conf_id[0]) {
				CLAY(CLAY_ID("em-lic"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 8 },
					.backgroundColor = gv_alfa(gv_t.aviso, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_alfa(gv_t.aviso, 0.5f), .width = GV_BORDA(1) } }) {
					gv_texto(gv_fmt("A Emma quer: %s", conf_nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(conf_desc), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					CLAY(CLAY_ID("em-lic-b"), { .layout = { .childGap = 10 } }) {
						if (gv_botao(CLAY_ID("em-lic-nao"), NULL, "Agora não", GV_BOTAO_ELEVADO)) {
							char p[64];
							snprintf(p, sizeof(p), "confirmar %s nao", conf_id);
							pedir(p);
							conf_id[0] = '\0';
						}
						if (gv_botao(CLAY_ID("em-lic-sim"), "check", "Pode", GV_BOTAO_CHEIO)) {
							char p[64];
							snprintf(p, sizeof(p), "confirmar %s sim", conf_id);
							pedir(p);
							conf_id[0] = '\0';
						}
					}
				}
			}
			if (erro[0]) {
				gv_texto(gv_str(erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
			}
		}
		if (rolar_fim) {
			gv_rolar_para(lista, 0, 1e7f, false);
			rolar_fim = false;
		}
		/* o contexto e a conversa nova */
		CLAY(CLAY_ID("em-meio"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("em-ctx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				if (ctx_ativo && ctx_titulo[0]) {
					gv_icone("desktop", 18, gv_t.texto_secundario);
					gv_texto(gv_fmt("Contexto: %s", ctx_titulo), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
					if (eu_botao_icone(CLAY_ID("em-ctx-x"), "close", false)) {
						ctx_ativo = false;
						pedir("contexto nao");
					}
				} else if (!ctx_ativo) {
					if (gv_botao(CLAY_ID("em-ctx-on"), "desktop", "Usar o que estou vendo", GV_BOTAO_SIMPLES)) {
						ctx_ativo = true;
						pedir("contexto sim");
						atualizar_contexto();
					}
				}
			}
			if (gv_botao(CLAY_ID("em-nova"), "plus-circle", "Nova conversa", GV_BOTAO_ELEVADO)) {
				pedir("nova");
				g_ptr_array_set_size(msgs, 0);
			}
		}
		/* escrever */
		CLAY(CLAY_ID("em-entrada"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				if ((gv_campo_chat(CLAY_ID("em-campo"), campo, sizeof(campo), ouvindo ? "Ouvindo…" : "Pergunte à Emma…", 5, true) & GV_CAMPO_ENTER) && campo[0]) {
					enviar(campo);
					campo[0] = '\0';
				}
			}
			if (eu_botao_icone(CLAY_ID("em-mic"), ouvindo ? "mic" : "mic", ouvindo)) {
				pedir("ouvir");
			}
			Clay_ElementId env = CLAY_ID("em-enviar");
			bool tocado = gv_tocado(env);
			CLAY(env, { .layout = { .sizing = { CLAY_SIZING_FIXED(46), CLAY_SIZING_FIXED(46) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_por_cima(env) ? gv_alfa(EU_VIOLETA, 0.85f) : EU_VIOLETA, .cornerRadius = CLAY_CORNER_RADIUS(23) }) {
				tocado |= gv_focavel(env, 23);
				gv_icone("send", 22, (Clay_Color){ 255, 255, 255, 255 });
			}
			if (tocado && campo[0]) {
				enviar(campo);
				campo[0] = '\0';
			}
		}
		CLAY(CLAY_ID("em-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			if (gv_botao(CLAY_ID("em-completo"), "external", "Abrir app completo", GV_BOTAO_SIMPLES)) {
				abrir_app_completo();
			}
		}
	}
}

int main(void) {
	msgs = g_ptr_array_new_with_free_func(eu_msg_liberar);
	eu_iniciar();
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Emma", .app_id = "lumna-emma-chat", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
		.ancoras = GV_ANCORA_DIREITA | GV_ANCORA_CIMA | GV_ANCORA_BAIXO, .largura = 480, .teclado = true, .margens = { 52, 20, 96, 0 } });
	if (!app) {
		return 1;
	}
	carregar_historico();
	atualizar_contexto();
	gv_focar(CLAY_ID("em-campo"));
	return gv_app_correr(app, desenhar, NULL);
}
