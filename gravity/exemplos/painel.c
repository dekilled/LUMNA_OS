/*
 * O Painel de controle do LUMNA, em Gravity: a janela das telas novas (barra própria, menu ao
 * lado, abas, linhas de ajuste, deslizante e botões) para conferir a paleta e os componentes.
 * Os ajustes ainda não mexem no sistema: cada mudança sai no terminal.
 *   ./painel
 */
#include <stdio.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"

static const struct {
	const char *icone, *nome;
} secoes[] = {
	{ "wifi", "Rede e Internet" },
	{ "bluetooth", "Bluetooth e dispositivos" },
	{ "volume", "Som" },
	{ "desktop", "Tela" },
	{ "brush", "Aparência" },
	{ "bell", "Notificações" },
	{ "battery", "Energia" },
	{ "shield", "Privacidade" },
	{ "info", "Sobre o LUMNA" },
};
#define NSECOES (int)(sizeof(secoes) / sizeof(secoes[0]))

static char busca[64];
static int secao = 4;
static int aba;
static bool escuro = true, transparencia = true, animacoes = true, luz_noturna;
static float escala = 0.25f, brilho = 0.8f;

static void menu(void) {
	CLAY(CLAY_ID("menu"), {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(280), CLAY_SIZING_GROW(0) },
			.padding = { 14, 14, 16, 16 }, .childGap = 4 },
		.border = { .color = gv_t.borda, .width = { .right = 1 } },
	}) {
		CLAY(CLAY_ID("menu-busca"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .bottom = 10 } } }) {
			gv_campo(CLAY_ID("busca"), busca, sizeof(busca), "Buscar ajustes", GV_CAMPO_BUSCA);
		}
		for (int i = 0; i < NSECOES; i++) {
			Clay_ElementId id = CLAY_IDI("secao", i);
			if (gv_tocado(id)) {
				secao = i;
				aba = 0;
			}
			bool esta = secao == i;
			bool sobre = gv_por_cima(id);
			CLAY(id, {
				.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) }, .padding = { 0, 12, 0, 0 },
					.childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = esta ? gv_t.selecionado : sobre ? gv_t.sob_cursor : (Clay_Color){ 0 },
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			}) {
				/* a marca do acento à esquerda do item escolhido */
				CLAY(CLAY_IDI("secao-marca", i), {
					.layout = { .sizing = { CLAY_SIZING_FIXED(3), CLAY_SIZING_FIXED(18) } },
					.backgroundColor = esta ? gv_t.acento : (Clay_Color){ 0 },
					.cornerRadius = CLAY_CORNER_RADIUS(2),
				}) {}
				gv_icone(secoes[i].icone, 20, esta ? gv_t.texto : gv_t.texto_secundario);
				gv_texto(gv_str(secoes[i].nome), gv_t.tamanho_corpo, esta ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
}

static void aparencia_geral(void) {
	if (gv_linha(CLAY_ID("escuro"), "moon", "Tema escuro", "Superfícies escuras em todo o sistema", &escuro)) {
		printf("tema escuro: %s\n", escuro ? "sim" : "não");
	}
	if (gv_linha(CLAY_ID("transp"), "window", "Transparência", "Painéis e menus deixam o fundo aparecer", &transparencia)) {
		printf("transparência: %s\n", transparencia ? "sim" : "não");
	}
	if (gv_linha(CLAY_ID("anim"), "sparkle", "Animações", "Movimento ao abrir, fechar e trocar de tela", &animacoes)) {
		gv_t.animacoes = animacoes;
		printf("animações: %s\n", animacoes ? "sim" : "não");
	}
	gv_separador(false, 0);
	if (gv_linha(CLAY_ID("fundo"), "image", "Papel de parede", "Aurora violeta", NULL)) {
		printf("abrir: papel de parede\n");
	}
	if (gv_linha(CLAY_ID("cursor"), "mouse", "Cursor", "LUMNA, 24 px", NULL)) {
		printf("abrir: cursor\n");
	}
}

static void aparencia_tela(void) {
	CLAY(CLAY_ID("ajuste-escala"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
		.padding = { 16, 16, 12, 12 }, .childGap = 8 } }) {
		CLAY(CLAY_ID("escala-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			CLAY(CLAY_ID("escala-nome"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Tamanho do texto e dos itens"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
			}
			gv_texto(gv_fmt("%d%%", 100 + (int)(escala * 4 + 0.5f) * 25), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		gv_deslizante(CLAY_ID("escala"), &escala, 0, 0);
	}
	CLAY(CLAY_ID("ajuste-brilho"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
		.padding = { 16, 16, 12, 12 }, .childGap = 8 } }) {
		CLAY(CLAY_ID("brilho-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			CLAY(CLAY_ID("brilho-nome"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Brilho"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
			}
			gv_texto(gv_fmt("%d%%", (int)(brilho * 100 + 0.5f)), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		gv_deslizante(CLAY_ID("brilho"), &brilho, 0, 0);
	}
	gv_separador(false, 0);
	if (gv_linha(CLAY_ID("noturna"), "sun", "Luz noturna", "Tons mais quentes depois das 20:00", &luz_noturna)) {
		printf("luz noturna: %s\n", luz_noturna ? "sim" : "não");
	}
}

static void conteudo(gv_app *app) {
	CLAY(CLAY_ID("conteudo"), {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.padding = { 32, 32, 24, 24 }, .childGap = 16 },
	}) {
		CLAY(CLAY_ID("cabeca"), { .layout = { .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone(secoes[secao].icone, 28, gv_t.icone);
			gv_texto(gv_str(secoes[secao].nome), gv_t.tamanho_titulo, gv_t.texto, GV_PESO_SEMINEGRITO);
		}
		static const char *const abas[] = { "Geral", "Tela", "Cores" };
		if (secao != 4) {
			gv_texto(CLAY_STRING("Esta seção chega junto com o serviço dela."), gv_t.tamanho_corpo, gv_t.texto_secundario,
				GV_PESO_NORMAL);
		} else {
			gv_abas(CLAY_ID("abas"), abas, 3, &aba);
		}
		if (secao == 4) CLAY(CLAY_ID("linhas"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
			.childGap = 2 } }) {
			if (aba == 0) {
				aparencia_geral();
			} else if (aba == 1) {
				aparencia_tela();
			} else {
				gv_texto(CLAY_STRING("Cor de acento: violeta do LUMNA"), gv_t.tamanho_corpo, gv_t.texto_secundario,
					GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_ID("espaco"), { .layout = { .sizing = { .height = CLAY_SIZING_GROW(0) } } }) {}
		CLAY(CLAY_ID("rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10,
			.childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			if (gv_botao(CLAY_ID("restaurar"), "refresh", "Restaurar padrão", GV_BOTAO_ELEVADO)) {
				escuro = transparencia = animacoes = true;
				luz_noturna = false;
				escala = 0.25f;
				brilho = 0.8f;
				printf("restaurar padrão\n");
			}
			if (gv_botao(CLAY_ID("aplicar"), "check", "Aplicar", GV_BOTAO_CHEIO)) {
				printf("aplicar\n");
			}
		}
	}
	fflush(stdout);
	(void)app;
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
		CLAY(CLAY_ID("corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			menu();
			conteudo(app);
		}
	}
	if (gv_tecla() == XKB_KEY_Escape && !gv_focado(CLAY_ID("busca"))) {
		gv_app_sair(app);
	}
}

int main(void) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Painel de controle", .app_id = "lumna-painel",
		.largura = 1040, .altura = 680 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
