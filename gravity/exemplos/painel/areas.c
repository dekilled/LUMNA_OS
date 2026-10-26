/*
 * Telas e modos → Áreas de trabalho: quantos espaços o Desktop usa (1 a 3) e como se troca entre eles.
 * Gaming e Mídia têm um espaço só cada. Vai para o [geral] areas do ~/.config/lumna/modos.ini no
 * Aplicar e vale na hora (orbit-msg areas): o que estava num espaço desligado passa para o último que
 * ficou ligado, então nenhuma janela se perde.
 */
#include "painel.h"

#include <string.h>

#include "modos.h"

static int cfg, gravada; /* índice em opcoes[]: areas - 1 */

static const char *const opcoes[] = { "1 área", "2 áreas", "3 áreas" };

void pn_areas_abrir(void) {
	struct modos_cfg c;
	modos_cfg_ler(&c);
	gravada = cfg = c.areas - 1;
}

bool pn_areas_pendente(void) {
	return cfg != gravada;
}

void pn_areas_aplicar(void) {
	/* lê de novo: a seção Modos também grava este arquivo */
	struct modos_cfg c;
	modos_cfg_ler(&c);
	c.areas = cfg + 1;
	if (modos_cfg_gravar(&c)) {
		modos_definir_areas(c.areas);
		gravada = cfg;
	}
}

void pn_areas_descartar(void) {
	cfg = gravada;
}

void pn_areas_restaurar(void) {
	cfg = MODOS_AREAS_MAX - 1;
}

static void atalho(Clay_ElementId id, const char *teclas, const char *faz) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 5, 5 }, .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(190) } } }) {
			gv_texto(gv_str(teclas), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_MEDIO);
		}
		gv_texto(gv_str(faz), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

void pn_areas_desenhar(void) {
	pn_ajuste_lista(CLAY_ID("ar-qtd"), "stack", "Áreas do Desktop", "As janelas de uma área desligada vão para a última que ficar", opcoes, MODOS_AREAS_MAX, &cfg);
	pn_subtitulo(CLAY_ID("ar-s1"), "Como trocar");
	atalho(CLAY_ID("ar-a1"), "Alt + 1, 2, 3", "mostra a área");
	atalho(CLAY_ID("ar-a2"), "Alt + Shift + 1, 2, 3", "leva a janela para a área");
	atalho(CLAY_ID("ar-a3"), "Ctrl + Alt + ← →", "a área do lado");
	atalho(CLAY_ID("ar-a4"), "Os pontos na barra", "tocar numa área leva até ela");
	CLAY(CLAY_ID("ar-nota"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 14, 0 } } }) {
		gv_texto(CLAY_STRING("Gaming e Mídia têm uma área só cada; trocar de modo desliza como trocar de área, e as janelas de cada modo ficam onde estavam."),
			gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}
