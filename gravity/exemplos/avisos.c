/*
 * Os avisos do LUMNA (lumna-avisos): o servidor das notificações e o que se vê delas.
 *
 * - No canto de cima, à direita, os avisos que chegam: o ícone da app (ou a imagem que vier),
 *   o título, o texto e os botões das ações. Somem sozinhos depois de uns segundos (o rato por
 *   cima segura-os); os urgentes ficam até se fechar. Tocar num aviso faz a ação dele (ou traz a
 *   janela da app) e ✕ dispensa-o.
 * - A central (lumna-avisos --centro, que é o que o sino da topbar chama): todos os avisos
 *   por app, o "Não perturbe" e o limpar. Fecha com Esc, tocando fora ou no sino outra vez.
 *
 * É uma camada só: pequena no canto enquanto há avisos, a tela inteira com a central aberta.
 * O "Não perturbe" fica em ~/.config/lumna/avisos.
 */
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "avisos.h"
#include "gravity.h"
#include "modos.h"

#define MAX_ITENS 100
#define MAX_NO_CANTO 4
#define LARGURA_AVISO 380
#define MARGEM 12
#define LARGURA_CENTRAL 420

struct item {
	struct aviso a;
	bool no_canto;   /* à vista no canto */
	bool saindo;     /* a deslizar para fora do canto */
	bool novo;       /* o primeiro quadro: a entrada parte de fora */
	double sai_em;   /* quando sai do canto (0: só quando a pessoa fechar) */
	double saida;    /* quando começou a sair */
};

static bool modo_silencio; /* o modo de agora (Gaming, Mídia) pede só os urgentes */
static gv_app *app_avisos;

/* o Orbit trocou um modo: Gaming e Mídia deixam passar só os urgentes (modos.ini) */
static void modo_mudou(gv_app *app, int fd, void *dados) {
	modos_assinar_ler(fd);
	modo_silencio = modos_nao_perturbe();
}
static struct item itens[MAX_ITENS]; /* do mais antigo ao mais novo */
static int nitens;
static bool centro;
static bool nao_perturbe;
static int altura_canto = 1;

/* ---- o "Não perturbe" guardado ---- */

static char *arquivo_config(void) {
	const char *base = getenv("XDG_CONFIG_HOME");
	if (base && *base) {
		return g_build_filename(base, "lumna", "avisos", NULL);
	}
	return g_build_filename(g_get_home_dir(), ".config", "lumna", "avisos", NULL);
}

static void ler_config(void) {
	char *caminho = arquivo_config();
	char *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		nao_perturbe = strstr(texto, "nao_perturbe=1") != NULL;
		g_free(texto);
	}
	g_free(caminho);
}

static void gravar_config(void) {
	char *caminho = arquivo_config();
	char *pasta = g_path_get_dirname(caminho);
	g_mkdir_with_parents(pasta, 0755);
	g_file_set_contents(caminho, nao_perturbe ? "nao_perturbe=1\n" : "nao_perturbe=0\n", -1, NULL);
	g_free(pasta);
	g_free(caminho);
}

/* ---- a lista ---- */

static int achar(uint32_t id) {
	for (int i = 0; i < nitens; i++) {
		if (itens[i].a.id == id) {
			return i;
		}
	}
	return -1;
}

/* Tira o aviso de vez (e o PNG que gravámos para ele). motivo 0: não avisa a app. */
static void remover(int i, uint32_t motivo) {
	const char *run = getenv("XDG_RUNTIME_DIR");
	if (run && itens[i].a.imagem[0] && g_str_has_prefix(itens[i].a.imagem, run) && strstr(itens[i].a.imagem, "/lumna-avisos/")) {
		unlink(itens[i].a.imagem);
	}
	if (motivo) {
		avisos_fechou(itens[i].a.id, motivo);
	}
	memmove(&itens[i], &itens[i + 1], (size_t)(nitens - i - 1) * sizeof(itens[0]));
	nitens--;
}

