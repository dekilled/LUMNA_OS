/* As listas do LUMNA Music: Minha biblioteca, Álbuns, Artistas, Pastas, o detalhe (álbum, artista, pasta, playlist) e os resultados da busca. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

#define LINHA 58.0f
#define ROL CLAY_ID("mu-rolagem")

void barra_de_rolagem_zero(void) {
	gv_rolar_para(ROL, 0, 0, false);
}

/* ---------------- a capa ---------------- */

void capa_quadrada(Clay_ElementId id, const mu_faixa *f, float lado, float raio) {
	const char *arq = M.cfg.capas && f ? capa_da_faixa(f) : "";
	Clay_ElementDeclaration d = { .layout = { .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.cornerRadius = CLAY_CORNER_RADIUS(raio), .backgroundColor = gv_t.elevada, .clip = { .horizontal = true, .vertical = true } };
	if (arq && *arq) {
		CLAY(id, gv_imagem(id, arq, GV_AJUSTE_COBRIR, d)) {}
	} else {
		CLAY(id, d) {
			gv_icone("music", lado * 0.42f, gv_t.texto_suave);
		}
	}
}

/* ---------------- a lista de faixas (a tabela) ---------------- */

static int grip_arrasto = -1;

static const char *tempo_curto(int s) {
	char b[24];
	mu_duracao_texto(s, b, sizeof(b));
	return gv_fmt("%s", b).chars;
}

/* y0: onde a tabela começa dentro da área rolável; grip: com a alça de arrastar (playlist) */
static void tabela_de_faixas(const int *idx, int n, float y0, bool grip) {
	if (n == 0) {
		return;
	}
	CLAY(CLAY_ID("mu-tab-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34) }, .padding = { grip ? 8 : 12, 12, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		if (grip) {
			CLAY(CLAY_ID("mu-tab-g"), { .layout = { .sizing = { CLAY_SIZING_FIXED(22) } } }) {}
		}
		CLAY(CLAY_ID("mu-tab-n"), { .layout = { .sizing = { CLAY_SIZING_FIXED(34) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
			gv_texto(CLAY_STRING("#"), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("mu-tab-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(46) } } }) {}
		CLAY(CLAY_ID("mu-tab-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(3) } } }) {
			gv_texto(CLAY_STRING("Título"), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("mu-tab-a"), { .layout = { .sizing = { CLAY_SIZING_GROW(2) } } }) {
			gv_texto(CLAY_STRING("Artista"), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("mu-tab-al"), { .layout = { .sizing = { CLAY_SIZING_GROW(2) } } }) {
			gv_texto(CLAY_STRING("Álbum"), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("mu-tab-d"), { .layout = { .sizing = { CLAY_SIZING_FIXED(64) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			gv_icone("clock", 18, gv_t.texto_secundario);
		}
		CLAY(CLAY_ID("mu-tab-m"), { .layout = { .sizing = { CLAY_SIZING_FIXED(38) } } }) {}
	}
	float topo = gv_rolagem_posicao(ROL).y;
	float alt_view = gv_caixa(ROL).height > 0 ? gv_caixa(ROL).height : 600;
	float ini = y0 + 34;
	int r0 = CLAMP((int)floorf((topo - alt_view * 0.5f - ini) / LINHA), 0, n), r1 = CLAMP((int)ceilf((topo + alt_view * 1.5f - ini) / LINHA), 0, n);
	if (r0 > 0) {
		CLAY(CLAY_ID("mu-tab-esp1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(r0 * LINHA) } } }) {}
	}
	const mu_faixa *atual = faixa_atual();
	for (int k = r0; k < r1; k++) {
		const mu_faixa *f = &M.ac.v[idx[k]];
		bool tocando_esta = atual && !strcmp(atual->caminho, f->caminho);
		Clay_ElementId id = CLAY_IDI("mu-lin", (uint32_t)k);
		bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-faixa"));
		if (pediu) {
			menu_da_faixa(idx[k]);
		}
		bool no_item = gv_tocado(id); /* antes do "...", que é chamado depois e fica com o toque que é dele */
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(LINHA) }, .padding = { grip ? 8 : 12, 12, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = tocando_esta ? gv_t.selecionado : grip_arrasto == k ? gv_t.sob_cursor : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			if (grip) {
				CLAY(CLAY_IDI("mu-grip", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(40) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_icone("grip", 18, gv_t.texto_suave);
				}
			}
			CLAY(CLAY_IDI("mu-lin-n", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_FIXED(34) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
				if (tocando_esta) {
					barras_tocando(CLAY_IDI("mu-lin-b", (uint32_t)k), 18, M.est.tocando);
				} else {
					gv_texto(gv_fmt("%d", k + 1), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
			capa_quadrada(CLAY_IDI("mu-lin-c", (uint32_t)k), f, 44, 6);
			CLAY(CLAY_IDI("mu-lin-t", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(3) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(f->titulo), gv_t.tamanho_corpo, tocando_esta ? gv_t.acento : gv_t.texto, GV_PESO_NORMAL);
			}
			CLAY(CLAY_IDI("mu-lin-a", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(2) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(f->artista), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(CLAY_IDI("mu-lin-al", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(2) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(f->album), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(CLAY_IDI("mu-lin-d", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_FIXED(64) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				gv_texto(gv_str(tempo_curto(f->duracao)), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			Clay_ElementId mais = CLAY_IDI("mu-lin-mais", (uint32_t)k);
			CLAY(CLAY_IDI("mu-lin-mx", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_FIXED(38), CLAY_SIZING_FIXED(38) } } }) {
				if (gv_botao_redondo(mais, "dots", 34, false, false)) {
					menu_da_faixa(idx[k]);
					gv_menu_pedir_no_ponteiro(CLAY_ID("menu-faixa"));
				}
			}
		}
		if (no_item && !pediu && grip_arrasto < 0) {
			tocar_lista(idx, n, k, M.cfg.aleatorio);
		}
	}
	if (n - r1 > 0) {
		CLAY(CLAY_ID("mu-tab-esp2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((n - r1) * LINHA) } } }) {}
	}
	/* arrastar a alça numa playlist: reescreve a ordem ao soltar */
	if (grip) {
		for (int k = r0; k < r1; k++) {
			Clay_BoundingBox gb = gv_caixa(CLAY_IDI("mu-grip", (uint32_t)k));
			if (grip_arrasto < 0 && gv_premido(CLAY_IDI("mu-lin", (uint32_t)k)) && gv_ponteiro_baixo() && gb.width > 0 && gv_ponteiro().x >= gb.x - 6 && gv_ponteiro().x <= gb.x + gb.width + 6) {
				grip_arrasto = k;
			}
		}
		if (grip_arrasto >= 0 && !gv_ponteiro_baixo()) {
			Clay_BoundingBox l0 = gv_caixa(CLAY_IDI("mu-lin", (uint32_t)r0));
			int alvo = l0.height > 0 ? CLAMP(r0 + (int)floorf((gv_ponteiro().y - l0.y) / LINHA), 0, n - 1) : grip_arrasto;
			if (alvo != grip_arrasto && grip_arrasto < n) {
				GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
				for (int k = 0; k < n; k++) {
					g_ptr_array_add(v, g_strdup(M.ac.v[idx[k]].caminho));
				}
				gpointer item = v->pdata[grip_arrasto];
				if (grip_arrasto < alvo) {
					memmove(&v->pdata[grip_arrasto], &v->pdata[grip_arrasto + 1], sizeof(gpointer) * (gsize)(alvo - grip_arrasto));
				} else {
					memmove(&v->pdata[alvo + 1], &v->pdata[alvo], sizeof(gpointer) * (gsize)(grip_arrasto - alvo));
				}
				v->pdata[alvo] = item;
				mu_playlist_gravar(M.det_playlist, (char *const *)v->pdata, (int)v->len);
				g_ptr_array_free(v, TRUE);
				recarregar_listas();
			}
			grip_arrasto = -1;
		}
	}
}

/* ---------------- os cabeçalhos ---------------- */

static void titulo_da_pagina(const char *titulo, const char *sub) {
	CLAY(CLAY_ID("mu-pag-t"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
		gv_texto(gv_str(titulo), 40, gv_t.texto, GV_PESO_NORMAL);
		if (sub && *sub) {
			gv_texto(gv_str(sub), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

static int duracao_total(const int *idx, int n) {
	int s = 0;
	for (int i = 0; i < n; i++) {
		s += M.ac.v[idx[i]].duracao;
	}
	return s;
}

static const char *sub_de_faixas(int n, int seg) {
	char d[32];
	mu_duracao_longa(seg, d, sizeof(d));
	return gv_fmt("%d %s · %s", n, n == 1 ? "faixa" : "faixas", d).chars;
}

static void botoes_de_tocar(const int *idx, int n) {
	CLAY(CLAY_ID("mu-det-bt"), { .layout = { .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		if (gv_botao_ext(CLAY_ID("mu-reproduzir"), &(gv_botao_opcoes){ .icone = "play", .texto = "Reproduzir", .estilo = GV_BOTAO_CHEIO, .altura = 48, .desativado = n == 0 })) {
			tocar_lista(idx, n, 0, M.cfg.aleatorio);
		}
		if (gv_botao_ext(CLAY_ID("mu-aleatorio-bt"), &(gv_botao_opcoes){ .icone = "shuffle", .texto = "Aleatório", .estilo = GV_BOTAO_CONTORNO, .altura = 48, .desativado = n == 0 })) {
			tocar_lista(idx, n, (int)g_random_int_range(0, n), true);
		}
		if (gv_botao_redondo(CLAY_ID("mu-mais-det"), "dots", 48, false, false)) {
			gv_popup_alternar(CLAY_ID("menu-det"));
		}
		GV_POPUP(CLAY_ID("menu-det"), CLAY_ID("mu-mais-det"), .alinhar = GV_ALINHAR_INICIO, .largura = 260) {
			CLAY(CLAY_ID("md-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
				if (gv_menu_item(CLAY_ID("md-seguir"), "arrow-up", "Tocar a seguir", NULL)) {
					fila_adicionar(idx, n, true);
					gv_popup_fechar(CLAY_ID("menu-det"));
				}
				if (gv_menu_item(CLAY_ID("md-fila"), "queue", "Adicionar à fila", NULL)) {
					fila_adicionar(idx, n, false);
					gv_popup_fechar(CLAY_ID("menu-det"));
				}
				if (M.vista == V_DETALHE && M.det_tipo == D_PLAYLIST && strcmp(M.det_playlist, MU_FAVORITAS)) {
					if (gv_menu_item(CLAY_ID("md-renomear"), "edit", "Renomear a playlist", NULL)) {
						abrir_dialogo_playlist(1, M.det_playlist);
						gv_popup_fechar(CLAY_ID("menu-det"));
					}
					if (gv_menu_item(CLAY_ID("md-apagar"), "trash", "Apagar a playlist", NULL)) {
						abrir_dialogo_playlist(2, M.det_playlist);
						gv_popup_fechar(CLAY_ID("menu-det"));
					}
				}
			}
		}
	}
}

static void vazio(const char *msg, bool com_botao) {
	CLAY(CLAY_ID("mu-vazio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 60, 0 }, .childGap = 16, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
		gv_icone("music", 56, gv_t.texto_suave);
		gv_texto(gv_str(msg), gv_t.tamanho_corpo + 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (com_botao && gv_botao_ext(CLAY_ID("mu-vazio-add"), &(gv_botao_opcoes){ .icone = "folder-plus", .texto = "Adicionar músicas", .estilo = GV_BOTAO_CHEIO, .altura = 46 })) {
			abrir_dialogo_pasta();
		}
	}
}

static void pagina(void (*corpo)(void)) {
	CLAY(ROL, gv_rolagem(ROL, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
		.padding = { 20, 20, 4, 24 }, .childGap = 14 } })) {
		corpo();
	}
}

static const char *estado_da_carga(void) {
	return gv_fmt("Procurando as suas músicas… %d de %d", M.prog_feitos, M.prog_total).chars;
}

/* ---------------- Minha biblioteca ---------------- */

static int *todas;
static int ntodas;

static void corpo_biblioteca(void) {
	CLAY(CLAY_ID("mu-pag-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(150) }, .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_ID("mu-pag-tx"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
			titulo_da_pagina("Minha biblioteca", M.carregando && M.ac.n == 0 ? estado_da_carga() : sub_de_faixas(ntodas, duracao_total(todas, ntodas)));
			botoes_de_tocar(todas, ntodas);
		}
	}
	if (ntodas == 0) {
		vazio(M.carregando ? estado_da_carga() : "Sua biblioteca está vazia.", !M.carregando);
	} else {
		tabela_de_faixas(todas, ntodas, 4 + 150 + 14, false);
	}
}

void tela_biblioteca(void) {
	pagina(corpo_biblioteca);
}

/* ---------------- Álbuns ---------------- */

static void cartao_de_album(int k, float lado, bool lista) {
	const mu_grupo *g = &M.albuns.v[k];
	const mu_faixa *f = g->primeira >= 0 ? &M.ac.v[g->primeira] : NULL;
	Clay_ElementId id = CLAY_IDI("mu-album", (uint32_t)k);
	if (lista) {
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(LINHA + 6) }, .padding = { 10, 12, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			capa_quadrada(CLAY_IDI("mu-album-c", (uint32_t)k), f, 48, 6);
			CLAY(CLAY_IDI("mu-album-t", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(3) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(g->nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
			CLAY(CLAY_IDI("mu-album-a", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(2) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(g->artista), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(CLAY_IDI("mu-album-y", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_FIXED(70) } } }) {
				gv_texto(g->ano ? gv_fmt("%d", g->ano) : gv_str(""), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(CLAY_IDI("mu-album-n", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_FIXED(90) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				gv_texto(gv_fmt("%d %s", g->n, g->n == 1 ? "faixa" : "faixas"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	} else {
		CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado + 56) }, .childGap = 8 } }) {
			CLAY(CLAY_IDI("mu-album-capa", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado) } } }) {
				capa_quadrada(CLAY_IDI("mu-album-c", (uint32_t)k), f, lado, 10);
			}
			CLAY(CLAY_IDI("mu-album-tx", (uint32_t)k), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(g->nome), gv_t.tamanho_corpo + 1, gv_por_cima(id) ? gv_t.acento : gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_str(g->artista), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
	if (gv_tocado(id)) {
		abrir_detalhe(D_ALBUM, k, NULL);
	}
}

static void grade_de_albuns(const mu_grupos *gs, bool lista, float y_ini, bool artistas) {
	float topo = gv_rolagem_posicao(ROL).y, alt_view = gv_caixa(ROL).height > 0 ? gv_caixa(ROL).height : 600;
	float larg = gv_caixa(ROL).width > 0 ? gv_caixa(ROL).width - 40 - 14 : 900;
	const float GAP = 22;
	int cols = lista ? 1 : MAX(1, (int)((larg + GAP) / (M.cfg.tam_capa + GAP)));
	float lado = lista ? 0 : floorf((larg - GAP * (cols - 1)) / cols);
	float alt_linha = lista ? LINHA + 6 : lado + 56 + GAP;
	int linhas = (gs->n + cols - 1) / cols;
	int r0 = CLAMP((int)floorf((topo - alt_view * 0.5f - y_ini) / alt_linha), 0, linhas), r1 = CLAMP((int)ceilf((topo + alt_view * 1.5f - y_ini) / alt_linha), 0, linhas);
	if (r0 > 0) {
		CLAY(CLAY_ID("mu-gr-esp1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(r0 * alt_linha) } } }) {}
	}
	for (int r = r0; r < r1; r++) {
		CLAY(CLAY_IDI("mu-gr-linha", (uint32_t)r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(alt_linha) }, .childGap = GAP } }) {
			for (int c = 0; c < cols; c++) {
				int k = r * cols + c;
				if (k >= gs->n) {
					break;
				}
				if (artistas) {
					const mu_grupo *g = &gs->v[k];
					Clay_ElementId id = CLAY_IDI("mu-artista", (uint32_t)k);
					CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado + 56) }, .childGap = 8, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
						capa_quadrada(CLAY_IDI("mu-artista-c", (uint32_t)k), g->primeira >= 0 ? &M.ac.v[g->primeira] : NULL, lado, lado / 2);
						gv_texto(gv_str(g->nome), gv_t.tamanho_corpo + 1, gv_por_cima(id) ? gv_t.acento : gv_t.texto, GV_PESO_MEDIO);
						gv_texto(gv_fmt("%d %s", g->n, g->n == 1 ? "faixa" : "faixas"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
					if (gv_tocado(id)) {
						abrir_detalhe(D_ARTISTA, k, NULL);
					}
				} else {
					cartao_de_album(k, lado, lista);
				}
			}
		}
	}
	if (linhas - r1 > 0) {
		CLAY(CLAY_ID("mu-gr-esp2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((linhas - r1) * alt_linha) } } }) {}
	}
}

static void corpo_albuns(void) {
	static const char *const ORDENS[] = { "Ordenar por: Título", "Ordenar por: Artista", "Ordenar por: Ano" };
	CLAY(CLAY_ID("mu-pag-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(84) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		titulo_da_pagina("Álbuns", M.carregando && M.ac.n == 0 ? estado_da_carga() : gv_fmt("%d %s na biblioteca", M.albuns.n, M.albuns.n == 1 ? "álbum" : "álbuns").chars);
		CLAY(CLAY_ID("mu-ordem-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(240) } } }) {
			int o = M.cfg.ordem_albuns;
			if (gv_lista_suspensa(CLAY_ID("mu-ordem"), ORDENS, 3, &o)) {
				M.cfg.ordem_albuns = o;
				mu_grupos_liberar(&M.albuns);
				mu_albuns(&M.ac, o, &M.albuns);
				salvar_cfg();
			}
		}
		if (gv_botao_redondo(CLAY_ID("mu-vista-grade"), "grid", 42, M.cfg.vista_albuns == 0, false)) {
			M.cfg.vista_albuns = 0;
			salvar_cfg();
		}
		if (gv_botao_redondo(CLAY_ID("mu-vista-lista"), "list", 42, M.cfg.vista_albuns == 1, false)) {
			M.cfg.vista_albuns = 1;
			salvar_cfg();
		}
	}
	if (M.albuns.n == 0) {
		vazio(M.carregando ? estado_da_carga() : "Nenhum álbum por aqui.", !M.carregando);
	} else {
		grade_de_albuns(&M.albuns, M.cfg.vista_albuns == 1, 4 + 84 + 14, false);
	}
}

void tela_albuns(void) {
	pagina(corpo_albuns);
}

/* ---------------- Artistas ---------------- */

static void corpo_artistas(void) {
	CLAY(CLAY_ID("mu-pag-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(84) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		titulo_da_pagina("Artistas", gv_fmt("%d %s", M.artistas.n, M.artistas.n == 1 ? "artista" : "artistas").chars);
	}
	if (M.artistas.n == 0) {
		vazio(M.carregando ? estado_da_carga() : "Nenhum artista por aqui.", !M.carregando);
	} else {
		grade_de_albuns(&M.artistas, false, 4 + 84 + 14, true);
	}
}

void tela_artistas(void) {
	pagina(corpo_artistas);
}

/* ---------------- Pastas ---------------- */

static void corpo_pastas(void) {
	CLAY(CLAY_ID("mu-pag-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(84) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		titulo_da_pagina("Pastas", gv_fmt("%d %s com música", M.pastas.n, M.pastas.n == 1 ? "pasta" : "pastas").chars);
	}
	if (M.pastas.n == 0) {
		vazio(M.carregando ? estado_da_carga() : "Nenhuma pasta com música.", !M.carregando);
	}
	for (int k = 0; k < M.pastas.n && k < 40; k++) {
		const mu_grupo *g = &M.pastas.v[k];
		Clay_ElementId id = CLAY_IDI("mu-pasta", (uint32_t)k);
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(LINHA + 6) }, .padding = { 14, 14, 0, 0 }, .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			gv_icone("folder", 28, gv_t.acento);
			CLAY(CLAY_IDI("mu-pasta-tx", (uint32_t)k), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(g->nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
				gv_texto(gv_str(g->artista), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			gv_texto(gv_fmt("%d %s", g->n, g->n == 1 ? "faixa" : "faixas"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (gv_tocado(id)) {
			abrir_detalhe(D_PASTA, k, NULL);
		}
	}
}

void tela_pastas(void) {
	pagina(corpo_pastas);
}

/* ---------------- o detalhe ---------------- */

static char det_chave[4096], det_titulo[256], det_sub[256];

static void definir_detalhe(void) {
	g_free(M.det_idx);
	M.det_idx = NULL;
	M.det_n = 0;
	det_titulo[0] = det_sub[0] = 0;
	if (M.det_tipo == D_PLAYLIST) {
		GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
		mu_playlist_ler(M.det_playlist, v);
		M.det_idx = g_new(int, v->len + 1);
		for (guint i = 0; i < v->len; i++) {
			int k = acervo_indice(v->pdata[i]);
			if (k >= 0) {
				M.det_idx[M.det_n++] = k;
			}
		}
		int faltam = (int)v->len - M.det_n;
		g_strlcpy(det_titulo, M.det_playlist, sizeof(det_titulo));
		g_snprintf(det_sub, sizeof(det_sub), "%s%s", sub_de_faixas(M.det_n, duracao_total(M.det_idx, M.det_n)), faltam > 0 ? gv_fmt(" · %d fora da biblioteca", faltam).chars : "");
		g_ptr_array_free(v, TRUE);
		return;
	}
	const mu_grupos *gs = M.det_tipo == D_ALBUM ? &M.albuns : M.det_tipo == D_ARTISTA ? &M.artistas : &M.pastas;
	int k = M.det_grupo;
	if (det_chave[0]) { /* depois de recarregar a biblioteca, acha o grupo pela chave */
		k = -1;
		for (int i = 0; i < gs->n; i++) {
			char *c = M.det_tipo == D_ALBUM ? g_strdup_printf("%s\x01%s", gs->v[i].nome, gs->v[i].artista) : g_strdup(M.det_tipo == D_ARTISTA ? gs->v[i].nome : gs->v[i].artista);
			if (!strcmp(c, det_chave)) {
				k = i;
			}
			g_free(c);
			if (k >= 0) {
				break;
			}
		}
	}
	if (k < 0 || k >= gs->n) {
		M.det_n = 0;
		g_strlcpy(det_titulo, "Não encontrado", sizeof(det_titulo));
		return;
	}
	M.det_grupo = k;
	M.det_idx = g_new(int, (guint)gs->v[k].n + 1);
	memcpy(M.det_idx, gs->v[k].idx, sizeof(int) * (gsize)gs->v[k].n);
	M.det_n = gs->v[k].n;
	g_strlcpy(det_titulo, gs->v[k].nome, sizeof(det_titulo));
	g_snprintf(det_sub, sizeof(det_sub), "%s%s%s", M.det_tipo == D_ALBUM ? gs->v[k].artista : "", M.det_tipo == D_ALBUM ? " · " : "", sub_de_faixas(M.det_n, gs->v[k].duracao));
	if (M.det_tipo == D_ALBUM && gs->v[k].ano) {
		char a[24];
		g_snprintf(a, sizeof(a), " · %d", gs->v[k].ano);
		g_strlcat(det_sub, a, sizeof(det_sub));
	}
}

void abrir_detalhe(int tipo, int grupo, const char *playlist) {
	M.det_tipo = tipo;
	M.det_grupo = grupo;
	det_chave[0] = 0;
	M.det_playlist[0] = 0;
	if (tipo == D_PLAYLIST) {
		g_strlcpy(M.det_playlist, playlist ? playlist : MU_FAVORITAS, sizeof(M.det_playlist));
	} else {
		const mu_grupos *gs = tipo == D_ALBUM ? &M.albuns : tipo == D_ARTISTA ? &M.artistas : &M.pastas;
		if (grupo >= 0 && grupo < gs->n) {
			char *c = tipo == D_ALBUM ? g_strdup_printf("%s\x01%s", gs->v[grupo].nome, gs->v[grupo].artista) : g_strdup(tipo == D_ARTISTA ? gs->v[grupo].nome : gs->v[grupo].artista);
			g_strlcpy(det_chave, c, sizeof(det_chave));
			g_free(c);
		}
	}
	if (M.vista != V_DETALHE) {
		M.vista_antes = M.vista;
	}
	M.vista = V_DETALHE;
	M.busca[0] = 0;
	definir_detalhe();
	barra_de_rolagem_zero();
}

void recarregar_listas(void) {
	g_free(todas);
	todas = g_new(int, (guint)M.ac.n + 1);
	ntodas = M.ac.n;
	for (int i = 0; i < M.ac.n; i++) {
		todas[i] = i;
	}
	if (M.vista == V_DETALHE) {
		definir_detalhe();
	}
	if (M.busca[0]) {
		g_free(M.busca_idx);
		M.busca_idx = g_new(int, (guint)M.ac.n + 1);
		M.busca_n = mu_buscar(&M.ac, M.busca, M.busca_idx);
	}
	listar_playlists();
}

static void corpo_detalhe(void) {
	const mu_faixa *capa = M.det_n > 0 ? &M.ac.v[M.det_idx[0]] : NULL;
	CLAY(CLAY_ID("mu-det-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(250) }, .padding = { 6, 0, 0, 0 }, .childGap = 28, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		if (M.det_tipo == D_PLAYLIST && !capa) {
			CLAY(CLAY_ID("mu-det-vazio-capa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(220), CLAY_SIZING_FIXED(220) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(14) }) {
				gv_icone(!strcmp(M.det_playlist, MU_FAVORITAS) ? "heart" : "music", 80, gv_t.texto_suave);
			}
		} else {
			capa_quadrada(CLAY_ID("mu-det-capa"), capa, 220, 14);
		}
		CLAY(CLAY_ID("mu-det-tx"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 }, .clip = { .horizontal = true } }) {
			CLAY(CLAY_ID("mu-det-tit"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(det_titulo), 52, gv_t.texto, GV_PESO_MEDIO);
			}
			gv_texto(gv_str(det_sub), gv_t.tamanho_corpo + 3, gv_t.acento_cursor, GV_PESO_NORMAL);
			botoes_de_tocar(M.det_idx, M.det_n);
		}
	}
	if (M.det_n == 0) {
		vazio(M.det_tipo == D_PLAYLIST ? "Esta playlist está vazia. Use o \"...\" de uma faixa para adicionar." : "Nada por aqui.", false);
	} else {
		tabela_de_faixas(M.det_idx, M.det_n, 4 + 250 + 14, M.det_tipo == D_PLAYLIST);
	}
}

void tela_detalhe(void) {
	pagina(corpo_detalhe);
}

/* ---------------- a busca ---------------- */

static void corpo_busca(void) {
	CLAY(CLAY_ID("mu-pag-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(96) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		titulo_da_pagina(gv_fmt("Resultados para \"%s\"", M.busca).chars, gv_fmt("%d %s", M.busca_n, M.busca_n == 1 ? "faixa" : "faixas").chars);
	}
	if (M.busca_n == 0) {
		vazio("Nada encontrado. Tente outra palavra.", false);
	} else {
		tabela_de_faixas(M.busca_idx, M.busca_n, 4 + 96 + 14, false);
	}
}

void tela_busca(void) {
	pagina(corpo_busca);
}

void abrir_detalhe_de_faixas(const char *titulo, const char *sub, const int *idx, int n) {
	(void)titulo; (void)sub; (void)idx; (void)n;
}
