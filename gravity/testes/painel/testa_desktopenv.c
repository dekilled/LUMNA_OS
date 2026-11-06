/* OnlyShowIn / NotShowIn contra o XDG_CURRENT_DESKTOP (servicos/desktopenv.h) */
#include <stdio.h>
#include <stdlib.h>

#include "desktopenv.h"

static int falhas;
static void quer(const char *atual, const char *so, const char *nao, bool esperado) {
	setenv("XDG_CURRENT_DESKTOP", atual, 1);
	if (desktopenv_visivel(so, nao) != esperado) {
		fprintf(stderr, "FALHOU: desktop=%s only=%s not=%s devia dar %d\n", atual, so, nao, esperado);
		falhas++;
	}
}

int main(void) {
	quer("LUMNA", "", "", true);                       /* sem restrição: aparece em qualquer um */
	quer("LUMNA", "Hyprland;", "", false);             /* só do Hyprland: some na sessão do Orbit */
	quer("Hyprland:LUMNA", "Hyprland;", "", true);     /* e aparece na clássica, que é "Hyprland:LUMNA" */
	quer("LUMNA", "KDE;LUMNA;", "", true);
	quer("LUMNA", "", "LUMNA;", false);                /* NotShowIn */
	quer("Hyprland:LUMNA", "", "Hyprland;", false);
	quer("KDE", "", "Hyprland;", true);
	quer("LUMNAX", "LUMNA;", "", false);               /* nome inteiro, não pedaço */
	unsetenv("XDG_CURRENT_DESKTOP");
	quer("", "Hyprland;", "", false);
	if (!falhas) {
		puts("desktopenv ok");
	}
	return falhas != 0;
}
