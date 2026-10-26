/*
 * O ponteiro virtual do Orbit (zwlr_virtual_pointer_v1): o celular vira um mouse a mais, que mexe
 * o cursor de verdade e funciona em qualquer janela. Liga sozinho no primeiro uso e religa se o
 * compositor reiniciou. Todas as chamadas devolvem false se o Orbit não está ao alcance.
 */
#ifndef LUMNA_PONTEIRO_H
#define LUMNA_PONTEIRO_H

#include <stdbool.h>

enum { PONTEIRO_ESQUERDO, PONTEIRO_DIREITO, PONTEIRO_MEIO };

bool ponteiro_mover(double dx, double dy);
bool ponteiro_botao(int botao, bool apertado);
/* rolar: positivo desce / vai para a direita, em pixels */
bool ponteiro_rolar(double dx, double dy);

#endif
