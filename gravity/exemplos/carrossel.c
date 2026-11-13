/*
 * lumna-carrossel: a troca de modo em carrossel (Painel > Modos > "Troca com Super + Tab" = Carrossel). Aparece por cima de tudo, na
 * tela em que se apertou Super+Tab, com os três modos lado a lado e o escolhido no centro. Quem decide é o Orbit: Tab gira, soltar o Super
 * traz o modo do centro, Esc cancela. Este programa só desenha e avisa o Orbit se alguém clica num cartão:
 *   - o Orbit chama com LUMNA_SAIDA (a tela) e LUMNA_MODO (o modo de agora);
 *   - a cada aviso do Orbit ele pergunta "orbit-msg carrossel" ({"aberto":..., "alvo":...}) e anima até o alvo;
 *   - quando o Orbit diz que fechou, o carrossel esmaece e sai.
 * Os cartões desta primeira versão são ilustrações de cada modo (ainda não são miniaturas da tela de verdade).
 *   ./carrossel
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gravity.h"
#include "modos.h"

#define CHAVE_FUNDO 0x6c630001u
#define CHAVE_POS 0x6c630002u
#define CHAVE_EXP 0x6c630003u
#define CHAVE_CARTAO 0x6c630010u /* + 8 por cartão */

static char saida[32];
static int atual = MODO_DESKTOP; /* o modo desta tela ao abrir */
static int alvo = MODO_DESKTOP;  /* o cartão do centro (o que o Orbit disse) */
static int virtual_i;            /* o alvo sem dar a volta (0, 1, 2, 3...): a animação gira sempre pelo caminho curto */
static bool fechando, confirmando, iniciado; /* fechando: cancelou, esmaece; confirmando: o cartão do centro cresce até a tela */
static float galfa = 1.0f;                     /* a transparência de tudo, no quadro de agora (entrar, crescer e sair) */

static Clay_Color mult(Clay_Color c, float f) {
	c.a = (float)c.a * (f < 0 ? 0 : f > 1 ? 1 : f) * galfa;
	return c;
}

static float lerp(float a, float b, float t) {
	return a + (b - a) * t;
}

/* pergunta ao Orbit o que está acontecendo */
static void perguntar(gv_app *app) {
	char r[256];
	if (!orbit_pedido("carrossel", r, sizeof(r)) || !strstr(r, "\"ok\":true")) {
		fechando = true; /* sem Orbit: nada a mostrar */
		return;
	}
	const char *a = strstr(r, "\"alvo\":\"");
	if (a) {
		char id[16] = "";
		sscanf(a + 8, "%15[a-z]", id);
		int novo = modos_de_id(id);
		if (novo >= 0 && novo != alvo) {
			int delta = ((novo - alvo + 1 + MODO_N * 4) % MODO_N) - 1; /* -1, 0 ou 1: o caminho curto na volta de três */
			virtual_i += delta;
			alvo = novo;
		}
	}
	if (!strstr(r, "\"aberto\":true")) {
		if (strstr(r, "\"fim\":\"confirmado\"")) {
			confirmando = true; /* o Orbit já trocou, atrás deste fundo: o cartão cresce e o fundo some, mostrando o modo novo */
		} else {
			fechando = true;
		}
	}
	gv_app_redesenhar(app);
}

static void aviso(gv_app *app, int fd, void *dados) {
	(void)dados;
	modos_assinar_ler(fd);
	perguntar(app);
}

static void pedir_confirmar(int modo) {
	char pedido[64], r[128];
	snprintf(pedido, sizeof(pedido), "carrossel confirmar %s", modos_id(modo));
	orbit_pedido(pedido, r, sizeof(r));
}

/* a ilustração de dentro do cartão de cada modo */
static void miniatura(int m, float w, float h, float a, bool centro) {
	Clay_Color tinta = mult(gv_t.acento, a);
	Clay_Color barra = mult((Clay_Color){ 12, 10, 24, 255 }, a);
	float raio = h * 0.05f;
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h) } },
		.backgroundColor = mult(m == MODO_DESKTOP ? (Clay_Color){ 30, 22, 62, 255 } : m == MODO_GAMING ? (Clay_Color){ 36, 20, 70, 255 } : (Clay_Color){ 18, 14, 36, 255 }, a),
		.cornerRadius = CLAY_CORNER_RADIUS(raio * 2), .clip = { .horizontal = true, .vertical = true } }) {
		/* a barra de cima */
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(h * 0.09f) }, .padding = { (uint16_t)(w * 0.03f), (uint16_t)(w * 0.03f), 0, 0 },
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .childGap = 6 }, .backgroundColor = barra }) {
			gv_icone("desktop", h * 0.06f, tinta);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
		}
		/* o miolo */
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .childGap = (uint16_t)(h * 0.04f) } }) {
			gv_icone(modos_icone(m), h * (m == MODO_DESKTOP ? 0.26f : 0.34f), mult(centro ? gv_t.acento : gv_t.icone, a));
			if (m == MODO_MIDIA) {
				CLAY_AUTO_ID({ .layout = { .childGap = (uint16_t)(w * 0.02f) } }) {
					static const Clay_Color capas[4] = { { 190, 90, 60, 255 }, { 70, 110, 190, 255 }, { 150, 70, 170, 255 }, { 200, 160, 70, 255 } };
					for (int i = 0; i < 4; i++) {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(w * 0.12f), CLAY_SIZING_FIXED(h * 0.2f) } },
							.backgroundColor = mult(capas[i], a * 0.85f), .cornerRadius = CLAY_CORNER_RADIUS(raio) }) {}
					}
				}
			}
		}
		/* a dock, só no Desktop */
		if (m == MODO_DESKTOP) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(h * 0.14f) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(w * 0.3f), CLAY_SIZING_FIXED(h * 0.08f) } },
					.backgroundColor = mult((Clay_Color){ 255, 255, 255, 50 }, a), .cornerRadius = CLAY_CORNER_RADIUS(h * 0.04f) }) {}
			}
		}
	}
}

