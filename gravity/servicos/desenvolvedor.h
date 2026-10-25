/*
 * O modo desenvolvedor (~/.config/lumna/desenvolvedor.ini): coisas a mais que o desktop não traz
 * de fábrica. Cada uma só vale com o modo ligado.
 *
 *   ativo = sim          o modo em si
 *   toque = sim          o desktop com tela de toque: o teclado de números no PIN do bloqueio
 *   ao_vivo = sim        o Painel aplica na hora, sem o botão Aplicar
 *
 * (O LUMNA Mobile já traz o teclado de números no PIN de fábrica.)
 */
#ifndef LUMNA_DESENVOLVEDOR_H
#define LUMNA_DESENVOLVEDOR_H

#include <stdbool.h>

struct lumna_dev {
	bool ativo, toque, ao_vivo;
};

/* lê o arquivo; o que vem já tem toque e ao_vivo desligados se o modo estiver desligado */
struct lumna_dev dev_ler(void);
/* o que se guarda (os valores como estão, mesmo com o modo desligado) */
struct lumna_dev dev_ler_cru(void);
bool dev_gravar(struct lumna_dev d);

#endif
