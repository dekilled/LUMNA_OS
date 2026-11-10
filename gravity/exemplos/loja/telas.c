#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

/* ---------------- peças ---------------- */

static bool selo(Clay_ElementId id, const char *texto, bool ativo, bool clicavel) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(30) }, .padding = { 14, 14, 0, 0 }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ativo ? gv_t.acento : (clicavel && gv_por_cima(id)) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(15),
		.border = { .color = ativo ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, ativo ? gv_t.sobre_acento : gv_t.texto, GV_PESO_NORMAL);
	}
	return clicavel && gv_tocado(id);
}

static bool caixa(Clay_ElementId id, const char *texto, bool *marcada) {
	bool tocou = false;
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_IDI("lj-cx", id.id), { .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = *marcada ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(5), .border = { .color = *marcada ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			if (*marcada) {
				gv_icone("check", 16, gv_t.sobre_acento);
			}
		}
		gv_texto(gv_str(texto), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
	}
	if (gv_tocado(id)) {
		*marcada = !*marcada;
		tocou = true;
	}
	return tocou;
}

void estrelas(const lj_pacote *p) {
	if (p->avaliacoes <= 0) {
		gv_texto(CLAY_STRING("Sem avaliações"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	CLAY_AUTO_ID({ .layout = { .childGap = 5, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("star", 18, gv_t.acento);
		gv_texto(gv_fmt("%.1f (%d)", p->nota, p->avaliacoes), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
	}
}

/* O botão de uma linha ou cartão: Instalar, Atualizar ou Instalado. Devolve verdadeiro se tocou. */
bool botao_acao(Clay_ElementId id, const lj_pacote *p) {
	if (p->instalado && !p->atualizavel) {
		/* instalado: mostra o estado; tocar abre o detalhe (onde se remove) */
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(40) }, .padding = { 16, 16, 0, 0 }, .childGap = 8, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : gv_t.selecionado, .cornerRadius = CLAY_CORNER_RADIUS(20) }) {
			gv_icone("check", 18, gv_t.texto_secundario);
			gv_texto(CLAY_STRING("Instalado"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		if (gv_tocado(id)) {
			abrir_detalhe(p->nome);
		}
		return false;
	}
	bool tocou = gv_botao_ext(id, &(gv_botao_opcoes){ .texto = p->atualizavel ? "Atualizar" : "Instalar", .estilo = GV_BOTAO_CHEIO, .altura = 40, .desativado = L.agindo });
	if (tocou) {
		pedir_acao(LJ_INSTALAR, p->nome);
	}
	return tocou;
}

static void titulo_pagina(const char *titulo, const char *sub) {
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
		gv_texto(gv_str(titulo), gv_t.tamanho_titulo + 4, gv_t.texto, GV_PESO_NEGRITO);
		if (sub && *sub) {
			gv_texto(gv_str(sub), gv_t.tamanho_destaque - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

static void aviso_carga(void) {
	CLAY(CLAY_ID("lj-carga"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone(L.carregando ? "refresh" : "info", 56, gv_t.texto_suave);
		gv_texto(L.carregando ? CLAY_STRING("Lendo os repositórios…") : gv_str(L.erro_carga), gv_t.tamanho_destaque, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (!L.carregando && gv_botao_ext(CLAY_ID("lj-tentar"), &(gv_botao_opcoes){ .icone = "refresh", .texto = "Tentar de novo", .estilo = GV_BOTAO_ELEVADO, .altura = 42 })) {
			recarregar_catalogo();
		}
	}
}

/* ---------------- Explorar ---------------- */

static void cartao(const lj_pacote *p, int i, float largura) {
	Clay_ElementId id = CLAY_IDI("lj-cartao", (uint32_t)i);
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIXED(214) }, .padding = CLAY_PADDING_ALL(18), .childGap = 10 },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		bool abrir = gv_tocado(id);   /* antes do botão: o último candidato sob o ponteiro vence */
		CLAY(CLAY_IDI("lj-cab", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_IDI("lj-ico", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(60) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone_app(p->nome, 56);
			}
			CLAY(CLAY_IDI("lj-nm", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(p->exibicao), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_str(p->nome), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_IDI("lj-desc", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) } }, .clip = { .vertical = true } }) {
			gv_texto(gv_str(p->descricao), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("lj-rod", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
			selo(CLAY_IDI("lj-repo", (uint32_t)i), p->repo, false, false);
			CLAY(CLAY_IDI("lj-estr", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				estrelas(p);
			}
			botao_acao(CLAY_IDI("lj-cacao", (uint32_t)i), p);
		}
		if (abrir && !gv_por_cima(CLAY_IDI("lj-cacao", (uint32_t)i))) {
			abrir_detalhe(p->nome);
		}
	}
}

void tela_explorar(void) {
	if (!L.cat) {
		aviso_carga();
		return;
	}
	Clay_ElementId rol = CLAY_ID("lj-rolagem");
	CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
		.padding = { 28, 28, 4, 28 }, .childGap = 16 } })) {
		titulo_pagina("Explorar", "Aplicativos dos repositórios do Arch Linux");
		static const char *const ABAS[] = { "Todos", "Desenvolvimento", "Gráficos", "Internet", "Multimídia", "Jogos", "Sistema" };
		CLAY(CLAY_ID("lj-abas-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			if (gv_abas(CLAY_ID("lj-abas"), ABAS, 7, &L.ex_cat)) {
				gv_rolar_para(rol, 0, 0, false);
			}
		}
		CLAY(CLAY_ID("lj-ex-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Aplicativos em destaque"), gv_t.tamanho_titulo - 4, gv_t.texto, GV_PESO_NEGRITO);
			}
			gv_texto(CLAY_STRING("Ordenar por:"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY(CLAY_ID("lj-ex-ordem-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(190) } } }) {
				static const char *const ORD[] = { "Nome", "Tamanho", "Melhor avaliados" };
				gv_lista_suspensa(CLAY_ID("lj-ex-ordem"), ORD, 3, &L.ex_ordem);
			}
		}
		static const enum lj_ordem ORDEM[] = { LJ_NOME, LJ_TAMANHO, LJ_NOTA };
		static int *ids;
		static int cap;
		if (cap < lj_n(L.cat)) {
			ids = g_realloc(ids, sizeof(int) * (size_t)lj_n(L.cat));
			cap = lj_n(L.cat);
		}
		int n = lj_destaques(L.cat, (enum lj_cat)CLAMP(L.ex_cat, 0, LJ_NCATS - 1), ORDEM[CLAMP(L.ex_ordem, 0, 2)], ids, cap);
		float disp = gv_app_largura(L.app) - 236 - 56;
		int cols = CLAMP((int)((disp + 16) / (320 + 16)), 1, 4);
		float larg = (disp - 16 * (cols - 1)) / (float)cols;
		for (int k = 0; k < n; k += cols) {
			CLAY(CLAY_IDI("lj-grade", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 16 } }) {
				for (int c = 0; c < cols && k + c < n; c++) {
					cartao(lj_pacote_n(L.cat, ids[k + c]), k + c, larg);
				}
			}
		}
		if (n == 0) {
			gv_texto(CLAY_STRING("Nada em destaque nesta categoria ainda. Use a busca para achar qualquer pacote."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("lj-ex-rod"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
			gv_icone("star", 18, gv_t.texto_secundario);
			gv_texto(CLAY_STRING("Avaliações da comunidade LUMNA"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

/* ---------------- linhas das listas ---------------- */

static const char *tamanho_texto(int64_t b) {
	if (b >= 1024LL * 1024 * 1024) {
		return gv_fmt("%.1f GB", (double)b / (1024.0 * 1024 * 1024)).chars;
	}
	if (b >= 1024 * 1024) {
		return gv_fmt("%.1f MB", (double)b / (1024.0 * 1024)).chars;
	}
	return gv_fmt("%.0f KB", (double)b / 1024.0).chars;
}

/* a coluna de avaliação some quando a janela é estreita (o nome do pacote precisa de espaço) */
static bool mostra_aval(void) {
	return gv_app_largura(L.app) - 236 - 56 - 240 - 18 >= 800;
}

/* modo 0: Pacotes (tipo, repositório, avaliação, ação); 1: Instalados (versão, tamanho, remover); 2: Atualizações (versão → nova, atualizar) */
static void linha_pacote(const lj_pacote *p, int i, int modo) {
	Clay_ElementId id = CLAY_IDI("lj-linha", (uint32_t)i);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(92) }, .padding = { 16, 16, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		bool abrir = gv_tocado(id);
		gv_icone("diamond", 30, gv_t.acento);
		CLAY(CLAY_IDI("lj-l-nome", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 2, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.clip = { .horizontal = true } }) {
			gv_texto(gv_str(p->nome), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			CLAY(CLAY_IDI("lj-l-desc", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) } }, .clip = { .vertical = true } }) {
				gv_texto(gv_str(p->descricao), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		if (modo == 0) {
			CLAY(CLAY_IDI("lj-l-tipo", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(120) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				selo(CLAY_IDI("lj-s-tipo", (uint32_t)i), lj_nome_tipo(p->tipo), false, false);
			}
			CLAY(CLAY_IDI("lj-l-repo", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(100) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				selo(CLAY_IDI("lj-s-repo", (uint32_t)i), p->repo, false, false);
			}
			if (mostra_aval()) {
				CLAY(CLAY_IDI("lj-l-aval", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(130) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
					estrelas(p);
				}
			}
			CLAY(CLAY_IDI("lj-l-acao", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(120) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				botao_acao(CLAY_IDI("lj-acao", (uint32_t)i), p);
			}
		} else if (modo == 1) {
			CLAY(CLAY_IDI("lj-l-ver", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(150) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				gv_texto(gv_str(p->instalada[0] ? p->instalada : p->versao), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(CLAY_IDI("lj-l-repo", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(110) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				selo(CLAY_IDI("lj-s-repo", (uint32_t)i), p->repo, false, false);
			}
			CLAY(CLAY_IDI("lj-l-tam", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(90) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				gv_texto(p->tamanho ? gv_str(tamanho_texto(p->tamanho)) : CLAY_STRING("—"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(CLAY_IDI("lj-l-acao", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(130) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_botao_ext(CLAY_IDI("lj-acao", (uint32_t)i), &(gv_botao_opcoes){ .texto = "Remover", .estilo = GV_BOTAO_PERIGO, .altura = 40, .desativado = L.agindo })) {
					pedir_acao(LJ_REMOVER, p->nome);
				}
			}
		} else {
			CLAY(CLAY_IDI("lj-l-ver", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(250) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				gv_texto(gv_fmt("%s  →  %s", p->instalada[0] ? p->instalada : p->versao, p->nova_versao[0] ? p->nova_versao : "?"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			CLAY(CLAY_IDI("lj-l-repo", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(110) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				selo(CLAY_IDI("lj-s-repo", (uint32_t)i), p->repo, false, false);
			}
			CLAY(CLAY_IDI("lj-l-acao", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(130) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				botao_acao(CLAY_IDI("lj-acao", (uint32_t)i), p);
			}
		}
		if (abrir && !gv_por_cima(CLAY_IDI("lj-acao", (uint32_t)i))) {
			abrir_detalhe(p->nome);
		}
	}
}

static void cabecalho_tabela(int modo) {
	CLAY(CLAY_ID("lj-tab-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 16, 16, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		CLAY(CLAY_ID("lj-tc-ico"), { .layout = { .sizing = { CLAY_SIZING_FIXED(30) } } }) {}
		CLAY(CLAY_ID("lj-tc-pac"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(CLAY_STRING("Pacote"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		static const char *const COLS[3][3] = { { "Tipo", "Repositório", "Avaliação" }, { "Versão", "Repositório", "Tamanho" }, { "Versão", "Repositório", "" } };
		static const float LARG[3][3] = { { 120, 100, 130 }, { 150, 110, 90 }, { 250, 110, 0 } };
		for (int c = 0; c < 3; c++) {
			if (LARG[modo][c] > 0 && !(modo == 0 && c == 2 && !mostra_aval())) {
				CLAY(CLAY_IDI("lj-tc", (uint32_t)c), { .layout = { .sizing = { CLAY_SIZING_FIXED(LARG[modo][c]) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
					gv_texto(gv_str(COLS[modo][c]), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
			}
		}
		CLAY(CLAY_ID("lj-tc-acao"), { .layout = { .sizing = { CLAY_SIZING_FIXED(modo == 0 ? 120 : 130) } } }) {}
	}
}

static void lista_resultados(int modo) {
	int n = L.nres;
	int quantos = MIN(n, L.mostrar);
	for (int k = 0; k < quantos; k++) {
		const lj_pacote *p = lj_resultado(L.cat, L.res[k]);
		if (p) {
			linha_pacote(p, k, modo);
		}
	}
	if (n > quantos) {
		CLAY(CLAY_ID("lj-mais-cx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 8, 8 }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
			if (gv_botao_ext(CLAY_ID("lj-mais"), &(gv_botao_opcoes){ .texto = gv_fmt("Mostrar mais (%d restantes)", n - quantos).chars, .estilo = GV_BOTAO_ELEVADO, .altura = 42 })) {
				L.mostrar += 100;
			}
		}
	}
	if (n == 0) {
		CLAY(CLAY_ID("lj-vazio"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(160) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_texto(modo == 2 ? CLAY_STRING("Tudo em dia: nenhuma atualização.") : modo == 1 ? CLAY_STRING("Nada instalado com este filtro.") : CLAY_STRING("Nenhum pacote encontrado."), gv_t.tamanho_destaque, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

/* ---------------- Pacotes ---------------- */

static void filtros(void) {
	CLAY(CLAY_ID("lj-filtros"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(240), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		Clay_ElementId rol = CLAY_ID("lj-filtros-rol");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.padding = CLAY_PADDING_ALL(14), .childGap = 8 } })) {
			bool mudou = false;
			CLAY(CLAY_ID("lj-f-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Filtros"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
				}
				CLAY(CLAY_ID("lj-limpar"), { .layout = { .padding = { 8, 8, 6, 6 } } }) {
					gv_texto(CLAY_STRING("Limpar"), gv_t.tamanho_corpo, gv_t.acento, GV_PESO_NORMAL);
				}
				if (gv_tocado(CLAY_ID("lj-limpar"))) {
					lj_filtro_padrao(&L.f);
					L.por_idx = 2;
					L.cat_idx = 0;
					L.nota_idx = 0;
					L.ordem_idx = 0;
					L.busca[0] = '\0';
					L.mostrar = 100;
					mudou = true;
				}
			}
			gv_texto(CLAY_STRING("Buscar por"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			static const char *const POR[] = { "Nome do pacote", "Descrição", "Nome e descrição" };
			mudou |= gv_lista_suspensa(CLAY_ID("lj-por"), POR, 3, &L.por_idx);
			gv_texto(CLAY_STRING("Tipo"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			static const char *const TIPOS[] = { "Todos", "Aplicativo", "Utilitário", "Biblioteca", "Driver", "Kernel" };
			CLAY(CLAY_ID("lj-tipos"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
				for (int linha = 0; linha < 3; linha++) {
					CLAY(CLAY_IDI("lj-tipos-l", (uint32_t)linha), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
						for (int c = 0; c < 2; c++) {
							int t = linha * 2 + c;
							if (selo(CLAY_IDI("lj-tipo", (uint32_t)t), TIPOS[t], (t == 0 && L.f.tipo < 0) || (t > 0 && L.f.tipo == t - 1), true)) {
								L.f.tipo = t == 0 ? -1 : t - 1;
								mudou = true;
							}
						}
					}
				}
			}
			gv_texto(CLAY_STRING("Categoria"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			static const char *const CATS[] = { "Todas", "Desenvolvimento", "Gráficos", "Internet", "Multimídia", "Jogos", "Sistema e hardware" };
			mudou |= gv_lista_suspensa(CLAY_ID("lj-cat"), CATS, 7, &L.cat_idx);
			gv_texto(CLAY_STRING("Repositório"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			mudou |= caixa(CLAY_ID("lj-repo-core"), "core", &L.f.core);
			mudou |= caixa(CLAY_ID("lj-repo-extra"), "extra", &L.f.extra);
			mudou |= caixa(CLAY_ID("lj-repo-multilib"), "multilib", &L.f.multilib);
			if (L.cfg.aur) {
				mudou |= caixa(CLAY_ID("lj-repo-aur"), "AUR", &L.f.aur);
			}
			gv_texto(CLAY_STRING("Avaliação"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			static const char *const NOTAS[] = { "Todas as avaliações", "4 estrelas ou mais", "3 estrelas ou mais" };
			mudou |= gv_lista_suspensa(CLAY_ID("lj-nota"), NOTAS, 3, &L.nota_idx);
			mudou |= caixa(CLAY_ID("lj-sem-aval"), "Incluir sem avaliações", &L.f.sem_avaliacao);
			if (mudou) {
				L.mostrar = 100;
				L.aur_em = L.busca[0] ? gv_agora() + 0.5 : 0;
				marcar_sujo();
			}
		}
	}
}

void tela_pacotes(void) {
	if (!L.cat) {
		aviso_carga();
		return;
	}
	CLAY(CLAY_ID("lj-pac"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 28, 28, 0, 20 }, .childGap = 14 } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(L.busca[0] ? gv_fmt("Resultados para “%s”", L.busca) : CLAY_STRING("Pacotes"), gv_t.tamanho_titulo + 4, gv_t.texto, GV_PESO_NEGRITO);
		}
		CLAY(CLAY_ID("lj-pac-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
			filtros();
			CLAY(CLAY_ID("lj-res-col"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
				CLAY(CLAY_ID("lj-res-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(0) } } }) {
						gv_texto(gv_fmt("%d %s", L.nres, L.nres == 1 ? "pacote" : "pacotes"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
					}
					if (L.aur_ocupado) {
						gv_texto(CLAY_STRING("Buscando no AUR…"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					} else if (L.aur_erro[0] && L.busca[0] && L.cfg.aur) {
						CLAY(CLAY_ID("lj-aur-erro"), { .layout = { .sizing = { CLAY_SIZING_FIT(0) } } }) {
							gv_texto(gv_str(L.aur_erro), gv_t.tamanho_corpo - 1, gv_t.aviso, GV_PESO_NORMAL);
						}
					}
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }, .childGap = 8 } }) {
						gv_icone("sparkle", 18, gv_t.acento);
						gv_texto(CLAY_STRING("Avaliações da comunidade LUMNA"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
					}
					gv_texto(CLAY_STRING("Ordenar por:"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
					CLAY(CLAY_ID("lj-ordem-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(170) } } }) {
						static const char *const ORD[] = { "Relevância", "Nome", "Tamanho", "Avaliação" };
						if (gv_lista_suspensa(CLAY_ID("lj-ordem"), ORD, 4, &L.ordem_idx)) {
							marcar_sujo();
						}
					}
				}
				cabecalho_tabela(0);
				Clay_ElementId rol = CLAY_ID("lj-rolagem");
				CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 8 } })) {
					lista_resultados(0);
				}
			}
		}
	}
}

/* ---------------- Instalados e Atualizações ---------------- */

void tela_instalados(void) {
	if (!L.cat) {
		aviso_carga();
		return;
	}
	CLAY(CLAY_ID("lj-inst"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 28, 28, 0, 20 }, .childGap = 14 } }) {
		titulo_pagina("Instalados", "O que está neste computador");
		CLAY(CLAY_ID("lj-inst-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_fmt("%d %s", L.nres, L.nres == 1 ? "pacote" : "pacotes"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			}
			gv_texto(CLAY_STRING("Mostrar também bibliotecas e dependências"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			bool v = L.inst_todos;
			CLAY(CLAY_ID("lj-ver-deps-cx"), { .layout = { .sizing = { CLAY_SIZING_FIT(0) } } }) {
				if (gv_interruptor(CLAY_ID("lj-ver-deps"), &v)) {
					L.inst_todos = v;
					L.mostrar = 100;
					marcar_sujo();
				}
			}
		}
		cabecalho_tabela(1);
		Clay_ElementId rol = CLAY_ID("lj-rolagem");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 8 } })) {
			lista_resultados(1);
		}
	}
}

void tela_atualizacoes(void) {
	if (!L.cat) {
		aviso_carga();
		return;
	}
	CLAY(CLAY_ID("lj-atu"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 28, 28, 0, 20 }, .childGap = 14 } }) {
		titulo_pagina("Atualizações", L.natualiz ? "Há versões novas para o seu sistema" : "Tudo em dia");
		CLAY(CLAY_ID("lj-atu-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_fmt("%d %s", L.nres, L.nres == 1 ? "atualização" : "atualizações"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			}
			if (L.nres > 0 && gv_botao_ext(CLAY_ID("lj-atualizar-tudo"), &(gv_botao_opcoes){ .icone = "download", .texto = "Atualizar tudo", .estilo = GV_BOTAO_CHEIO, .altura = 42, .desativado = L.agindo })) {
				pedir_acao(LJ_ATUALIZAR_TUDO, "");
			}
		}
		cabecalho_tabela(2);
		Clay_ElementId rol = CLAY_ID("lj-rolagem");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 8 } })) {
			lista_resultados(2);
		}
	}
}

/* ---------------- Drivers e kernels (fase 2) ---------------- */

void tela_drivers(void) {
	CLAY(CLAY_ID("lj-drv"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 28, 28, 0, 20 }, .childGap = 16 } }) {
		titulo_pagina("Drivers e kernels", "Pacotes para o hardware deste computador");
		CLAY(CLAY_ID("lj-drv-aviso"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(20), .childGap = 8 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Esta parte chega na próxima fase."), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Ela vai detectar a sua placa de vídeo e o processador, mostrar só os drivers e kernels compatíveis e checar se cada um serve antes de instalar. Por ora, dá para achar pacotes de driver e kernel na aba Pacotes, filtrando por Tipo."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
}

/* ---------------- Configurações ---------------- */

void tela_config(void) {
	CLAY(CLAY_ID("lj-cfg"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 28, 28, 0, 20 }, .childGap = 14 } }) {
		titulo_pagina("Configurações", "Como a Loja busca e instala");
		bool v = L.cfg.aur;
		if (gv_linha(CLAY_ID("lj-cfg-aur"), "package", "Buscar também no AUR", "Pacotes da comunidade, não oficiais: compilados nesta máquina", &v)) {
			L.cfg.aur = v;
			gravar_cfg();
			if (!L.cfg.aur && L.cat) {
				lj_aur_limpar(L.cat);
			}
			marcar_sujo();
		}
		v = L.cfg.confirmar;
		if (gv_linha(CLAY_ID("lj-cfg-confirmar"), "shield", "Perguntar antes de instalar ou remover", "Instalar do AUR sempre pergunta, aconteça o que acontecer", &v)) {
			L.cfg.confirmar = v;
			gravar_cfg();
		}
		CLAY(CLAY_ID("lj-cfg-helper-lin"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 16, 16, 12, 12 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_icone("terminal", 24, gv_t.icone);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(CLAY_STRING("Programa do AUR"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
				gv_texto(L.helper ? gv_fmt("Em uso: %s", L.helper) : CLAY_STRING("Nenhum instalado (paru ou yay): não dá para instalar do AUR"), gv_t.tamanho_corpo - 1, L.helper ? gv_t.texto_secundario : gv_t.aviso, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("lj-cfg-helper-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(220) } } }) {
				static const char *const H[] = { "Automático", "paru", "yay" };
				int idx = !strcmp(L.cfg.helper, "paru") ? 1 : !strcmp(L.cfg.helper, "yay") ? 2 : 0;
				if (gv_lista_suspensa(CLAY_ID("lj-cfg-helper"), H, 3, &idx)) {
					g_strlcpy(L.cfg.helper, idx == 1 ? "paru" : idx == 2 ? "yay" : "", sizeof(L.cfg.helper));
					gravar_cfg();
				}
			}
		}
	}
}

/* ---------------- o detalhe ---------------- */

static void fato(const char *rotulo, const char *valor, int i) {
	CLAY(CLAY_IDI("lj-fato", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		CLAY(CLAY_IDI("lj-fato-r", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(130) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(valor), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
	}
}

void detalhe_dialogo(void) {
	const lj_pacote *p = L.cat ? lj_achar(L.cat, L.det) : NULL;
	if (!p) {
		L.det_aberto = false;
		return;
	}
	bool fechar = false, remover = false, instalar = false, site = false;
	CLAY(CLAY_ID("lj-det"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 90, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("lj-det-cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(620), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(24), .childGap = 12 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			CLAY(CLAY_ID("lj-det-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY(CLAY_ID("lj-det-ico"), { .layout = { .sizing = { CLAY_SIZING_FIXED(64), CLAY_SIZING_FIXED(64) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_icone_app(p->nome, 60);
				}
				CLAY(CLAY_ID("lj-det-nm"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
					gv_texto(gv_str(p->exibicao), gv_t.tamanho_titulo, gv_t.texto, GV_PESO_NEGRITO);
					gv_texto(gv_fmt("%s  ·  %s", p->nome, p->repo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				fechar = gv_botao_redondo(CLAY_ID("lj-det-fechar"), "close", 40, false, false);
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(p->descricao), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
			}
			if (!strcmp(p->repo, "aur")) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(10) }, .backgroundColor = gv_alfa(gv_t.aviso, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
					gv_texto(CLAY_STRING("Do AUR: não é um pacote oficial do Arch. O código é compilado nesta máquina."), gv_t.tamanho_corpo - 1, gv_t.aviso, GV_PESO_NORMAL);
				}
			}
			int n = 0;
			if (p->instalado && p->atualizavel) {
				fato("Instalada", gv_fmt("%s  →  %s disponível", p->instalada[0] ? p->instalada : p->versao, p->nova_versao).chars, n++);
			} else {
				fato("Versão", p->versao, n++);
			}
			fato("Tipo", gv_fmt("%s  ·  %s", lj_nome_tipo(p->tipo), lj_nome_cat(p->cat == LJ_TODAS ? LJ_SISTEMA : p->cat)).chars, n++);
			if (p->baixar) {
				fato("Download", tamanho_texto(p->baixar), n++);
			}
			if (p->tamanho) {
				fato("Instalado ocupa", tamanho_texto(p->tamanho), n++);
			}
			if (p->licenca[0]) {
				fato("Licença", p->licenca, n++);
			}
			if (p->mantenedor[0]) {
				fato("Mantenedor", gv_fmt("%s  ·  %d votos%s", p->mantenedor, p->votos, p->fora_de_data ? "  ·  marcado como desatualizado" : "").chars, n++);
			}
			if (p->url[0]) {
				CLAY(CLAY_ID("lj-det-url"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
					CLAY(CLAY_ID("lj-fato-site"), { .layout = { .sizing = { CLAY_SIZING_FIXED(130) } } }) {
						gv_texto(CLAY_STRING("Site"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
						gv_texto(gv_str(p->url), gv_t.tamanho_corpo, gv_t.acento, GV_PESO_NORMAL);
					}
				}
				site = gv_tocado(CLAY_ID("lj-det-url"));
			}
			CLAY(CLAY_ID("lj-det-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 8, 0 }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (p->instalado) {
					remover = gv_botao_ext(CLAY_ID("lj-det-remover"), &(gv_botao_opcoes){ .texto = "Remover", .estilo = GV_BOTAO_PERIGO, .altura = 42, .desativado = L.agindo });
				}
				if (!p->instalado || p->atualizavel) {
					instalar = gv_botao_ext(CLAY_ID("lj-det-acao"), &(gv_botao_opcoes){ .texto = p->atualizavel ? "Atualizar" : "Instalar", .estilo = GV_BOTAO_CHEIO, .altura = 42, .desativado = L.agindo });
				}
			}
		}
	}
	char nome[96];
	g_strlcpy(nome, p->nome, sizeof(nome));
	char url[200];
	g_strlcpy(url, p->url, sizeof(url));
	if (fechar) {
		L.det_aberto = false;
	} else if (remover) {
		pedir_acao(LJ_REMOVER, nome);
	} else if (instalar) {
		pedir_acao(LJ_INSTALAR, nome);
	} else if (site && url[0] && (g_str_has_prefix(url, "http://") || g_str_has_prefix(url, "https://"))) {
		char *argv[] = { "xdg-open", url, NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
	}
}
