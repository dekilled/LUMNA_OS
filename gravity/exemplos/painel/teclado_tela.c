/*
 * Teclado e mouse → Teclado na tela: quando o teclado do LUMNA aparece sozinho e de que tamanho. Vai para ~/.config/lumna/teclado.ini no Aplicar; o
 * lumna-teclado lê de novo a cada campo de texto que abre (servicos/teclado_cfg.h).
 */
#include "painel.h"

#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "teclado_cfg.h"

static tcfg cfg, gravada;

void pn_ktela_abrir(void) {
	tcfg_ler(&gravada);
	cfg = gravada;
}

bool pn_ktela_pendente(void) {
	return memcmp(&cfg, &gravada, sizeof(cfg)) != 0;
}

void pn_ktela_aplicar(void) {
	if (tcfg_gravar(&cfg)) {
		gravada = cfg;
	}
}

void pn_ktela_descartar(void) {
	cfg = gravada;
}

void pn_ktela_restaurar(void) {
	snprintf(cfg.modo, sizeof(cfg.modo), "auto");
	snprintf(cfg.tamanho, sizeof(cfg.tamanho), "medio");
}

static int indice(const char *valor, const char *const *chaves, int n) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(valor, chaves[i])) {
			return i;
		}
	}
	return 0;
}

void pn_ktela_desenhar(void) {
	static const char *const modos[] = { "Sozinho: sem teclado físico ou ao tocar na tela", "Sempre que um campo de texto abrir", "Só quando eu pedir" };
	static const char *const chaves_modo[] = { "auto", "sempre", "nunca" };
	static const char *const tamanhos[] = { "Pequeno", "Médio", "Grande" };
	static const char *const chaves_tam[] = { "pequeno", "medio", "grande" };

	pn_subtitulo(CLAY_ID("kt-s0"), "Quando aparecer");
	int m = indice(cfg.modo, chaves_modo, 3);
	if (pn_ajuste_lista(CLAY_ID("kt-modo"), "keyboard", "Teclado na tela", "Sobe de baixo e empurra as janelas; some quando você sai do campo", modos, 3, &m)) {
		snprintf(cfg.modo, sizeof(cfg.modo), "%s", chaves_modo[m]);
	}
	pn_subtitulo(CLAY_ID("kt-s1"), "Tamanho");
	int t = indice(cfg.tamanho, chaves_tam, 3);
	pn_linha_comecar(CLAY_ID("kt-tam"), "sliders", "Tamanho das teclas", "Teclas maiores acertam mais no toque; menores sobram mais tela");
	if (pn_segmentos(CLAY_ID("kt-tam-s"), tamanhos, 3, &t)) {
		snprintf(cfg.tamanho, sizeof(cfg.tamanho), "%s", chaves_tam[t]);
	}
	pn_linha_acabar();
	CLAY(CLAY_ID("kt-dica"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 10, 4 } } }) {
		gv_texto(CLAY_STRING("Segure uma letra para ver os acentos (a → á à â ã). O layout é o do seu idioma (Painel > Idioma e região) e dá para trocar na própria tecla "
				     "de idioma. Pelo atalho, \"lumna-teclado alternar\" abre e fecha o teclado a qualquer momento."),
			0, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}
