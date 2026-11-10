/* As mídias por data (grade e lista), com seleção: só as linhas à vista entram no desenho (a biblioteca pode ter dezenas de milhares). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "arquivos.h"

#define GAP 6
#define CAB_GRUPO 48
#define ALT_LINHA 60

static bool ponteiro_em(Clay_BoundingBox b) {
	Clay_Vector2 p = gv_ponteiro();
	return b.width > 0 && p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
}

/* a miniatura (ou o espaço dela, enquanto não sai) num quadrado de lado × lado */
void miniatura(const gl_midia *m, float lado, Clay_ElementId id, bool arredondar) {
	const char *arq = gl_miniatura(m, G.cfg.qualidade, ao_miniatura_pronta, NULL);
	float raio = arredondar ? 10 : 6;
	if (arq) {
		CLAY(id, gv_imagem(id, arq, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado) } },
			.cornerRadius = CLAY_CORNER_RADIUS(raio), .backgroundColor = gv_t.elevada })) {}
		return;
	}
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(raio) }) {
		gv_icone(m->tipo == GL_VIDEO ? "video" : "image", fminf(lado * 0.32f, 40), gv_t.texto_suave);
	}
}

/* o quadradinho de marcar (canto da miniatura) */
static bool caixa_de_marcar(Clay_ElementId id, bool marcada, bool visivel, float x, float y) {
	if (!visivel && !marcada) {
		return false;
	}
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = marcada ? gv_t.acento : gv_alfa(gv_cor(0x000000ff), 0.45f), .cornerRadius = CLAY_CORNER_RADIUS(7),
		.border = { .color = marcada ? gv_t.acento : gv_alfa(gv_t.texto, 0.7f), .width = GV_BORDA(2) },
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
			.offset = { x, y }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE } }) {
		if (marcada) {
			gv_icone("check", 16, gv_t.sobre_acento);
		}
	}
	return gv_tocado(id);
}

static void selo_de_video(const gl_midia *m, float margem) {
	char t[16];
	gl_duracao_texto(gl_duracao_conhecida(m->caminho, m->mtime), t, sizeof(t));
	CLAY_AUTO_ID({ .layout = { .padding = { 8, 9, 4, 4 }, .childGap = 5, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = gv_alfa(gv_cor(0x000000ff), 0.62f),
		.cornerRadius = CLAY_CORNER_RADIUS(8),
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM },
			.offset = { -margem, -margem }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
		gv_icone("play", 12, gv_cor(0xffffffff));
		if (t[0]) {
			gv_texto(gv_str(t), 12, gv_cor(0xffffffff), GV_PESO_MEDIO);
		}
	}
}

static void selo_de_favorita(float margem) {
	CLAY_AUTO_ID({ .layout = { .padding = CLAY_PADDING_ALL(4) }, .backgroundColor = gv_alfa(gv_cor(0x000000ff), 0.55f), .cornerRadius = CLAY_CORNER_RADIUS(8),
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM },
			.offset = { margem, -margem }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
		gv_icone("star", 14, gv_cor(0xfbbf24ff));
	}
}

/* um clique numa mídia: Shift faz um intervalo, Ctrl marca ou desmarca, com a seleção já ativa só marca, e sem nada selecionado abre */
static void clicou_na_midia(int k) {
	uint32_t mods = gv_modificadores();
	const gl_midia *m = midia_vis(k);
	if ((mods & GV_MOD_SHIFT) && G.ancora >= 0 && G.ancora < G.nvis) {
		if (!(mods & GV_MOD_CTRL)) {
			sel_limpar();
		}
		sel_intervalo(G.ancora, k);
	} else if ((mods & GV_MOD_CTRL) || sel_n() > 0) {
		sel_alternar(m->caminho);
		G.ancora = k;
	} else {
		G.ancora = k;
		abrir_midia(k);
	}
}

static void celula(int k, float lado) {
	const gl_midia *m = midia_vis(k);
	Clay_ElementId id = CLAY_IDI("g-cel", (uint32_t)k);
	Clay_ElementId chk = CLAY_IDI("g-chk", (uint32_t)k);
	bool marcada = g_hash_table_contains(G.sel, m->caminho);
	bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-midia"));
	if (pediu && ponteiro_em(gv_caixa(id))) {
		G.ancora = k;
		menu_da_midia(k);
	}
	bool sobre = gv_por_cima(id) || gv_por_cima(chk);
	bool algum = sel_n() > 0;
	bool marcou = false;
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado) } }, .cornerRadius = CLAY_CORNER_RADIUS(11),
		.border = { .color = marcada ? gv_t.acento : gv_alfa(gv_t.texto, sobre ? 0.25f : 0), .width = GV_BORDA(marcada ? 3 : 1) } }) {
		miniatura(m, lado, CLAY_IDI("g-mini", (uint32_t)k), true);
		marcou = caixa_de_marcar(chk, marcada, sobre || algum, 8, 8);
		if (m->tipo == GL_VIDEO) {
			selo_de_video(m, 8);
		}
		if (m->favorita) {
			selo_de_favorita(8);
		}
		if (G.cfg.nomes) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 8, 12, 4 } }, .clip = { .horizontal = true },
				.backgroundColor = gv_alfa(gv_cor(0x000000ff), 0.5f), .cornerRadius = CLAY_CORNER_RADIUS(8),
				.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM },
					.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
				gv_texto(gv_str(m->nome), 11, gv_cor(0xffffffff), GV_PESO_NORMAL);
			}
		}
	}
	if (marcou) {
		sel_alternar(m->caminho);
		G.ancora = k;
	} else if (gv_tocado(id)) {
		clicou_na_midia(k);
	}
}

