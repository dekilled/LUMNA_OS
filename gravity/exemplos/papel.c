/*
 * O papel de parede do desktop novo: uma camada no fundo de cada saída, com a imagem a cobrir a
 * tela. A imagem descodifica no tamanho da tela (um 4K num ecrã 1080 gasta a memória de 1080).
 *   ./papel [imagem]     sem imagem: ~/.config/lumna/papel (um caminho), senão o do LUMNA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gravity.h"

static char imagem[1024];

static void escolher(int argc, char **argv) {
	if (argc > 1) {
		snprintf(imagem, sizeof(imagem), "%s", argv[1]);
		return;
	}
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *casa = getenv("HOME");
	char caminho[1024];
	if (config && *config) {
		snprintf(caminho, sizeof(caminho), "%s/lumna/papel", config);
	} else {
		snprintf(caminho, sizeof(caminho), "%s/.config/lumna/papel", casa ? casa : "");
	}
	FILE *f = fopen(caminho, "r");
	if (f && fgets(imagem, sizeof(imagem), f)) {
		imagem[strcspn(imagem, "\r\n")] = '\0';
	}
	if (f) {
		fclose(f);
	}
	if (!imagem[0]) {
		snprintf(imagem, sizeof(imagem), "/usr/share/lumna/wallpaper.png");
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)app;
	(void)dados;
	Clay_ElementId id = CLAY_ID("papel");
	CLAY(id, gv_imagem(id, imagem, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {}
}

int main(int argc, char **argv) {
	escolher(argc, argv);
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Papel de parede", .app_id = "lumna-papel", .tipo = GV_CAMADA,
		.nivel = GV_NIVEL_FUNDO, .ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
