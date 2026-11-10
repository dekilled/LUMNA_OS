/* O player de vídeo da Galeria: o libmpv toca dentro da janela (servicos/video_player.c) e os controles do LUMNA ficam por cima: tocar/pausar, a barra de
 * tempo, o volume, a velocidade, as faixas de áudio e as legendas, repetir, tela cheia, o vídeo anterior e o seguinte da lista. Os controles somem
 * depois de uns segundos sem mexer (com o vídeo tocando). Também fala MPRIS (a tela de bloqueio e o celular o controlam). */
#include <cairo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "musica_mpris.h"
#include "video_player.h"

#define OCULTAR_APOS 3.0

static struct {
	vp *p;
	vp_estado e;
	bool ativo;
	int k;                       /* a posição em vis */
	char caminho[4096], nome[256];
	gl_midia m;                  /* a mídia (cópia) */
	cairo_surface_t *surf;
	uint8_t *dados;
	int sw, sh, sstride;
	double ultimo_mov;
	Clay_Vector2 ptr;
	bool seek_arr, vol_arr;
	float seek_v;
	bool tela_cheia;
	bool menu_aberto;
	double toque_ate;            /* o duplo clique */
	mm_mpris *mm;
	double mm_ultimo;
	bool aguarda_quadro;
} V;

static gint acordando;

static void acordou_na_principal(void *d) {
	(void)d;
	g_atomic_int_set(&acordando, 0);
	gv_app_redesenhar(G.app);
}

static void acorda(void *d) {
	(void)d;
	if (g_atomic_int_compare_and_exchange(&acordando, 0, 1)) { /* um aviso por quadro, não um por evento */
		gv_na_linha_principal(acordou_na_principal, NULL);
	}
}

/* ---------------- tela cheia ---------------- */

static void tela_cheia(bool sim) {
	const gv_janela *js[64];
	int n = gv_janelas(js, 64);
	for (int i = 0; i < n; i++) {
		if (js[i]->app_id && !strcmp(js[i]->app_id, "lumna-galeria") && js[i]->ativa) {
			gv_janela_tela_cheia(js[i]->id, sim);
			V.tela_cheia = sim;
			return;
		}
	}
	V.tela_cheia = false;
}

bool player_tela_cheia(void) {
	return V.ativo && G.tela == T_PLAYER && V.tela_cheia;
}

/* ---------------- o MPRIS ---------------- */

typedef struct {
	int cmd;
	int64_t val;
} pcmd;

static void proximo_video(int dir);

static void mpris_na_principal(void *d) {
	pcmd *c = d;
	if (V.ativo && V.p) {
		switch (c->cmd) {
		case 1: vp_pausar(V.p, false); break;
		case 2: vp_pausar(V.p, true); break;
		case 3: vp_pausar(V.p, V.e.tocando); break;
		case 4: vp_pausar(V.p, true); break;
		case 5: proximo_video(1); break;
		case 6: proximo_video(-1); break;
		case 7: vp_buscar(V.p, fmax(0, V.e.pos + (double)c->val / 1e6)); break;
		case 8: vp_buscar(V.p, (double)c->val / 1e6); break;
		case 9: vp_volume(V.p, (int)lround((double)c->val / 1000.0)); G.cfg.volume = CLAMP(V.e.volume, 0, 100); break;
		default: break;
		}
	}
	V.mm_ultimo = 0;
	gv_app_redesenhar(G.app);
	g_free(c);
}

static void mp_cmd(int cmd, int64_t v) {
	pcmd *c = g_new0(pcmd, 1);
	c->cmd = cmd;
	c->val = v;
	gv_na_linha_principal(mpris_na_principal, c);
}

