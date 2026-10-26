/*
 * Aparência → Papel de parede, como no esboço: a tela e o modo que se editam, a prévia grande com
 * o nome e o Escolher arquivo, o ajuste, a Coleção LUMNA e o trocar sozinho. Grava o papel.ini
 * (servicos/papelini.h) no Aplicar; o papel vigia-o e troca na hora, em cada tela.
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "papelini.h"

static struct papel_cfg cfg, gravada;
static int tela;          /* 0: todas as telas; k: a k-ésima de gv_saidas */
static int modo;          /* desktop, gaming, mídia */
static bool todos_modos = true;
static bool ver_todos;
static char colecao[PAPEL_COLECAO_MAX][512];
static int ncolecao;

static const char *const modos_id[] = { "desktop", "gaming", "midia" };

void pn_papel_abrir(void) {
	papel_ler(&cfg);
	gravada = cfg;
	ncolecao = papel_colecao(colecao, PAPEL_COLECAO_MAX);
	pn_suas_carregar();
	/* começa no modo de agora, a valer para todos se ainda não há um por modo */
	const char *agora = papel_modo_atual();
	for (int i = 0; i < 3; i++) {
		if (!strcmp(agora, modos_id[i])) {
			modo = i;
		}
	}
	todos_modos = true;
	for (int i = 0; i < cfg.n; i++) {
		todos_modos &= !strcmp(cfg.itens[i].modo, "todos") || !cfg.itens[i].imagem[0];
	}
	tela = 0;
}

bool pn_papel_pendente(void) {
	char *a = papel_texto(&cfg), *b = papel_texto(&gravada);
	bool diferente = strcmp(a, b) != 0;
	g_free(a);
	g_free(b);
	return diferente;
}

void pn_papel_aplicar(void) {
	/* um por tela quando há algum papel de uma tela só */
	cfg.por_tela = false;
	for (int i = 0; i < cfg.n; i++) {
		cfg.por_tela |= cfg.itens[i].saida[0] && cfg.itens[i].imagem[0];
	}
	if (papel_gravar(&cfg)) {
		gravada = cfg;
	}
}

void pn_papel_descartar(void) {
	cfg = gravada;
}

void pn_papel_restaurar(void) {
	memset(&cfg, 0, sizeof(cfg));
	cfg.intervalo = 30;
}

/* a tela e o modo que se editam agora */
static const char *saida_editada(void) {
	const char *saidas[8];
	int n = gv_saidas(pn_app, saidas, 8);
	return tela > 0 && tela <= n ? saidas[tela - 1] : "";
}

static const char *modo_editado(void) {
	return todos_modos ? "todos" : modos_id[modo];
}

/* o que se vê agora na tela e no modo escolhidos (com o que já se mexeu) */
static const struct papel_item *atual(void) {
	/* "todas as telas": o geral deste modo, sem o de uma tela só */
	struct papel_cfg c = cfg;
	c.por_tela = tela > 0;
	static struct papel_item it;
	const struct papel_item *p = papel_para(&c, modos_id[modo], saida_editada());
	if (p) {
		it = *p;
	} else {
		memset(&it, 0, sizeof(it));
		snprintf(it.imagem, sizeof(it.imagem), "%s", papel_padrao());
	}
	return &it;
}

static void escolher(const char *caminho) {
	int ajuste = atual()->ajuste;
	/* "Todas as telas" vale por cima: os papéis de uma tela só (deste modo, ou de todos com
	 * "Todos os modos") saem; com "Todos os modos", os de cada modo também */
	if (!saida_editada()[0]) {
		for (int i = 0; i < cfg.n; i++) {
			struct papel_item *x = &cfg.itens[i];
			bool do_modo = todos_modos || !strcmp(x->modo, modo_editado());
			if (do_modo && (x->saida[0] || (todos_modos && strcmp(x->modo, "todos")))) {
				x->imagem[0] = '\0';
			}
		}
	}
	struct papel_item *it = papel_editar(&cfg, modo_editado(), saida_editada());
	snprintf(it->imagem, sizeof(it->imagem), "%s", caminho);
	it->ajuste = ajuste;
}

/* uma miniatura escolhível */

/* uma caixinha de marcar com o texto ao lado */
static bool marcar(Clay_ElementId id, const char *texto, bool *valor) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .childGap = 10, .padding = { 6, 6, 6, 6 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 } }) {
		tocado |= gv_focavel(id, gv_t.raio_pequeno);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20) },
				.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = *valor ? gv_t.acento : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(5),
			.border = { .color = *valor ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(*valor ? 0 : 2) } }) {
			if (*valor) {
				gv_icone("check", 14, gv_t.sobre_acento);
			}
		}
		gv_texto(gv_str(texto), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
	}
	if (tocado) {
		*valor = !*valor;
	}
	return tocado;
}

