/*
 * OnlyShowIn / NotShowIn de um .desktop: o app só aparece nos desktops listados (ou não aparece nos listados).
 * O desktop atual é o XDG_CURRENT_DESKTOP (a lista, separada por ":": "LUMNA", "Hyprland:LUMNA"...).
 */
#ifndef LUMNA_DESKTOPENV_H
#define LUMNA_DESKTOPENV_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* a lista do .desktop ("Hyprland;KDE;") tem o desktop nome? */
static inline bool desktopenv_lista_tem(const char *lista, const char *nome, size_t n) {
	for (const char *p = lista; p && *p;) {
		const char *fim = strchr(p, ';');
		size_t tam = fim ? (size_t)(fim - p) : strlen(p);
		if (tam == n && !strncmp(p, nome, n)) {
			return true;
		}
		p = fim ? fim + 1 : p + tam;
	}
	return false;
}

/* o app (com estes OnlyShowIn e NotShowIn, "" ou NULL se não tem) aparece no desktop de agora? */
static inline bool desktopenv_visivel(const char *so_em, const char *nao_em) {
	const char *atual = getenv("XDG_CURRENT_DESKTOP");
	bool tem_so = so_em && *so_em, tem_nao = nao_em && *nao_em;
	if (!tem_so && !tem_nao) {
		return true;
	}
	bool em_so = false, em_nao = false;
	for (const char *p = atual; p && *p;) {
		const char *fim = strchr(p, ':');
		size_t tam = fim ? (size_t)(fim - p) : strlen(p);
		if (tam) {
			em_so |= tem_so && desktopenv_lista_tem(so_em, p, tam);
			em_nao |= tem_nao && desktopenv_lista_tem(nao_em, p, tam);
		}
		p = fim ? fim + 1 : p + tam;
	}
	return (!tem_so || em_so) && !em_nao;
}

#endif
