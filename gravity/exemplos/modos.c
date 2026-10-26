/*
 * lumna-modos: o seletor de modo que aparece no meio da tela quando se troca de modo (Super+Tab, a
 * topbar, o Painel). Mostra Desktop, Gaming e Mídia com o de agora em destaque e some sozinho
 * depois de um instante (o cursor em cima segura). Tocar num modo troca para ele. O Orbit o chama
 * a cada troca, com LUMNA_SAIDA (onde aparece) e LUMNA_MODO; uma chamada nova toma o lugar da
 * anterior.
 *   ./modos
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gravity.h"
#include "modos.h"

static int modo_agora = MODO_DESKTOP;
static double inicio;
static char saida[32];

static char *arquivo_pid(void) {
	static char c[200];
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	snprintf(c, sizeof(c), "%s/lumna-modos.pid", runtime && *runtime ? runtime : "/tmp");
	return c;
}

/* a anterior sai; esta fica com o lugar */
static void tomar_lugar(void) {
	FILE *f = fopen(arquivo_pid(), "r");
	if (f) {
		int pid = 0;
		if (fscanf(f, "%d", &pid) == 1 && pid > 1 && pid != getpid()) {
			kill(pid, SIGTERM);
		}
		fclose(f);
	}
	f = fopen(arquivo_pid(), "w");
	if (f) {
		fprintf(f, "%d\n", getpid());
		fclose(f);
	}
}

static void desenhar(gv_app *app, void *dados) {
	Clay_ElementId pilula = CLAY_ID("pilula");
	if (gv_por_cima(pilula)) {
		inicio = gv_agora(); /* o cursor em cima segura */
	}
	double resta = 1.8 - (gv_agora() - inicio);
	if (resta <= 0) {
		gv_app_sair(app);
		return;
	}
	gv_acordar_em(resta + 0.02);
	CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
		.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(pilula, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 18, 18, 16, 14 }, .childGap = 12,
				.childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
			.backgroundColor = gv_alfa(gv_t.fundo, 0.94f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande + 6),
			.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			CLAY_AUTO_ID({ .layout = { .childGap = 12 } }) {
				for (int m = 0; m < MODO_N; m++) {
					Clay_ElementId id = CLAY_IDI("modo", (uint32_t)m);
					bool esta = m == modo_agora;
					if (gv_tocado(id) && !esta) {
						modos_definir(m, saida[0] ? saida : NULL);
						modo_agora = m;
						inicio = gv_agora();
					}
					CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(140), CLAY_SIZING_FIXED(104) },
							.padding = CLAY_PADDING_ALL(12), .childGap = 10, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
						.backgroundColor = esta ? gv_alfa(gv_t.acento, 0.16f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
						.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio + 2),
						.border = { .color = esta ? gv_t.acento : gv_t.borda, .width = GV_BORDA(esta ? 2 : 1) } }) {
						gv_icone(modos_icone(m), 34, esta ? gv_t.acento : gv_t.icone);
						gv_texto(gv_str(modos_nome(m)), gv_t.tamanho_corpo + 1, esta ? gv_t.texto : gv_t.texto_secundario, esta ? GV_PESO_MEDIO : GV_PESO_NORMAL);
					}
				}
			}
			gv_texto(CLAY_STRING("Super + Tab troca de modo"), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
}

int main(void) {
	const char *s = getenv("LUMNA_SAIDA");
	snprintf(saida, sizeof(saida), "%s", s ? s : "");
	int m = modos_de_id(getenv("LUMNA_MODO"));
	if (m >= 0) {
		modo_agora = m;
	} else {
		struct modos_estado e;
		if (modos_ler(&e)) {
			modo_agora = modos_em_foco(&e);
		}
	}
	tomar_lugar();
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Modos", .app_id = "lumna-modos", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
		.largura = 520, .altura = 190, .saida = saida[0] ? saida : NULL });
	if (!app) {
		return 1;
	}
	inicio = gv_agora();
	return gv_app_correr(app, desenhar, NULL);
}
