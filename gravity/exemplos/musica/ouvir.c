/* "Ouvir agora": a capa, a faixa e a letra (sincronizada ou não). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

#define ROL_LETRA CLAY_ID("mu-letra-rol")

/* ---------------- as barrinhas de "tocando agora" ---------------- */

static bool barras_flag[2] = { false, true };

static void desenhar_barras(cairo_t *cr, Clay_BoundingBox cx, void *dados) {
	bool animar = *(bool *)dados;
	double t = gv_agora(), w = cx.width / 5.0;
	cairo_set_source_rgba(cr, 0.69, 0.45, 1.0, 1);
	for (int i = 0; i < 3; i++) {
		double f = animar ? 0.35 + 0.65 * (0.5 + 0.5 * sin(t * (5.0 + i * 1.7) + i * 2.0)) : 0.45 + 0.2 * i;
		double h = cx.height * f;
		cairo_rectangle(cr, cx.x + w * (1 + 2 * i) - w * 0.5, cx.y + cx.height - h, w, h);
		cairo_fill(cr);
	}
}

void barras_tocando(Clay_ElementId id, float lado, bool animar) {
	CLAY(id, gv_livre(desenhar_barras, &barras_flag[animar ? 1 : 0], (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado) } } })) {}
}

/* ---------------- a letra ---------------- */

typedef struct {
	char caminho[4096];
	mu_letra l;
	bool ok;
} letra_job;

static void letra_chegou(void *d) {
	letra_job *j = d;
	const mu_faixa *f = faixa_atual();
	if (f && !strcmp(f->caminho, M.letra_de)) {
		mu_letra_liberar(&M.letra);
		M.letra = j->l;
		memset(&j->l, 0, sizeof(j->l));
		M.letra_lendo = false;
		M.letra_linha = -2;
		gv_app_redesenhar(M.app);
	}
	mu_letra_liberar(&j->l);
	g_free(j);
}

static gpointer letra_thread(gpointer d) {
	letra_job *j = d;
	j->ok = mu_letra_ler(j->caminho, &j->l);
	gv_na_linha_principal(letra_chegou, j);
	return NULL;
}

void ouvir_letra_mudou(void) {
	mu_letra_liberar(&M.letra);
	M.letra_linha = -2;
	M.letra_seguir = M.cfg.letras_seguir;
	const mu_faixa *f = faixa_atual();
	if (!f) {
		M.letra_de[0] = 0;
		M.letra_lendo = false;
		return;
	}
	g_strlcpy(M.letra_de, f->caminho, sizeof(M.letra_de));
	M.letra_lendo = true;
	letra_job *j = g_new0(letra_job, 1);
	g_strlcpy(j->caminho, f->caminho, sizeof(j->caminho));
	g_thread_unref(g_thread_new("musica-letra", letra_thread, j));
}

static float altura_da_linha(void) {
	static const float A[] = { 56, 70, 86 };
	return A[CLAMP(M.cfg.letras_tamanho, 0, 2)];
}

void ouvir_quadro(void) {
	if (!M.letra.sincronizada || M.letra.n == 0) {
		M.letra_linha = M.letra.sincronizada ? -1 : M.letra_linha;
		return;
	}
	double pos = M.carregada_no_motor ? M.est.pos : M.pos_pendente;
	int l = mu_letra_linha(&M.letra, pos);
	if (l != M.letra_linha) {
		M.letra_linha = l;
		if (M.letra_seguir && M.vista == V_OUVIR) {
			float alt = gv_caixa(ROL_LETRA).height > 0 ? gv_caixa(ROL_LETRA).height : 500;
			gv_rolar_para(ROL_LETRA, 0, fmaxf(0, (float)(l < 0 ? 0 : l) * altura_da_linha() - alt / 2 + altura_da_linha()), true);
		}
	}
}

