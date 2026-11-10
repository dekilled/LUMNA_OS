/* A barra lateral, a barra do player, o painel da fila, os menus e os diálogos do LUMNA Music. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

const char *artista_da_faixa(const mu_faixa *f) {
	return f->artista;
}

static bool aba_aberta_dlg;

/* ---------------- o coração ---------------- */

static bool coracao_cheio_flag[2] = { false, true };

static void desenhar_coracao(cairo_t *cr, Clay_BoundingBox cx, void *dados) {
	bool cheio = *(bool *)dados;
	double s = fmin(cx.width, cx.height) * 0.78, x = cx.x + (cx.width - s) / 2, y = cx.y + (cx.height - s) / 2 + s * 0.04;
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_scale(cr, s / 24.0, s / 24.0);
	cairo_move_to(cr, 12, 21);
	cairo_curve_to(cr, 3, 14.5, 1.5, 9, 4.5, 5.8);
	cairo_curve_to(cr, 7.2, 3, 10.7, 4, 12, 6.8);
	cairo_curve_to(cr, 13.3, 4, 16.8, 3, 19.5, 5.8);
	cairo_curve_to(cr, 22.5, 9, 21, 14.5, 12, 21);
	cairo_close_path(cr);
	if (cheio) {
		cairo_set_source_rgba(cr, 0.69, 0.45, 1.0, 1);
		cairo_fill(cr);
	} else {
		cairo_set_source_rgba(cr, 0.75, 0.75, 0.82, 1);
		cairo_set_line_width(cr, 1.8);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

void coracao(Clay_ElementId id, bool cheio, float lado, bool *clicou) {
	CLAY(id, gv_livre(desenhar_coracao, &coracao_cheio_flag[cheio ? 1 : 0], (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado) } },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(lado / 2) })) {}
	*clicou = gv_tocado(id);
}

/* ---------------- a barra lateral ---------------- */

static int pl_menu = -1;

static bool item_lateral(Clay_ElementId id, const char *icone, const char *texto, bool ativo) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 14, 10, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ativo ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
		.border = { .color = gv_t.acento, .width = { .left = ativo ? 3 : 0 } } }) {
		gv_icone(icone, 24, ativo ? gv_t.acento : gv_t.texto_secundario);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo, ativo ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	return gv_tocado(id);
}

