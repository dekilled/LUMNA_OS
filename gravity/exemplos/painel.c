/*
 * O Painel de controle do LUMNA, em Gravity, como na tela nova: abas com a busca à direita, a
 * grade de atalhos em duas colunas e as atualizações no rodapé. Serve para conferir a paleta e
 * os componentes; os ajustes ainda não mexem no sistema: cada toque sai no terminal.
 *   ./painel
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"

struct atalho {
	const char *icone, *titulo, *subtitulo;
};

/* em cada aba, a coluna da esquerda e depois a da direita */
static const struct atalho geral[] = {
	{ "brush", "Aparência", "Cores, tema e papel de parede" },
	{ "wifi", "Rede e internet", "Wi-Fi, cabo e conexões" },
	{ "bluetooth", "Bluetooth", "Dispositivos e acessórios" },
	{ "grid", "Aplicativos", "Instalação e permissões" },
	{ "desktop", "Telas e modos", "Monitores, Desktop, Gaming e Mídia" },
	{ "volume", "Som", "Saída, microfone e volume" },
	{ "phone", "Celular Connect", "Conecte seu celular" },
	{ "mic", "Emma", "Voz e preferências" },
};
static const struct atalho sistema[] = {
	{ "info", "Sobre", "LUMNA OS 1.0.0 neste computador" },
	{ "drive", "Armazenamento", "Espaço usado e limpeza" },
	{ "battery", "Energia", "Bateria e suspensão" },
	{ "keyboard", "Teclado e mouse", "Layout, atalhos e ponteiro" },
	{ "clock", "Data e hora", "Fuso horário e formato" },
	{ "globe", "Idioma e região", "Português (Brasil)" },
	{ "users", "Usuários", "Contas e senhas" },
	{ "shield", "Privacidade", "Permissões e segurança" },
};
#define N 8

static int aba;
static char busca[64];
static bool escuro = true, transparencia = true, animacoes = true;
static float escala = 0.25f;

static bool combina(const struct atalho *a) {
	return !busca[0] || strcasestr(a->titulo, busca) || strcasestr(a->subtitulo, busca);
}

/* uma coluna de atalhos, com a linha entre eles */
static void coluna(Clay_ElementId id, const struct atalho *lista, int de, int ate) {
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } } }) {
		bool primeiro = true;
		for (int i = de; i < ate; i++) {
			if (!combina(&lista[i])) {
				continue;
			}
			if (!primeiro) {
				gv_separador(false, 0);
			}
			primeiro = false;
			if (gv_linha(CLAY_IDI("atalho", aba * 100 + i), lista[i].icone, lista[i].titulo, lista[i].subtitulo, NULL)) {
				printf("abrir: %s\n", lista[i].titulo);
			}
		}
	}
}

static void grade(const struct atalho *lista) {
	int achados = 0;
	for (int i = 0; i < N; i++) {
		achados += combina(&lista[i]);
	}
	if (!achados) {
		CLAY(CLAY_ID("nada"), { .layout = { .padding = { 18, 18, 20, 20 } } }) {
			gv_texto(gv_fmt("Nada encontrado para “%s”.", busca), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		return;
	}
	CLAY(CLAY_ID("grade"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 20 } }) {
		coluna(CLAY_ID("col-esq"), lista, 0, N / 2);
		gv_separador(true, 0);
		coluna(CLAY_ID("col-dir"), lista, N / 2, N);
	}
}

static void aparencia(void) {
	if (gv_linha(CLAY_ID("escuro"), "moon", "Tema escuro", "Superfícies escuras em todo o sistema", &escuro)) {
		printf("tema escuro: %s\n", escuro ? "sim" : "não");
	}
	gv_separador(false, 0);
	if (gv_linha(CLAY_ID("transp"), "window", "Transparência", "Painéis e menus deixam o fundo aparecer", &transparencia)) {
		printf("transparência: %s\n", transparencia ? "sim" : "não");
	}
	gv_separador(false, 0);
	if (gv_linha(CLAY_ID("anim"), "sparkle", "Animações", "Movimento ao abrir, fechar e trocar de tela", &animacoes)) {
		gv_t.animacoes = animacoes;
		printf("animações: %s\n", animacoes ? "sim" : "não");
	}
	gv_separador(false, 0);
	CLAY(CLAY_ID("escala"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
		.padding = { 18, 18, 16, 12 }, .childGap = 8 } }) {
		CLAY(CLAY_ID("escala-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			CLAY(CLAY_ID("escala-nome"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Tamanho do texto e dos itens"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_NORMAL);
			}
			gv_texto(gv_fmt("%d%%", 100 + (int)(escala * 4 + 0.5f) * 25), gv_t.tamanho_corpo, gv_t.texto_secundario,
				GV_PESO_NORMAL);
		}
		gv_deslizante(CLAY_ID("escala-valor"), &escala, 0, 0);
	}
	gv_separador(false, 0);
	if (gv_linha(CLAY_ID("papel"), "image", "Papel de parede", "Aurora violeta", NULL)) {
		printf("abrir: papel de parede\n");
	}
}

