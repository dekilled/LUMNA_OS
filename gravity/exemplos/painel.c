/*
 * O Painel de controle do LUMNA (lumna-painel), como nos esboços (docs/visual/04 e as telas de
 * Tema, Cores e Papel de parede). O plano inteiro está em PAINEL.md.
 *
 * - O início: as abas Geral, Inicialização e Sistema, a busca em tudo, os usados recentemente e
 *   a grade das categorias.
 * - Uma categoria: a barra lateral com as seções dela (e o filtro), o conteúdo da seção e, se ela
 *   tem ajustes, o rodapé Restaurar padrão | Aplicar. Sair com algo por aplicar pergunta antes.
 * - As categorias e as seções ficam em exemplos/painel/ (uma categoria por arquivo).
 *
 *   ./painel                       o início
 *   ./painel --secao aparencia/dock   direto numa seção (--dock e --papel são atalhos)
 */
#define _GNU_SOURCE
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "painel.h"

#define MAX_RECENTES 4

static gv_app *app_painel;
static int aba;                 /* a aba do início */
static int cat = -1, sec;       /* a categoria aberta (-1: o início) e a seção dela */
static char busca[64], filtro[64];
static char recentes[MAX_RECENTES][64]; /* "categoria/seção", o mais recente primeiro */

/* o destino guardado enquanto se pergunta o que fazer com o que está por aplicar */
enum { DESTINO_INICIO = -1, DESTINO_SAIR = -2 };
static bool perguntando;
static int destino_cat, destino_sec, destino_aba;

static const pn_secao *secao_aberta(void) {
	return cat >= 0 ? &pn_categorias[cat]->secoes[sec] : NULL;
}

/* ---- os recentes (~/.config/lumna/painel-recentes) ---- */

static void recentes_ler(void) {
	char caminho[1024];
	pn_caminho(caminho, sizeof(caminho), "painel-recentes");
	FILE *f = fopen(caminho, "r");
	if (!f) {
		return;
	}
	for (int i = 0; i < MAX_RECENTES && fgets(recentes[i], sizeof(recentes[i]), f); i++) {
		recentes[i][strcspn(recentes[i], "\r\n")] = '\0';
	}
	fclose(f);
}

static void recentes_marcar(const char *id) {
	char novos[MAX_RECENTES][64] = { { 0 } };
	snprintf(novos[0], sizeof(novos[0]), "%s", id);
	for (int i = 0, n = 1; i < MAX_RECENTES && n < MAX_RECENTES; i++) {
		if (recentes[i][0] && strcmp(recentes[i], id)) {
			snprintf(novos[n++], sizeof(novos[0]), "%.63s", recentes[i]);
		}
	}
	memcpy(recentes, novos, sizeof(recentes));
	char texto[MAX_RECENTES * 70] = "";
	for (int i = 0; i < MAX_RECENTES && recentes[i][0]; i++) {
		strcat(texto, recentes[i]);
		strcat(texto, "\n");
	}
	pn_gravar("painel-recentes", texto);
}

/* "aparencia/dock" → a categoria e a seção */
static bool achar(const char *id, int *c, int *s) {
	for (int i = 0; i < pn_ncategorias; i++) {
		size_t l = strlen(pn_categorias[i]->id);
		if (strncmp(id, pn_categorias[i]->id, l) || (id[l] != '/' && id[l] != '\0')) {
			continue;
		}
		*c = i;
		*s = 0;
		for (int k = 0; id[l] == '/' && k < pn_categorias[i]->nsecoes; k++) {
			if (!strcmp(id + l + 1, pn_categorias[i]->secoes[k].id)) {
				*s = k;
			}
		}
		return true;
	}
	return false;
}

/* ---- navegar ---- */

static void ir_agora(int c, int s, int a) {
	if (c == DESTINO_SAIR) {
		gv_app_sair(app_painel);
		return;
	}
	cat = c;
	sec = s;
	if (c == DESTINO_INICIO) {
		aba = a;
		return;
	}
	filtro[0] = '\0';
	const pn_secao *x = secao_aberta();
	if (x->abrir) {
		x->abrir();
	}
	char id[64];
	snprintf(id, sizeof(id), "%s/%s", pn_categorias[c]->id, x->id);
	recentes_marcar(id);
}

/* sair da seção com algo por aplicar pergunta antes */
static void ir(int c, int s, int a) {
	const pn_secao *x = secao_aberta();
	if (x && x->pendente && x->pendente() && !(c == cat && s == sec)) {
		perguntando = true;
		destino_cat = c;
		destino_sec = s;
		destino_aba = a;
		return;
	}
	ir_agora(c, s, a);
}

