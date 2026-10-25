/*
 * A topbar do desktop novo, em Gravity: o LUMNA e o modo à esquerda, as áreas de trabalho ao
 * meio, Wi-Fi, som, Bluetooth e o relógio à direita. O Bluetooth abre o menu da tela nova num
 * popup que desce da barra (uma camada de 40 px; o menu sai dela).
 * Os aparelhos ainda são de mentira: o BlueZ vem com os serviços do sistema.
 *   ./barra
 */
#include <stdio.h>
#include <time.h>

#include "gravity.h"

static int area;
static bool bluetooth = true;

static const struct {
	const char *icone, *nome;
	int bateria; /* -1: não diz */
} conectados[] = {
	{ "headphones", "Fone sem fio", 80 },
	{ "gamepad", "Controle de jogo", -1 },
};

static const struct {
	const char *icone, *nome;
} disponiveis[] = {
	{ "keyboard", "Teclado Bluetooth" },
	{ "speaker", "Caixa de som" },
};

static int pareando = -1;

static void titulo_secao(Clay_ElementId id, const char *texto) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 10, 6 } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void aparelho(Clay_ElementId id, const char *icone, const char *nome, const char *estado) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(64) }, .padding = { 12, 6, 8, 8 }, .childGap = 16,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone(icone, 26, gv_t.icone);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			gv_texto(gv_str(nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
			if (estado) {
				gv_texto(gv_str(estado), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
}

static void menu_bluetooth(void) {
	GV_POPUP(CLAY_ID("menu-bt"), CLAY_ID("bt"), .alinhar = GV_ALINHAR_FIM, .largura = 440, .distancia = 10) {
		CLAY(CLAY_ID("bt-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .padding = { 12, 10, 0, 0 },
			.childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone("bluetooth", 24, gv_t.icone);
			CLAY(CLAY_ID("bt-titulo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Bluetooth"), gv_t.tamanho_destaque + 1, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_interruptor(CLAY_ID("bt-ligado"), &bluetooth)) {
				printf("bluetooth: %s\n", bluetooth ? "ligado" : "desligado");
			}
		}
		gv_separador(false, 0);
		if (!bluetooth) {
			CLAY(CLAY_ID("bt-desligado"), { .layout = { .padding = { 12, 12, 16, 16 } } }) {
				gv_texto(CLAY_STRING("O Bluetooth está desligado."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		} else {
			titulo_secao(CLAY_ID("bt-s1"), "Conectados");
			for (int i = 0; i < 2; i++) {
				CLAY(CLAY_IDI("bt-con", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10,
					.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					aparelho(CLAY_IDI("bt-con-ap", i), conectados[i].icone, conectados[i].nome, "Conectado");
					if (conectados[i].bateria >= 0) {
						gv_icone("battery", 20, gv_t.texto_secundario);
						gv_texto(gv_fmt("%d%%", conectados[i].bateria), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
					if (gv_botao(CLAY_IDI("bt-mais", i), "more", NULL, GV_BOTAO_SIMPLES)) {
						printf("opções de %s\n", conectados[i].nome);
					}
				}
				if (i == 0) {
					gv_separador(false, 0);
				}
			}
			gv_separador(false, 0);
			CLAY(CLAY_ID("bt-s2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				titulo_secao(CLAY_ID("bt-s2-titulo"), "Disponíveis");
				if (gv_botao(CLAY_ID("bt-buscar"), "refresh", "Buscar", GV_BOTAO_SIMPLES)) {
					printf("buscar aparelhos\n");
				}
			}
			for (int i = 0; i < 2; i++) {
				CLAY(CLAY_IDI("bt-disp", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .right = 6 },
					.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					aparelho(CLAY_IDI("bt-disp-ap", i), disponiveis[i].icone, disponiveis[i].nome, pareando == i ? "Pareando…" : NULL);
					if (gv_botao_ext(CLAY_IDI("bt-parear", i), &(gv_botao_opcoes){ .texto = "Parear", .estilo = GV_BOTAO_ELEVADO,
							.altura = 38, .desativado = pareando == i })) {
						pareando = i;
						printf("parear %s\n", disponiveis[i].nome);
					}
				}
				if (i == 0) {
					gv_separador(false, 0);
				}
			}
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("bt-config"), "settings", "Configurações de Bluetooth", NULL)) {
			printf("abrir: configurações de Bluetooth\n");
			gv_popup_fechar(CLAY_ID("menu-bt"));
		}
	}
}

static bool icone_barra(Clay_ElementId id, const char *icone, bool aberto) {
	bool tocado = gv_tocado(id);
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(32) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = aberto ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
	}) {
		tocado |= gv_focavel(id, gv_t.raio_pequeno);
		gv_icone(icone, 20, gv_t.icone);
	}
	return tocado;
}

static void desenhar(gv_app *app, void *dados) {
	(void)app;
	(void)dados;
	CLAY(CLAY_ID("barra"), {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 18, 14, 0, 0 },
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.fundo,
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } },
	}) {
		/* esquerda: o LUMNA e o modo */
		CLAY(CLAY_ID("esq"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone("lumna", 20, gv_t.acento);
			gv_texto(CLAY_STRING("L U M N A"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			gv_separador(true, 18);
			gv_texto(CLAY_STRING("Desktop"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		/* meio: as áreas de trabalho */
		CLAY(CLAY_ID("areas"), { .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			for (int i = 0; i < 3; i++) {
				Clay_ElementId id = CLAY_IDI("area", i);
				if (gv_tocado(id)) {
					area = i;
				}
				float w = gv_anim(id.id, area == i ? 34 : 22, gv_t.duracao_normal);
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(16) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(4) } },
						.backgroundColor = area == i ? gv_t.acento : gv_t.borda_controle, .cornerRadius = CLAY_CORNER_RADIUS(2) }) {}
				}
			}
		}
		/* direita: o estado do sistema e o relógio */
		CLAY(CLAY_ID("dir"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6,
			.childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER } } }) {
			icone_barra(CLAY_ID("wifi"), "wifi", false);
			icone_barra(CLAY_ID("som"), "volume", false);
			if (icone_barra(CLAY_ID("bt"), "bluetooth", gv_popup_aberto(CLAY_ID("menu-bt")))) {
				gv_popup_alternar(CLAY_ID("menu-bt"));
			}
			time_t agora = time(NULL);
			struct tm t;
			localtime_r(&agora, &t);
			CLAY(CLAY_ID("relogio"), { .layout = { .padding = { 10, 4, 0, 0 } } }) {
				gv_texto(gv_fmt("%02d:%02d", t.tm_hour, t.tm_min), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			}
			/* acorda na virada do minuto */
			gv_acordar_em(60 - t.tm_sec + 0.05);
		}
	}
	menu_bluetooth();
	fflush(stdout);
}

int main(void) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Barra", .app_id = "lumna-barra", .tipo = GV_CAMADA,
		.nivel = GV_NIVEL_CIMA, .ancoras = GV_ANCORA_CIMA | GV_ANCORA_ESQUERDA | GV_ANCORA_DIREITA,
		.altura = 40, .zona_exclusiva = 40 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
