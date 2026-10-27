/*
 * O armazenamento para o Painel: o disco do sistema (total, usado, livre), o que dá para limpar (lixeira,
 * arquivos temporários, cache de pacotes) e os outros discos e pendrives (lsblk), com montar e ejetar (udisks).
 *
 * Tudo aqui bloqueia (medir pastas grandes leva tempo): chame numa tarefa.
 * Para testar: $LUMNA_ARMAZ_RAIZ (o ponto medido no lugar de /), $LUMNA_PACMAN_CACHE (no lugar de
 * /var/cache/pacman/pkg), $TMPDIR, $HOME, $XDG_DATA_HOME e um lsblk/udisksctl/pacman falsos no PATH.
 */
#ifndef LUMNA_ARMAZENAMENTO_H
#define LUMNA_ARMAZENAMENTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARMAZ_MAX_DISCOS 24

struct disco {
	char dev[48];        /* /dev/sdb1 */
	char disco[48];      /* /dev/sdb: o aparelho inteiro (para desligar) */
	char nome[96];       /* o rótulo, ou "Disco de 32 GB" */
	char fs[24];
	char ponto[256];     /* onde está montado, ou vazio */
	int64_t tamanho, usado;
	bool removivel;
	bool montado;
	bool do_sistema;     /* tem / ou /boot ou swap: não se ejeta */
};

struct armazenamento {
	int64_t total, usado, livre;       /* do disco do sistema */
	int64_t lixeira, temporarios, cache_pacman;
	struct disco discos[ARMAZ_MAX_DISCOS];
	int ndiscos;
};

void armazenamento_ler(struct armazenamento *a);

/* limpezas: 0 se deu certo, senão o erro em erro */
int armazenamento_esvaziar_lixeira(char *erro, size_t tam);
int armazenamento_limpar_temporarios(char *erro, size_t tam);
int armazenamento_limpar_cache_pacman(char *erro, size_t tam);

int armazenamento_montar(const struct disco *d, char *erro, size_t tam);
/* desmonta o que está montado do aparelho; se for removível, desliga-o também */
int armazenamento_ejetar(const struct disco *d, char *erro, size_t tam);

#endif