static void desenhar_brilho(cairo_t *cr, Clay_BoundingBox cx, void *dados) {
	float *c = dados;
	cairo_pattern_t *p = cairo_pattern_create_radial(cx.x + cx.width * 0.72, cx.y + cx.height * 0.55, 10, cx.x + cx.width * 0.72, cx.y + cx.height * 0.55, fmax(cx.width, cx.height) * 0.7);
	cairo_pattern_add_color_stop_rgba(p, 0, c[0], c[1], c[2], 0.30);
	cairo_pattern_add_color_stop_rgba(p, 1, c[0], c[1], c[2], 0.0);
	cairo_set_source(cr, p);
	cairo_rectangle(cr, cx.x, cx.y, cx.width, cx.height);
	cairo_fill(cr);
	cairo_pattern_destroy(p);
}

static void letra_corpo(void) {
	static const float N[] = { 26, 32, 38 }, G[] = { 34, 44, 52 };
	float tam = N[CLAY_ID("x").id ? CLAMP(M.cfg.letras_tamanho, 0, 2) : 1], grande = G[CLAMP(M.cfg.letras_tamanho, 0, 2)], alt = altura_da_linha();
	if (!M.cfg.letras_mostrar) {
		gv_texto(CLAY_STRING("As letras estão desligadas nas configurações."), gv_t.tamanho_corpo + 2, gv_t.texto_suave, GV_PESO_NORMAL);
		return;
	}
	if (M.letra_lendo) {
		gv_texto(CLAY_STRING("Procurando a letra…"), gv_t.tamanho_corpo + 2, gv_t.texto_suave, GV_PESO_NORMAL);
		return;
	}
	if (M.letra.n == 0) {
		CLAY(CLAY_ID("mu-sem-letra"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
			gv_texto(CLAY_STRING("Sem letra para esta faixa."), gv_t.tamanho_corpo + 4, gv_t.texto_secundario, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("Ponha um arquivo .lrc com o mesmo nome ao lado da música (ou a letra na tag do arquivo)."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		return;
	}
	CLAY(CLAY_ID("mu-letra-esp0"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(alt) } } }) {}
	for (int i = 0; i < M.letra.n && i < 400; i++) {
		bool atual = M.letra.sincronizada && i == M.letra_linha;
		Clay_ElementId id = CLAY_IDI("mu-letra-l", (uint32_t)i);
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(alt) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
			CLAY(CLAY_IDI("mu-letra-t", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(52) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (atual) {
					char b[24];
					mu_duracao_texto((int)M.letra.v[i].tempo, b, sizeof(b));
					gv_texto(gv_str(b), gv_t.tamanho_corpo - 2, gv_t.acento_cursor, GV_PESO_NORMAL);
				}
			}
			CLAY(CLAY_IDI("mu-letra-b", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(3), CLAY_SIZING_FIXED(atual ? grande + 6 : 0) } }, .backgroundColor = gv_t.acento }) {}
			CLAY(CLAY_IDI("mu-letra-x", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
				Clay_Color cor = atual ? gv_cor(0xd8c4ffff) : (M.letra.sincronizada ? gv_alfa(gv_t.texto_secundario, 0.78f) : gv_t.texto);
				gv_texto(gv_str(M.letra.v[i].texto), atual ? grande : tam, cor, atual ? GV_PESO_MEDIO : GV_PESO_NORMAL);
			}
		}
		if (M.letra.sincronizada && gv_tocado(id) && M.carregada_no_motor) {
			mp_buscar(M.mp, M.letra.v[i].tempo);
		}
	}
	CLAY(CLAY_ID("mu-letra-esp1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(alt * 4) } } }) {}
}

/* ---------------- a tela ---------------- */

void tela_ouvir(void) {
	const mu_faixa *f = faixa_atual();
	static float cor[3];
	if (f) {
		capa_cor(f, &cor[0], &cor[1], &cor[2]);
	}
	CLAY(CLAY_ID("mu-ouvir"), gv_livre(desenhar_brilho, cor, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {
		if (!f) {
			CLAY(CLAY_ID("mu-ouvir-vazio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 18, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("music", 80, gv_t.acento);
				gv_texto(CLAY_STRING("Nada tocando"), 36, gv_t.texto, GV_PESO_NORMAL);
				gv_texto(gv_fmt("%d %s na biblioteca", M.ac.n, M.ac.n == 1 ? "faixa" : "faixas"), gv_t.tamanho_corpo + 2, gv_t.texto_secundario, GV_PESO_NORMAL);
				CLAY(CLAY_ID("mu-ouvir-bt"), { .layout = { .childGap = 14 } }) {
					if (gv_botao_ext(CLAY_ID("mu-reproduzir"), &(gv_botao_opcoes){ .icone = "play", .texto = "Tocar a biblioteca", .estilo = GV_BOTAO_CHEIO, .altura = 50, .desativado = M.ac.n == 0 })) {
						alternar_toque();
					}
					if (gv_botao_ext(CLAY_ID("mu-aleatorio-bt"), &(gv_botao_opcoes){ .icone = "shuffle", .texto = "Aleatório", .estilo = GV_BOTAO_CONTORNO, .altura = 50, .desativado = M.ac.n == 0 })) {
						M.cfg.aleatorio = true;
						alternar_toque();
					}
				}
			}
		} else
		CLAY(CLAY_ID("mu-ouvir-cx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 20, 0, 6, 0 } }, .clip = { .horizontal = true, .vertical = true } }) {
			CLAY(CLAY_ID("mu-ouvir-esq"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(340), CLAY_SIZING_GROW(0) }, .padding = { 0, 24, 0, 0 }, .childGap = 8 },
				.border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
				capa_quadrada(CLAY_ID("mu-ouvir-capa"), f, 292, 14);
				CLAY(CLAY_ID("mu-ouvir-tit"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 10, 0 } }, .clip = { .horizontal = true } }) {
					gv_texto(gv_str(f->titulo), 32, gv_t.texto, GV_PESO_MEDIO);
				}
				gv_texto(gv_str(f->artista), 24, gv_t.acento_cursor, GV_PESO_NORMAL);
				gv_texto(gv_str(f->album), gv_t.tamanho_corpo + 3, gv_t.texto, GV_PESO_NORMAL);
				CLAY(CLAY_ID("mu-ouvir-bts"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 4, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					bool clicou;
					coracao(CLAY_ID("mu-coracao-o"), eh_favorita(f->caminho), 46, &clicou);
					if (clicou) {
						mu_faixa copia;
						mu_faixa_copiar(&copia, f);
						alternar_favorita(&copia);
						mu_faixa_liberar(&copia);
					}
					if (gv_botao_redondo(CLAY_ID("mu-mais-det"), "dots", 46, false, false)) {
						int k = acervo_indice(f->caminho);
						if (k >= 0) {
							menu_da_faixa(k);
							gv_menu_pedir_no_ponteiro(CLAY_ID("menu-faixa"));
						}
					}
				}
			}
			CLAY(CLAY_ID("mu-ouvir-dir"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 36, 20, 0, 0 } } }) {
				CLAY(CLAY_ID("mu-letra-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_texto(CLAY_STRING("Letra"), 44, gv_t.texto, GV_PESO_MEDIO);
					if (M.cfg.letras_mostrar && M.letra.n > 0) {
						CLAY(CLAY_ID("mu-letra-chip"), { .layout = { .padding = { 12, 14, 6, 6 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
							.backgroundColor = gv_alfa(gv_t.acento, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(20), .border = { .color = gv_alfa(gv_t.acento, 0.5f), .width = GV_BORDA(1) } }) {
							CLAY(CLAY_ID("mu-letra-pt"), { .layout = { .sizing = { CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8) } }, .backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
							gv_texto(M.letra.sincronizada ? CLAY_STRING("Sincronizada") : CLAY_STRING("Não sincronizada"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
						}
					}
					CLAY(CLAY_ID("mu-letra-folga"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
					if (M.letra.sincronizada) {
						CLAY(CLAY_ID("mu-seguir-cx"), { .layout = { .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
							gv_interruptor(CLAY_ID("mu-seguir"), &M.letra_seguir);
							gv_texto(CLAY_STRING("Seguir reprodução"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
						}
					}
				}
				CLAY(ROL_LETRA, gv_rolagem(ROL_LETRA, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
					.padding = { 0, 10, 0, 0 } } })) {
					letra_corpo();
				}
			}
		}
	}
}