static double duracao(const struct aviso *a) {
	if (a->expira_ms == 0 || a->urgencia == AVISO_CRITICA) {
		return 0;
	}
	if (a->expira_ms > 0) {
		return a->expira_ms / 1000.0;
	}
	return a->urgencia == AVISO_BAIXA ? 4 : 6;
}

static void chegou_aviso(const struct aviso *a) {
	int i = achar(a->id);
	if (i >= 0) {
		/* substitui: vai para o fim (o mais novo), sem perder o PNG antigo à toa */
		struct item velho = itens[i];
		memmove(&itens[i], &itens[i + 1], (size_t)(nitens - i - 1) * sizeof(itens[0]));
		nitens--;
		if (strcmp(velho.a.imagem, a->imagem) && velho.a.imagem[0] && strstr(velho.a.imagem, "/lumna-avisos/")) {
			unlink(velho.a.imagem);
		}
	}
	if (nitens == MAX_ITENS) {
		remover(0, AVISO_EXPIROU);
	}
	struct item *it = &itens[nitens++];
	memset(it, 0, sizeof(*it));
	it->a = *a;
	it->no_canto = !centro && ((!nao_perturbe && !modo_silencio) || a->urgencia == AVISO_CRITICA);
	it->novo = i < 0; /* uma atualização (a música seguinte) não volta a entrar a deslizar */
	double d = duracao(a);
	it->sai_em = d > 0 ? gv_agora() + d : 0;
	if (!it->no_canto && a->transitorio) {
		/* não vai para a central nem aparece: acabou aqui */
		remover(nitens - 1, AVISO_EXPIROU);
	}
}

static void abrir_centro(bool abrir) {
	if (centro == abrir) {
		return;
	}
	centro = abrir;
	if (abrir) {
		/* os do canto passam para a central; os transitórios acabam */
		for (int i = nitens - 1; i >= 0; i--) {
			itens[i].no_canto = itens[i].saindo = false;
			if (itens[i].a.transitorio) {
				remover(i, AVISO_EXPIROU);
			}
		}
		gv_app_camada_ancoras(app_avisos, GV_ANCORA_TUDO, true);
		gv_app_camada(app_avisos, 0, 0, 0);
		gv_app_entrada_em(app_avisos, CLAY_ID("raiz"));
	} else {
		altura_canto = 1;
		gv_app_camada_ancoras(app_avisos, GV_ANCORA_CIMA | GV_ANCORA_DIREITA, false);
		gv_app_camada(app_avisos, LARGURA_AVISO + 2 * MARGEM, altura_canto, 0);
		gv_app_entrada_em(app_avisos, CLAY_ID("pilha"));
	}
	gv_app_redesenhar(app_avisos);
}

static void eventos(gv_app *app, int fd, void *dados) {
	struct aviso_evento ev[8];
	int n;
	while ((n = avisos_ler(ev, 8)) > 0) {
		for (int k = 0; k < n; k++) {
			if (ev[k].tipo == AVISO_NOVO) {
				chegou_aviso(&ev[k].aviso);
			} else if (ev[k].tipo == AVISO_FECHAR) {
				int i = achar(ev[k].id);
				if (i >= 0) {
					remover(i, AVISO_FECHADO_PELA_APP);
				}
			} else if (ev[k].tipo == AVISO_CENTRO) {
				abrir_centro(!centro);
			}
		}
	}
}

/* ---- o que a pessoa faz ---- */

/* A janela da app que avisou, para a trazer quando o aviso não tem ação. */
static void trazer_janela(const struct aviso *a) {
	const gv_janela *js[64];
	int n = gv_janelas(js, 64);
	for (int k = n - 1; k >= 0; k--) {
		const char *id = js[k]->app_id;
		if ((a->entrada[0] && !strcasecmp(id, a->entrada)) || (a->app[0] && !strcasecmp(id, a->app))) {
			gv_janela_ativar(js[k]->id);
			return;
		}
	}
}

static void fazer_acao(int i, const char *chave) {
	avisos_acao(itens[i].a.id, chave);
	if (itens[i].a.residente) {
		itens[i].no_canto = false;
	} else {
		remover(i, AVISO_DISPENSADO);
	}
}