void barra_lateral(void) {
	CLAY(CLAY_ID("mu-lateral"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(250), CLAY_SIZING_GROW(0) } }, .border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
		Clay_ElementId rol = CLAY_ID("mu-lat-rol");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.padding = { 12, 12, 14, 10 }, .childGap = 2 } })) {
			static const char *const ICONES[] = { "home", "library", "disc", "user", "folder" };
			static const char *const NOMES[] = { "Ouvir agora", "Minha biblioteca", "Álbuns", "Artistas", "Pastas" };
			for (int v = 0; v < 5; v++) {
				bool ativa = M.vista == v || (v == V_ALBUNS && M.vista == V_DETALHE && M.det_tipo == D_ALBUM) || (v == V_ARTISTAS && M.vista == V_DETALHE && M.det_tipo == D_ARTISTA)
					|| (v == V_PASTAS && M.vista == V_DETALHE && M.det_tipo == D_PASTA);
				if (item_lateral(CLAY_IDI("mu-nav", (uint32_t)v), ICONES[v], NOMES[v], ativa) && !(M.vista == v)) {
					ir_para(v);
				} else if (gv_tocado(CLAY_IDI("mu-nav", (uint32_t)v)) && M.vista == v) {
					barra_de_rolagem_zero();
				}
			}
			CLAY(CLAY_ID("mu-lat-div"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
			CLAY(CLAY_ID("mu-pl-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 10, 0, 14, 4 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Playlists"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				if (gv_botao_redondo(CLAY_ID("mu-nova-playlist"), "plus", 32, false, false)) {
					abrir_dialogo_playlist(0, "");
				}
			}
			if (M.playlists) {
				for (guint i = 0; i < M.playlists->len && i < 20; i++) {
					const char *nome = M.playlists->pdata[i];
					Clay_ElementId id = CLAY_IDI("mu-pl", (uint32_t)i);
					bool ativa = M.vista == V_DETALHE && M.det_tipo == D_PLAYLIST && !strcmp(M.det_playlist, nome);
					bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-playlist"));
					if (pediu) {
						pl_menu = (int)i;
					}
					if (item_lateral(id, !strcmp(nome, MU_FAVORITAS) ? "heart" : "music", nome, ativa) && !pediu) {
						abrir_detalhe(D_PLAYLIST, 0, nome);
					}
				}
			}
		}
		CLAY(CLAY_ID("mu-lat-rodape"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 8, 12 }, .childGap = 2 }, .border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
			if (item_lateral(CLAY_ID("mu-adicionar"), "folder-plus", "Adicionar músicas", false)) {
				abrir_dialogo_pasta();
			}
			if (item_lateral(CLAY_ID("mu-config"), "settings", "Configurações", M.vista == V_AJUSTES)) {
				ir_para(V_AJUSTES);
			}
		}
	}
	GV_MENU_PONTEIRO(CLAY_ID("menu-playlist")) {
		CLAY(CLAY_ID("mp-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			const char *nome = M.playlists && pl_menu >= 0 && pl_menu < (int)M.playlists->len ? M.playlists->pdata[pl_menu] : NULL;
			if (nome && gv_menu_item(CLAY_ID("mp-tocar"), "play", "Tocar", NULL)) {
				abrir_detalhe(D_PLAYLIST, 0, nome);
				if (M.det_n > 0) {
					tocar_lista(M.det_idx, M.det_n, 0, M.cfg.aleatorio);
				}
				gv_popup_fechar(CLAY_ID("menu-playlist"));
			}
			if (nome && strcmp(nome, MU_FAVORITAS)) {
				if (gv_menu_item(CLAY_ID("mp-renomear"), "edit", "Renomear", NULL)) {
					abrir_dialogo_playlist(1, nome);
					gv_popup_fechar(CLAY_ID("menu-playlist"));
				}
				if (gv_menu_item(CLAY_ID("mp-apagar"), "trash", "Apagar a playlist", NULL)) {
					abrir_dialogo_playlist(2, nome);
					gv_popup_fechar(CLAY_ID("menu-playlist"));
				}
			}
		}
	}
}

/* ---------------- a barra do player ---------------- */

static bool seek_arrastando, vol_arrastando;
static float seek_v;
static int vol_antes_mudo = 80;

static void tempo(char *s, size_t n, double seg) {
	mu_duracao_texto((int)seg, s, n);
}

static const char *saida_rotulo(void) {
	if (!M.cfg.saida[0]) {
		return "Neste computador";
	}
	if (M.dispositivos) {
		for (guint i = 0; i < M.dispositivos->len; i++) {
			const char *l = M.dispositivos->pdata[i];
			const char *tab = strchr(l, '\t');
			if (tab && (gsize)(tab - l) == strlen(M.cfg.saida) && !strncmp(l, M.cfg.saida, (gsize)(tab - l))) {
				return tab + 1;
			}
		}
	}
	return M.cfg.saida;
}

void barra_do_player(void) {
	const mu_faixa *f = faixa_atual();
	double pos = M.carregada_no_motor || !f ? M.est.pos : M.pos_pendente;
	double dur = M.est.dur > 0 && M.carregada_no_motor ? M.est.dur : (f ? f->duracao : 0);
	CLAY(CLAY_ID("mu-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(92) }, .padding = { 24, 24, 0, 0 }, .childGap = 18, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		/* a faixa */
		CLAY(CLAY_ID("mu-bar-faixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(300) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (f) {
				capa_quadrada(CLAY_ID("mu-capa-bar"), f, 62, 8);
				if (gv_tocado(CLAY_ID("mu-capa-bar"))) {
					ir_para(V_OUVIR);
				}
				CLAY(CLAY_ID("mu-bar-txt"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 }, .clip = { .horizontal = true } }) {
					gv_texto(gv_str(f->titulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(artista_da_faixa(f)), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				bool clicou;
				coracao(CLAY_ID("mu-coracao"), eh_favorita(f->caminho), 34, &clicou);
				if (clicou) {
					mu_faixa copia;
					mu_faixa_copiar(&copia, f);
					alternar_favorita(&copia);
					mu_faixa_liberar(&copia);
				}
			} else {
				gv_texto(CLAY_STRING("Nada tocando"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		/* os controles e a barra de tempo */
		CLAY(CLAY_ID("mu-bar-centro"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("mu-controles"), { .layout = { .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				if (gv_botao_redondo(CLAY_ID("mu-aleatorio"), "shuffle", 40, M.cfg.aleatorio, false)) {
					M.cfg.aleatorio = !M.cfg.aleatorio;
					extern void musica_aleatorio_mudou(void);
					musica_aleatorio_mudou();
					salvar_cfg();
				}
				if (gv_botao_redondo(CLAY_ID("mu-ant"), "skip-back", 40, false, false)) {
					anterior();
				}
				if (gv_botao_redondo(CLAY_ID("mu-play"), M.est.tocando ? "pause" : "play", 54, true, false)) {
					alternar_toque();
				}
				if (gv_botao_redondo(CLAY_ID("mu-prox"), "skip-forward", 40, false, false)) {
					proxima();
				}
				if (gv_botao_redondo(CLAY_ID("mu-repetir"), M.cfg.repetir == 2 ? "repeat-one" : "repeat", 40, M.cfg.repetir != 0, false)) {
					M.cfg.repetir = (M.cfg.repetir + 1) % 3;
					extern void musica_repetir_mudou(void);
					musica_repetir_mudou();
					salvar_cfg();
				}
			}
			CLAY(CLAY_ID("mu-tempo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				char a[32], b[32];
				tempo(a, sizeof(a), seek_arrastando ? seek_v * dur : pos);
				tempo(b, sizeof(b), dur);
				CLAY(CLAY_ID("mu-t1"), { .layout = { .sizing = { CLAY_SIZING_FIXED(46) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
					gv_texto(gv_str(a), gv_t.tamanho_legenda + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				CLAY(CLAY_ID("mu-seek-cx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					float v = seek_arrastando ? seek_v : (dur > 0 ? (float)(pos / dur) : 0);
					if (gv_deslizante(CLAY_ID("mu-seek"), &v, 4, 12) && f && dur > 0) {
						seek_arrastando = true;
						seek_v = CLAMP(v, 0.0f, 1.0f);
					}
				}
				CLAY(CLAY_ID("mu-t2"), { .layout = { .sizing = { CLAY_SIZING_FIXED(46) } } }) {
					gv_texto(gv_str(b), gv_t.tamanho_legenda + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
			if (seek_arrastando && !gv_ponteiro_baixo()) {
				seek_arrastando = false;
				if (f && dur > 0) {
					if (!M.carregada_no_motor) {
						M.pos_pendente = seek_v * dur;
					} else {
						mp_buscar(M.mp, seek_v * dur);
					}
				}
			}
		}
		/* volume, saída, letra e fila */
		CLAY(CLAY_ID("mu-bar-dir"), { .layout = { .sizing = { CLAY_SIZING_FIXED(444) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao_redondo(CLAY_ID("mu-vol-icone"), M.cfg.volume == 0 ? "volume-off" : "volume", 38, false, false)) {
				if (M.cfg.volume > 0) {
					vol_antes_mudo = M.cfg.volume;
					M.cfg.volume = 0;
				} else {
					M.cfg.volume = vol_antes_mudo > 0 ? vol_antes_mudo : 60;
				}
				mp_volume(M.mp, M.cfg.volume);
				salvar_cfg();
			}
			CLAY(CLAY_ID("mu-vol-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(92) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				float v = M.cfg.volume / 100.0f;
				if (gv_deslizante(CLAY_ID("mu-volume"), &v, 4, 12)) {
					M.cfg.volume = (int)lroundf(v * 100);
					mp_volume(M.mp, M.cfg.volume);
					vol_arrastando = true;
				}
				if (vol_arrastando && !gv_ponteiro_baixo()) {
					vol_arrastando = false;
					salvar_cfg();
				}
			}
			Clay_ElementId saida = CLAY_ID("mu-saida");
			CLAY(saida, { .layout = { .sizing = { CLAY_SIZING_FIXED(184), CLAY_SIZING_FIXED(40) }, .padding = { 10, 8, 0, 0 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_por_cima(saida) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				gv_icone("computer", 20, gv_t.texto_secundario);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
					gv_texto(gv_str(saida_rotulo()), gv_t.tamanho_corpo - 2, gv_t.texto, GV_PESO_NORMAL);
				}
				gv_icone("chevron-down", 16, gv_t.texto_secundario);
			}
			if (gv_tocado(saida)) {
				gv_popup_alternar(CLAY_ID("menu-saida"));
			}
			GV_POPUP(CLAY_ID("menu-saida"), saida, .lado = GV_POPUP_ACIMA, .alinhar = GV_ALINHAR_FIM, .largura = 330) {
				CLAY(CLAY_ID("ms-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
					CLAY(CLAY_ID("ms-tit"), { .layout = { .padding = { 10, 10, 6, 6 } } }) {
						gv_texto(CLAY_STRING("Tocar em"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
					}
					bool achou = false;
					if (M.dispositivos) {
						for (guint i = 0; i < M.dispositivos->len && i < 12; i++) {
							char *l = g_strdup(M.dispositivos->pdata[i]);
							char *tab = strchr(l, '\t');
							if (!tab) {
								g_free(l);
								continue;
							}
							*tab = 0;
							const char *nome = l, *desc = tab + 1;
							bool padrao = !strcmp(nome, "auto");
							bool marcado = padrao ? !M.cfg.saida[0] : !strcmp(nome, M.cfg.saida);
							achou |= marcado;
							if (gv_menu_item_ext(CLAY_IDI("mu-saida-i", (uint32_t)i), padrao ? "computer" : "speaker", gv_t.texto_secundario, padrao ? "Neste computador (padrão do sistema)" : desc, NULL, marcado)) {
								g_strlcpy(M.cfg.saida, padrao ? "" : nome, sizeof(M.cfg.saida));
								aplicar_cfg();
								salvar_cfg();
								gv_popup_fechar(CLAY_ID("menu-saida"));
							}
							g_free(l);
						}
					}
					if (!M.dispositivos || M.dispositivos->len == 0) {
						gv_texto(CLAY_STRING("Procurando as saídas de áudio…"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
					(void)achou;
				}
			}
			if (gv_botao_redondo(CLAY_ID("mu-letra-bt"), "lyrics", 40, M.vista == V_OUVIR, false)) {
				if (M.vista == V_OUVIR) {
					ir_para(M.vista_antes == V_OUVIR ? V_BIBLIOTECA : M.vista_antes);
				} else {
					ir_para(V_OUVIR);
				}
			}
			if (gv_botao_redondo(CLAY_ID("mu-fila-bt"), "queue", 40, M.cfg.fila_aberta, false)) {
				M.cfg.fila_aberta = !M.cfg.fila_aberta;
				salvar_cfg();
			}
		}
	}
}

/* ---------------- o painel da fila ---------------- */

#define FILA_LINHA 76.0f

static int fila_arrasto = -1;
int musica_fila_arrasto(void) {
	return fila_arrasto;
}
static int fila_menu = -1;

static void linha_da_fila(int i, bool atual) {
	const mu_faixa *f = &M.fila[i].f;
	Clay_ElementId id = CLAY_IDI("mu-fl", (uint32_t)i);
	bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-fila"));
	if (pediu) {
		fila_menu = i;
	}
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(FILA_LINHA - 4) }, .padding = { 8, 8, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = fila_arrasto == i ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		capa_quadrada(CLAY_IDI("mu-fcapa", (uint32_t)i), f, 56, 6);
		if (atual) {
			barras_tocando(CLAY_IDI("mu-fbarras", (uint32_t)i), 22, M.est.tocando);
		}
		CLAY(CLAY_IDI("mu-ftxt", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(f->titulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(gv_str(artista_da_faixa(f)), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (!atual) {
			CLAY(CLAY_IDI("mu-fgrip", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(30), CLAY_SIZING_FIXED(40) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("bars", 20, gv_t.texto_suave);
			}
		}
	}
	if (gv_tocado(id) && !pediu && fila_arrasto < 0) {
		tocar_indice(i);
	}
}

void painel_da_fila(void) {
	CLAY(CLAY_ID("mu-fila"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(350), CLAY_SIZING_GROW(0) } }, .border = { .color = gv_t.borda, .width = { .left = 1 } } }) {
		CLAY(CLAY_ID("mu-fila-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(72) }, .padding = { 20, 14, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone("queue", 26, gv_t.acento);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Fila de reprodução"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao_redondo(CLAY_ID("mu-fila-mais"), "dots", 38, false, false)) {
				gv_popup_alternar(CLAY_ID("menu-fila-mais"));
			}
			GV_POPUP(CLAY_ID("menu-fila-mais"), CLAY_ID("mu-fila-mais"), .alinhar = GV_ALINHAR_FIM, .largura = 250) {
				CLAY(CLAY_ID("mfm-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
					if (gv_menu_item(CLAY_ID("mu-fila-salvar"), "save", "Salvar como playlist…", NULL)) {
						if (M.nfila > 0) {
							abrir_dialogo_playlist(3, "");
						} else {
							aviso("A fila está vazia");
						}
						gv_popup_fechar(CLAY_ID("menu-fila-mais"));
					}
					if (gv_menu_item(CLAY_ID("mu-fila-limpar"), "trash", "Limpar a fila", NULL)) {
						fila_limpar();
						gv_popup_fechar(CLAY_ID("menu-fila-mais"));
					}
				}
			}
		}
		Clay_ElementId rol = CLAY_ID("mu-fila-rol");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.padding = { 12, 12, 0, 12 }, .childGap = 0 } })) {
			if (M.nfila == 0 || M.atual < 0) {
				CLAY(CLAY_ID("mu-fila-vazia"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 8, 24, 8 } } }) {
					gv_texto(CLAY_STRING("A fila está vazia. Toque uma música ou um álbum."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			} else {
				CLAY(CLAY_ID("mu-fila-t1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 8, 6, 8 } } }) {
					gv_texto(CLAY_STRING("Tocando agora"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
				linha_da_fila(M.atual, true);
				CLAY(CLAY_ID("mu-fila-sep"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
				CLAY(CLAY_ID("mu-fila-t2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 8, 14, 8 } } }) {
					gv_texto(CLAY_STRING("A seguir"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				/* só o que aparece: o resto vira espaço */
				int primeiro = M.atual + 1, total = M.nfila - primeiro;
				float topo = gv_rolagem_posicao(rol).y - 150, alt_view = gv_caixa(rol).height > 0 ? gv_caixa(rol).height : 600;
				int r0 = CLAMP((int)floorf(topo / FILA_LINHA), 0, total), r1 = CLAMP((int)ceilf((topo + alt_view + 150) / FILA_LINHA) + 1, 0, total);
				if (r0 > 0) {
					CLAY(CLAY_ID("mu-fila-esp1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(r0 * FILA_LINHA) } } }) {}
				}
				for (int k = r0; k < r1; k++) {
					linha_da_fila(primeiro + k, false);
				}
				if (total - r1 > 0) {
					CLAY(CLAY_ID("mu-fila-esp2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((total - r1) * FILA_LINHA) } } }) {}
				}
				/* arrastar a alça: a linha onde soltar */
				for (int k = r0; k < r1; k++) {
					Clay_ElementId g = CLAY_IDI("mu-fgrip", (uint32_t)(primeiro + k));
					Clay_BoundingBox gb = gv_caixa(g);
					/* o aperto é da linha (é ela que tem o toque); só vale se começou em cima da alça */
					if (fila_arrasto < 0 && gv_premido(CLAY_IDI("mu-fl", (uint32_t)(primeiro + k))) && gv_ponteiro_baixo() && gb.width > 0 && gv_ponteiro().x >= gb.x - 6 && gv_ponteiro().x <= gb.x + gb.width + 6) {
						fila_arrasto = primeiro + k;
					}
				}
				if (fila_arrasto >= 0 && total > 0) {
					/* o alvo vem da posição das linhas que estão na tela */
					Clay_BoundingBox l0 = gv_caixa(CLAY_IDI("mu-fl", (uint32_t)(primeiro + r0)));
					int alvo = fila_arrasto;
					if (l0.height > 0) {
						alvo = primeiro + r0 + (int)floorf((gv_ponteiro().y - l0.y) / FILA_LINHA);
						alvo = CLAMP(alvo, primeiro, M.nfila - 1);
					}
					if (!gv_ponteiro_baixo()) {
						fila_mover(fila_arrasto, alvo);
						fila_arrasto = -1;
					}
				}
			}
		}
	}
	GV_MENU_PONTEIRO(CLAY_ID("menu-fila")) {
		CLAY(CLAY_ID("mq-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			if (gv_menu_item(CLAY_ID("mq-seguir"), "arrow-up", "Tocar a seguir", NULL)) {
				if (fila_menu > M.atual + 1) {
					fila_mover(fila_menu, M.atual + 1);
				}
				gv_popup_fechar(CLAY_ID("menu-fila"));
			}
			if (gv_menu_item(CLAY_ID("mq-remover"), "close", "Tirar da fila", NULL)) {
				fila_remover(fila_menu);
				gv_popup_fechar(CLAY_ID("menu-fila"));
			}
		}
	}
}

/* ---------------- o menu de uma faixa ---------------- */

static int menu_ac = -1;
static bool menu_tem_playlist;

void menu_da_faixa(int acervo_idx) {
	menu_ac = acervo_idx;
	menu_tem_playlist = M.vista == V_DETALHE && M.det_tipo == D_PLAYLIST;
}

static void corpo_menu_faixa(void) {
	if (menu_ac < 0 || menu_ac >= M.ac.n) {
		return;
	}
	const mu_faixa *f = &M.ac.v[menu_ac];
	int um[1] = { menu_ac };
	if (gv_menu_item(CLAY_ID("mf-tocar"), "play", "Tocar agora", NULL)) {
		tocar_lista(um, 1, 0, false);
		gv_popup_fechar(CLAY_ID("menu-faixa"));
	}
	if (gv_menu_item(CLAY_ID("mf-seguir"), "arrow-up", "Tocar a seguir", NULL)) {
		fila_adicionar(um, 1, true);
		gv_popup_fechar(CLAY_ID("menu-faixa"));
	}
	if (gv_menu_item(CLAY_ID("mf-fila"), "queue", "Adicionar à fila", NULL)) {
		fila_adicionar(um, 1, false);
		gv_popup_fechar(CLAY_ID("menu-faixa"));
	}
	if (gv_menu_item(CLAY_ID("mf-fav"), "heart", eh_favorita(f->caminho) ? "Tirar das Favoritas" : "Favoritar", NULL)) {
		alternar_favorita(f);
		gv_popup_fechar(CLAY_ID("menu-faixa"));
	}
	CLAY(CLAY_ID("mf-sep"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) }, .padding = { 0, 0, 0, 0 } }, .backgroundColor = gv_t.borda }) {}
	if (M.playlists) {
		int n = 0;
		for (guint i = 0; i < M.playlists->len && n < 10; i++) {
			const char *nome = M.playlists->pdata[i];
			if (!strcmp(nome, MU_FAVORITAS)) {
				continue;
			}
			bool tem = mu_playlist_tem(nome, f->caminho);
			if (gv_menu_item_ext(CLAY_IDI("mf-pl", (uint32_t)n++), "plus", gv_t.texto_secundario, gv_fmt("Adicionar a %s", nome).chars, NULL, tem)) {
				char *c[] = { f->caminho };
				mu_playlist_adicionar(nome, c, 1);
				aviso("Adicionada a %s", nome);
				recarregar_listas();
				gv_popup_fechar(CLAY_ID("menu-faixa"));
			}
		}
	}
	extern void musica_nova_playlist_com(const char *caminho);
	if (gv_menu_item(CLAY_ID("mf-nova"), "folder-plus", "Nova playlist com esta faixa…", NULL)) {
		musica_nova_playlist_com(f->caminho);
		gv_popup_fechar(CLAY_ID("menu-faixa"));
	}
	CLAY(CLAY_ID("mf-sep2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
	if (gv_menu_item(CLAY_ID("mf-album"), "disc", "Ir para o álbum", NULL)) {
		for (int k = 0; k < M.albuns.n; k++) {
			for (int j = 0; j < M.albuns.v[k].n; j++) {
				if (M.albuns.v[k].idx[j] == menu_ac) {
					abrir_detalhe(D_ALBUM, k, NULL);
					k = M.albuns.n;
					break;
				}
			}
		}
		gv_popup_fechar(CLAY_ID("menu-faixa"));
	}
	if (gv_menu_item(CLAY_ID("mf-artista"), "user", "Ir para o artista", NULL)) {
		for (int k = 0; k < M.artistas.n; k++) {
			if (!strcmp(M.artistas.v[k].nome, f->artista)) {
				abrir_detalhe(D_ARTISTA, k, NULL);
				break;
			}
		}
		gv_popup_fechar(CLAY_ID("menu-faixa"));
	}
	if (gv_menu_item(CLAY_ID("mf-atlas"), "folder", "Mostrar no Atlas", NULL)) {
		char *argv[] = { "lumna-atlas", f->pasta, NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
		gv_popup_fechar(CLAY_ID("menu-faixa"));
	}
	if (menu_tem_playlist && gv_menu_item(CLAY_ID("mf-tirar"), "close", "Tirar desta playlist", NULL)) {
		mu_playlist_remover(M.det_playlist, f->caminho);
		aviso("Tirada da playlist");
		recarregar_listas();
		gv_popup_fechar(CLAY_ID("menu-faixa"));
	}
}

/* ---------------- os diálogos ---------------- */

enum { DL_NENHUM, DL_PL_NOVA, DL_PL_RENOMEAR, DL_PL_APAGAR, DL_PL_FILA, DL_PASTA };
static int dlg;
static char dlg_texto[1024], dlg_erro[200], dlg_alvo[256], dlg_levar[4096];

bool dialogo_aberto(void) {
	return dlg != DL_NENHUM;
}

void abrir_dialogo_playlist(int modo, const char *nome) {
	dlg = modo == 0 ? DL_PL_NOVA : modo == 1 ? DL_PL_RENOMEAR : modo == 2 ? DL_PL_APAGAR : DL_PL_FILA;
	g_strlcpy(dlg_texto, modo == 1 ? nome : "", sizeof(dlg_texto));
	g_strlcpy(dlg_alvo, nome, sizeof(dlg_alvo));
	dlg_erro[0] = 0;
	dlg_levar[0] = 0;
	if (modo == 0 || modo == 1 || modo == 3) {
		gv_focar(CLAY_ID("mu-dlg-campo"));
	}
}

void musica_nova_playlist_com(const char *caminho) {
	abrir_dialogo_playlist(0, "");
	g_strlcpy(dlg_levar, caminho, sizeof(dlg_levar));
}

void abrir_dialogo_pasta(void) {
	dlg = DL_PASTA;
	g_snprintf(dlg_texto, sizeof(dlg_texto), "%s/", g_get_home_dir());
	dlg_erro[0] = 0;
	gv_focar(CLAY_ID("mu-dlg-campo"));
}

static bool confirmou;

static void dialogo_cartao(float largura, const char *titulo, const char *texto, const char *ok_rotulo, bool campo, bool perigo) {
	CLAY(CLAY_ID("mu-dlg"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("mu-dlg-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(22), .childGap = 14 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			if (texto && *texto) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_str(texto), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
			if (campo) {
				if (gv_campo(CLAY_ID("mu-dlg-campo"), dlg_texto, sizeof(dlg_texto), "", GV_CAMPO_TEXTO) & GV_CAMPO_ENTER) {
					confirmou = true;
				}
				if (dlg_erro[0]) {
					gv_texto(gv_str(dlg_erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
				}
			}
			CLAY(CLAY_ID("mu-dlg-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_botao_ext(CLAY_ID("mu-dlg-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
					dlg = DL_NENHUM;
				}
				if (gv_botao_ext(CLAY_ID("mu-dlg-ok"), &(gv_botao_opcoes){ .texto = ok_rotulo, .estilo = perigo ? GV_BOTAO_PERIGO : GV_BOTAO_CHEIO })) {
					confirmou = true;
				}
			}
		}
	}
}

static void confirmar_dialogo(void) {
	char *nome = g_strstrip(g_strdup(dlg_texto));
	switch (dlg) {
	case DL_PL_NOVA:
	case DL_PL_FILA:
		if (!mu_playlist_criar(nome)) {
			g_snprintf(dlg_erro, sizeof(dlg_erro), "Esse nome não serve (vazio, repetido ou com símbolos proibidos).");
			g_free(nome);
			return;
		}
		if (dlg == DL_PL_FILA) {
			char **v = g_new(char *, (guint)M.nfila + 1);
			for (int i = 0; i < M.nfila; i++) {
				v[i] = M.fila[i].f.caminho;
			}
			mu_playlist_gravar(nome, v, M.nfila);
			g_free(v);
		} else if (dlg_levar[0]) {
			char *c[] = { dlg_levar };
			mu_playlist_adicionar(nome, c, 1);
		}
		listar_playlists();
		aviso("Playlist \"%s\" criada", nome);
		break;
	case DL_PL_RENOMEAR:
		if (!mu_playlist_renomear(dlg_alvo, nome)) {
			g_snprintf(dlg_erro, sizeof(dlg_erro), "Esse nome não serve (vazio, repetido ou com símbolos proibidos).");
			g_free(nome);
			return;
		}
		if (M.vista == V_DETALHE && M.det_tipo == D_PLAYLIST && !strcmp(M.det_playlist, dlg_alvo)) {
			g_strlcpy(M.det_playlist, nome, sizeof(M.det_playlist));
		}
		listar_playlists();
		break;
	case DL_PL_APAGAR:
		mu_playlist_apagar(dlg_alvo);
		if (M.vista == V_DETALHE && M.det_tipo == D_PLAYLIST && !strcmp(M.det_playlist, dlg_alvo)) {
			ir_para(V_BIBLIOTECA);
		}
		listar_playlists();
		aviso("Playlist apagada (as músicas continuam onde estão)");
		break;
	case DL_PASTA: {
		char *p = g_strstrip(g_strdup(dlg_texto));
		size_t n = strlen(p);
		while (n > 1 && p[n - 1] == '/') {
			p[--n] = 0;
		}
		bool ok = true;
		if (!g_file_test(p, G_FILE_TEST_IS_DIR)) {
			g_snprintf(dlg_erro, sizeof(dlg_erro), "Essa pasta não existe.");
			ok = false;
		}
		for (int i = 0; ok && i < M.cfg.npastas; i++) {
			if (!strcmp(M.cfg.pastas[i], p)) {
				g_snprintf(dlg_erro, sizeof(dlg_erro), "Essa pasta já está na biblioteca.");
				ok = false;
			}
		}
		if (ok && M.cfg.npastas >= MU_PASTAS_MAX) {
			g_snprintf(dlg_erro, sizeof(dlg_erro), "Não dá para adicionar mais pastas.");
			ok = false;
		}
		if (!ok) {
			g_free(p);
			g_free(nome);
			return;
		}
		g_strlcpy(M.cfg.pastas[M.cfg.npastas++], p, sizeof(M.cfg.pastas[0]));
		salvar_cfg();
		recarregar_biblioteca();
		aviso("Pasta adicionada à biblioteca");
		g_free(p);
		break;
	}
	default:
		break;
	}
	g_free(nome);
	dlg = DL_NENHUM;
}

void menus_e_dialogos(void) {
	/* o menu de uma faixa (o "..." de uma linha ou o botão direito) */
	GV_MENU_PONTEIRO(CLAY_ID("menu-faixa")) {
		CLAY(CLAY_ID("mf-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			corpo_menu_faixa();
		}
	}
	const char *aviso_t = aviso_atual();
	if (aviso_t) {
		CLAY(CLAY_ID("mu-aviso"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM }, .padding = { 0, 0, 0, 112 } },
			.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 80, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
			CLAY(CLAY_ID("mu-aviso-c"), { .layout = { .padding = { 18, 18, 10, 10 } }, .backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
				.border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				gv_texto(gv_str(aviso_t), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
		}
	}
	confirmou = false;
	switch (dlg) {
	case DL_PL_NOVA: dialogo_cartao(460, "Nova playlist", "Dê um nome à playlist.", "Criar", true, false); break;
	case DL_PL_FILA: dialogo_cartao(460, "Salvar a fila como playlist", "Dê um nome à playlist.", "Salvar", true, false); break;
	case DL_PL_RENOMEAR: dialogo_cartao(460, "Renomear a playlist", "Escolha o novo nome.", "Renomear", true, false); break;
	case DL_PL_APAGAR: dialogo_cartao(440, "Apagar a playlist?", gv_fmt("A playlist \"%s\" some; as músicas continuam onde estão.", dlg_alvo).chars, "Apagar", false, true); break;
	case DL_PASTA: dialogo_cartao(560, "Adicionar pasta", "Digite o caminho de uma pasta com músicas.", "Adicionar", true, false); break;
	default: break;
	}
	if (dlg != DL_NENHUM && gv_tecla() == 0xff1b) {
		dlg = DL_NENHUM;
	}
	if (confirmou && dlg != DL_NENHUM) {
		confirmar_dialogo();
	}
	(void)aba_aberta_dlg;
}
