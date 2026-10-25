/*
 * A dock do desktop novo, em Gravity: a pílula em baixo, ao centro, com os apps fixos (o
 * lançador, arquivos, navegador, loja, painel e, depois do separador, a Emma) e os apps abertos
 * que não estão fixos. Um ponto por baixo de quem tem janela aberta, violeta no app ativo.
 * Tocar abre o app, ou traz a janela para a frente; com várias janelas do mesmo app, alterna.
 *
 * As janelas vêm do compositor (wlr-foreign-toplevel, o Orbit tem). Os fixos vêm de
 * ~/.config/lumna/dock, uma linha por app (# comenta, "-" é um separador):
 *   ícone ; nome ; app_id ; comando
 *   folder ; Arquivos ; lumna-arquivos ; lumna-arquivos
 *   -
 *   mic ; Emma ; lumna-emma ; lumna-emma
 * O ícone é um do LUMNA (o desenho de linha) ou, se não houver, o do tema de ícones.
 *   ./dock
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gravity.h"

#define MAX_FIXOS 24
#define MAX_JANELAS 64

struct fixo {
	char icone[64], nome[96], app_id[96], comando[256];
	bool separador;
};

static struct fixo fixos[MAX_FIXOS];
static int nfixos;
static double abriu_em[MAX_FIXOS]; /* o salto de "abrindo" até a janela aparecer */

static const struct fixo padrao[] = {
	{ "lumna", "Aplicativos", "lumna-apps", "lumna-apps" },
	{ "folder", "Arquivos", "lumna-arquivos", "lumna-arquivos" },
	{ "globe", "Navegador", "firefox", "firefox" },
	{ "bag", "Loja", "lumna-loja", "lumna-loja" },
	{ "sliders", "Painel de controle", "lumna-painel", "lumna-painel" },
	{ .separador = true },
	{ "mic", "Emma", "lumna-emma", "lumna-emma" },
};

static char *aparar(char *s) {
	while (isspace((unsigned char)*s)) {
		s++;
	}
	char *fim = s + strlen(s);
	while (fim > s && isspace((unsigned char)fim[-1])) {
		*--fim = '\0';
	}
	return s;
}

static void carregar(void) {
	char caminho[1024];
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *casa = getenv("HOME");
	if (config && *config) {
		snprintf(caminho, sizeof(caminho), "%s/lumna/dock", config);
	} else {
		snprintf(caminho, sizeof(caminho), "%s/.config/lumna/dock", casa ? casa : "");
	}
	FILE *f = fopen(caminho, "r");
	if (!f) {
		nfixos = sizeof(padrao) / sizeof(padrao[0]);
		memcpy(fixos, padrao, sizeof(padrao));
		return;
	}
	char linha[512];
	while (fgets(linha, sizeof(linha), f) && nfixos < MAX_FIXOS) {
		char *s = aparar(linha);
		if (!*s || *s == '#') {
			continue;
		}
		struct fixo *x = &fixos[nfixos];
		memset(x, 0, sizeof(*x));
		if (!strcmp(s, "-")) {
			x->separador = true;
			nfixos++;
			continue;
		}
		char *campos[4] = { 0 };
		int n = 0;
		for (char *p = strtok(s, ";"); p && n < 4; p = strtok(NULL, ";")) {
			campos[n++] = aparar(p);
		}
		if (n < 4) {
			fprintf(stderr, "dock: %s: linha sem os 4 campos (ícone ; nome ; app_id ; comando)\n", caminho);
			continue;
		}
		snprintf(x->icone, sizeof(x->icone), "%s", campos[0]);
		snprintf(x->nome, sizeof(x->nome), "%s", campos[1]);
		snprintf(x->app_id, sizeof(x->app_id), "%s", campos[2]);
		snprintf(x->comando, sizeof(x->comando), "%s", campos[3]);
		nfixos++;
	}
	fclose(f);
}

