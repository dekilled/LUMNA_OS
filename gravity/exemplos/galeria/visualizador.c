/* Uma foto na janela inteira: zoom (roda, + e -, toque duplo), arrastar quando está maior que a tela, setas para a anterior e a seguinte, detalhes. Vídeos não
 * entram aqui: tocar num vídeo o abre no player. */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "arquivos.h"

static float zoom = 1;          /* 1 = ajustada à tela (ou o tamanho real, se a pessoa pediu) */
static float pan_x, pan_y;
static int nat_w, nat_h;        /* o tamanho da foto, em pixels */
static char aberta_caminho[4096];
static bool detalhes;
static Clay_Vector2 anterior = { -1, -1 };
static double ultimo_toque;

static void carregar_tamanho(const char *caminho) {
	nat_w = nat_h = 0;
	gdk_pixbuf_get_file_info(caminho, &nat_w, &nat_h);
	g_strlcpy(aberta_caminho, caminho, sizeof(aberta_caminho));
}

void visualizador_abrir(int k) {
	G.aberta = k;
	G.tela = T_VISUALIZADOR;
	zoom = 1;
	pan_x = pan_y = 0;
	detalhes = false;
	carregar_tamanho(midia_vis(k)->caminho);
}

void visualizador_fechar(void) {
	G.tela = T_BIBLIOTECA;
}

bool visualizador_reposicionar(void) {
	for (int k = 0; k < G.nvis; k++) {
		if (!strcmp(midia_vis(k)->caminho, aberta_caminho)) {
			G.aberta = k;
			return true;
		}
	}
	return false;
}

/* a próxima (dir = 1) ou a anterior (-1) FOTO da vista; mantém onde está se não há */
static void andar(int dir) {
	for (int k = G.aberta + dir; k >= 0 && k < G.nvis; k += dir) {
		if (midia_vis(k)->tipo == GL_FOTO) {
			visualizador_abrir(k);
			return;
		}
	}
}

static int fotos_antes_e_depois(bool depois) {
	int n = 0;
	for (int k = G.aberta + (depois ? 1 : -1); k >= 0 && k < G.nvis; k += depois ? 1 : -1) {
		n += midia_vis(k)->tipo == GL_FOTO;
	}
	return n;
}

static bool botao_do_topo(Clay_ElementId id, const char *icone, bool ligado, bool perigo) {
	return gv_botao_redondo(id, icone, 40, ligado, perigo);
}

