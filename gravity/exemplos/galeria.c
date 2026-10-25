/*
 * A galeria do Gravity: os componentes e o tema mudando ao vivo. Os controlos de cima mexem no
 * gv_t (cor de acento, cantos, tamanho do texto, animações, curva) e tudo por baixo segue.
 *   ./galeria
 */
#include <math.h>
#include <stdio.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"

static const struct {
	const char *nome;
	uint32_t acento, cursor, premido, foco;
} cores[] = {
	{ "Violeta", 0xa78bfaff, 0xb79fffff, 0x8f73dcff, 0xc4b5fdff }, /* o do LUMNA */
	{ "Rosa", 0xf472b6ff, 0xf68fc6ff, 0xd95c9eff, 0xf9a8d4ff },
	{ "Azul", 0x86b8e8ff, 0x9dc6efff, 0x6e9fd0ff, 0xb5d4f4ff },
	{ "Verde", 0x78c6a3ff, 0x8ed4b4ff, 0x62ad8bff, 0xa9dcc4ff },
	{ "Âmbar", 0xe4bb77ff, 0xedc98dff, 0xc9a160ff, 0xf0d6a8ff },
};

static struct {
	int cor;
	float raio;    /* 0..1 → 0..24 px */
	float texto;   /* 0..1 → 13..19 px */
	bool animacoes;
	bool mola;
	bool wifi, bluetooth;
	float brilho;
	int toques;
} g = { .raio = 0.5f, .texto = 2 / 6.0f, .animacoes = true, .wifi = true, .brilho = 0.7f };

static void aplicar_tema(void) {
	gv_t.acento = gv_cor(cores[g.cor].acento);
	gv_t.acento_cursor = gv_cor(cores[g.cor].cursor);
	gv_t.acento_premido = gv_cor(cores[g.cor].premido);
	gv_t.foco = gv_cor(cores[g.cor].foco);
	gv_t.raio_grande = roundf(g.raio * 24);
	gv_t.raio_medio = roundf(g.raio * 16);
	gv_t.raio_pequeno = roundf(g.raio * 12);
	gv_t.tamanho_corpo = roundf(13 + g.texto * 6);
	gv_t.tamanho_titulo = gv_t.tamanho_corpo + 7;
	gv_t.animacoes = g.animacoes;
	gv_t.curva = g.mola ? GV_CURVA_MOLA : GV_CURVA_SUAVE;
}

/* uma linha: rótulo à esquerda, o controlo à direita */
#define LINHA(id) CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(48) }, .childGap = 12, \
	.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } })

static void rotulo(const char *texto, Clay_ElementId id) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(180) } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void titulo_secao(const char *texto) {
	CLAY_AUTO_ID({ .layout = { .padding = { .top = 18, .bottom = 6 } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_SEMINEGRITO);
	}
}

static void tema(void) {
	titulo_secao("TEMA (MUDA TUDO AO VIVO)");
	LINHA(CLAY_ID("l-cor")) {
		rotulo("Cor de acento", CLAY_ID("r-cor"));
		for (int i = 0; i < 5; i++) {
			Clay_ElementId id = CLAY_IDI("cor", i);
			if (gv_tocado(id)) {
				g.cor = i;
			}
			float escolhida = gv_anim(id.id, g.cor == i ? 1 : 0, gv_t.duracao_normal);
			CLAY(id, {
				.layout = { .sizing = { CLAY_SIZING_FIXED(34), CLAY_SIZING_FIXED(34) },
					.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.cornerRadius = CLAY_CORNER_RADIUS(17),
				.border = { .color = gv_alfa(gv_t.texto, 0.9f * escolhida), .width = GV_BORDA(2) },
			}) {
				float lado = 22 + 4 * (1 - escolhida);
				CLAY_AUTO_ID({
					.layout = { .sizing = { CLAY_SIZING_FIXED(lado), CLAY_SIZING_FIXED(lado) } },
					.backgroundColor = gv_cor(cores[i].acento),
					.cornerRadius = CLAY_CORNER_RADIUS(lado / 2),
				}) {}
			}
		}
	}
	LINHA(CLAY_ID("l-raio")) {
		rotulo("Cantos", CLAY_ID("r-raio"));
		gv_deslizante(CLAY_ID("raio"), &g.raio, 8, 22);
		CLAY(CLAY_ID("v-raio"), { .layout = { .sizing = { CLAY_SIZING_FIXED(52) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			gv_texto(gv_fmt("%.0f px", roundf(g.raio * 24)), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
	}
	LINHA(CLAY_ID("l-texto")) {
		rotulo("Tamanho do texto", CLAY_ID("r-texto"));
		gv_deslizante(CLAY_ID("texto"), &g.texto, 8, 22);
		CLAY(CLAY_ID("v-texto"), { .layout = { .sizing = { CLAY_SIZING_FIXED(52) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			gv_texto(gv_fmt("%.0f px", roundf(13 + g.texto * 6)), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
	}
	LINHA(CLAY_ID("l-anim")) {
		rotulo("Animações", CLAY_ID("r-anim"));
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		gv_interruptor(CLAY_ID("anim"), &g.animacoes);
	}
	LINHA(CLAY_ID("l-mola")) {
		rotulo("Curva de mola", CLAY_ID("r-mola"));
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		gv_interruptor(CLAY_ID("mola"), &g.mola);
	}
}

static void componentes(void) {
	titulo_secao("COMPONENTES");
	CLAY(CLAY_ID("botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		if (gv_botao(CLAY_ID("b-cheio"), "check", "Confirmar", GV_BOTAO_CHEIO)) {
			g.toques++;
		}
		if (gv_botao(CLAY_ID("b-contorno"), "refresh", "Repetir", GV_BOTAO_CONTORNO)) {
			g.toques++;
		}
		if (gv_botao(CLAY_ID("b-simples"), "settings", "Ajustes", GV_BOTAO_SIMPLES)) {
			g.toques++;
		}
	}
	CLAY(CLAY_ID("rapidos"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .top = 14 }, .childGap = 12,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		if (gv_botao_redondo(CLAY_ID("q-wifi"), "wifi", 52, g.wifi, false)) {
			g.wifi = !g.wifi;
		}
		if (gv_botao_redondo(CLAY_ID("q-bt"), "bluetooth", 52, g.bluetooth, false)) {
			g.bluetooth = !g.bluetooth;
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(8) } } }) {}
		gv_icone("sun", 22, gv_t.icone);
		gv_deslizante(CLAY_ID("brilho"), &g.brilho, 12, 28);
	}
	CLAY(CLAY_ID("rodape"), { .layout = { .padding = { .top = 16 } } }) {
		gv_texto(gv_fmt("Wi-Fi %s · Bluetooth %s · brilho %d%% · %d toques nos botões", g.wifi ? "ligado" : "desligado",
				g.bluetooth ? "ligado" : "desligado", (int)lroundf(g.brilho * 100), g.toques),
			gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	if (gv_tecla() == XKB_KEY_Escape) {
		gv_app_sair(app);
	}
	aplicar_tema();
	CLAY(CLAY_ID("raiz"), {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(20),
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.fundo,
	}) {
		Clay_ElementDeclaration c = gv_cartao();
		c.layout.sizing = (Clay_Sizing){ CLAY_SIZING_GROW(0, 520) };
		c.layout.padding = (Clay_Padding){ 24, 24, 22, 20 };
		CLAY(CLAY_ID("cartao"), c) {
			CLAY(CLAY_ID("topo"), { .layout = { .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("star", 30, gv_t.icone);
				gv_texto(CLAY_STRING("Gravity"), gv_t.tamanho_titulo, gv_t.texto, GV_PESO_SEMINEGRITO);
			}
			CLAY(CLAY_ID("sub"), { .layout = { .padding = { .top = 4 } } }) {
				gv_texto(CLAY_STRING("A interface do LUMNA OS, em C, por cima do Orbit."), gv_t.tamanho_corpo,
					gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			tema();
			componentes();
		}
	}
}

int main(void) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Gravity", .app_id = "gravity-galeria", .largura = 600, .altura = 760 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