static void abrir(const char *comando) {
	pid_t p = fork();
	if (p == 0) {
		setsid();
		if (fork() == 0) {
			execl("/bin/sh", "sh", "-c", comando, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}

/* as janelas de um app (pelo app_id, sem ligar a maiúsculas nem ao ".desktop") */
static bool do_app(const gv_janela *j, const char *app_id) {
	size_t n = strlen(app_id);
	if (n > 8 && !strcmp(app_id + n - 8, ".desktop")) {
		n -= 8;
	}
	return strlen(j->app_id) == n && !strncasecmp(j->app_id, app_id, n);
}

/* Tocar num app: sem janelas abre; com uma traz para a frente; com várias vai para a seguinte
 * (a que vem depois da ativa). */
static void tocar(const char *app_id, const char *comando, const gv_janela **janelas, int n, double *abriu) {
	const gv_janela *delas[MAX_JANELAS];
	int k = 0, ativa = -1;
	for (int i = 0; i < n; i++) {
		if (do_app(janelas[i], app_id)) {
			if (janelas[i]->ativa) {
				ativa = k;
			}
			delas[k++] = janelas[i];
		}
	}
	if (!k) {
		if (comando && *comando) {
			printf("abrir %s: %s\n", app_id, comando);
			fflush(stdout);
			abrir(comando);
			if (abriu) {
				*abriu = gv_agora();
			}
		}
		return;
	}
	gv_janela_ativar(delas[(ativa + 1) % k]->id);
}

static bool tem_icone_lumna(const char *nome) {
	/* os do LUMNA são poucos e sem pontos nem barras; o resto é do tema */
	static const char *const lumna[] = { "lumna", "folder", "globe", "bag", "sliders", "mic", "settings", "music",
		"camera", "image", "phone", "chat", "terminal", "mail", "video", "gamepad", "document", "grid", "user" };
	for (size_t i = 0; i < sizeof(lumna) / sizeof(lumna[0]); i++) {
		if (!strcmp(nome, lumna[i])) {
			return true;
		}
	}
	return false;
}

/* Um botão da dock: o ícone e, por baixo, o ponto de quem tem janela. */
static bool item(Clay_ElementId id, const char *icone, bool app_do_tema, int janelas, bool ativo, double abriu) {
	bool tocado = gv_tocado(id);
	bool premido = gv_premido(id);
	/* abrindo: o ícone salta até a janela aparecer (ou passar 6 s) */
	double desde = gv_agora() - abriu;
	float salto = 0;
	if (abriu > 0 && !janelas && desde < 6) {
		double fase = desde * 2.2 - (int)(desde * 2.2);
		salto = (float)(4 * fase * (1 - fase)) * 8;
		gv_acordar_em(0.016);
	}
	CLAY(id, {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(56), CLAY_SIZING_FIXED(60) },
			.padding = { 0, 0, (uint16_t)(10 - salto), 0 }, .childGap = (uint16_t)(5 + salto), .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
		.backgroundColor = premido ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
	}) {
		tocado |= gv_focavel(id, gv_t.raio_medio);
		if (app_do_tema) {
			gv_icone_app(icone, 30);
		} else {
			gv_icone(icone, 28, icone && !strcmp(icone, "lumna") ? gv_t.acento : gv_t.icone);
		}
		/* o ponto: violeta no ativo, discreto nos abertos; nada nos fechados */
		float largura = gv_anim(id.id ^ 0xd0c4u, ativo ? 14 : janelas ? 5 : 0, gv_t.duracao_normal);
		Clay_Color cor = ativo ? gv_t.acento : gv_t.texto_suave;
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIXED(5) } },
			.backgroundColor = largura > 0.5f ? cor : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(2.5f) }) {}
	}
	return tocado;
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	const gv_janela *janelas[MAX_JANELAS];
	int n = gv_janelas(janelas, MAX_JANELAS);
	Clay_ElementId pilula = CLAY_ID("pilula");
	gv_app_entrada_em(app, pilula);
	CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
		.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(pilula, {
			.layout = { .padding = { 10, 10, 6, 6 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.fundo,
			.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande + 4),
			.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
			.userData = gv_sombra(18, 6, 0.45f),
		}) {
			for (int i = 0; i < nfixos; i++) {
				const struct fixo *x = &fixos[i];
				if (x->separador) {
					CLAY(CLAY_IDI("sep", i), { .layout = { .padding = { 6, 6, 0, 0 } } }) {
						gv_separador(true, 32);
					}
					continue;
				}
				int abertas = 0;
				bool ativo = false;
				for (int k = 0; k < n; k++) {
					if (do_app(janelas[k], x->app_id)) {
						abertas++;
						ativo |= janelas[k]->ativa;
					}
				}
				if (abertas) {
					abriu_em[i] = 0;
				}
				if (item(CLAY_IDI("fixo", i), x->icone, !tem_icone_lumna(x->icone), abertas, ativo, abriu_em[i])) {
					tocar(x->app_id, x->comando, janelas, n, &abriu_em[i]);
				}
			}
			/* os abertos que não são fixos, um por app, pela ordem em que abriram */
			bool separou = false;
			for (int k = 0; k < n; k++) {
				const gv_janela *j = janelas[k];
				bool fixo = false, repetido = false;
				for (int i = 0; i < nfixos && !fixo; i++) {
					fixo = !fixos[i].separador && do_app(j, fixos[i].app_id);
				}
				for (int a = 0; a < k && !repetido; a++) {
					repetido = !strcasecmp(janelas[a]->app_id, j->app_id);
				}
				if (fixo || repetido || !j->app_id[0]) {
					continue;
				}
				if (!separou) {
					CLAY(CLAY_ID("sep-abertos"), { .layout = { .padding = { 6, 6, 0, 0 } } }) {
						gv_separador(true, 32);
					}
					separou = true;
				}
				int abertas = 0;
				bool ativo = false;
				for (int a = 0; a < n; a++) {
					if (!strcasecmp(janelas[a]->app_id, j->app_id)) {
						abertas++;
						ativo |= janelas[a]->ativa;
					}
				}
				if (item(CLAY_IDI("aberto", (int)j->id), j->app_id, true, abertas, ativo, 0)) {
					tocar(j->app_id, NULL, janelas, n, NULL);
				}
			}
		}
	}
}

int main(void) {
	carregar();
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Dock", .app_id = "lumna-dock", .tipo = GV_CAMADA,
		.nivel = GV_NIVEL_CIMA, .ancoras = GV_ANCORA_BAIXO | GV_ANCORA_ESQUERDA | GV_ANCORA_DIREITA,
		.altura = 92, .zona_exclusiva = 92 });
	if (!app) {
		return 1;
	}
	if (!gv_janelas_disponivel()) {
		fprintf(stderr, "dock: o compositor não diz as janelas abertas (falta o wlr-foreign-toplevel)\n");
	}
	return gv_app_correr(app, desenhar, NULL);
}
