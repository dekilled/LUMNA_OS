/*
 * A área de transferência do Celular Connect, em Gravity: escrever (ou colar) um texto de várias
 * linhas e mandá-lo para o celular, com os enviados por baixo. Mostra o texto que quebra em
 * várias linhas e o campo de várias linhas. O envio ainda sai no terminal: o Celular Connect
 * vem depois.
 *   ./transferencia
 */
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"

#define MAX_ENVIADOS 8

static char texto[2048];
static struct {
	char texto[2048];
	char hora[8];
} enviados[MAX_ENVIADOS];
static int nenviados;

static void enviar(void) {
	if (!texto[0]) {
		return;
	}
	/* o mais novo em cima */
	memmove(&enviados[1], &enviados[0], sizeof(enviados[0]) * (MAX_ENVIADOS - 1));
	snprintf(enviados[0].texto, sizeof(enviados[0].texto), "%s", texto);
	time_t agora = time(NULL);
	struct tm t;
	localtime_r(&agora, &t);
	snprintf(enviados[0].hora, sizeof(enviados[0].hora), "%02d:%02d", t.tm_hour, t.tm_min);
	if (nenviados < MAX_ENVIADOS) {
		nenviados++;
	}
	printf("enviar ao celular (%zu bytes):\n%s\n---\n", strlen(texto), texto);
	fflush(stdout);
	texto[0] = '\0';
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	CLAY(CLAY_ID("janela"), {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
	}) {
		gv_barra_janela(app, CLAY_ID("barra"), "clipboard", "Área de transferência");
		Clay_ElementId corpo = CLAY_ID("corpo");
		CLAY(corpo, gv_rolagem(corpo, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
			.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
				.padding = { 28, 28, 22, 22 }, .childGap = 14 } })) {
			gv_texto(CLAY_STRING("Compartilhar texto entre dispositivos"), gv_t.tamanho_titulo, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("O que você escrever aqui vai para a área de transferência do Galaxy Note20 Ultra. "
				"Endereços, códigos e listas inteiras: as quebras de linha vão junto. Enter quebra a linha; "
				"Ctrl+Enter envia."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			if (gv_campo_area(CLAY_ID("texto"), texto, sizeof(texto), "Escreva ou cole um texto…", 4, 9) & GV_CAMPO_ENTER) {
				enviar();
			}
			CLAY(CLAY_ID("acoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10,
				.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY(CLAY_ID("contagem"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_fmt("%ld caracteres", g_utf8_strlen(texto, -1)), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
				}
				if (gv_botao(CLAY_ID("limpar"), NULL, "Limpar", GV_BOTAO_ELEVADO)) {
					texto[0] = '\0';
				}
				if (gv_botao_ext(CLAY_ID("enviar"), &(gv_botao_opcoes){ .icone = "send", .texto = "Enviar ao celular",
						.estilo = GV_BOTAO_CHEIO, .desativado = !texto[0] })) {
					enviar();
				}
			}
			if (nenviados) {
				CLAY(CLAY_ID("titulo-enviados"), { .layout = { .padding = { .top = 10 } } }) {
					gv_texto(CLAY_STRING("Enviados"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_NORMAL);
				}
			}
			for (int i = 0; i < nenviados; i++) {
				CLAY(CLAY_IDI("enviado", i), {
					.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 16, 16, 12, 12 },
						.childGap = 6 },
					.backgroundColor = gv_t.elevada,
					.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
					.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
				}) {
					gv_texto(gv_str(enviados[i].texto), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
					gv_texto(gv_fmt("Enviado às %s", enviados[i].hora), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
		}
	}
	if (gv_tecla() == XKB_KEY_Escape && !gv_focado(CLAY_ID("texto"))) {
		gv_app_sair(app);
	}
}

int main(void) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Área de transferência", .app_id = "lumna-transferencia",
		.largura = 620, .altura = 640 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
