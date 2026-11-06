/* Uma janela de teste com o app_id pedido (para o Orbit achar que é a Mídia, um jogo ou uma janela comum): ./janela APP_ID */
#include "gravity.h"

static void desenhar(gv_app *app, void *dados) {
	CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.fundo }) {}
}

int main(int argc, char **argv) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "teste", .app_id = argc > 1 ? argv[1] : "janela-teste", .tipo = GV_JANELA, .largura = 400, .altura = 300 });
	return app ? gv_app_correr(app, desenhar, NULL) : 1;
}