/* ---- a busca: em todas as categorias, seções e no que cada uma vai ter ---- */

static bool combina(const char *texto, const char *procura) {
	return texto && strcasestr(texto, procura);
}

static void resultados(void) {
	int n = 0;
	for (int c = 0; c < pn_ncategorias; c++) {
		const pn_categoria *k = pn_categorias[c];
		for (int s = 0; s < k->nsecoes; s++) {
			const pn_secao *x = &k->secoes[s];
			if (!combina(k->nome, busca) && !combina(x->nome, busca) && !combina(x->descricao, busca) &&
				!combina(x->planos, busca)) {
				continue;
			}
			if (n++) {
				gv_separador(false, 0);
			}
			if (gv_linha(CLAY_IDI("achado", (uint32_t)(c * 64 + s)), x->icone, gv_fmt("%s  ›  %s", k->nome, x->nome).chars,
				    x->descricao, NULL)) {
				busca[0] = '\0';
				ir(c, s, aba);
			}
		}
	}
	if (!n) {
		CLAY(CLAY_ID("nada"), { .layout = { .padding = { 18, 18, 20, 20 } } }) {
			gv_texto(gv_fmt("Nada encontrado para “%s”.", busca), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

/* ---- o início ---- */

static void cabeca(const char *titulo, const char *sub) {
	CLAY(CLAY_ID("cabeca"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 12, 12, 0, 16 }, .childGap = 8 } }) {
		gv_texto(gv_str(titulo), 28, gv_t.texto, GV_PESO_NORMAL);
		gv_texto(gv_str(sub), gv_t.tamanho_destaque, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void usados_recentemente(void) {
	if (!recentes[0][0]) {
		return;
	}
	CLAY(CLAY_ID("recentes-t"), { .layout = { .padding = { 12, 12, 0, 10 } } }) {
		gv_texto(CLAY_STRING("Usados recentemente"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_MEDIO);
	}
	CLAY(CLAY_ID("recentes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .padding = { 12, 12, 0, 18 } } }) {
		for (int i = 0; i < MAX_RECENTES; i++) {
			int c, s;
			if (!recentes[i][0] || !achar(recentes[i], &c, &s)) {
				continue;
			}
			const pn_secao *x = &pn_categorias[c]->secoes[s];
			Clay_ElementId id = CLAY_IDI("recente", (uint32_t)i);
			bool tocado = gv_tocado(id);
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_PERCENT(0.25f) }, .padding = { 14, 14, 12, 12 }, .childGap = 12,
					.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada,
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
				tocado |= gv_focavel(id, gv_t.raio_medio);
				gv_icone(x->icone, 22, gv_t.acento);
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2 } }) {
					gv_texto(gv_str(x->nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
					gv_texto(gv_str(pn_categorias[c]->nome), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			if (tocado) {
				ir(c, s, aba);
			}
		}
	}
}

/* a grade em duas colunas: metade à esquerda, o resto à direita */
static void grade(const char *const *icones, const char *const *nomes, const char *const *subs, const int *alvo_c,
	const int *alvo_s, int n) {
	CLAY(CLAY_ID("grade"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 20 } }) {
		int metade = (n + 1) / 2;
		for (int col = 0; col < 2; col++) {
			CLAY(CLAY_IDI("coluna", (uint32_t)col), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } } }) {
				for (int i = col ? metade : 0; i < (col ? n : metade); i++) {
					if (i != (col ? metade : 0)) {
						gv_separador(false, 0);
					}
					if (gv_linha(CLAY_IDI("item", (uint32_t)(aba * 100 + i)), icones[i], nomes[i], subs[i], NULL)) {
						ir(alvo_c[i], alvo_s[i], aba);
					}
				}
			}
			if (col == 0) {
				gv_separador(true, 0);
			}
		}
	}
}

static void grade_da_aba(void) {
	const char *icones[32], *nomes[32], *subs[32];
	int alvo_c[32], alvo_s[32], n = 0;
	if (aba == PN_INICIALIZACAO) {
		/* a Inicialização é uma categoria só: a grade mostra as seções dela */
		for (int c = 0; c < pn_ncategorias; c++) {
			const pn_categoria *k = pn_categorias[c];
			for (int s = 0; k->aba == PN_INICIALIZACAO && s < k->nsecoes && n < 32; s++, n++) {
				icones[n] = k->secoes[s].icone;
				nomes[n] = k->secoes[s].nome;
				subs[n] = k->secoes[s].descricao;
				alvo_c[n] = c;
				alvo_s[n] = s;
			}
		}
	} else {
		for (int c = 0; c < pn_ncategorias && n < 32; c++) {
			if ((int)pn_categorias[c]->aba == aba) {
				icones[n] = pn_categorias[c]->icone;
				nomes[n] = pn_categorias[c]->nome;
				subs[n] = pn_categorias[c]->subtitulo;
				alvo_c[n] = c;
				alvo_s[n++] = 0;
			}
		}
	}
	grade(icones, nomes, subs, alvo_c, alvo_s, n);
}

static void atualizacoes(void) {
	CLAY(CLAY_ID("atualizacoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 18, 12, 16, 4 },
		.childGap = 22, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		gv_icone("refresh", 30, gv_t.icone);
		CLAY(CLAY_ID("atualizacoes-texto"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
			gv_texto(CLAY_STRING("Atualizações"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("Mantenha seu sistema sempre atualizado."), gv_t.tamanho_corpo, gv_t.texto_secundario,
				GV_PESO_NORMAL);
		}
		if (gv_botao(CLAY_ID("verificar"), NULL, "Verificar", GV_BOTAO_ELEVADO)) {
			int c, s;
			if (achar("sobre/atualizacoes", &c, &s)) {
				ir(c, s, aba);
			}
		}
		gv_separador(true, 44);
		gv_texto(CLAY_STRING("LUMNA OS 1.0.0"), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}

static void inicio(void) {
	Clay_ElementId conteudo = CLAY_ID("conteudo");
	CLAY(conteudo, gv_rolagem(conteudo, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.padding = { 22, 22, 26, 14 } } })) {
		if (busca[0]) {
			resultados();
		} else {
			if (aba == PN_GERAL) {
				cabeca("Tudo no seu lugar", "Personalize o seu LUMNA e deixe tudo do seu jeito.");
				usados_recentemente();
			} else if (aba == PN_INICIALIZACAO) {
				cabeca("Ao ligar e ao entrar", "O que abre sozinho e como o LUMNA começa.");
			} else {
				cabeca("O seu sistema", "O computador, a energia, o teclado, a hora e as contas.");
			}
			grade_da_aba();
		}
		CLAY(CLAY_ID("espaco"), { .layout = { .sizing = { .height = CLAY_SIZING_GROW(0) } } }) {}
		atualizacoes();
	}
}

/* ---- uma categoria: a barra lateral e a seção ---- */

static void item_lateral(Clay_ElementId id, const char *icone, const char *nome, bool ativo, bool *tocado) {
	*tocado = gv_tocado(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(52) }, .childGap = 14,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ativo ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
		.cornerRadius = { 0, gv_t.raio_pequeno, 0, gv_t.raio_pequeno } }) {
		*tocado |= gv_focavel(id, gv_t.raio_pequeno);
		/* a faixa do acento à esquerda do escolhido */
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(4), CLAY_SIZING_GROW(0) } },
			.backgroundColor = ativo ? gv_t.acento : (Clay_Color){ 0 } }) {}
		CLAY_AUTO_ID({ .layout = { .padding = { 12, 0, 0, 0 } } }) {
			gv_icone(icone, 24, ativo ? gv_t.texto : gv_t.icone);
		}
		gv_texto(gv_str(nome), gv_t.tamanho_corpo + 1, gv_t.texto, ativo ? GV_PESO_MEDIO : GV_PESO_NORMAL);
	}
}

