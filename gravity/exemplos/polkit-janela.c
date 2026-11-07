#define _DEFAULT_SOURCE 1 /* explicit_bzero */
/*
 * lumna-polkit-janela: a janela de senha do administrador, no desenho do LUMNA. Quem a abre é o lumna-polkit (o agente do
 * polkit): ela mostra o que está pedindo permissão e a conta, recebe a senha e a escreve na saída padrão (uma linha), com
 * saída 0; cancelar (Esc, o botão ou fechar) sai com 1 e não escreve nada. Cobre a tela inteira, por cima de tudo, com o
 * teclado só dela: quem pede a senha de administrador não pode ser coberto nem imitado por outra janela.
 *
 *   lumna-polkit-janela --mensagem "Instalar programas" --usuario maria [--erro "Senha incorreta."]
 */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"

static const char *mensagem = "Este programa pede permissão de administrador.";
static const char *usuario = "";
static const char *erro_inicial = "";
static char senha[256];
static int saida_codigo = 1;
static bool primeiro = true;

static void autorizar(gv_app *app) {
	if (!senha[0]) {
		return;
	}
	/* a senha vai para quem abriu a janela, por esta saída */
	printf("%s\n", senha);
	fflush(stdout);
	explicit_bzero(senha, sizeof(senha));
	saida_codigo = 0;
	gv_app_sair(app);
}

static void cancelar(gv_app *app) {
	explicit_bzero(senha, sizeof(senha));
	saida_codigo = 1;
	gv_app_sair(app);
}

static void desenhar(gv_app *app, void *dados) {
	if (primeiro) {
		primeiro = false;
		gv_focar(CLAY_ID("senha"));
	}
	if (gv_tecla_carregada(XKB_KEY_Escape)) {
		cancelar(app);
		return;
	}
	CLAY(CLAY_ID("pk-raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
		.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("pk-cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(440) },
			.padding = CLAY_PADDING_ALL(24), .childGap = 14 },
			.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
			.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			CLAY(CLAY_ID("pk-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("lock", 28, gv_t.acento);
				gv_texto(CLAY_STRING("Autenticação necessária"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_SEMINEGRITO);
			}
			gv_texto(gv_str(mensagem), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			gv_texto(gv_fmt("Conta: %s", usuario), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
			if (gv_campo(CLAY_ID("senha"), senha, sizeof(senha), "Senha", GV_CAMPO_SENHA) & GV_CAMPO_ENTER) {
				autorizar(app);
			}
			if (erro_inicial[0]) {
				gv_texto(gv_str(erro_inicial), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("pk-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_botao_ext(CLAY_ID("pk-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
					cancelar(app);
				}
				if (gv_botao_ext(CLAY_ID("pk-ok"), &(gv_botao_opcoes){ .texto = "Autorizar", .estilo = GV_BOTAO_CHEIO, .desativado = !senha[0] })) {
					autorizar(app);
				}
			}
		}
	}
}

int main(int argc, char **argv) {
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--mensagem")) {
			mensagem = argv[i + 1];
		} else if (!strcmp(argv[i], "--usuario")) {
			usuario = argv[i + 1];
		} else if (!strcmp(argv[i], "--erro")) {
			erro_inicial = argv[i + 1];
		}
	}
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Autenticação", .app_id = "lumna-polkit-janela", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
		.ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1, .teclado_exclusivo = true });
	if (!app) {
		return 1;
	}
	gv_app_correr(app, desenhar, NULL);
	return saida_codigo;
}