static void tocar(int i) {
	for (int k = 0; k < itens[i].a.nacoes; k++) {
		if (!strcmp(itens[i].a.acoes[k][0], "default")) {
			fazer_acao(i, "default");
			abrir_centro(false);
			return;
		}
	}
	trazer_janela(&itens[i].a);
	remover(i, AVISO_DISPENSADO);
	abrir_centro(false);
}

/* ---- desenho ---- */

static const char *ha_quanto(int64_t quando) {
	static char buf[32];
	int64_t d = (int64_t)time(NULL) - quando;
	if (d < 60) {
		return "agora";
	}
	if (d < 3600) {
		snprintf(buf, sizeof(buf), "há %d min", (int)(d / 60));
	} else if (d < 86400) {
		snprintf(buf, sizeof(buf), "há %d h", (int)(d / 3600));
	} else if (d < 2 * 86400) {
		return "ontem";
	} else {
		time_t t = (time_t)quando;
		struct tm tm;
		localtime_r(&t, &tm);
		strftime(buf, sizeof(buf), "%d/%m", &tm);
	}
	return buf;
}

/* um ✕ pequeno, redondo */
static bool botao_x(Clay_ElementId id, float tamanho) {
	bool tocado = gv_tocado(id);
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_FIXED(tamanho), CLAY_SIZING_FIXED(tamanho) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_premido(id) ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(tamanho / 2),
	}) {
		tocado |= gv_focavel(id, tamanho / 2);
		gv_icone("close", tamanho * 0.6f, gv_t.texto_secundario);
	}
	return tocado;
}

static void icone_do_aviso(const struct aviso *a, uint32_t chave) {
	Clay_ElementId id = CLAY_IDI("img", chave);
	if (a->imagem[0]) {
		CLAY(id, gv_imagem(id, a->imagem, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) } },
			.cornerRadius = CLAY_CORNER_RADIUS(10) })) {}
	} else if (a->icone[0] == '/') {
		CLAY(id, gv_imagem(id, a->icone, GV_AJUSTE_CONTER, (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) } } })) {}
	} else if (a->icone[0]) {
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone_app(a->icone, 36);
		}
	} else {
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.selecionado, .cornerRadius = CLAY_CORNER_RADIUS(20) }) {
			gv_icone("bell", 20, gv_t.acento);
		}
	}
}

/* O corpo com um limite (o resto vê-se na app): umas 5 linhas. */
static Clay_String corpo_curto(const char *corpo) {
	if (g_utf8_strlen(corpo, -1) <= 280) {
		return gv_str(corpo);
	}
	char buf[1100];
	g_utf8_strncpy(buf, corpo, 277);
	strcat(buf, "…");
	return gv_str(buf);
}