static void lateral(void) {
	const pn_categoria *k = pn_categorias[cat];
	CLAY(CLAY_ID("lateral"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(310), CLAY_SIZING_GROW(0) },
			.padding = { 0, 12, 18, 14 }, .childGap = 6 },
		.border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
		Clay_ElementId voltar = CLAY_ID("voltar");
		bool tocado = gv_tocado(voltar);
		CLAY(voltar, { .layout = { .padding = { 22, 12, 8, 8 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(voltar) ? gv_t.sob_cursor : (Clay_Color){ 0 },
			.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
			tocado |= gv_focavel(voltar, gv_t.raio_pequeno);
			gv_icone("back", 20, gv_t.texto);
			gv_texto(CLAY_STRING("Todas as categorias"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		if (tocado) {
			ir(DESTINO_INICIO, 0, (int)k->aba);
		}
		CLAY(CLAY_ID("filtro-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 22, 0, 8, 12 } } }) {
			gv_campo(CLAY_ID("filtro"), filtro, sizeof(filtro), "Filtrar painéis", GV_CAMPO_BUSCA);
		}
		CLAY(CLAY_ID("lateral-titulo"), { .layout = { .padding = { 22, 0, 10, 6 } } }) {
			char *maiusculas = g_utf8_strup(k->nome, -1);
			gv_texto(gv_str(maiusculas), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_MEDIO);
			g_free(maiusculas);
		}
		/* as seções rolam se não couberem (numa janela baixa) */
		Clay_ElementId lista = CLAY_ID("lateral-lista");
		CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
			.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 6 } })) {
			for (int s = 0; s < k->nsecoes; s++) {
				const pn_secao *x = &k->secoes[s];
				if (filtro[0] && !combina(x->nome, filtro) && !combina(x->descricao, filtro) && !combina(x->planos, filtro)) {
					continue;
				}
				bool t;
				item_lateral(CLAY_IDI("secao", (uint32_t)s), x->icone, x->nome, s == sec, &t);
				if (t && s != sec) {
					ir(cat, s, aba);
				}
			}
		}
		CLAY(CLAY_ID("lateral-pe"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 18, 0, 10, 0 } },
			.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
			if (gv_botao(CLAY_ID("emma"), "mic", "Perguntar à Emma", GV_BOTAO_SIMPLES)) {
				pn_executar("lumna-emma");
			}
		}
	}
}

/* uma seção ainda por fazer: o que ela vai ter */
static void em_breve(const pn_secao *x) {
	CLAY(CLAY_ID("breve"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 18, 0 },
		.childGap = 12 } }) {
		CLAY(CLAY_ID("breve-selo"), { .layout = { .padding = { 10, 10, 4, 4 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.selecionado, .cornerRadius = CLAY_CORNER_RADIUS(10) }) {
			gv_icone("clock", 14, gv_t.acento);
			gv_texto(CLAY_STRING("Em construção"), gv_t.tamanho_legenda, gv_t.acento, GV_PESO_MEDIO);
		}
		gv_texto(CLAY_STRING("O que vai ter aqui:"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		const char *p = x->planos ? x->planos : "";
		for (int i = 0; *p; i++) {
			size_t l = strcspn(p, ";");
			char item[256];
			snprintf(item, sizeof(item), "%.*s", (int)(l < 255 ? l : 255), p);
			CLAY(CLAY_IDI("plano", (uint32_t)i), { .layout = { .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(6), CLAY_SIZING_FIXED(6) } }, .backgroundColor = gv_t.acento,
					.cornerRadius = CLAY_CORNER_RADIUS(3) }) {}
				gv_texto(gv_str(item), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
			p += l;
			if (*p == ';') {
				p++;
			}
		}
	}
}

static void rodape(const pn_secao *x) {
	if (!x->aplicar) {
		return;
	}
	bool pendente = x->pendente && x->pendente();
	CLAY(CLAY_ID("rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 30, 26, 12, 16 }, .childGap = 12,
		.childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER } } }) {
		if (pendente) {
			CLAY(CLAY_ID("rodape-aviso"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Há mudanças por aplicar"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		if (x->restaurar && gv_botao(CLAY_ID("restaurar"), NULL, "Restaurar padrão", GV_BOTAO_ELEVADO)) {
			x->restaurar();
		}
		if (gv_botao_ext(CLAY_ID("aplicar"), &(gv_botao_opcoes){ .texto = "Aplicar", .estilo = GV_BOTAO_CHEIO, .desativado = !pendente })) {
			x->aplicar();
		}
	}
}

static void categoria(void) {
	const pn_categoria *k = pn_categorias[cat];
	const pn_secao *x = secao_aberta();
	CLAY(CLAY_ID("categoria"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		lateral();
		CLAY(CLAY_ID("lado-direito"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			Clay_ElementId conteudo = CLAY_ID("secao-conteudo");
			CLAY(conteudo, gv_rolagem(conteudo, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
				.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
					.padding = { 30, 26, 24, 16 } } })) {
				CLAY(CLAY_ID("secao-cabeca"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
					.padding = { 0, 0, 0, 16 }, .childGap = 6 }, .border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
					gv_texto(gv_str(k->nome), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
					gv_texto(gv_str(x->nome), 30, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(x->descricao), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				CLAY(CLAY_ID("secao-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
					.padding = { 0, 0, 10, 0 } } }) {
					if (x->desenhar) {
						x->desenhar();
					} else {
						em_breve(x);
					}
				}
			}
			rodape(x);
		}
	}
}

/* ---- a pergunta antes de sair com algo por aplicar ---- */

static void pergunta(void) {
	if (!perguntando) {
		return;
	}
	const pn_secao *x = secao_aberta();
	CLAY(CLAY_ID("veu"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.veu, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 50 } }) {
		CLAY(CLAY_ID("pergunta"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(460) },
				.padding = CLAY_PADDING_ALL(24), .childGap = 12 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
			.border = { .color = gv_t.borda, .width = GV_BORDA(1) }, .userData = gv_sombra(30, 10, 0.4f) }) {
			gv_texto(CLAY_STRING("Aplicar as mudanças?"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_fmt("Há mudanças em %s que ainda não foram aplicadas.", x->nome), gv_t.tamanho_corpo, gv_t.texto_secundario,
				GV_PESO_NORMAL);
			CLAY(CLAY_ID("pergunta-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .padding = { 0, 0, 10, 0 },
				.childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_botao(CLAY_ID("p-descartar"), NULL, "Descartar", GV_BOTAO_SIMPLES)) {
					x->descartar();
					perguntando = false;
					ir_agora(destino_cat, destino_sec, destino_aba);
				}
				if (gv_botao(CLAY_ID("p-continuar"), NULL, "Continuar a editar", GV_BOTAO_ELEVADO)) {
					perguntando = false;
				}
				if (gv_botao(CLAY_ID("p-aplicar"), NULL, "Aplicar", GV_BOTAO_CHEIO)) {
					x->aplicar();
					perguntando = false;
					ir_agora(destino_cat, destino_sec, destino_aba);
				}
			}
		}
	}
	if (gv_tecla() == XKB_KEY_Escape) {
		perguntando = false;
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	CLAY(CLAY_ID("janela"), {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
	}) {
		gv_barra_janela(app, CLAY_ID("barra"), "settings", "Painel de controle");
		/* as abas e a busca na mesma faixa; dentro de uma categoria, fica marcada a aba dela */
		CLAY(CLAY_ID("faixa"), {
			.layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 28, 20, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.border = { .color = gv_t.borda, .width = { .bottom = 1 } },
		}) {
			static const char *const abas[] = { "Geral", "Inicialização", "Sistema" };
			int marcada = cat >= 0 ? (int)pn_categorias[cat]->aba : aba;
			int antes = marcada;
			if (gv_abas(CLAY_ID("abas"), abas, PN_NABAS, &marcada) || (cat >= 0 && marcada != antes)) {
				busca[0] = '\0';
				ir(DESTINO_INICIO, 0, marcada);
			}
			CLAY(CLAY_ID("faixa-meio"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			CLAY(CLAY_ID("faixa-busca"), { .layout = { .sizing = { CLAY_SIZING_FIXED(320) } } }) {
				if (gv_campo(CLAY_ID("busca"), busca, sizeof(busca), "Buscar uma configuração", GV_CAMPO_BUSCA) && busca[0] &&
					cat >= 0) {
					/* começar a buscar dentro de uma categoria volta ao início com os resultados */
					ir(DESTINO_INICIO, 0, (int)pn_categorias[cat]->aba);
				}
			}
		}
		if (cat >= 0) {
			categoria();
		} else {
			inicio();
		}
	}
	pergunta();
	fflush(stdout);
	if (!perguntando && gv_tecla() == XKB_KEY_Escape && !gv_focado(CLAY_ID("busca")) && !gv_focado(CLAY_ID("filtro"))) {
		if (cat >= 0) {
			ir(DESTINO_INICIO, 0, (int)pn_categorias[cat]->aba);
		} else {
			ir(DESTINO_SAIR, 0, 0);
		}
	}
}

int main(int argc, char **argv) {
	recentes_ler();
	const char *abrir = NULL;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--dock")) {
			abrir = "aparencia/dock";
		} else if (!strcmp(argv[i], "--papel")) {
			abrir = "aparencia/papel";
		} else if (!strcmp(argv[i], "--secao") && i + 1 < argc) {
			abrir = argv[++i];
		}
	}
	int c, s;
	if (abrir && achar(abrir, &c, &s)) {
		ir_agora(c, s, 0);
	}
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Painel de controle", .app_id = "lumna-painel",
		.largura = 1160, .altura = 740 });
	if (!app) {
		return 1;
	}
	app_painel = app;
	return gv_app_correr(app, desenhar, NULL);
}