static void linha_da_lista(int k, int par) {
	const gl_midia *m = midia_vis(k);
	Clay_ElementId id = CLAY_IDI("g-lin", (uint32_t)k);
	Clay_ElementId chk = CLAY_IDI("g-lchk", (uint32_t)k);
	bool marcada = g_hash_table_contains(G.sel, m->caminho);
	bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-midia"));
	if (pediu && ponteiro_em(gv_caixa(id))) {
		G.ancora = k;
		menu_da_midia(k);
	}
	bool marcou = false;
	char data[48], tam[32], dur[16];
	struct tm tm;
	localtime_r(&m->data, &tm);
	strftime(data, sizeof(data), "%d/%m/%Y %H:%M", &tm);
	ar_tamanho_humano(m->tamanho, tam, sizeof(tam));
	gl_duracao_texto(m->tipo == GL_VIDEO ? gl_duracao_conhecida(m->caminho, m->mtime) : -1, dur, sizeof(dur));
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ALT_LINHA - 4) }, .padding = { 10, 14, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = marcada ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : par ? gv_alfa(gv_t.texto, 0.025f) : (Clay_Color){ 0, 0, 0, 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(10) }) {
		CLAY(chk, { .layout = { .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = marcada ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(7),
			.border = { .color = marcada ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(2) } }) {
			if (marcada) {
				gv_icone("check", 15, gv_t.sobre_acento);
			}
		}
		marcou = gv_tocado(chk);
		miniatura(m, 40, CLAY_IDI("g-lmini", (uint32_t)k), false);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(m->nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			char *pasta = g_path_get_dirname(m->caminho);
			gv_texto(gv_str(pasta), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
			g_free(pasta);
		}
		if (m->favorita) {
			gv_icone("star", 16, gv_cor(0xfbbf24ff));
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
			gv_texto(gv_str(data), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(80) } } }) {
			gv_texto(gv_str(tam), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(64) } } }) {
			gv_texto(gv_str(m->tipo == GL_VIDEO ? (dur[0] ? dur : "Vídeo") : "Foto"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	if (marcou) {
		sel_alternar(m->caminho);
		G.ancora = k;
	} else if (gv_tocado(id)) {
		clicou_na_midia(k);
	}
}

static void espaco(Clay_ElementId id, float altura) {
	if (altura >= 1) {
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(altura) } } }) {}
	}
}

