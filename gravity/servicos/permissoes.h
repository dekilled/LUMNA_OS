/*
 * O que cada app pode usar (câmera, microfone, localização, captura de tela, avisos, segundo plano): o que o
 * app pediu e a pessoa respondeu pelos portais do freedesktop, guardado no PermissionStore
 * (org.freedesktop.impl.portal.PermissionStore, que o xdg-desktop-portal sobe sozinho). Vale para apps em sandbox
 * (Flatpak) e para qualquer um que use os portais; um app comum, sem sandbox, não passa por aqui e o sistema
 * não pode limitá-lo. Para testar, o D-Bus da sessão leva a um PermissionStore de mentira.
 */
#ifndef LUMNA_PERMISSOES_H
#define LUMNA_PERMISSOES_H

#include <stdbool.h>
#include <stddef.h>

#define PERM_MAX 160

struct perm_entrada {
	char tabela[24];   /* "devices", "location", "screenshot", "notifications", "background" */
	char id[48];       /* "camera", "microphone", "location"... */
	char app[160];     /* o id do app ("org.mozilla.firefox") */
	char valor[48];    /* a resposta: "yes", "no", "ask"... (a primeira) */
	bool pode;         /* vale como "permitido" */
	bool muda;         /* dá para trocar permitido/negado (a localização só se tira) */
	const char *nome;  /* "Câmera" */
	const char *icone;
};

/* tudo o que há guardado, por app e depois pela permissão; -1 se o PermissionStore não respondeu */
int perm_listar(struct perm_entrada *lista, int max);
bool perm_definir(const struct perm_entrada *e, bool permitir);
bool perm_tirar(const struct perm_entrada *e);

#endif
