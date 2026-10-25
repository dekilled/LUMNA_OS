/*
 * Os apps instalados, em Gravity: lê os .desktop (XDG_DATA_DIRS e ~/.local/share), lista com
 * rolagem e busca, e tocar abre o app. É o começo do lançador do desktop novo.
 *   ./apps
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"

#define MAX_APPS 512

struct app {
	char id[128];
	char nome[96];
	char comentario[160];
	char exec[256];
	char icone[256];
};

static struct app apps[MAX_APPS];
static int napps;
static char busca[64];

/* "Name[pt_BR]" ganha a "Name[pt]", que ganha a "Name" */
static int nota_idioma(const char *chave, const char *base) {
	size_t n = strlen(base);
	if (strncmp(chave, base, n)) {
		return -1;
	}
	if (!chave[n]) {
		return 0;
	}
	if (!strcmp(chave + n, "[pt_BR]")) {
		return 2;
	}
	if (!strcmp(chave + n, "[pt]")) {
		return 1;
	}
	return -1;
}

static void ler(const char *caminho, const char *id) {
	for (int i = 0; i < napps; i++) {
		if (!strcmp(apps[i].id, id)) {
			return; /* o primeiro diretório ganha (~/.local antes do sistema) */
		}
	}
	FILE *f = fopen(caminho, "r");
	if (!f) {
		return;
	}
	struct app a = { 0 };
	snprintf(a.id, sizeof(a.id), "%s", id);
	int nota_nome = -1, nota_comentario = -1;
	bool na_entrada = false, aplicacao = false, escondido = false;
	char linha[1024];
	while (fgets(linha, sizeof(linha), f)) {
		linha[strcspn(linha, "\r\n")] = '\0';
		if (linha[0] == '[') {
			na_entrada = !strcmp(linha, "[Desktop Entry]");
			continue;
		}
		char *igual = strchr(linha, '=');
		if (!na_entrada || !igual) {
			continue;
		}
		*igual = '\0';
		const char *valor = igual + 1;
		int nota;
		if ((nota = nota_idioma(linha, "Name")) > nota_nome) {
			nota_nome = nota;
			snprintf(a.nome, sizeof(a.nome), "%s", valor);
		} else if ((nota = nota_idioma(linha, "Comment")) > nota_comentario) {
			nota_comentario = nota;
			snprintf(a.comentario, sizeof(a.comentario), "%s", valor);
		} else if (!strcmp(linha, "Exec")) {
			snprintf(a.exec, sizeof(a.exec), "%s", valor);
		} else if (!strcmp(linha, "Icon")) {
			snprintf(a.icone, sizeof(a.icone), "%s", valor);
		} else if (!strcmp(linha, "Type")) {
			aplicacao = !strcmp(valor, "Application");
		} else if (!strcmp(linha, "NoDisplay") || !strcmp(linha, "Hidden")) {
			escondido |= !strcmp(valor, "true");
		}
	}
	fclose(f);
	if (aplicacao && !escondido && a.nome[0] && a.exec[0] && napps < MAX_APPS) {
		apps[napps++] = a;
	}
}

static void ler_diretorio(const char *dir) {
	DIR *d = opendir(dir);
	if (!d) {
		return;
	}
	struct dirent *e;
	while ((e = readdir(d))) {
		size_t n = strlen(e->d_name);
		if (n > 8 && !strcmp(e->d_name + n - 8, ".desktop")) {
			char caminho[1024];
			snprintf(caminho, sizeof(caminho), "%s/%s", dir, e->d_name);
			ler(caminho, e->d_name);
		}
	}
	closedir(d);
}

static int por_nome(const void *a, const void *b) {
	return strcasecmp(((const struct app *)a)->nome, ((const struct app *)b)->nome);
}

