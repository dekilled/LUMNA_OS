/* Uma janela de teste com o app_id pedido (para o Orbit achar que é a Mídia, um jogo ou uma janela comum): ./janela APP_ID [camada]
 * Com "camada" vira uma camada de tela cheia sem tela pedida, vermelha (como a capa "Abrindo" da Mídia). Com "azul" é uma janela azul (dá para ver na captura
 * se ela está de fato na tela). */
#include <string.h>

#include "gravity.h"

static void desenhar(gv_app *app, void *dados) {
	CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = dados == (void *)2 ? (Clay_Color){ 0, 0, 255, 255 } : dados ? (Clay_Color){ 255, 0, 0, 255 } : gv_t.fundo }) {}
}

int main(int argc, char **argv) {
	bool camada = argc > 2 && !strcmp(argv[2], "camada");
	bool azul = argc > 2 && !strcmp(argv[2], "azul");
	gv_opcoes o = { .titulo = "teste", .app_id = argc > 1 ? argv[1] : "janela-teste", .tipo = GV_JANELA, .largura = 400, .altura = 300 };
	if (camada) {
		o = (gv_opcoes){ .titulo = "teste", .app_id = o.app_id, .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY, .ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1 };
	}
	gv_app *app = gv_app_criar(&o);
	return app ? gv_app_correr(app, desenhar, azul ? (void *)2 : camada ? (void *)1 : NULL) : 1;
}
