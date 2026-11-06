/* servicos/permissoes.c contra o PermissionStore de mentira (testes/painel/permissoes_falsas.c) */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "permissoes.h"

static const struct perm_entrada *achar(const struct perm_entrada *l, int n, const char *app, const char *id) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(l[i].app, app) && !strcmp(l[i].id, id)) {
			return &l[i];
		}
	}
	return NULL;
}

int main(void) {
	struct perm_entrada l[PERM_MAX];
	int n = perm_listar(l, PERM_MAX);
	assert(n == 4);
	assert(achar(l, n, "org.exemplo.Video", "camera")->pode && !achar(l, n, "org.exemplo.Chat", "camera")->pode);
	assert(achar(l, n, "org.exemplo.Chat", "microphone")->pode && !strcmp(achar(l, n, "org.exemplo.Chat", "microphone")->nome, "Microfone"));
	assert(achar(l, n, "org.exemplo.Mapa", "location")->pode && !achar(l, n, "org.exemplo.Mapa", "location")->muda);
	assert(!strcmp(l[0].app, "org.exemplo.Chat")); /* por app */
	puts("listar ok");
	assert(perm_definir(achar(l, n, "org.exemplo.Video", "camera"), false));
	assert(perm_definir(achar(l, n, "org.exemplo.Chat", "camera"), true));
	n = perm_listar(l, PERM_MAX);
	assert(!achar(l, n, "org.exemplo.Video", "camera")->pode && achar(l, n, "org.exemplo.Chat", "camera")->pode);
	assert(!perm_definir(achar(l, n, "org.exemplo.Mapa", "location"), false)); /* a localização só se tira */
	puts("trocar ok");
	assert(perm_tirar(achar(l, n, "org.exemplo.Mapa", "location")));
	n = perm_listar(l, PERM_MAX);
	assert(n == 3 && !achar(l, n, "org.exemplo.Mapa", "location"));
	puts("tirar ok");
	puts("permissoes ok");
	return 0;
}
