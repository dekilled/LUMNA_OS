/*
 * A dock do desktop novo, em Gravity: a pílula em baixo, ao centro, com os apps fixos (o
 * lançador, arquivos, navegador, loja, painel e, depois do separador, a Emma) e os apps abertos
 * que não estão fixos. Um ponto por baixo de quem tem janela aberta, violeta no app ativo.
 * Tocar abre o app, ou traz a janela para a frente; com várias janelas do mesmo app, alterna.
 * Com o rato parado num app com várias janelas aparece a lista delas (ícone e título; tocar numa
 * vai para ela, o ✕ fecha só essa).
 * Botão direito (ou segurar o dedo, ou a tecla Menu) abre o menu do app: abrir nova janela,
 * fixar ou remover da dock, fechar as janelas e as preferências da dock.
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
#include <dirent.h>
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

static char caminho_config[1024];

/* o menu do botão direito: de que app é, e o elemento onde se prende */
static struct {
	struct fixo app;
	int fixo;              /* o índice nos fixos (-1: aberto e não fixo) */
	Clay_ElementId ancora;
} menu;

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
	char *caminho = caminho_config;
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *casa = getenv("HOME");
	if (config && *config) {
		snprintf(caminho_config, sizeof(caminho_config), "%s/lumna/dock", config);
	} else {
		snprintf(caminho_config, sizeof(caminho_config), "%s/.config/lumna/dock", casa ? casa : "");
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

/* grava os fixos (cria a pasta se for preciso) */
static void salvar(void) {
	char pasta[1024];
	snprintf(pasta, sizeof(pasta), "%s", caminho_config);
	char *barra = strrchr(pasta, '/');
	if (barra) {
		*barra = '\0';
		char cmd[1100];
		snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", pasta);
		if (system(cmd) != 0) {
			fprintf(stderr, "dock: não deu para criar %s\n", pasta);
		}
	}
	FILE *f = fopen(caminho_config, "w");
	if (!f) {
		fprintf(stderr, "dock: não deu para gravar %s\n", caminho_config);
		return;
	}
	fprintf(f, "# A dock do LUMNA: ícone ; nome ; app_id ; comando (\"-\" separa)\n");
	for (int i = 0; i < nfixos; i++) {
		if (fixos[i].separador) {
			fprintf(f, "-\n");
		} else {
			fprintf(f, "%s ; %s ; %s ; %s\n", fixos[i].icone, fixos[i].nome, fixos[i].app_id, fixos[i].comando);
		}
	}
	fclose(f);
}

/* Name (de preferência em português), Icon e Exec (sem os %U) do .desktop de um app_id: pelo
 * nome do ficheiro ou pelo StartupWMClass. */
static bool ler_desktop(const char *caminho, const char *app_id, bool pelo_nome, struct fixo *x) {
	FILE *f = fopen(caminho, "r");
	if (!f) {
		return false;
	}
	char linha[1024], nome[96] = "", nome_pt[96] = "", icone[64] = "", exec[256] = "", classe[96] = "";
	bool entrada = false;
	while (fgets(linha, sizeof(linha), f)) {
		linha[strcspn(linha, "\r\n")] = '\0';
		if (linha[0] == '[') {
			entrada = !strcmp(linha, "[Desktop Entry]");
			continue;
		}
		char *igual = strchr(linha, '=');
		if (!entrada || !igual) {
			continue;
		}
		*igual = '\0';
		const char *v = igual + 1;
		if (!strcmp(linha, "Name")) {
			snprintf(nome, sizeof(nome), "%s", v);
		} else if (!strcmp(linha, "Name[pt_BR]") || (!nome_pt[0] && !strcmp(linha, "Name[pt]"))) {
			snprintf(nome_pt, sizeof(nome_pt), "%s", v);
		} else if (!strcmp(linha, "Icon")) {
			snprintf(icone, sizeof(icone), "%s", v);
		} else if (!strcmp(linha, "Exec")) {
			size_t j = 0;
			for (const char *s = v; *s && j < sizeof(exec) - 1; s++) {
				if (s[0] == '%' && s[1]) {
					s++;
					continue;
				}
				exec[j++] = *s;
			}
			exec[j] = '\0';
		} else if (!strcmp(linha, "StartupWMClass")) {
			snprintf(classe, sizeof(classe), "%s", v);
		}
	}
	fclose(f);
	if (!pelo_nome && strcasecmp(classe, app_id)) {
		return false;
	}
	snprintf(x->nome, sizeof(x->nome), "%s", nome_pt[0] ? nome_pt : nome[0] ? nome : app_id);
	snprintf(x->icone, sizeof(x->icone), "%s", icone[0] ? icone : app_id);
	snprintf(x->comando, sizeof(x->comando), "%s", aparar(exec)[0] ? aparar(exec) : app_id);
	return true;
}

static void achar_app(const char *app_id, struct fixo *x) {
	memset(x, 0, sizeof(*x));
	snprintf(x->app_id, sizeof(x->app_id), "%s", app_id);
	snprintf(x->nome, sizeof(x->nome), "%s", app_id);
	snprintf(x->icone, sizeof(x->icone), "%s", app_id);
	snprintf(x->comando, sizeof(x->comando), "%s", app_id);
	char dirs[2048];
	const char *casa = getenv("HOME");
	const char *sistema = getenv("XDG_DATA_DIRS");
	snprintf(dirs, sizeof(dirs), "%s/.local/share:%s", casa ? casa : "", sistema && *sistema ? sistema : "/usr/local/share:/usr/share");
	/* duas voltas: primeiro pelo nome do ficheiro, depois pelo StartupWMClass */
	for (int volta = 0; volta < 2; volta++) {
		char copia[2048];
		snprintf(copia, sizeof(copia), "%s", dirs);
		for (char *d = strtok(copia, ":"); d; d = strtok(NULL, ":")) {
			char pasta[1024];
			snprintf(pasta, sizeof(pasta), "%s/applications", d);
			DIR *dir = opendir(pasta);
			if (!dir) {
				continue;
			}
			struct dirent *e;
			while ((e = readdir(dir))) {
				size_t n = strlen(e->d_name);
				if (n <= 8 || strcmp(e->d_name + n - 8, ".desktop")) {
					continue;
				}
				bool pelo_nome = n - 8 == strlen(app_id) && !strncasecmp(e->d_name, app_id, n - 8);
				if (volta == 0 && !pelo_nome) {
					continue;
				}
				char caminho[1300];
				snprintf(caminho, sizeof(caminho), "%s/%s", pasta, e->d_name);
				if (ler_desktop(caminho, app_id, volta == 0, x)) {
					closedir(dir);
					return;
				}
			}
			closedir(dir);
		}
	}
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

/* ---- a lista das janelas de um app, ao passar o rato ---- */

#define PREVIA_ESPERA 0.45 /* s parado no ícone até aparecer */
#define PREVIA_SAIDA 0.3   /* s fora do ícone e da lista até sumir */

static struct {
	char app_id[96], nome[96], icone[64];
	Clay_ElementId ancora;
	double desde, saiu;
	bool bloqueada;        /* tocou no ícone: só volta a aparecer depois de sair dele */
} previa;

/* chamado para cada ícone, depois de declarado: este é o que está debaixo do rato? */
static Clay_ElementId previa_candidato;
static const char *previa_cand_app, *previa_cand_nome, *previa_cand_icone;

static bool tem_icone_lumna(const char *nome);

static void previa_ver(Clay_ElementId id, const char *app_id, const char *nome, const char *icone, int abertas, bool tocado) {
	if (!gv_por_cima(id)) {
		return;
	}
	if (tocado) {
		previa.bloqueada = true;
	}
	if (abertas >= 2) {
		previa_candidato = id;
		previa_cand_app = app_id;
		previa_cand_nome = nome;
		previa_cand_icone = icone;
	}
}

static void previa_rever(void) {
	Clay_ElementId lista = CLAY_ID("previa");
	double agora = gv_agora();
	bool aberta = gv_popup_aberto(lista);
	bool no_icone = previa_candidato.id && previa_candidato.id == previa.ancora.id;
	if (previa_candidato.id && !no_icone) {
		/* um ícone novo debaixo do rato: começa a contar (e larga a lista do outro) */
		previa.ancora = previa_candidato;
		snprintf(previa.app_id, sizeof(previa.app_id), "%s", previa_cand_app);
		snprintf(previa.nome, sizeof(previa.nome), "%s", previa_cand_nome);
		snprintf(previa.icone, sizeof(previa.icone), "%s", previa_cand_icone);
		previa.desde = agora;
		previa.bloqueada = false;
		no_icone = true;
		if (aberta) {
			gv_popup_fechar(lista);
			aberta = false;
		}
	}
	if (gv_popup_aberto(CLAY_ID("menu"))) {
		/* o menu do botão direito manda */
		gv_popup_fechar(lista);
		return;
	}
	if (no_icone || (aberta && gv_por_cima(lista))) {
		previa.saiu = 0;
		if (!aberta && no_icone && !previa.bloqueada) {
			if (agora - previa.desde >= PREVIA_ESPERA) {
				gv_popup_abrir(lista);
			} else {
				gv_acordar_em(PREVIA_ESPERA - (agora - previa.desde) + 0.01);
			}
		}
	} else {
		if (!previa_candidato.id) {
			previa.ancora = (Clay_ElementId){ 0 };
			previa.bloqueada = false;
		}
		if (aberta) {
			if (!previa.saiu) {
				previa.saiu = agora;
			}
			if (agora - previa.saiu >= PREVIA_SAIDA) {
				gv_popup_fechar(lista);
				previa.saiu = 0;
			} else {
				gv_acordar_em(PREVIA_SAIDA - (agora - previa.saiu) + 0.01);
			}
		}
	}
	previa_candidato = (Clay_ElementId){ 0 };
}

/* títulos longos cortados com reticências (a lista tem largura fixa) */
static const char *encurtar(const char *t, char *buf, size_t tam, int max) {
	int n = 0;
	const char *p = t;
	while (*p && n < max) {
		p++;
		while ((*p & 0xc0) == 0x80) {
			p++;
		}
		n++;
	}
	if (!*p) {
		return t;
	}
	snprintf(buf, tam, "%.*s…", (int)(p - t), t);
	return buf;
}

static void previa_desenhar(const gv_janela **janelas, int n) {
	GV_POPUP(CLAY_ID("previa"), previa.ancora, .lado = GV_POPUP_ACIMA, .alinhar = GV_ALINHAR_CENTRO, .largura = 330,
			.distancia = 10, .sem_grab = true) {
		CLAY(CLAY_ID("previa-topo"), { .layout = { .padding = { 12, 12, 8, 6 } } }) {
			gv_texto(gv_str(previa.nome), gv_t.tamanho_legenda + 1, gv_t.texto_secundario, GV_PESO_MEDIO);
		}
		for (int k = 0; k < n; k++) {
			const gv_janela *j = janelas[k];
			if (!do_app(j, previa.app_id)) {
				continue;
			}
			Clay_ElementId linha = CLAY_IDI("previa-janela", (int)j->id);
			Clay_ElementId fechar = CLAY_IDI("previa-fechar", (int)j->id);
			bool tocou = gv_tocado(linha) && !gv_por_cima(fechar);
			char buf[160];
			CLAY(linha, {
				.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 10, 4, 0, 0 }, .childGap = 12,
					.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = j->ativa ? gv_t.selecionado : gv_por_cima(linha) ? gv_t.sob_cursor : (Clay_Color){ 0 },
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			}) {
				tocou |= gv_focavel(linha, gv_t.raio_medio);
				if (tem_icone_lumna(previa.icone)) {
					gv_icone(previa.icone, 22, gv_t.icone);
				} else {
					gv_icone_app(previa.icone, 24);
				}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_str(encurtar(j->titulo[0] ? j->titulo : previa.nome, buf, sizeof(buf), 30)), gv_t.tamanho_corpo,
						j->ativa ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				if (gv_botao_ext(fechar, &(gv_botao_opcoes){ .icone = "close", .estilo = GV_BOTAO_SIMPLES, .altura = 34 })) {
					printf("fechar a janela \"%s\"\n", j->titulo);
					gv_janela_fechar(j->id);
				}
			}
			if (tocou) {
				printf("ir para a janela \"%s\"\n", j->titulo);
				gv_janela_ativar(j->id);
				gv_popup_fechar(CLAY_ID("previa"));
			}
		}
	}
	fflush(stdout);
}

static void menu_abrir(Clay_ElementId ancora, const struct fixo *app, int fixo) {
	menu.app = *app;
	menu.fixo = fixo;
	menu.ancora = ancora;
	gv_popup_abrir(CLAY_ID("menu"));
}

static void menu_desenhar(const gv_janela **janelas, int n) {
	int delas = 0;
	for (int k = 0; k < n; k++) {
		delas += do_app(janelas[k], menu.app.app_id);
	}
	GV_POPUP(CLAY_ID("menu"), menu.ancora, .lado = GV_POPUP_ACIMA, .alinhar = GV_ALINHAR_CENTRO, .largura = 270, .distancia = 10) {
		CLAY(CLAY_ID("menu-topo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 12, 12, 8, 8 }, .childGap = 2 } }) {
			gv_texto(gv_str(menu.app.nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(delas ? gv_fmt("%d janela%s aberta%s", delas, delas > 1 ? "s" : "", delas > 1 ? "s" : "")
				: CLAY_STRING("Não está aberto"), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		gv_separador(false, 0);
		if (menu.app.comando[0] && gv_menu_item(CLAY_ID("m-nova"), "plus", delas ? "Abrir nova janela" : "Abrir", NULL)) {
			printf("nova janela de %s: %s\n", menu.app.app_id, menu.app.comando);
			abrir(menu.app.comando);
			gv_popup_fechar(CLAY_ID("menu"));
		}
		if (menu.fixo >= 0) {
			if (gv_menu_item(CLAY_ID("m-remover"), "minus", "Remover da dock", NULL)) {
				printf("remover da dock: %s\n", menu.app.app_id);
				memmove(&fixos[menu.fixo], &fixos[menu.fixo + 1], (nfixos - menu.fixo - 1) * sizeof(fixos[0]));
				memmove(&abriu_em[menu.fixo], &abriu_em[menu.fixo + 1], (nfixos - menu.fixo - 1) * sizeof(abriu_em[0]));
				nfixos--;
				salvar();
				gv_popup_fechar(CLAY_ID("menu"));
			}
		} else if (nfixos < MAX_FIXOS && gv_menu_item(CLAY_ID("m-fixar"), "pin", "Fixar na dock", NULL)) {
			/* entra antes do separador da Emma (se houver), no fim dos apps */
			int onde = nfixos;
			for (int i = 0; i < nfixos; i++) {
				if (fixos[i].separador) {
					onde = i;
					break;
				}
			}
			memmove(&fixos[onde + 1], &fixos[onde], (nfixos - onde) * sizeof(fixos[0]));
			memmove(&abriu_em[onde + 1], &abriu_em[onde], (nfixos - onde) * sizeof(abriu_em[0]));
			fixos[onde] = menu.app;
			abriu_em[onde] = 0;
			nfixos++;
			printf("fixar na dock: %s (%s, %s)\n", menu.app.app_id, menu.app.nome, menu.app.comando);
			salvar();
			gv_popup_fechar(CLAY_ID("menu"));
		}
		char fechar[48];
		snprintf(fechar, sizeof(fechar), delas > 1 ? "Fechar %d janelas" : "Fechar", delas);
		if (delas && gv_menu_item(CLAY_ID("m-fechar"), "close", fechar, NULL)) {
			for (int k = 0; k < n; k++) {
				if (do_app(janelas[k], menu.app.app_id)) {
					gv_janela_fechar(janelas[k]->id);
				}
			}
			printf("fechar: %s\n", menu.app.app_id);
			gv_popup_fechar(CLAY_ID("menu"));
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("m-pref"), "settings", "Preferências da dock", NULL)) {
			printf("preferências da dock\n");
			abrir("lumna-painel");
			gv_popup_fechar(CLAY_ID("menu"));
		}
	}
	fflush(stdout);
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
				Clay_ElementId id = CLAY_IDI("fixo", i);
				if (gv_menu_contexto(id)) {
					menu_abrir(id, x, i);
				}
				bool tocado = item(id, x->icone, !tem_icone_lumna(x->icone), abertas, ativo, abriu_em[i]);
				previa_ver(id, x->app_id, x->nome, x->icone, abertas, tocado);
				if (tocado) {
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
				Clay_ElementId id = CLAY_IDI("aberto", (int)j->id);
				if (gv_menu_contexto(id)) {
					struct fixo x;
					achar_app(j->app_id, &x);
					menu_abrir(id, &x, -1);
				}
				bool tocado = item(id, j->app_id, true, abertas, ativo, 0);
				previa_ver(id, j->app_id, j->app_id, j->app_id, abertas, tocado);
				if (tocado) {
					tocar(j->app_id, NULL, janelas, n, NULL);
				}
			}
		}
	}
	previa_rever();
	previa_desenhar(janelas, n);
	menu_desenhar(janelas, n);
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
