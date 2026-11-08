/*
 * Os ajustes do teclado na tela (~/.config/lumna/teclado.ini, que o Painel > Teclado na tela grava) e os emojis recentes
 * (~/.local/share/lumna/teclado/recentes).
 *   [teclado]
 *   modo = auto        auto: abre só sem teclado físico ou quando se tocou na tela · sempre · nunca (só pelo atalho)
 *   tamanho = medio    pequeno · medio · grande
 */
#ifndef LUMNA_TECLADO_CFG_H
#define LUMNA_TECLADO_CFG_H

#include <stdbool.h>

typedef struct tcfg {
	char modo[16];     /* "auto" | "sempre" | "nunca" */
	char tamanho[16];  /* "pequeno" | "medio" | "grande" */
} tcfg;

void tcfg_ler(tcfg *cfg);          /* o que está no arquivo (os padrões se faltar) */
bool tcfg_gravar(const tcfg *cfg);
char *tcfg_caminho(void);          /* g_free */
float tcfg_escala(const tcfg *cfg); /* 0.8 / 1 / 1.2 */

void tcfg_recentes_ler(void);      /* carrega os recentes para o teclado_layout */
void tcfg_recentes_gravar(void);

#endif