static void m_tocar(void *d) { mp_cmd(1, 0); }
static void m_pausar(void *d) { mp_cmd(2, 0); }
static void m_alternar(void *d) { mp_cmd(3, 0); }
static void m_parar(void *d) { mp_cmd(4, 0); }
static void m_prox(void *d) { mp_cmd(5, 0); }
static void m_ant(void *d) { mp_cmd(6, 0); }
static void m_buscar(void *d, int64_t x) { mp_cmd(7, x); }
static void m_pos(void *d, int64_t x) { mp_cmd(8, x); }
static void m_vol(void *d, double v) { mp_cmd(9, (int64_t)lround(v * 100000.0)); }

static void mpris_atualizar(void) {
	if (!V.mm) {
		return;
	}
	double agora = gv_agora();
	mm_estado e;
	memset(&e, 0, sizeof(e));
	e.volume = CLAMP(V.e.volume, 0, 100) / 100.0;
	if (V.ativo && G.tela == T_PLAYER) {
		e.tem_faixa = true;
		e.tocando = V.e.tocando;
		g_strlcpy(e.titulo, V.nome, sizeof(e.titulo));
		g_strlcpy(e.artista, "Galeria", sizeof(e.artista));
		g_strlcpy(e.album, "Vídeos", sizeof(e.album));
		char *h = g_compute_checksum_for_string(G_CHECKSUM_MD5, V.caminho, -1);
		g_strlcpy(e.id, h, 17);
		g_free(h);
		e.duracao_us = (int64_t)(V.e.dur * 1e6);
		e.pos_us = (int64_t)(V.e.pos * 1e6);
		e.pode_proxima = e.pode_anterior = true;
	}
	static mm_estado ultimo;
	bool mudou = memcmp(e.titulo, ultimo.titulo, sizeof(e.titulo)) || memcmp(e.id, ultimo.id, sizeof(e.id)) || e.tocando != ultimo.tocando || e.tem_faixa != ultimo.tem_faixa
		|| fabs(e.volume - ultimo.volume) > 0.001 || llabs(e.duracao_us - ultimo.duracao_us) > 100000;
	if (mudou || agora - V.mm_ultimo > 3) {
		mm_atualizar(V.mm, &e);
		if (!mudou && ultimo.tocando == e.tocando && agora - V.mm_ultimo > 0 && fabs((double)e.pos_us - ((double)ultimo.pos_us + (e.tocando ? (agora - V.mm_ultimo) * 1e6 * V.e.velocidade : 0))) > 1500000) {
			mm_pulou(V.mm, e.pos_us);
		}
		ultimo = e;
		V.mm_ultimo = agora;
	}
}

/* ---------------- abrir, fechar, a lista ---------------- */

static void abrir_k(int k) {
	if (k < 0 || k >= G.nvis) {
		return;
	}
	const gl_midia *m = midia_vis(k);
	if (m->tipo != GL_VIDEO) {
		return;
	}
	if (!V.p) {
		V.p = vp_criar(acorda, NULL);
		if (!V.p) {
			aviso("Não deu para iniciar o player de vídeo (o mpv está instalado?).");
			return;
		}
		mm_acoes ac = { m_tocar, m_pausar, m_alternar, m_parar, m_prox, m_ant, m_buscar, m_pos, m_vol, NULL, NULL, NULL, NULL, NULL };
		V.mm = mm_iniciar(&ac, "lumna_galeria");
	}
	V.k = k;
	G.aberta = k;
	g_strlcpy(V.caminho, m->caminho, sizeof(V.caminho));
	g_strlcpy(V.nome, m->nome, sizeof(V.nome));
	V.m = *m;
	V.m.caminho = V.caminho;
	V.m.nome = V.nome;
	V.ativo = true;
	V.seek_arr = V.vol_arr = false;
	V.ultimo_mov = gv_agora();
	G.tela = T_PLAYER;
	vp_abrir(V.p, V.caminho, 0, G.cfg.repetir, G.cfg.volume);
	gv_app_redesenhar(G.app);
}

void player_abrir(int k) {
	abrir_k(k);
}

static void proximo_video(int dir) {
	for (int k = V.k + dir; k >= 0 && k < G.nvis; k += dir) {
		if (midia_vis(k)->tipo == GL_VIDEO) {
			abrir_k(k);
			return;
		}
	}
}