/* Um aviso, no canto ou na central. Devolve falso se saiu da lista (não usar mais o índice). */
static bool cartao(int i, bool na_central) {
	struct item *it = &itens[i];
	const struct aviso *a = &it->a;
	Clay_ElementId id = (na_central ? CLAY_IDI("c", a->id) : CLAY_IDI("t", a->id));
	/* o toque no cartão antes dos de dentro: os botões, chamados depois, ficam com o aperto */
	bool tocado = gv_tocado(id);
	bool por_cima = gv_por_cima(id);
	int acao = -1, fechar = 0;
	bool critico = a->urgencia == AVISO_CRITICA;
	CLAY(id, {
		.layout = { .sizing = { na_central ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIXED(LARGURA_AVISO) },
			.padding = { 14, 12, 14, 14 }, .childGap = 12, .layoutDirection = CLAY_TOP_TO_BOTTOM },
		.backgroundColor = na_central ? (por_cima ? gv_t.sob_cursor : gv_t.elevada) : gv_t.elevada,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = critico ? gv_alfa(gv_t.perigo, 0.6f) : gv_t.borda, .width = CLAY_BORDER_OUTSIDE(1) },
		.userData = na_central ? NULL : gv_sombra(24, 8, 0.35f),
	}) {
		tocado |= gv_focavel(id, gv_t.raio_grande);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
			icone_do_aviso(a, a->id * 2 + na_central);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6,
					.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					if (!na_central) {
						gv_texto(gv_str(a->app[0] ? a->app : "Aviso"), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_MEDIO);
						gv_texto(CLAY_STRING("·"), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
					}
					gv_texto(gv_str(ha_quanto(a->quando)), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
					if (critico) {
						gv_texto(CLAY_STRING("· Urgente"), gv_t.tamanho_legenda, gv_t.perigo, GV_PESO_MEDIO);
					}
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
					/* o ✕ aparece com o rato por cima (com o dedo, está sempre) */
					if (por_cima || na_central || critico) {
						if (botao_x((na_central ? CLAY_IDI("cx", a->id) : CLAY_IDI("tx", a->id)), 22)) {
							fechar = 1;
						}
					} else {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) } } }) {}
					}
				}
				if (a->titulo[0]) {
					gv_texto(gv_str(a->titulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_SEMINEGRITO);
				}
				if (a->corpo[0]) {
					gv_texto(corpo_curto(a->corpo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
		}
		/* as ações com botão (a "default" é tocar no aviso) */
		bool tem_botoes = false;
		for (int k = 0; k < a->nacoes; k++) {
			tem_botoes |= strcmp(a->acoes[k][0], "default") != 0;
		}
		if (tem_botoes) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .padding = { 52, 0, 0, 0 } } }) {
				for (int k = 0; k < a->nacoes; k++) {
					if (!strcmp(a->acoes[k][0], "default")) {
						continue;
					}
					if (gv_botao_ext((na_central ? CLAY_IDI("ca", a->id * AVISO_MAX_ACOES + k) : CLAY_IDI("ta", a->id * AVISO_MAX_ACOES + k)),
						    &(gv_botao_opcoes){ .texto = a->acoes[k][1], .estilo = GV_BOTAO_ELEVADO, .crescer = true, .altura = 34 })) {
						acao = k;
					}
				}
			}
		}
	}
	if (fechar) {
		remover(i, AVISO_DISPENSADO);
		return false;
	}
	if (acao >= 0) {
		char chave[64];
		snprintf(chave, sizeof(chave), "%s", a->acoes[acao][0]);
		fazer_acao(i, chave);
		return false;
	}
	if (tocado) {
		tocar(i);
		return false;
	}
	if (por_cima && it->no_canto && it->sai_em > 0) {
		/* o rato em cima segura o aviso */
		it->sai_em = fmax(it->sai_em, gv_agora() + 2);
	}
	return true;
}

static void desenhar_canto(void) {
	double agora = gv_agora();
	double proximo = 0;
	CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
		.padding = CLAY_PADDING_ALL(MARGEM), .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
		CLAY(CLAY_ID("pilha"), { .layout = { .childGap = 10, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
			int mostrados = 0;
			for (int i = nitens - 1; i >= 0; i--) {
				struct item *it = &itens[i];
				if (!it->no_canto) {
					continue;
				}
				if (mostrados == MAX_NO_CANTO) {
					/* sem lugar: fica só na central */
					it->no_canto = false;
					continue;
				}
				mostrados++;
				if (!it->saindo && it->sai_em > 0 && agora >= it->sai_em) {
					it->saindo = true;
					it->saida = agora;
				}
				uint32_t chave = CLAY_IDI("anim", it->a.id).id;
				if (it->novo) {
					gv_anim(chave, LARGURA_AVISO + MARGEM, 0); /* entra de fora da tela */
					it->novo = false;
				}
				float desvio = gv_anim(chave, it->saindo ? LARGURA_AVISO + MARGEM : 0, gv_t.duracao_lenta);
				float fator = gv_anim(chave + 1, it->saindo ? 0 : 1, gv_t.duracao_lenta);
				if (it->saindo && agora - it->saida > gv_t.duracao_lenta / 1000.0 + 0.05) {
					it->no_canto = it->saindo = false;
					if (it->a.transitorio) {
						remover(i, AVISO_EXPIROU);
					}
					gv_app_redesenhar(NULL);
					continue;
				}
				Clay_ElementId caixa = CLAY_IDI("tc", it->a.id);
				float altura = gv_caixa(CLAY_IDI("t", it->a.id)).height;
				CLAY(caixa, { .layout = { .sizing = { CLAY_SIZING_FIXED(LARGURA_AVISO),
						      it->saindo && altura > 0 ? CLAY_SIZING_FIXED(altura * fator) : CLAY_SIZING_FIT(0) } },
					.clip = { .horizontal = true, .vertical = it->saindo } }) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(desvio) } } }) {}
					if (!cartao(i, false)) {
						gv_app_redesenhar(NULL);
					}
				}
				if (!it->saindo && it->sai_em > 0 && (proximo == 0 || it->sai_em < proximo)) {
					proximo = it->sai_em;
				}
			}
		}
	}
	if (proximo > 0) {
		gv_acordar_em(proximo - agora + 0.02);
	}
	/* a camada do tamanho da pilha (do quadro anterior: o Clay mede depois de declarar) */
	float h = gv_caixa(CLAY_ID("pilha")).height;
	int quer = h > 0 ? (int)h + 2 * MARGEM + 24 /* a sombra */ : 1;
	if (quer != altura_canto) {
		altura_canto = quer;
		gv_app_camada(app_avisos, LARGURA_AVISO + 2 * MARGEM, altura_canto, 0);
	}
}