static void carregar(void) {
	char dir[1024];
	const char *casa = getenv("HOME");
	const char *dados = getenv("XDG_DATA_HOME");
	if (dados && *dados) {
		snprintf(dir, sizeof(dir), "%s/applications", dados);
		ler_diretorio(dir);
	} else if (casa) {
		snprintf(dir, sizeof(dir), "%s/.local/share/applications", casa);
		ler_diretorio(dir);
	}
	const char *sistema = getenv("XDG_DATA_DIRS");
	char *lista = strdup(sistema && *sistema ? sistema : "/usr/local/share:/usr/share");
	for (char *s = strtok(lista, ":"); s; s = strtok(NULL, ":")) {
		snprintf(dir, sizeof(dir), "%s/applications", s);
		ler_diretorio(dir);
	}
	free(lista);
	qsort(apps, napps, sizeof(apps[0]), por_nome);
}

/* Exec sem os códigos de campo (%U, %f...), por um sh, num processo solto */
static void abrir(const struct app *a) {
	char cmd[256];
	size_t j = 0;
	for (const char *s = a->exec; *s && j < sizeof(cmd) - 1; s++) {
		if (s[0] == '%' && s[1]) {
			if (s[1] == '%') {
				cmd[j++] = '%';
			}
			s++;
			continue;
		}
		cmd[j++] = *s;
	}
	cmd[j] = '\0';
	printf("abrir %s: %s\n", a->nome, cmd);
	fflush(stdout);
	pid_t p = fork();
	if (p == 0) {
		setsid();
		if (fork() == 0) {
			execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}

static bool combina(const struct app *a) {
	return !busca[0] || strcasestr(a->nome, busca) || strcasestr(a->comentario, busca);
}

/* uma linha com o ícone do app (do tema de ícones), o nome e a descrição */
static bool item(Clay_ElementId id, const struct app *a) {
	bool tocado = gv_tocado(id);
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(72) }, .padding = { 14, 18, 12, 12 }, .childGap = 18,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_premido(id) ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
	}) {
		tocado |= gv_focavel(id, gv_t.raio_medio);
		gv_icone_app(a->icone, 40);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
			gv_texto(gv_str(a->nome), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_NORMAL);
			if (a->comentario[0]) {
				gv_texto(gv_str(a->comentario), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
	return tocado;
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	CLAY(CLAY_ID("janela"), {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
	}) {
		gv_barra_janela(app, CLAY_ID("barra"), "grid", "Aplicativos");
		CLAY(CLAY_ID("topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 22, 22, 18, 14 } } }) {
			if (gv_campo(CLAY_ID("busca"), busca, sizeof(busca), "Buscar aplicativos", GV_CAMPO_BUSCA) & GV_CAMPO_MUDOU) {
				gv_rolar_para(CLAY_ID("lista"), 0, 0, false);
			}
		}
		Clay_ElementId lista = CLAY_ID("lista");
		CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
			.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
				.padding = { 22, 22, 0, 18 } } })) {
			int mostrados = 0;
			for (int i = 0; i < napps; i++) {
				if (!combina(&apps[i])) {
					continue;
				}
				if (mostrados++) {
					gv_separador(false, 0);
				}
				if (item(CLAY_IDI("app", i), &apps[i])) {
					abrir(&apps[i]);
				}
			}
			if (!mostrados) {
				CLAY(CLAY_ID("nenhum"), { .layout = { .padding = { 18, 18, 20, 20 } } }) {
					gv_texto(busca[0] ? gv_fmt("Nenhum aplicativo com “%s”.", busca) : CLAY_STRING("Nenhum aplicativo instalado."),
						gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
		}
		CLAY(CLAY_ID("rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 22, 22, 12, 14 } },
			.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
			gv_texto(gv_fmt("%d aplicativos", napps), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
	if (gv_tecla() == XKB_KEY_Escape) {
		if (busca[0]) {
			busca[0] = '\0';
		} else {
			gv_app_sair(app);
		}
	}
}

int main(void) {
	carregar();
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Aplicativos", .app_id = "lumna-apps", .largura = 560, .altura = 720 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
