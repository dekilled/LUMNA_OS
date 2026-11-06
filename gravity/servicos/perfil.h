/*
 * O perfil de energia (economia, equilibrado, desempenho): o power-profiles-daemon, pelo powerprofilesctl. Para
 * testar, o PATH leva a um powerprofilesctl falso.
 */
#ifndef LUMNA_PERFIL_H
#define LUMNA_PERFIL_H

#include <stdbool.h>
#include <stddef.h>

struct perfil_info {
	bool existe;          /* o powerprofilesctl respondeu */
	char atual[24];       /* "balanced" */
	char lista[4][24];    /* os que o computador oferece */
	int n;
	bool degradado;       /* o desempenho está limitado (calor, bateria) */
};

bool perfil_ler(struct perfil_info *p);
bool perfil_definir(const char *perfil, char *erro, size_t tam);
/* "power-saver" → "Economia" */
const char *perfil_nome(const char *id);

#endif