static void painel_de_detalhes(const gl_midia *m) {
	CLAY(CLAY_ID("vz-detalhes"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(300), CLAY_SIZING_GROW(0) }, .padding = { 20, 20, 18, 14 }, .childGap = 14 },
		.border = { .color = gv_t.borda, .width = { .left = 1 } } }) {
		gv_texto(CLAY_STRING("Detalhes"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_NORMAL);
		char data[64], tam[32];
		struct tm tm;
		localtime_r(&m->data, &tm);
		strftime(data, sizeof(data), "%d/%m/%Y, %H:%M", &tm);
		ar_tamanho_humano(m->tamanho, tam, sizeof(tam));
		char *pasta = g_path_get_dirname(m->caminho);
		const struct { const char *r, *v; } L[] = {
			{ "Nome", m->nome }, { "Data", data }, { "Tamanho", tam },
			{ "Dimensões", nat_w > 0 ? gv_fmt("%d × %d", nat_w, nat_h).chars : "—" }, { "Pasta", pasta },
		};
		for (size_t i = 0; i < G_N_ELEMENTS(L); i++) {
			CLAY(CLAY_IDI("vz-det-l", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(gv_str(L[i].r), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
				CLAY(CLAY_IDI("vz-det-v", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
					gv_texto(gv_str(L[i].v), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
			}
		}
		g_free(pasta);
		if (gv_botao_ext(CLAY_ID("vz-mostrar"), &(gv_botao_opcoes){ .icone = "folder", .texto = "Mostrar no Atlas", .estilo = GV_BOTAO_ELEVADO, .altura = 40 })) {
			char *pasta2 = g_path_get_dirname(m->caminho);
			char *argv[] = { "lumna-atlas", pasta2, NULL };
			g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
			g_free(pasta2);
		}
	}
}

void visualizador_desenhar(void) {
	if (G.aberta < 0 || G.aberta >= G.nvis) {
		visualizador_fechar();
		return;
	}
	const gl_midia *m = midia_vis(G.aberta);
	if (strcmp(aberta_caminho, m->caminho)) {
		carregar_tamanho(m->caminho); /* a lista mudou por baixo (uma recarga, uma exclusão) */
	}
	gl_midia copia = *m; /* a lista pode mudar durante os cliques abaixo */
	Clay_ElementId area = CLAY_ID("vz-area");
	CLAY(CLAY_ID("vz-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 16, 16, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		if (gv_botao_ext(CLAY_ID("vz-voltar"), &(gv_botao_opcoes){ .icone = "arrow-left", .texto = "Voltar à galeria", .estilo = GV_BOTAO_SIMPLES, .altura = 42 })) {
			visualizador_fechar();
		}
		CLAY(CLAY_ID("vz-nome"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(copia.nome), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
		}
		if (botao_do_topo(CLAY_ID("vz-mais"), "plus-circle", false, false)) {
			zoom = fminf(zoom * 1.25f, 12);
		}
		if (botao_do_topo(CLAY_ID("vz-menos"), "minus", false, false)) {
			zoom = fmaxf(zoom / 1.25f, 1);
			if (zoom <= 1.001f) {
				pan_x = pan_y = 0;
			}
		}
		if (botao_do_topo(CLAY_ID("vz-fav"), "star", copia.favorita, false)) {
			alternar_favorita(&copia);
		}
		if (botao_do_topo(CLAY_ID("vz-info"), "info", detalhes, false)) {
			detalhes = !detalhes;
		}
		if (botao_do_topo(CLAY_ID("vz-lixeira"), "trash", false, true)) {
			char *c[] = { copia.caminho };
			mover_para_lixeira(c, 1);
			visualizador_fechar();
			return;
		}
	}
	CLAY(CLAY_ID("vz-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(area, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } }, .clip = { .horizontal = true, .vertical = true },
			.backgroundColor = gv_t.fundo }) {
			Clay_BoundingBox vp = gv_caixa(area);
			float vw = vp.width > 0 ? vp.width : 900, vh = vp.height > 0 ? vp.height : 560;
			float base = 1;
			if (nat_w > 0 && nat_h > 0) {
				base = G.cfg.ajustar_tela ? fminf(1, fminf(vw / nat_w, vh / nat_h)) : 1;
				if (G.cfg.ajustar_tela && (nat_w < vw && nat_h < vh)) {
					base = 1; /* uma foto pequena não é esticada para encher a tela */
				}
			}
			float w = nat_w > 0 ? nat_w * base * zoom : vw, h = nat_h > 0 ? nat_h * base * zoom : vh;
			/* a roda dá zoom em volta do ponteiro; arrastar anda quando passa da tela */
			Clay_Vector2 p = gv_ponteiro();
			bool sobre = p.x >= vp.x && p.x <= vp.x + vp.width && p.y >= vp.y && p.y <= vp.y + vp.height;
			float roda = sobre ? gv_roda() : 0;
			if (roda != 0 && nat_w > 0) {
				float novo = fminf(fmaxf(zoom * expf(-roda * 0.04f), 1), 12);
				float fator = novo / zoom;
				float cx = p.x - (vp.x + vp.width / 2), cy = p.y - (vp.y + vp.height / 2);
				pan_x = cx - (cx - pan_x) * fator;
				pan_y = cy - (cy - pan_y) * fator;
				zoom = novo;
				w = nat_w * base * zoom;
				h = nat_h * base * zoom;
			}
			if (gv_premido(area) && anterior.x >= 0 && (w > vw || h > vh)) {
				pan_x += p.x - anterior.x;
				pan_y += p.y - anterior.y;
			}
			anterior = gv_premido(area) ? p : (Clay_Vector2){ -1, -1 };
			float lim_x = fmaxf(0, (w - vw) / 2), lim_y = fmaxf(0, (h - vh) / 2);
			pan_x = fminf(fmaxf(pan_x, -lim_x), lim_x);
			pan_y = fminf(fmaxf(pan_y, -lim_y), lim_y);
			if (zoom <= 1.001f) {
				pan_x = pan_y = 0;
			}
			CLAY(CLAY_ID("vz-foto"), gv_imagem(CLAY_ID("vz-foto"), copia.caminho, GV_AJUSTE_CONTER, (Clay_ElementDeclaration){
				.layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h) } },
				.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER },
					.offset = { pan_x, pan_y }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } })) {}
			/* o toque duplo alterna entre ajustada e 2,5× */
			if (gv_tocado(area)) {
				double agora = gv_agora();
				if (agora - ultimo_toque < 0.35) {
					zoom = zoom > 1.05f ? 1 : 2.5f;
					if (zoom <= 1) {
						pan_x = pan_y = 0;
					}
					ultimo_toque = 0;
				} else {
					ultimo_toque = agora;
				}
			}
			/* as setas de lado (só se há outra foto) */
			if (fotos_antes_e_depois(false)) {
				CLAY(CLAY_ID("vz-ant-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(48) } },
					.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_CENTER },
						.offset = { 16, 0 }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE } }) {
					if (gv_botao_redondo(CLAY_ID("vz-ant"), "arrow-left", 48, false, false)) {
						andar(-1);
					}
				}
			}
			if (fotos_antes_e_depois(true)) {
				CLAY(CLAY_ID("vz-prox-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(48) } },
					.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_CENTER, .parent = CLAY_ATTACH_POINT_RIGHT_CENTER },
						.offset = { -16, 0 }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE } }) {
					if (gv_botao_redondo(CLAY_ID("vz-prox"), "arrow-right", 48, false, false)) {
						andar(1);
					}
				}
			}
		}
		if (detalhes) {
			painel_de_detalhes(&copia);
		}
	}
	/* o teclado */
	for (int i = 0;; i++) {
		uint32_t t = gv_tecla_n(i);
		if (!t) {
			break;
		}
		if (t == 0xff1b || t == 0xff08) {
			visualizador_fechar();
		} else if (t == 0xff51) {
			andar(-1);
		} else if (t == 0xff53) {
			andar(1);
		} else if (t == '+' || t == '=') {
			zoom = fminf(zoom * 1.25f, 12);
		} else if (t == '-') {
			zoom = fmaxf(zoom / 1.25f, 1);
		} else if (t == '0') {
			zoom = 1;
			pan_x = pan_y = 0;
		} else if (t == 'i' || t == 'I') {
			detalhes = !detalhes;
		} else if (t == 'f' || t == 'F') {
			alternar_favorita(&copia);
		} else if (t == 0xffff) {
			char *c[] = { copia.caminho };
			mover_para_lixeira(c, 1);
			visualizador_fechar();
			return;
		}
	}
}