static void vazio(void) {
	const char *icone = "image", *titulo = "Nenhuma mídia por aqui", *dica = "";
	if (G.carregando && G.acervo.n == 0) {
		titulo = "Procurando suas fotos e vídeos…";
	} else if (G.busca[0]) {
		icone = "search";
		titulo = "Nada encontrado";
		dica = "Tente outra palavra.";
	} else if (G.vista == V_FAVORITOS) {
		icone = "star";
		titulo = "Nenhum favorito ainda";
		dica = "Marque uma foto ou vídeo e escolha Favoritar.";
	} else if (G.vista == V_LIXEIRA) {
		icone = "trash";
		titulo = "A lixeira está vazia";
	} else if (G.vista == V_ALBUM) {
		icone = "folder";
		titulo = "Este álbum está vazio";
		dica = "Selecione mídias na biblioteca e escolha Adicionar ao álbum.";
	} else if (G.vista == V_TODAS && G.cfg.npastas == 0) {
		dica = "Escolha as pastas em Configurações > Biblioteca.";
	} else if (G.vista == V_TODAS) {
		dica = "Coloque fotos e vídeos nas pastas da biblioteca, ou use Importar.";
	}
	CLAY(CLAY_ID("g-vazio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(360) }, .childGap = 10,
		.childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone(icone, 44, gv_t.texto_suave);
		gv_texto(gv_str(titulo), gv_t.tamanho_destaque, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (dica[0]) {
			gv_texto(gv_str(dica), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
}

void grade_desenhar(Clay_ElementId rolagem) {
	if (G.nvis == 0) {
		vazio();
		return;
	}
	float topo = gv_rolagem_posicao(rolagem).y;
	float alt_view = gv_caixa(rolagem).height > 0 ? gv_caixa(rolagem).height : 600;
	float larg = gv_caixa(rolagem).width > 0 ? gv_caixa(rolagem).width - 48 - 14 : 900;
	bool lista = G.cfg.vista == GL_VISTA_LISTA;
	int cols = lista ? 1 : (int)((larg + GAP) / (G.cfg.tam + GAP));
	cols = cols < 1 ? 1 : cols;
	float lado = lista ? 0 : floorf((larg - GAP * (cols - 1)) / cols);
	float alt_linha = lista ? ALT_LINHA : lado + GAP;
	float y = 0; /* o começo do grupo, na área rolável (sem o enchimento de cima) */
	for (int g = 0; g < G.ngrupos; g++) {
		const gl_grupo *gr = &G.grupos[g];
		int linhas = (gr->n + cols - 1) / cols;
		float cab = gr->rotulo[0] ? CAB_GRUPO : 0;
		float alt_grupo = cab + linhas * alt_linha + 6;
		float margem = alt_view * 0.5f;
		if (y + alt_grupo < topo - margem || y > topo + alt_view + margem) {
			espaco(CLAY_IDI("g-esp-g", (uint32_t)g), alt_grupo);
			y += alt_grupo;
			continue;
		}
		if (cab > 0) {
			CLAY(CLAY_IDI("g-cab", (uint32_t)g), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(cab) }, .padding = { 2, 0, 10, 8 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(gv_str(gr->rotulo), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_NORMAL);
			}
		}
		float y0 = y + cab;
		int r0 = (int)floorf((topo - margem - y0) / alt_linha), r1 = (int)ceilf((topo + alt_view + margem - y0) / alt_linha);
		r0 = r0 < 0 ? 0 : r0;
		r1 = r1 > linhas ? linhas : r1;
		espaco(CLAY_IDI("g-esp-c", (uint32_t)g), r0 * alt_linha);
		for (int r = r0; r < r1; r++) {
			CLAY(CLAY_IDI("g-linha", (uint32_t)(g * 100000 + r)), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(alt_linha) }, .childGap = GAP } }) {
				for (int c = 0; c < cols; c++) {
					int i = r * cols + c;
					if (i >= gr->n) {
						break;
					}
					if (lista) {
						linha_da_lista(gr->inicio + i, (gr->inicio + i) & 1);
					} else {
						celula(gr->inicio + i, lado);
					}
				}
			}
		}
		espaco(CLAY_IDI("g-esp-b", (uint32_t)g), (linhas - r1) * alt_linha + 6);
		y += alt_grupo;
	}
}
