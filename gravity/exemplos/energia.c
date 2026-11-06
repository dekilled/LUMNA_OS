/*
 * lumna-energia: o menu de desligar, no meio da tela: Bloquear, Suspender, Reiniciar e Desligar (e Cancelar). É o que o
 * botão de ligar abre quando o Painel (Energia → Botão de ligar) está em "Perguntar". Esc, Cancelar ou um clique fora
 * fecham; chamar de novo com ele aberto também. As ações são as do Connect (servicos/energia.h).
 *   ./energia
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "energia.h"
#include "gravity.h"

static char *arquivo_pid(void) {
	static char c[200];
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	snprintf(c, sizeof(c), "%s/lumna-energia.pid", runtime && *runtime ? runtime : "/tmp");
	return c;
}

/* chamar outra vez fecha o aberto */
static bool ja_aberto(void) {
	FILE *f = fopen(arquivo_pid(), "r");
	if (f) {
		int pid = 0;
		if (fscanf(f, "%d", &pid) == 1 && pid > 1 && kill(pid, 0) == 0) {
			char nome[64] = "", caminho[64];
			snprintf(caminho, sizeof(caminho), "/proc/%d/comm", pid);
			FILE *c = fopen(caminho, "r");
			if (c) {
				if (!fgets(nome, sizeof(nome), c)) {
					nome[0] = '\0';
				}
				fclose(c);
			}
			if (strstr(nome, "energia")) {
				fclose(f);
				kill(pid, SIGTERM);
				return true;
			}
		}
		fclose(f);
	}
	f = fopen(arquivo_pid(), "w");
	if (f) {
		fprintf(f, "%d\n", getpid());
		fclose(f);
	}
	return false;
}

static gv_app *app_energia;
static char erro[200];

static void ao_terminar(int s) {
	(void)s;
	if (app_energia) {
		gv_app_sair(app_energia);
	}
}

static void acao(const char *nome) {
	if (energia_acao(nome, erro, sizeof(erro))) {
		gv_app_sair(app_energia);
	}
}

static void desenhar(gv_app *app, void *dados) {
	if (gv_tecla_carregada(XKB_KEY_Escape)) {
		gv_app_sair(app);
		return;
	}
	Clay_ElementId cartao = CLAY_ID("cartao");
	if (gv_tocado(CLAY_ID("raiz")) && !gv_por_cima(cartao)) {
		gv_app_sair(app); /* um clique fora fecha */
		return;
	}
	CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_cor(0x05040a99) }) {
		CLAY(cartao, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(420) }, .padding = CLAY_PADDING_ALL(22), .childGap = 12 },
			.backgroundColor = gv_alfa(gv_t.fundo, 0.97f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande + 6),
			.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("O que fazer?"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			static const struct { const char *id, *icone, *nome, *acao; } itens[] = {
				{ "bloquear", "lock", "Bloquear a tela", "lock" },
				{ "suspender", "moon", "Suspender", "suspend" },
				{ "reiniciar", "refresh", "Reiniciar", "reboot" },
				{ "desligar", "power", "Desligar", "poweroff" },
			};
			for (int i = 0; i < 4; i++) {
				CLAY(CLAY_IDI("en-linha", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					if (gv_botao_ext(CLAY_IDI("en-item", i), &(gv_botao_opcoes){ .texto = itens[i].nome, .icone = itens[i].icone, .crescer = true,
						    .estilo = i == 3 ? GV_BOTAO_PERIGO : GV_BOTAO_ELEVADO })) {
						acao(itens[i].acao);
					}
				}
			}
			if (gv_botao_ext(CLAY_ID("en-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_SIMPLES, .crescer = true })) {
				gv_app_sair(app);
			}
			if (erro[0]) {
				gv_texto(gv_str(erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
			}
		}
	}
}

int main(void) {
	if (ja_aberto()) {
		return 0;
	}
	signal(SIGTERM, ao_terminar);
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Energia", .app_id = "lumna-energia", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
		.ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1, .teclado = true, .teclado_exclusivo = true });
	if (!app) {
		unlink(arquivo_pid());
		return 1;
	}
	app_energia = app;
	int r = gv_app_correr(app, desenhar, NULL);
	unlink(arquivo_pid());
	return r;
}
