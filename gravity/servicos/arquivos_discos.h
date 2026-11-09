/*
 * Os discos e pendrives da página inicial do Atlas: os que estão ligados mas não montados (para o botão "Montar") e montar e ejetar.
 * Em cima de servicos/armazenamento (lsblk e udisks), sem desenho. Bloqueia: chame numa thread.
 */
#ifndef LUMNA_ARQUIVOS_DISCOS_H
#define LUMNA_ARQUIVOS_DISCOS_H

#include <stddef.h>

#include "armazenamento.h"

/* os discos para mostrar na página inicial: os que têm sistema de arquivos e ainda não estão montados, sem os do sistema.
 * Os montados já aparecem como unidades (ar_unidades). Devolve quantos. */
int ar_discos_para_montar(struct disco *v, int max);

/* monta o disco (udisks) e devolve onde ele ficou (g_free); NULL e a mensagem em erro se não deu */
char *ar_disco_montar(const struct disco *d, char *erro, size_t tam);

/* o disco montado em ponto (que se pode ejetar: não é do sistema): 1 e o disco em saida, ou 0 */
int ar_disco_do_ponto(const char *ponto, struct disco *saida);

#endif