void player_fechar(void) {
	if (V.tela_cheia) {
		tela_cheia(false);
	}
	if (V.p) {
		vp_parar(V.p);
	}
	if (V.vol_arr || G.cfg.volume != V.e.volume) {
		gravar_cfg();
	}
	V.ativo = false;
	G.tela = T_BIBLIOTECA;
	if (V.surf) {
		cairo_surface_destroy(V.surf);
		V.surf = NULL;
	}
	V.sw = V.sh = 0;
	mpris_atualizar();
}

void player_encerrar(void) {
	if (V.mm) {
		mm_parar(V.mm);
		V.mm = NULL;
	}
	if (V.p) {
		vp_liberar(V.p);
		V.p = NULL;
	}
}

bool visualizador_reposicionar_video(void) {
	for (int k = 0; k < G.nvis; k++) {
		if (!strcmp(midia_vis(k)->caminho, V.caminho)) {
			V.k = G.aberta = k;
			return true;
		}
	}
	return false;
}

/* ---------------- o desenho do vídeo ---------------- */

static void desenhar_video(cairo_t *cr, Clay_BoundingBox cx, void *dados) {
	(void)dados;
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, cx.x, cx.y, cx.width, cx.height);
	cairo_fill(cr);
	if (V.surf && V.sw == (int)cx.width && V.sh == (int)cx.height) {
		cairo_surface_flush(V.surf);
		cairo_set_source_surface(cr, V.surf, cx.x, cx.y);
		cairo_paint(cr);
	}
}

static const char *tempo(double s) {
	char b[32];
	int n = (int)(s < 0 ? 0 : s);
	if (n >= 3600) {
		g_snprintf(b, sizeof(b), "%d:%02d:%02d", n / 3600, n / 60 % 60, n % 60);
	} else {
		g_snprintf(b, sizeof(b), "%d:%02d", n / 60, n % 60);
	}
	return gv_fmt("%s", b).chars;
}

static const double VELOCIDADES[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };

static void faixas_menu(const char *titulo, bool legendas, Clay_ElementId base_id) {
	vp_faixa f[24];
	int n = vp_faixas(V.p, legendas, f, 24);
	CLAY(CLAY_IDI("pl-m-tit", base_id.id), { .layout = { .padding = { 10, 10, 6, 6 } } }) {
		gv_texto(gv_str(titulo), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	if (legendas && gv_menu_item_ext(CLAY_IDI("pl-leg-i", 0), "close", gv_t.texto_secundario, "Sem legenda", NULL, V.e.legenda == 0)) {
		vp_legenda(V.p, 0);
		gv_popup_fechar(CLAY_ID("menu-faixas"));
	}
	for (int i = 0; i < n; i++) {
		Clay_ElementId iid = legendas ? CLAY_IDI("pl-leg-i", (uint32_t)(i + 1)) : CLAY_IDI("pl-aud-i", (uint32_t)i);
		if (gv_menu_item_ext(iid, legendas ? "text" : "volume", gv_t.texto_secundario, f[i].titulo, NULL, f[i].escolhida)) {
			if (legendas) {
				vp_legenda(V.p, f[i].id);
			} else {
				vp_audio(V.p, f[i].id);
			}
			gv_popup_fechar(CLAY_ID("menu-faixas"));
		}
	}
	if (n == 0 && !legendas) {
		gv_texto(CLAY_STRING("Este vídeo não tem áudio."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	if (n == 0 && legendas) {
		CLAY(CLAY_ID("pl-sem-leg"), { .layout = { .padding = { 10, 10, 4, 8 } } }) {
			gv_texto(CLAY_STRING("Sem legendas (ponha um .srt com o mesmo nome do vídeo)."), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

static void controles(void) {
	Clay_ElementId area = CLAY_ID("pl-area");
	(void)area;
	CLAY(CLAY_ID("pl-ctl"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 12, 14 }, .childGap = 6 },
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE },
		.backgroundColor = gv_alfa(gv_t.superficie, 0.88f) }) {
		double pos = V.seek_arr ? V.seek_v * V.e.dur : V.e.pos;
		CLAY(CLAY_ID("pl-tempo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("pl-t1"), { .layout = { .sizing = { CLAY_SIZING_FIXED(58) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				gv_texto(gv_str(tempo(pos)), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("pl-seek-cx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				float v = V.seek_arr ? V.seek_v : (V.e.dur > 0 ? (float)(V.e.pos / V.e.dur) : 0);
				if (gv_deslizante(CLAY_ID("pl-seek"), &v, 5, 14) && V.e.dur > 0) {
					V.seek_arr = true;
					V.seek_v = CLAMP(v, 0.0f, 1.0f);
				}
			}
			CLAY(CLAY_ID("pl-t2"), { .layout = { .sizing = { CLAY_SIZING_FIXED(58) } } }) {
				gv_texto(gv_str(tempo(V.e.dur)), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		if (V.seek_arr && !gv_ponteiro_baixo()) {
			V.seek_arr = false;
			vp_buscar(V.p, V.seek_v * V.e.dur);
		}
		CLAY(CLAY_ID("pl-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao_redondo(CLAY_ID("pl-ant"), "skip-back", 42, false, false)) {
				proximo_video(-1);
			}
			if (gv_botao_redondo(CLAY_ID("pl-play"), V.e.tocando ? "pause" : "play", 52, true, false)) {
				vp_pausar(V.p, V.e.tocando);
			}
			if (gv_botao_redondo(CLAY_ID("pl-prox"), "skip-forward", 42, false, false)) {
				proximo_video(1);
			}
			if (gv_botao_redondo(CLAY_ID("pl-vol-icone"), V.e.volume == 0 ? "volume-off" : "volume", 40, false, false)) {
				static int antes = 60;
				if (V.e.volume > 0) {
					antes = V.e.volume;
					vp_volume(V.p, 0);
				} else {
					vp_volume(V.p, antes);
				}
				G.cfg.volume = CLAMP(V.e.volume, 0, 100);
			}
			CLAY(CLAY_ID("pl-vol-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(110) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				float v = (float)V.e.volume / 100.0f;
				if (gv_deslizante(CLAY_ID("pl-vol"), &v, 4, 12)) {
					vp_volume(V.p, (int)lroundf(CLAMP(v, 0.0f, 1.0f) * 100));
					G.cfg.volume = CLAMP((int)lroundf(v * 100), 0, 100);
					V.vol_arr = true;
				}
				if (V.vol_arr && !gv_ponteiro_baixo()) {
					V.vol_arr = false;
					gravar_cfg();
				}
			}
			CLAY(CLAY_ID("pl-folga"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			/* a velocidade */
			Clay_ElementId vel = CLAY_ID("pl-vel");
			CLAY(vel, { .layout = { .sizing = { CLAY_SIZING_FIXED(70), CLAY_SIZING_FIXED(40) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_por_cima(vel) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				gv_texto(gv_fmt("%g×", V.e.velocidade), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_tocado(vel)) {
				gv_popup_alternar(CLAY_ID("menu-vel"));
			}
			GV_POPUP(CLAY_ID("menu-vel"), vel, .lado = GV_POPUP_ACIMA, .alinhar = GV_ALINHAR_FIM, .largura = 150) {
				CLAY(CLAY_ID("pl-vel-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
					for (int i = 0; i < 6; i++) {
						if (gv_menu_item_ext(CLAY_IDI("pl-vel-i", (uint32_t)i), NULL, gv_t.texto, gv_fmt("%g×%s", VELOCIDADES[i], i == 2 ? " (normal)" : "").chars, NULL, fabs(V.e.velocidade - VELOCIDADES[i]) < 0.01)) {
							vp_velocidade(V.p, VELOCIDADES[i]);
							gv_popup_fechar(CLAY_ID("menu-vel"));
						}
					}
				}
			}
			/* o áudio e as legendas */
			if (gv_botao_redondo(CLAY_ID("pl-faixas"), "text", 40, V.e.legenda > 0, false)) {
				gv_popup_alternar(CLAY_ID("menu-faixas"));
			}
			GV_POPUP(CLAY_ID("menu-faixas"), CLAY_ID("pl-faixas"), .lado = GV_POPUP_ACIMA, .alinhar = GV_ALINHAR_FIM, .largura = 360) {
				CLAY(CLAY_ID("pl-faixas-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
					faixas_menu("Legendas", true, CLAY_ID("pl-leg"));
					CLAY(CLAY_ID("pl-faixas-sep"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
					faixas_menu("Áudio", false, CLAY_ID("pl-aud"));
				}
			}
			if (gv_botao_redondo(CLAY_ID("pl-repetir"), "repeat", 40, V.e.repetir, false)) {
				vp_repetir(V.p, !V.e.repetir);
				G.cfg.repetir = !V.e.repetir;
				gravar_cfg();
			}
			if (gv_botao_redondo(CLAY_ID("pl-tela"), V.tela_cheia ? "minimize" : "maximize", 40, V.tela_cheia, false)) {
				tela_cheia(!V.tela_cheia);
			}
		}
	}
	V.menu_aberto = gv_popup_aberto(CLAY_ID("menu-vel")) || gv_popup_aberto(CLAY_ID("menu-faixas"));
}

/* Nada de `return` dentro de um bloco CLAY (deixaria o layout desbalanceado): o botão só anota a ação, que roda depois que o bloco fecha. */
enum { A_NADA, A_VOLTAR, A_EDITAR, A_MPV, A_LIXEIRA };

static void barra_do_alto(void) {
	int acao = A_NADA;
	CLAY(CLAY_ID("pl-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 16, 16, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		if (gv_botao_ext(CLAY_ID("pl-voltar"), &(gv_botao_opcoes){ .icone = "arrow-left", .texto = "Voltar à galeria", .estilo = GV_BOTAO_SIMPLES, .altura = 42 })) {
			acao = A_VOLTAR;
		}
		CLAY(CLAY_ID("pl-nome"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(V.nome), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
		}
		if (gv_botao_redondo(CLAY_ID("pl-fav"), "star", 42, (V.k < G.nvis && midia_vis(V.k)->favorita), false)) {
			alternar_favorita(&V.m);
		}
		if (gv_botao_ext(CLAY_ID("pl-editar"), &(gv_botao_opcoes){ .icone = "edit", .texto = "Editar", .estilo = GV_BOTAO_CONTORNO, .altura = 42 })) {
			acao = A_EDITAR;
		}
		if (gv_botao_redondo(CLAY_ID("pl-mais"), "dots", 42, false, false)) {
			gv_popup_alternar(CLAY_ID("menu-pl-mais"));
		}
		GV_POPUP(CLAY_ID("menu-pl-mais"), CLAY_ID("pl-mais"), .alinhar = GV_ALINHAR_FIM, .largura = 300) {
			CLAY(CLAY_ID("pl-mais-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
				if (gv_menu_item(CLAY_ID("pl-m-mpv"), "external", "Abrir no mpv (janela separada)", NULL)) {
					acao = A_MPV;
					gv_popup_fechar(CLAY_ID("menu-pl-mais"));
				}
				if (gv_menu_item(CLAY_ID("pl-m-atlas"), "folder", "Mostrar no Atlas", NULL)) {
					char *pasta = g_path_get_dirname(V.caminho);
					char *argv[] = { "lumna-atlas", pasta, NULL };
					g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
					g_free(pasta);
					gv_popup_fechar(CLAY_ID("menu-pl-mais"));
				}
				if (gv_menu_item(CLAY_ID("pl-m-lixeira"), "trash", "Mover para a lixeira", "Del")) {
					acao = A_LIXEIRA;
					gv_popup_fechar(CLAY_ID("menu-pl-mais"));
				}
			}
		}
	}
	gl_midia copia = V.m;
	switch (acao) {
	case A_VOLTAR: player_fechar(); break;
	case A_EDITAR: player_fechar(); editor_video_abrir(&copia); break;
	case A_MPV: player_fechar(); abrir_no_player_externo(&copia); break;
	case A_LIXEIRA: {
		char *c[] = { copia.caminho };
		player_fechar();
		mover_para_lixeira(c, 1);
		break;
	}
	default: break;
	}
}

static void teclado(void) {
	for (int i = 0;; i++) {
		uint32_t t = gv_tecla_n(i);
		if (!t) {
			break;
		}
		V.ultimo_mov = gv_agora();
		uint32_t mods = gv_modificadores();
		if (t == 0xff1b) {
			if (V.tela_cheia) {
				tela_cheia(false);
			} else {
				player_fechar();
				return;
			}
		} else if (t == ' ') {
			vp_pausar(V.p, V.e.tocando);
		} else if (t == 0xff53) {
			vp_buscar(V.p, V.e.pos + ((mods & GV_MOD_SHIFT) ? 30 : 5));
		} else if (t == 0xff51) {
			vp_buscar(V.p, V.e.pos - ((mods & GV_MOD_SHIFT) ? 30 : 5));
		} else if (t == 0xff52) {
			vp_volume(V.p, MIN(100, V.e.volume + 5));
			G.cfg.volume = CLAMP(V.e.volume, 0, 100);
		} else if (t == 0xff54) {
			vp_volume(V.p, MAX(0, V.e.volume - 5));
			G.cfg.volume = CLAMP(V.e.volume, 0, 100);
		} else if (t == 'f' || t == 'F') {
			tela_cheia(!V.tela_cheia);
		} else if (t == 'm' || t == 'M') {
			vp_volume(V.p, V.e.volume > 0 ? 0 : 60);
		} else if (t == 'n' || t == 'N') {
			proximo_video(1);
		} else if (t == 'p' || t == 'P') {
			proximo_video(-1);
		} else if (t == 'e' || t == 'E') {
			gl_midia copia = V.m;
			player_fechar();
			editor_video_abrir(&copia);
			return;
		} else if (t == 0xffff) {
			char *c[] = { V.caminho };
			mover_para_lixeira(c, 1);
			player_fechar();
			return;
		}
	}
}

void player_desenhar(void) {
	if (!V.ativo || !V.p) {
		G.tela = T_BIBLIOTECA;
		return;
	}
	vp_atualizar(V.p, &V.e);
	double agora = gv_agora();
	Clay_Vector2 p = gv_ponteiro();
	if (fabsf(p.x - V.ptr.x) > 1 || fabsf(p.y - V.ptr.y) > 1 || gv_ponteiro_baixo()) {
		V.ultimo_mov = agora;
	}
	V.ptr = p;
	bool mostrar = !V.e.tocando || V.seek_arr || V.menu_aberto || agora - V.ultimo_mov < OCULTAR_APOS || V.e.erro;
	if (!player_tela_cheia() || mostrar) {
		/* a barra do alto some com o resto quando se assiste em tela cheia sem mexer */
	}
	teclado();
	if (!V.ativo) {
		return;
	}
	if (!V.tela_cheia || mostrar) {
		barra_do_alto();
		if (!V.ativo) {
			return;
		}
	}
	bool erro_mpv = false;
	Clay_ElementId area = CLAY_ID("pl-area");
	CLAY(area, gv_livre(desenhar_video, NULL, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = { 0, 0, 0, 255 } })) {
		/* o aviso do meio */
		if (V.e.carregando) {
			gv_texto(CLAY_STRING("Abrindo o vídeo…"), gv_t.tamanho_corpo + 4, gv_t.texto_secundario, GV_PESO_NORMAL);
		} else if (V.e.erro) {
			CLAY(CLAY_ID("pl-erro"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 14, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				gv_icone("video", 56, gv_t.texto_suave);
				gv_texto(CLAY_STRING("Não deu para tocar este vídeo."), gv_t.tamanho_corpo + 4, gv_t.texto, GV_PESO_NORMAL);
				if (gv_botao_ext(CLAY_ID("pl-erro-mpv"), &(gv_botao_opcoes){ .icone = "external", .texto = "Tentar no mpv", .estilo = GV_BOTAO_ELEVADO, .altura = 44 })) {
					erro_mpv = true;
				}
			}
		} else if (!V.e.tocando && !V.seek_arr) {
			if (gv_botao_redondo(CLAY_ID("pl-central"), "play", 84, true, false)) {
				vp_pausar(V.p, false);
			}
		}
		if (V.ativo && (!V.tela_cheia || mostrar) && V.e.aberto) {
			controles();
		}
	}
	if (erro_mpv) {
		gl_midia copia = V.m;
		player_fechar();
		abrir_no_player_externo(&copia);
		return;
	}
	/* clicar no vídeo pausa/toca; duplo clique, tela cheia */
	if (V.ativo && gv_tocado(area) && !V.e.erro) {
		if (agora < V.toque_ate) {
			tela_cheia(!V.tela_cheia);
			V.toque_ate = 0;
		} else {
			V.toque_ate = agora + 0.35;
			vp_pausar(V.p, V.e.tocando);
		}
	}
	/* o quadro, no tamanho da caixa de agora (do quadro anterior) */
	Clay_BoundingBox cx = gv_caixa(area);
	int w = (int)cx.width, h = (int)cx.height;
	if (w > 8 && h > 8) {
		uint8_t *d;
		int st;
		bool mudou = vp_quadro(V.p, w, h, &d, &st);
		if (mudou || !V.surf || V.dados != d || V.sw != w || V.sh != h) {
			if (V.surf) {
				cairo_surface_destroy(V.surf);
			}
			V.surf = cairo_image_surface_create_for_data(d, CAIRO_FORMAT_RGB24, w, h, st);
			V.dados = d;
			V.sw = w;
			V.sh = h;
			V.sstride = st;
		}
		if (mudou) {
			cairo_surface_mark_dirty(V.surf);
		}
	}
	if (V.e.tocando || V.e.carregando) {
		gv_acordar_em(0.25);
	}
	if (V.e.tocando && !mostrar) {
		gv_acordar_em(0.5);
	}
	mpris_atualizar();
}

/* ---------------- para os testes (LUMNA_GALERIA_DEBUG) ---------------- */

void player_estado(GString *s) {
	if (!V.ativo) {
		return;
	}
	char px[32] = "-1,-1,-1";
	if (V.surf && V.dados && V.sw > 4 && V.sh > 4) {
		const uint8_t *q = V.dados + (size_t)(V.sh / 2) * (size_t)V.sstride + (size_t)(V.sw / 2) * 4;
		g_snprintf(px, sizeof(px), "%d,%d,%d", q[2], q[1], q[0]);
	}
	g_string_append_printf(s, " pl=1 pl_tocando=%d pl_pos=%.1f pl_dur=%.1f pl_fim=%d pl_erro=%d pl_vol=%d pl_vel=%.2f pl_naudio=%d pl_nleg=%d pl_audio=%d pl_leg=%d pl_w=%d pl_h=%d pl_px=%s pl_rep=%d pl_tc=%d pl_k=%d pl_car=%d pl_mpris=%d",
		V.e.tocando, V.e.pos, V.e.dur, V.e.fim, V.e.erro, V.e.volume, V.e.velocidade, V.e.naudio, V.e.nlegenda, V.e.audio, V.e.legenda, V.e.w, V.e.h, px, V.e.repetir, V.tela_cheia, V.k, V.e.carregando, V.mm != NULL);
}