static void atualizacoes(void) {
	gv_separador(false, 0);
	CLAY(CLAY_ID("atualizacoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 18, 12, 16, 4 },
		.childGap = 22, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("refresh", 30, gv_t.icone);
		CLAY(CLAY_ID("atualizacoes-texto"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
			gv_texto(CLAY_STRING("Atualizações"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("Mantenha seu sistema sempre atualizado."), gv_t.tamanho_corpo, gv_t.texto_secundario,
				GV_PESO_NORMAL);
		}
		if (gv_botao(CLAY_ID("verificar"), NULL, "Verificar", GV_BOTAO_ELEVADO)) {
			printf("verificar atualizações\n");
		}
		gv_separador(true, 44);
		gv_texto(CLAY_STRING("LUMNA OS 1.0.0"), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
	}
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
		/* as abas e a busca na mesma faixa, com a linha de lado a lado em baixo */
		CLAY(CLAY_ID("faixa"), {
			.layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 28, 20, 0, 0 },
				.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.border = { .color = gv_t.borda, .width = { .bottom = 1 } },
		}) {
			static const char *const abas[] = { "Geral", "Aparência", "Sistema" };
			if (gv_abas(CLAY_ID("abas"), abas, 3, &aba)) {
				busca[0] = '\0';
			}
			CLAY(CLAY_ID("faixa-meio"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			CLAY(CLAY_ID("faixa-busca"), { .layout = { .sizing = { CLAY_SIZING_FIXED(314) } } }) {
				gv_campo(CLAY_ID("busca"), busca, sizeof(busca), "Buscar uma configuração", GV_CAMPO_BUSCA);
			}
		}
		CLAY(CLAY_ID("conteudo"), {
			.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
				.padding = { 22, 22, 26, 14 } },
		}) {
			if (aba == 0) {
				CLAY(CLAY_ID("cabeca"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 12, 12, 0, 18 },
					.childGap = 8 } }) {
					gv_texto(CLAY_STRING("Tudo no seu lugar"), 28, gv_t.texto, GV_PESO_NORMAL);
					gv_texto(CLAY_STRING("Personalize o seu LUMNA e deixe tudo do seu jeito."), gv_t.tamanho_destaque,
						gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				grade(geral);
			} else if (aba == 1) {
				aparencia();
			} else {
				grade(sistema);
			}
			CLAY(CLAY_ID("espaco"), { .layout = { .sizing = { .height = CLAY_SIZING_GROW(0) } } }) {}
			atualizacoes();
		}
	}
	fflush(stdout);
	if (gv_tecla() == XKB_KEY_Escape && !gv_focado(CLAY_ID("busca"))) {
		gv_app_sair(app);
	}
}

int main(void) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Painel de controle", .app_id = "lumna-painel",
		.largura = 1060, .altura = 720 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