static void cartao(int m, float pos, float largura, bool *tocado_algum, float e, float tela_w, float tela_h, float dy) {
	float d = (float)m - pos;
	d -= (float)MODO_N * roundf(d / (float)MODO_N); /* o cartão mais perto da posição, na volta de três */
	float ad = fabsf(d);
	float esc = 1.0f - 0.30f * fminf(ad, 1.0f);
	float a = ad <= 1.0f ? 1.0f - 0.45f * ad : fmaxf(0.0f, 0.55f - (ad - 1.0f) * 1.1f); /* some ao dar a volta por trás */
	float w = largura * esc, h = w * 9.0f / 16.0f;
	float centro = fmaxf(0.0f, 1.0f - ad); /* 1 no centro, 0 nos lados */
	/* confirmado: o do centro cresce até o tamanho da tela (e a moldura some); os outros esmaecem */
	float resto = 1.0f; /* o que sobra de moldura */
	float texto = 1.0f; /* e de nome (some mais depressa: o cartão cobre o lugar dele) */
	bool cresce = e > 0.0f && centro > 0.5f;
	float fuga = 0.0f; /* os vizinhos: quanto já se afastaram para fora da tela */
	if (cresce) {
		w = lerp(w, tela_w, e);
		h = lerp(h, tela_h, e);
		resto = 1.0f - e;
		texto = fmaxf(0.0f, 1.0f - 3.0f * e);
	} else {
		fuga = e * 0.55f * tela_w * (d < 0 ? -1.0f : 1.0f);
	}
	float desloc_y = cresce ? e * dy : 0.0f; /* leva o centro do cartão ao centro da tela (o palco fica entre o título e as teclas) */
	Clay_ElementId id = CLAY_IDI("cartao", (uint32_t)m);
	if (gv_tocado(id) && a > 0.5f && !confirmando) {
		*tocado_algum = true;
		pedir_confirmar(m);
	}
	if (gv_por_cima(id)) {
		*tocado_algum = true;
	}
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 14, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 30 - (int16_t)(ad * 10),
			.attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER }, .offset = { d * largura * 0.92f * (cresce ? 1.0f - e : 1.0f) + fuga, desloc_y } } }) {
		/* o brilho de fora e a borda: só o do centro acende */
		CLAY_AUTO_ID({ .layout = { .padding = CLAY_PADDING_ALL((uint16_t)(6.0f * resto)) }, .backgroundColor = mult(gv_t.acento, 0.20f * centro * a * resto),
			.cornerRadius = CLAY_CORNER_RADIUS((h * 0.1f + 6) * resto) }) {
			CLAY_AUTO_ID({ .layout = { .padding = CLAY_PADDING_ALL((uint16_t)(3.0f * resto)) }, .backgroundColor = mult(gv_t.borda, a * (1.0f - centro)),
				.cornerRadius = CLAY_CORNER_RADIUS(h * 0.1f * resto), .border = { .color = mult(gv_t.acento, a * centro), .width = GV_BORDA(2.0f * resto) } }) {
				miniatura(m, w, h, a, centro > 0.5f);
			}
		}
		/* o nome e o resto, pendurados embaixo da miniatura: o que muda de texto não mexe na miniatura */
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
			.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 30 - (int16_t)(ad * 10), .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
				.attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM }, .offset = { 0, 14 } } }) {
			CLAY_AUTO_ID({ .layout = { .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone(modos_icone(m), 22.0f + 8.0f * centro, mult(centro > 0.5f ? gv_t.acento : gv_t.icone, a * texto));
				gv_texto(gv_str(modos_nome(m)), 17.0f + 7.0f * centro, mult(centro > 0.5f ? gv_t.texto : gv_t.texto_secundario, a * texto), centro > 0.5f ? GV_PESO_MEDIO : GV_PESO_NORMAL);
			}
			if (centro > 0.6f) {
				gv_texto(gv_str(modos_descricao(m)), gv_t.tamanho_corpo - 1, mult(gv_t.texto_suave, centro * a * texto), GV_PESO_NORMAL);
				if (m == atual) {
					gv_texto(CLAY_STRING("Modo atual desta tela"), gv_t.tamanho_corpo - 2, mult(gv_t.acento, centro * a * texto), GV_PESO_MEDIO);
				}
			}
		}
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	if (!iniciado) {
		iniciado = true;
		gv_anim(CHAVE_FUNDO, 0.0f, 0); /* nasce transparente e acende */
		gv_anim(CHAVE_POS, (float)virtual_i, 0);
		gv_anim(CHAVE_EXP, 1.0f, 0); /* entra de tela cheia: o do centro encolhe até ser cartão e os vizinhos chegam de fora */
	}
	float fundo = gv_anim(CHAVE_FUNDO, fechando ? 0.0f : 1.0f, fechando ? 140 : 180);
	float e = gv_anim(CHAVE_EXP, confirmando ? 1.0f : 0.0f, 420); /* o zoom: 1 é o cartão do centro na tela cheia e os vizinhos fora; 0, a seleção. Entra de 1 a 0 e confirmar vai de 0 a 1 */
	if ((fechando && fundo < 0.02f) || (confirmando && e > 0.995f)) {
		gv_app_sair(app);
		return;
	}
	/* crescendo, na segunda metade tudo esmaece e deixa ver o modo novo, que o Orbit já pôs atrás */
	float s = !confirmando || e < 0.6f ? 0.0f : fminf(1.0f, (e - 0.6f) / 0.4f);
	galfa = fundo * (1.0f - s * s * (3.0f - 2.0f * s));
	float pos = gv_anim(CHAVE_POS, (float)virtual_i, 280);
	Clay_BoundingBox caixa = gv_caixa(CLAY_ID("raiz"));
	Clay_BoundingBox palco = gv_caixa(CLAY_ID("palco"));
	float tela_w = caixa.width > 100 ? caixa.width : 1280, tela_h = caixa.height > 100 ? caixa.height : 720;
	float largura = fminf(tela_w * 0.34f, 640.0f);
	float dy = palco.height > 0 ? (tela_h / 2.0f) - (palco.y - caixa.y + palco.height / 2.0f) : 0.0f;
	float resto = fmaxf(0.0f, 1.0f - 3.0f * e); /* o título e as teclas somem logo que o cartão começa a crescer */
	bool sobre_cartao = false;
	CLAY(CLAY_ID("raiz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.padding = { 40, 40, 36, 30 }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
		.backgroundColor = mult((Clay_Color){ 7, 6, 16, 255 }, 1.0f) }) { /* o fundo é do carrossel: opaco, cobre a barra, a dock e o modo de trás */
		/* em cima: o título e a hora */
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("grid", 22, mult(gv_t.acento, resto));
				gv_texto(CLAY_STRING("Workspaces"), 22, mult(gv_t.texto, resto), GV_PESO_MEDIO);
			}
			char hora[16];
			time_t t = time(NULL);
			struct tm tm;
			localtime_r(&t, &tm);
			strftime(hora, sizeof(hora), "%H:%M", &tm);
			gv_texto(gv_str(hora), gv_t.tamanho_corpo, mult(gv_t.texto_suave, resto), GV_PESO_NORMAL);
			gv_acordar_em(20.0);
		}
		/* o palco: os cartões flutuam em volta do centro dele */
		CLAY(CLAY_ID("palco"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			for (int m = 0; m < MODO_N; m++) {
				cartao(m, pos, largura, &sobre_cartao, e, tela_w, tela_h, dy);
			}
		}
		/* embaixo: as teclas */
		gv_texto(CLAY_STRING("Tab ou ← →  gira     Soltar o Super  traz o modo     Esc  cancela"), gv_t.tamanho_corpo - 1, mult(gv_t.texto_suave, 0.8f * resto), GV_PESO_NORMAL);
	}
	if (!fechando && !confirmando && gv_tocado(CLAY_ID("raiz")) && !sobre_cartao) {
		char r[64];
		orbit_pedido("carrossel cancelar", r, sizeof(r)); /* um clique fora dos cartões cancela */
	}
}

int main(void) {
	const char *s = getenv("LUMNA_SAIDA");
	snprintf(saida, sizeof(saida), "%s", s ? s : "");
	int m = modos_de_id(getenv("LUMNA_MODO"));
	if (m >= 0) {
		atual = alvo = m;
	}
	virtual_i = alvo;
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Workspaces", .app_id = "lumna-carrossel", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
		.ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1 /* por cima da barra e da dock */, .largura = 0, .altura = 0, .saida = saida[0] ? saida : NULL });
	if (!app) {
		return 1;
	}
	int fd = modos_assinar();
	if (fd >= 0) {
		gv_app_vigiar(app, fd, aviso, NULL);
	}
	perguntar(app); /* o Orbit pode ter girado (ou fechado) enquanto este programa abria */
	return gv_app_correr(app, desenhar, NULL);
}