static void desenhar_centro(void) {
	if (gv_tecla_carregada(XKB_KEY_Escape)) {
		abrir_centro(false);
		return;
	}
	float entrada = gv_anim(CLAY_ID("painel").id, 1, gv_t.duracao_normal);
	CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(MARGEM) },
		.backgroundColor = gv_alfa(gv_t.veu, gv_t.veu.a / 255.0f * 0.6f * entrada) }) {
		/* tocar fora do painel fecha */
		Clay_ElementId fora = CLAY_ID("fora");
		if (gv_tocado(fora)) {
			abrir_centro(false);
		}
		CLAY(fora, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}
		CLAY(CLAY_ID("painel-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(LARGURA_CENTRAL), CLAY_SIZING_GROW(0) } },
			.clip = { .horizontal = true } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED((1 - entrada) * 60) } } }) {}
			CLAY(CLAY_ID("painel"), {
				.layout = { .sizing = { CLAY_SIZING_FIXED(LARGURA_CENTRAL), CLAY_SIZING_GROW(0) }, .padding = { 18, 18, 20, 18 },
					.childGap = 14, .layoutDirection = CLAY_TOP_TO_BOTTOM },
				.backgroundColor = gv_t.superficie,
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
				.border = { .color = gv_t.borda, .width = CLAY_BORDER_OUTSIDE(1) },
				.userData = gv_sombra(30, 10, 0.4f),
			}) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10,
					.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_texto(CLAY_STRING("Notificações"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_SEMINEGRITO);
					if (nitens > 0) {
						CLAY_AUTO_ID({ .layout = { .padding = { 8, 8, 2, 2 } }, .backgroundColor = gv_t.selecionado,
							.cornerRadius = CLAY_CORNER_RADIUS(10) }) {
							gv_texto(gv_fmt("%d", nitens), gv_t.tamanho_legenda, gv_t.acento, GV_PESO_SEMINEGRITO);
						}
					}
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
					if (nitens > 0 && gv_botao(CLAY_ID("limpar"), "trash", "Limpar", GV_BOTAO_SIMPLES)) {
						for (int i = nitens - 1; i >= 0; i--) {
							if (!itens[i].a.residente) {
								remover(i, AVISO_DISPENSADO);
							}
						}
					}
				}
				if (gv_linha(CLAY_ID("nao-perturbe"), "moon", "Não perturbe",
					    nao_perturbe ? "Só os urgentes aparecem no canto" : "Os avisos aparecem no canto", &nao_perturbe)) {
					gravar_config();
				}
				gv_separador(false, 0);
				CLAY(CLAY_ID("lista"), gv_rolagem(CLAY_ID("lista"), GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
					.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 10,
						.layoutDirection = CLAY_TOP_TO_BOTTOM } })) {
					if (nitens == 0) {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 10,
							.layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
							CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(64), CLAY_SIZING_FIXED(64) },
								.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
								.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(32) }) {
								gv_icone("bell", 28, gv_t.texto_suave);
							}
							gv_texto(CLAY_STRING("Tudo em dia"), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_MEDIO);
							gv_texto(CLAY_STRING("Os avisos novos aparecem aqui."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
						}
					}
					/* agrupados por app, a app do aviso mais novo primeiro */
					char feitos[MAX_ITENS][64];
					int nfeitos = 0;
					for (int i = nitens - 1; i >= 0; i--) {
						const char *app = itens[i].a.app;
						bool visto = false;
						for (int g = 0; g < nfeitos; g++) {
							visto |= !strcmp(feitos[g], app);
						}
						if (visto) {
							continue;
						}
						snprintf(feitos[nfeitos++], 64, "%s", app);
						int quantos = 0;
						for (int k = 0; k < nitens; k++) {
							quantos += !strcmp(itens[k].a.app, app);
						}
						bool limpar_grupo = false;
						CLAY(CLAY_IDI("grupo", (uint32_t)nfeitos), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8,
							.padding = { 4, 2, nfeitos > 1 ? 8 : 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
							gv_texto(gv_str(app[0] ? app : "Avisos"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_MEDIO);
							if (quantos > 1) {
								gv_texto(gv_fmt("%d", quantos), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
							}
							CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
							if (quantos > 1 && gv_botao(CLAY_IDI("limpar-grupo", (uint32_t)nfeitos), NULL, "Limpar", GV_BOTAO_SIMPLES)) {
								limpar_grupo = true;
							}
						}
						for (int k = nitens - 1; k >= 0; k--) {
							if (!strcmp(itens[k].a.app, feitos[nfeitos - 1])) {
								if (!cartao(k, true)) {
									gv_app_redesenhar(NULL);
									break; /* a lista mudou: o resto no próximo quadro */
								}
							}
						}
						if (limpar_grupo) {
							for (int k = nitens - 1; k >= 0; k--) {
								if (!strcmp(itens[k].a.app, feitos[nfeitos - 1]) && !itens[k].a.residente) {
									remover(k, AVISO_DISPENSADO);
								}
							}
						}
						if (i >= nitens) {
							i = nitens; /* removemos: continuar do fim é seguro (os grupos feitos saltam) */
						}
					}
				}
			}
		}
	}
	/* os relógios "há 5 min" andam sozinhos */
	gv_acordar_em(30);
}