void pn_papel_desenhar(void) {
	/* a tela e o modo */
	const char *saidas[8];
	int ns = gv_saidas(pn_app, saidas, 8);
	const char *telas[9];
	char nomes_telas[8][80];
	telas[0] = "Todas as telas";
	for (int i = 0; i < ns; i++) {
		snprintf(nomes_telas[i], sizeof(nomes_telas[i]), i == 0 ? "Tela principal (%s)" : "%s", saidas[i]);
		telas[i + 1] = nomes_telas[i];
	}
	if (tela > ns) {
		tela = 0;
	}
	CLAY(CLAY_ID("papel-filtros"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .padding = { 0, 0, 6, 16 },
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("Monitor"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(230) } } }) {
			gv_lista_suspensa(CLAY_ID("papel-tela"), telas, ns + 1, &tela);
		}
		gv_texto(CLAY_STRING("Modo"), gv_t.tamanho_corpo, todos_modos ? gv_t.texto_suave : gv_t.texto, GV_PESO_NORMAL);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(170) } } }) {
			static const char *const modos[] = { "Desktop", "Gaming", "Mídia" };
			gv_lista_suspensa(CLAY_ID("papel-modo"), modos, 3, &modo);
		}
		marcar(CLAY_ID("papel-todos"), "Todos os modos", &todos_modos);
	}

	/* a prévia, o nome e o ajuste */
	const struct papel_item *it = atual();
	char nome[128];
	papel_nome(it->imagem, nome, sizeof(nome));
	bool da_colecao = false;
	for (int i = 0; i < ncolecao; i++) {
		da_colecao |= !strcmp(colecao[i], it->imagem);
	}
	CLAY(CLAY_ID("papel-atual"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		/* a moldura por fora; a imagem por dentro (o fundo aparece nas sobras do Ajustar) */
		CLAY(CLAY_ID("papel-moldura"), { .layout = { .sizing = { CLAY_SIZING_FIXED(380), CLAY_SIZING_FIXED(214) }, .padding = CLAY_PADDING_ALL(1) },
			.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			Clay_ElementId previa = CLAY_ID("papel-previa");
			CLAY(previa, gv_imagem(previa, it->imagem, it->ajuste == PAPEL_AJUSTAR ? GV_AJUSTE_CONTER
				: it->ajuste == PAPEL_ESTICAR ? GV_AJUSTE_ESTICAR : GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
				.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio - 1) })) {}
		}
		CLAY(CLAY_ID("papel-info"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
			gv_texto(gv_str(nome), gv_t.tamanho_destaque + 1, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(da_colecao ? CLAY_STRING("Coleção LUMNA") : CLAY_STRING("Suas imagens"), gv_t.tamanho_corpo - 1,
				gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 8, 8 } } }) {
				if (gv_botao(CLAY_ID("papel-arquivo"), "folder", "Escolher arquivo", GV_BOTAO_ELEVADO)) {
					gv_popup_alternar(CLAY_ID("papel-suas"));
				}
			}
			CLAY_AUTO_ID({ .layout = { .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(CLAY_STRING("Ajuste"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(200) } } }) {
					static const char *const ajustes[] = { "Preencher", "Ajustar", "Esticar" };
					int a = it->ajuste;
					if (gv_lista_suspensa(CLAY_ID("papel-ajuste"), ajustes, 3, &a)) {
						struct papel_item *e = papel_editar(&cfg, modo_editado(), saida_editada());
						if (!e->imagem[0]) {
							snprintf(e->imagem, sizeof(e->imagem), "%s", it->imagem);
						}
						e->ajuste = a;
					}
				}
			}
		}
	}

	/* as imagens da pessoa, no popup do Escolher arquivo */
	const char *sua = pn_suas_popup(CLAY_ID("papel-suas"), CLAY_ID("papel-arquivo"), it->imagem);
	if (sua) {
		escolher(sua);
	}

	/* a Coleção LUMNA */
	CLAY(CLAY_ID("papel-colecao-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 22, 10 },
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("Coleção LUMNA"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		if (ncolecao > 4 && gv_botao(CLAY_ID("papel-ver"), NULL, ver_todos ? "Ver menos" : "Ver todos", GV_BOTAO_SIMPLES)) {
			ver_todos = !ver_todos;
		}
	}
	float largura = (gv_caixa(CLAY_ID("papel-colecao-t")).width - 3 * 14) / 4;
	if (largura < 120) {
		largura = 120;
	}
	int mostrar = ver_todos ? ncolecao : (ncolecao < 4 ? ncolecao : 4);
	for (int linha = 0; linha * 4 < mostrar; linha++) {
		CLAY(CLAY_IDI("colecao-linha", (uint32_t)linha), { .layout = { .childGap = 14, .padding = { 0, 0, 0, 10 } } }) {
			for (int i = linha * 4; i < mostrar && i < linha * 4 + 4; i++) {
				char n[128];
				papel_nome(colecao[i], n, sizeof(n));
				if (pn_miniatura(CLAY_IDI("colecao", (uint32_t)i), colecao[i], largura, largura * 0.5f, !strcmp(colecao[i], it->imagem), n)) {
					escolher(colecao[i]);
				}
			}
		}
	}

	/* trocar sozinho */
	pn_linha_comecar(CLAY_ID("papel-trocar"), "clock", "Trocar automaticamente", "Alternar entre os papéis de parede da coleção");
	{
		gv_interruptor(CLAY_ID("papel-trocar-v"), &cfg.trocar);
		gv_texto(CLAY_STRING("Intervalo"), gv_t.tamanho_corpo, cfg.trocar ? gv_t.texto : gv_t.texto_suave, GV_PESO_NORMAL);
		static const char *const intervalos[] = { "5 minutos", "15 minutos", "30 minutos", "1 hora", "1 dia" };
		static const int minutos[] = { 5, 15, 30, 60, 1440 };
		int escolha = 2;
		for (int i = 0; i < 5; i++) {
			if (minutos[i] == cfg.intervalo) {
				escolha = i;
			}
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(160) } } }) {
			if (gv_lista_suspensa(CLAY_ID("papel-intervalo"), intervalos, 5, &escolha)) {
				cfg.intervalo = minutos[escolha];
			}
		}
	}
	pn_linha_acabar();
}
