/*
 * lumna-perfil: o PIN para entrar no perfil NSFW da Emma. A barra do topo só o abre (tocar no escudo): uma janela de camada própria, por cima
 * de tudo, com o teclado só dela, no mesmo molde da tela de entrada. O PIN é o do bloqueio; quem confere é o serviço da Emma (lumna-emma).
 * Sai com o perfil trocado (código 0), ou com Esc / Cancelar (código 1).
 */
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "emmac.h"
#include "gravity.h"
#include "json.h"

static gv_app *app;
static char pin[16], erro[200];
static bool conferindo;

static void entrar(void) {
	if (!pin[0] || conferindo) {
		return;
	}
	conferindo = true;
	char *p = g_strdup_printf("perfil nsfw %s", pin);
	char r[512] = "";
	bool falou = emmac_pedido(p, r, sizeof(r)); /* conferir o PIN leva uma fração de segundo: bloquear aqui é aceitável */
	memset(p, 0, strlen(p));
	g_free(p);
	struct json *j = falou ? json_ler(r) : NULL;
	if (j && json_bool(json_chave(j, "ok"), false)) {
		json_liberar(j);
		gv_app_sair(app);
		return;
	}
	g_strlcpy(erro, !falou ? "O serviço da Emma não respondeu." : j && *json_chave_texto(j, "erro") ? json_chave_texto(j, "erro") : "Não deu para trocar o perfil.", sizeof(erro));
	json_liberar(j);
	memset(pin, 0, sizeof(pin));
	conferindo = false;
	gv_focar(CLAY_ID("pf-pin"));
}

static int saida = 1;

static void desenhar(gv_app *a, void *dados) {
	(void)dados;
	if (gv_tecla() == XKB_KEY_Escape) {
		gv_app_sair(a);
		return;
	}
	CLAY(CLAY_ID("pf-fundo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = (Clay_Color){ 0, 0, 0, 150 } }) {
		CLAY(CLAY_ID("pf-cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(380) }, .padding = CLAY_PADDING_ALL(24), .childGap = 14 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(20), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			CLAY_AUTO_ID({ .layout = { .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("eye", 24, gv_t.perigo);
				gv_texto(CLAY_STRING("Entrar no perfil NSFW"), gv_t.tamanho_destaque + 1, gv_t.texto, GV_PESO_MEDIO);
			}
			gv_texto(CLAY_STRING("O NSFW tem a Emma, as conversas e as memórias dele, separadas das de visita. Digite o PIN do bloqueio."),
				gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY(CLAY_ID("pf-campo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				if (gv_campo(CLAY_ID("pf-pin"), pin, sizeof(pin), "PIN do bloqueio", GV_CAMPO_SENHA) & GV_CAMPO_ENTER) {
					entrar();
				}
			}
			if (erro[0]) {
				gv_texto(gv_str(erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("pf-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
				if (gv_botao_ext(CLAY_ID("pf-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO, .altura = 40, .crescer = true })) {
					gv_app_sair(a);
				}
				if (gv_botao_ext(CLAY_ID("pf-entrar"), &(gv_botao_opcoes){ .icone = "eye", .texto = "Entrar", .estilo = GV_BOTAO_CHEIO, .altura = 40, .crescer = true,
					    .desativado = !pin[0] || conferindo })) {
					entrar();
				}
			}
		}
	}
}

int main(void) {
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Perfil", .app_id = "lumna-perfil", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
		.ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1, .teclado_exclusivo = true });
	if (!app) {
		return 1;
	}
	gv_focar(CLAY_ID("pf-pin"));
	gv_app_correr(app, desenhar, NULL);
	return saida && !conferindo && erro[0] ? 1 : 0;
}
