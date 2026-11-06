/*
 * Telas e modos → Luz noturna: cores mais quentes à noite. Desligada, sempre ligada ou num horário (com a luz mudando aos poucos
 * nos 30 minutos de cada ponta) e quão quente (a temperatura). Vai para ~/.config/lumna/luz.ini no Aplicar e o lumna-luz aplica
 * pela gama da tela (servicos/luz.h).
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "luz.h"

static struct luz_cfg cfg, gravada;

void pn_luz_abrir(void) {
	luz_ler(&gravada);
	cfg = gravada;
}

bool pn_luz_pendente(void) {
	return memcmp(&cfg, &gravada, sizeof(cfg)) != 0;
}

void pn_luz_aplicar(void) {
	if (luz_gravar(&cfg)) {
		gravada = cfg;
	}
}

void pn_luz_descartar(void) {
	cfg = gravada;
}

void pn_luz_restaurar(void) {
	cfg = (struct luz_cfg){ .modo = LUZ_DESLIGADA, .temperatura = 4000, .inicio = 20 * 60, .fim = 6 * 60 };
}

/* os horários de meia em meia hora */
#define NHORAS 48
static char horas[NHORAS][8];
static const char *nomes_horas[NHORAS];

static int indice_hora(int minutos) {
	int i = (minutos + 15) / 30;
	return i < 0 ? 0 : i >= NHORAS ? 0 : i;
}

void pn_luz_desenhar(void) {
	for (int i = 0; i < NHORAS; i++) {
		g_snprintf(horas[i], sizeof(horas[i]), "%02d:%02d", i / 2, (i % 2) * 30);
		nomes_horas[i] = horas[i];
	}
	static const char *const modos[] = { "Desligada", "Sempre ligada", "Num horário" };
	pn_subtitulo(CLAY_ID("lz-s0"), "Quando");
	pn_ajuste_lista(CLAY_ID("lz-modo"), "moon", "Luz noturna", "Tira o azul da tela para cansar menos os olhos à noite", modos, 3, &cfg.modo);
	if (cfg.modo == LUZ_HORARIO) {
		int ini = indice_hora(cfg.inicio), fim = indice_hora(cfg.fim);
		if (pn_ajuste_lista(CLAY_ID("lz-ini"), "clock", "Começa às", "A luz vai esquentando nos 30 minutos seguintes", nomes_horas, NHORAS, &ini)) {
			cfg.inicio = ini * 30;
		}
		if (pn_ajuste_lista(CLAY_ID("lz-fim"), "clock", "Termina às", "E vai voltando ao normal nos 30 minutos seguintes", nomes_horas, NHORAS, &fim)) {
			cfg.fim = fim * 30;
		}
	}
	if (cfg.modo != LUZ_DESLIGADA) {
		pn_subtitulo(CLAY_ID("lz-s1"), "Quão quente");
		pn_linha_comecar(CLAY_ID("lz-temp"), "sliders", "Temperatura da cor", gv_fmt("%d K: %s", cfg.temperatura, cfg.temperatura < 3400 ? "bem quente" : cfg.temperatura < 4600 ? "quente" : "suave").chars);
		CLAY(CLAY_ID("lz-temp-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(260) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			/* a esquerda é a mais quente (2500 K); a direita, a mais suave (6000 K) */
			float v = (cfg.temperatura - 2500) / 3500.0f;
			if (gv_deslizante(CLAY_ID("lz-temp-d"), &v, 6, 18)) {
				cfg.temperatura = 2500 + (int)(v * 3500.0f / 100.0f + 0.5f) * 100;
			}
		}
		pn_linha_acabar();
	}
	CLAY(CLAY_ID("lz-dica"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 10, 4 } } }) {
		gv_texto(CLAY_STRING("Quem aplica é o lumna-luz, que sobe com a sessão. Só um programa de cor pode controlar a tela por vez."),
			gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}