static void desenhar(gv_app *app, void *dados) {
	(void)app;
	(void)dados;
	if (centro) {
		desenhar_centro();
	} else {
		desenhar_canto();
	}
}

int main(int argc, char **argv) {
	if (argc > 1 && !strcmp(argv[1], "--centro")) {
		/* o sino da topbar: o lumna-avisos que já corre abre ou fecha a central */
		if (!avisos_pedir_centro()) {
			fprintf(stderr, "lumna-avisos: não está a correr\n");
			return 1;
		}
		return 0;
	}
	int fd = avisos_iniciar();
	if (fd < 0) {
		fprintf(stderr, "lumna-avisos: já há um a correr (ou não há barramento da sessão)\n");
		return 1;
	}
	ler_config();
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Avisos", .app_id = "lumna-avisos", .tipo = GV_CAMADA,
		.nivel = GV_NIVEL_OVERLAY, .ancoras = GV_ANCORA_CIMA | GV_ANCORA_DIREITA,
		.largura = LARGURA_AVISO + 2 * MARGEM, .altura = 1 });
	if (!app) {
		return 1;
	}
	app_avisos = app;
	modo_silencio = modos_nao_perturbe();
	int fd_modos = modos_assinar();
	if (fd_modos >= 0) {
		gv_app_vigiar(app, fd_modos, modo_mudou, NULL);
	}
	gv_app_entrada_em(app, CLAY_ID("pilha"));
	gv_app_vigiar(app, fd, eventos, NULL);
	return gv_app_correr(app, desenhar, NULL);
}
