/*
 * Conectar ao Wi-Fi, em Gravity: buscar a rede (a lista filtra enquanto se escreve), tocar
 * nela e escrever a senha. Mostra os campos de texto: busca, senha com o olho, Enter para ir.
 * As redes são de mentira (o NetworkManager vem depois, como o som veio para o mixer).
 *   ./wifi
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"

static const struct {
	const char *nome;
	int sinal; /* 0..3 */
	bool aberta;
} redes[] = {
	{ "LUMNA-Casa", 3, false },
	{ "LUMNA-Casa 5G", 3, false },
	{ "Vizinho_2.4", 2, false },
	{ "Café da Esquina", 1, true },
	{ "NET_Virtua_87", 1, false },
	{ "Biblioteca Pública", 0, true },
};
#define NREDES (int)(sizeof(redes) / sizeof(redes[0]))

static char busca[64];
static char senha[64];
static int escolhida = -1;
static char aviso[128];

static void conectar(void) {
	if (escolhida < 0) {
		return;
	}
	if (!redes[escolhida].aberta && strlen(senha) < 8) {
		snprintf(aviso, sizeof(aviso), "A senha do Wi-Fi tem pelo menos 8 caracteres.");
		return;
	}
	snprintf(aviso, sizeof(aviso), "Conectando a %s…", redes[escolhida].nome);
	printf("conectar a \"%s\" com uma senha de %zu caracteres\n", redes[escolhida].nome, strlen(senha));
	fflush(stdout);
}

static void lista(void) {
	CLAY(CLAY_ID("redes"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
		.padding = { .top = 8 } } }) {
		int mostradas = 0;
		for (int i = 0; i < NREDES; i++) {
			if (busca[0] && !strcasestr(redes[i].nome, busca)) {
				continue;
			}
			mostradas++;
			Clay_ElementId id = CLAY_IDI("rede", i);
			if (gv_tocado(id)) {
				escolhida = i;
				senha[0] = '\0';
				aviso[0] = '\0';
				if (!redes[i].aberta) {
					gv_focar(CLAY_ID("senha"));
				}
			}
			bool esta = escolhida == i;
			CLAY(id, {
				.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(52) }, .padding = { 12, 12, 0, 0 },
					.childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = esta ? gv_alfa(gv_t.acento, 0.14f) : gv_alfa(gv_cor(0xffffffff), gv_por_cima(id) ? 0.04f : 0),
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio - 4),
			}) {
				Clay_Color forca = redes[i].sinal >= 2 ? gv_t.icone : gv_t.texto_fraco;
				gv_icone("wifi", 22, forca);
				CLAY(CLAY_IDI("rede-nome", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_str(redes[i].nome), gv_t.tamanho_corpo, gv_t.texto, esta ? GV_PESO_SEMINEGRITO : GV_PESO_NORMAL);
				}
				if (!redes[i].aberta) {
					gv_icone("lock", 16, gv_t.texto_fraco);
				}
			}
		}
		if (!mostradas) {
			CLAY(CLAY_ID("nenhuma"), { .layout = { .padding = { 12, 12, 14, 14 } } }) {
				gv_texto(gv_fmt("Nenhuma rede com “%s”.", busca), 14, gv_t.texto_fraco, GV_PESO_NORMAL);
			}
		}
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	CLAY(CLAY_ID("raiz"), {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(20),
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_cor(0x0b0814ff),
	}) {
		Clay_ElementDeclaration c = gv_cartao();
		c.layout.sizing = (Clay_Sizing){ CLAY_SIZING_GROW(0, 440) };
		c.layout.padding = (Clay_Padding){ 20, 20, 22, 18 };
		c.layout.childGap = 12;
		CLAY(CLAY_ID("cartao"), c) {
			CLAY(CLAY_ID("topo"), { .layout = { .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("wifi", 30, gv_t.icone);
				gv_texto(CLAY_STRING("Wi-Fi"), gv_t.tamanho_titulo, gv_t.texto, GV_PESO_SEMINEGRITO);
			}
			gv_campo(CLAY_ID("busca"), busca, sizeof(busca), "Buscar redes", GV_CAMPO_BUSCA);
			lista();
			if (escolhida >= 0) {
				CLAY(CLAY_ID("conectar-a"), { .layout = { .padding = { .top = 6 } } }) {
					gv_texto(gv_fmt("Conectar a %s", redes[escolhida].nome), 14, gv_t.texto_suave, GV_PESO_NORMAL);
				}
				if (!redes[escolhida].aberta &&
					(gv_campo(CLAY_ID("senha"), senha, sizeof(senha), "Senha", GV_CAMPO_SENHA) & GV_CAMPO_ENTER)) {
					conectar();
				}
				if (aviso[0]) {
					gv_texto(gv_str(aviso), 13, strstr(aviso, "pelo menos") ? gv_t.perigo : gv_t.acento, GV_PESO_NORMAL);
				}
				CLAY(CLAY_ID("botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .top = 4 }, .childGap = 10 } }) {
					if (gv_botao(CLAY_ID("cancelar"), NULL, "Cancelar", GV_BOTAO_CONTORNO)) {
						escolhida = -1;
						senha[0] = '\0';
						aviso[0] = '\0';
					}
					if (gv_botao(CLAY_ID("ir"), "check", "Conectar", GV_BOTAO_CHEIO)) {
						conectar();
					}
				}
			}
		}
	}
	if (gv_tecla() == XKB_KEY_Escape && !gv_focado(CLAY_ID("busca")) && !gv_focado(CLAY_ID("senha"))) {
		gv_app_sair(app);
	}
}

int main(void) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Wi-Fi", .app_id = "lumna-wifi", .largura = 480, .altura = 700 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
