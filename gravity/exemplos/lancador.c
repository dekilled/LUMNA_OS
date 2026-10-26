/*
 * O lançador do desktop novo (o "Início"), em Gravity: cobre a tela por baixo da topbar, com a
 * busca, as abas (Favoritos, Trabalho, Internet, Jogos, Mídia, Sistema), os recentes e, em
 * baixo, Bloquear, Desligar e a Emma.
 *
 * - Favoritos: cartões grandes com os apps favoritados (~/.config/lumna/favoritos, um id de
 *   .desktop por linha) e os arquivos recentes (~/.local/share/recently-used.xbel).
 * - As outras abas: a grade dos apps da categoria (pelo Categories= do .desktop) e, ao lado, os
 *   detalhes do app — o que está debaixo do rato. Segurar um app (rato ou dedo) ou o botão
 *   direito fixa os detalhes nele: o rato nos outros já não os troca, e os botões dele ficam à
 *   mão (abrir, favoritar, fixar na dock, propriedades, desinstalar); o ✕ solta. Cada vez que
 *   o lançador abre, os detalhes começam soltos (ao_abrir).
 * - Tocar num app abre-o e fecha o lançador. A busca procura em todos os apps (nome,
 *   descrição, palavras-chave) e nos recentes. Esc fecha; "/" vai à busca.
 *
 * Chamar outra vez fecha o que estiver aberto (a dock usa isso: o losango abre e fecha).
 *   ./lancador
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"

#define MAX_APPS 512
#define MAX_FAVORITOS 64
#define MAX_RECENTES 24

enum aba { FAVORITOS, TRABALHO, INTERNET, JOGOS, MIDIA, SISTEMA, NABAS };
static const char *const nomes_abas[NABAS] = { "Favoritos", "Trabalho", "Internet", "Jogos", "Mídia", "Sistema" };

struct app {
	char id[128];        /* "firefox" (o nome do .desktop sem a extensão) */
	char caminho[1024];  /* o .desktop */
	char nome[96], descricao[256], icone[128], comando[256], palavras[256];
	char janela[128];    /* o app_id das janelas dele (StartupWMClass, ou o id): o que a dock compara */
	enum aba aba;
	char tipo[48];       /* "Desenvolvimento", "Escritório"... (o que aparece nos detalhes) */
};

struct recente {
	char caminho[512], nome[128], onde[96];
	const char *icone;
	time_t quando;
	bool pasta;
};

static struct app apps[MAX_APPS];
static int napps;
static char favoritos[MAX_FAVORITOS][128];
static int nfavoritos;
static struct recente recentes[MAX_RECENTES];
static int nrecentes;
static char pasta_config[1024];

static int aba = FAVORITOS;
static char busca[128];
static int detalhe = -1;     /* o app nos detalhes */
static bool fixado;          /* os detalhes estão presos nele (o rato nos outros não os troca) */
static bool confirmar_remocao;
static gv_app *app_lancador;

/* ---- os apps ---- */

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
	return !strcmp(chave + n, "[pt]") ? 1 : -1;
}

/* a aba e o tipo pelo Categories= (as categorias do freedesktop) */
static void classificar(struct app *a, const char *cats) {
	char c[512];
	snprintf(c, sizeof(c), ";%s;", cats);
#define TEM(x) (strstr(c, ";" x ";") != NULL)
	if (TEM("Game")) {
		a->aba = JOGOS;
		snprintf(a->tipo, sizeof(a->tipo), "Jogo");
	} else if (TEM("WebBrowser") || TEM("Email") || TEM("Chat") || TEM("InstantMessaging") || TEM("Network")) {
		a->aba = INTERNET;
		snprintf(a->tipo, sizeof(a->tipo), "%s", TEM("WebBrowser") ? "Navegador" : TEM("Email") ? "E-mail"
			: TEM("Chat") || TEM("InstantMessaging") ? "Conversa" : "Internet");
	} else if ((TEM("AudioVideo") || TEM("Audio") || TEM("Video") || TEM("Player")) && !TEM("Recorder") &&
		!TEM("AudioVideoEditing") && !TEM("Development") && !TEM("Graphics")) {
		a->aba = MIDIA;
		snprintf(a->tipo, sizeof(a->tipo), "%s", TEM("Video") ? "Vídeo" : TEM("Audio") ? "Música e áudio" : "Mídia");
	} else if ((TEM("System") || TEM("Settings") || TEM("PackageManager") || TEM("Monitor")) && !TEM("TerminalEmulator")) {
		a->aba = SISTEMA;
		snprintf(a->tipo, sizeof(a->tipo), "%s", TEM("Settings") ? "Configurações" : "Sistema");
	} else {
		a->aba = TRABALHO;
		snprintf(a->tipo, sizeof(a->tipo), "%s", TEM("Development") ? "Desenvolvimento" : TEM("Office") ? "Escritório"
			: TEM("Graphics") ? "Gráficos" : TEM("AudioVideo") ? "Áudio e vídeo" : TEM("TerminalEmulator") ? "Terminal"
			: TEM("Education") || TEM("Science") ? "Educação" : "Utilitário");
	}
#undef TEM
}

static void ler_desktop(const char *caminho, const char *id) {
	for (int i = 0; i < napps; i++) {
		if (!strcmp(apps[i].id, id)) {
			return; /* o primeiro diretório ganha (~/.local antes do sistema) */
		}
	}
	FILE *f = fopen(caminho, "r");
	if (!f || napps >= MAX_APPS) {
		if (f) {
			fclose(f);
		}
		return;
	}
	struct app a = { 0 };
	snprintf(a.id, sizeof(a.id), "%s", id);
	snprintf(a.caminho, sizeof(a.caminho), "%s", caminho);
	int nn = -1, nd = -1, ng = -1;
	char generico[128] = "", cats[512] = "";
	bool entrada = false, aplicacao = false, escondido = false;
	char linha[1024];
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
		int n;
		if ((n = nota_idioma(linha, "Name")) > nn) {
			nn = n;
			snprintf(a.nome, sizeof(a.nome), "%s", v);
		} else if ((n = nota_idioma(linha, "Comment")) > nd) {
			nd = n;
			snprintf(a.descricao, sizeof(a.descricao), "%s", v);
		} else if ((n = nota_idioma(linha, "GenericName")) > ng) {
			ng = n;
			snprintf(generico, sizeof(generico), "%s", v);
		} else if (nota_idioma(linha, "Keywords") >= 0) {
			size_t u = strlen(a.palavras);
			snprintf(a.palavras + u, sizeof(a.palavras) - u, "%s;", v);
		} else if (!strcmp(linha, "Icon")) {
			snprintf(a.icone, sizeof(a.icone), "%s", v);
		} else if (!strcmp(linha, "Exec")) {
			size_t j = 0;
			for (const char *s = v; *s && j < sizeof(a.comando) - 1; s++) {
				if (s[0] == '%' && s[1]) {
					s++;
					continue;
				}
				a.comando[j++] = *s;
			}
			a.comando[j] = '\0';
		} else if (!strcmp(linha, "StartupWMClass")) {
			snprintf(a.janela, sizeof(a.janela), "%s", v);
		} else if (!strcmp(linha, "Categories")) {
			snprintf(cats, sizeof(cats), "%s", v);
		} else if (!strcmp(linha, "Type")) {
			aplicacao = !strcmp(v, "Application");
		} else if (!strcmp(linha, "NoDisplay") || !strcmp(linha, "Hidden")) {
			escondido |= !strcmp(v, "true");
		}
	}
	fclose(f);
	if (!aplicacao || escondido || !a.nome[0] || !a.comando[0]) {
		return;
	}
	if (!a.descricao[0]) {
		snprintf(a.descricao, sizeof(a.descricao), "%s", generico);
	}
	if (!a.janela[0]) {
		snprintf(a.janela, sizeof(a.janela), "%s", a.id);
	}
	classificar(&a, cats);
	apps[napps++] = a;
}

static void ler_pasta(const char *dir) {
	DIR *d = opendir(dir);
	if (!d) {
		return;
	}
	struct dirent *e;
	while ((e = readdir(d))) {
		size_t n = strlen(e->d_name);
		if (n > 8 && !strcmp(e->d_name + n - 8, ".desktop")) {
			char caminho[1024], id[128];
			snprintf(caminho, sizeof(caminho), "%s/%s", dir, e->d_name);
			snprintf(id, sizeof(id), "%.*s", (int)(n - 8), e->d_name);
			ler_desktop(caminho, id);
		}
	}
	closedir(d);
}

static int por_nome(const void *a, const void *b) {
	return g_utf8_collate(((const struct app *)a)->nome, ((const struct app *)b)->nome);
}

static void carregar_apps(void) {
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s/applications", g_get_user_data_dir());
	ler_pasta(dir);
	for (const char *const *d = g_get_system_data_dirs(); *d; d++) {
		snprintf(dir, sizeof(dir), "%s/applications", *d);
		ler_pasta(dir);
	}
	qsort(apps, napps, sizeof(apps[0]), por_nome);
}

/* os apps do próprio LUMNA têm o desenho de linha; os outros, o ícone do tema */
static const char *icone_lumna(const struct app *a) {
	static const struct {
		const char *id, *icone;
	} lumna[] = {
		{ "lumna-arquivos", "folder" }, { "lumna-navegador", "globe" }, { "lumna-editor", "document" },
		{ "lumna-loja", "bag" }, { "lumna-painel", "sliders" }, { "lumna-emma", "mic" }, { "lumna-apps", "grid" },
		{ "lumna-rede", "wifi" }, { "lumna-transferencia", "clipboard" }, { "lumna-mixer", "volume" },
	};
	for (size_t i = 0; i < sizeof(lumna) / sizeof(lumna[0]); i++) {
		if (!strcmp(a->id, lumna[i].id)) {
			return lumna[i].icone;
		}
	}
	return NULL;
}

static void icone_app(const struct app *a, float tamanho, bool destaque) {
	const char *l = icone_lumna(a);
	if (l) {
		gv_icone(l, tamanho, destaque ? gv_t.acento : gv_t.icone);
	} else {
		gv_icone_app(a->icone, tamanho);
	}
}

/* ---- favoritos (um id por linha) ---- */

static void carregar_favoritos(void) {
	char caminho[1100];
	snprintf(caminho, sizeof(caminho), "%s/favoritos", pasta_config);
	nfavoritos = 0;
	FILE *f = fopen(caminho, "r");
	if (!f) {
		/* os de fábrica: os do LUMNA que houver */
		static const char *const padrao[] = { "lumna-arquivos", "lumna-navegador", "firefox", "lumna-editor",
			"lumna-loja", "lumna-painel" };
		for (size_t i = 0; i < sizeof(padrao) / sizeof(padrao[0]) && nfavoritos < MAX_FAVORITOS; i++) {
			snprintf(favoritos[nfavoritos++], sizeof(favoritos[0]), "%s", padrao[i]);
		}
		return;
	}
	char linha[128];
	while (fgets(linha, sizeof(linha), f) && nfavoritos < MAX_FAVORITOS) {
		linha[strcspn(linha, "\r\n")] = '\0';
		if (linha[0] && linha[0] != '#') {
			snprintf(favoritos[nfavoritos++], sizeof(favoritos[0]), "%s", linha);
		}
	}
	fclose(f);
}

static void gravar_favoritos(void) {
	char caminho[1100], cmd[1100];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", pasta_config);
	if (system(cmd) != 0) {
		return;
	}
	snprintf(caminho, sizeof(caminho), "%s/favoritos", pasta_config);
	FILE *f = fopen(caminho, "w");
	if (!f) {
		return;
	}
	fprintf(f, "# Os favoritos do lançador do LUMNA: um id de .desktop por linha\n");
	for (int i = 0; i < nfavoritos; i++) {
		fprintf(f, "%s\n", favoritos[i]);
	}
	fclose(f);
}

static bool e_favorito(const struct app *a) {
	for (int i = 0; i < nfavoritos; i++) {
		if (!strcmp(favoritos[i], a->id)) {
			return true;
		}
	}
	return false;
}

static void alternar_favorito(const struct app *a) {
	for (int i = 0; i < nfavoritos; i++) {
		if (!strcmp(favoritos[i], a->id)) {
			memmove(favoritos[i], favoritos[i + 1], (nfavoritos - i - 1) * sizeof(favoritos[0]));
			nfavoritos--;
			gravar_favoritos();
			return;
		}
	}
	if (nfavoritos < MAX_FAVORITOS) {
		snprintf(favoritos[nfavoritos++], sizeof(favoritos[0]), "%s", a->id);
		gravar_favoritos();
	}
}

/* ---- a dock (a mesma lista que ela lê e vigia) ---- */

static bool na_dock(const struct app *a) {
	char caminho[1100], linha[512];
	snprintf(caminho, sizeof(caminho), "%s/dock", pasta_config);
	FILE *f = fopen(caminho, "r");
	bool tem = false;
	while (f && fgets(linha, sizeof(linha), f)) {
		char *p = strchr(linha, ';');
		char *q = p ? strchr(p + 1, ';') : NULL;
		if (q) {
			char id[128];
			if (sscanf(q + 1, " %127[^ ;]", id) == 1 && (!strcasecmp(id, a->id) || !strcasecmp(id, a->janela))) {
				tem = true;
			}
		}
	}
	if (f) {
		fclose(f);
	}
	return tem;
}

static void fixar_na_dock(const struct app *a) {
	char caminho[1100];
	snprintf(caminho, sizeof(caminho), "%s/dock", pasta_config);
	FILE *f = fopen(caminho, "r");
	if (!f) {
		/* ainda não há a lista: a dock usa a de fábrica; esta passa a ser ela mais o app */
		f = fopen(caminho, "w");
		if (!f) {
			return;
		}
		fprintf(f, "# A dock do LUMNA: ícone ; nome ; app_id ; comando (\"-\" separa)\n"
			"lumna ; Início ; @lancador ; lumna-lancador\nfolder ; Arquivos ; lumna-arquivos ; lumna-arquivos\n"
			"globe ; Navegador ; firefox ; firefox\nbag ; Loja ; lumna-loja ; lumna-loja\n"
			"sliders ; Painel de controle ; lumna-painel ; lumna-painel\n");
		const char *l = icone_lumna(a);
		fprintf(f, "%s ; %s ; %s ; %s\n-\nmic ; Emma ; lumna-emma ; lumna-emma\n", l ? l : a->icone, a->nome, a->janela, a->comando);
		fclose(f);
		return;
	}
	/* entra antes do primeiro separador */
	char *texto = NULL;
	size_t tam = 0;
	FILE *m = open_memstream(&texto, &tam);
	char linha[512];
	bool entrou = false;
	while (fgets(linha, sizeof(linha), f)) {
		if (!entrou && linha[0] == '-') {
			const char *l = icone_lumna(a);
			fprintf(m, "%s ; %s ; %s ; %s\n", l ? l : a->icone, a->nome, a->janela, a->comando);
			entrou = true;
		}
		fputs(linha, m);
	}
	if (!entrou) {
		const char *l = icone_lumna(a);
		fprintf(m, "%s ; %s ; %s ; %s\n", l ? l : a->icone, a->nome, a->janela, a->comando);
	}
	fclose(m);
	fclose(f);
	f = fopen(caminho, "w");
	if (f) {
		fwrite(texto, 1, tam, f);
		fclose(f);
	}
	free(texto);
}

/* ---- recentes (o recently-used.xbel do freedesktop) ---- */

static int por_data(const void *a, const void *b) {
	time_t x = ((const struct recente *)a)->quando, y = ((const struct recente *)b)->quando;
	return x < y ? 1 : x > y ? -1 : 0;
}

static void carregar_recentes(void) {
	char caminho[1024];
	snprintf(caminho, sizeof(caminho), "%s/recently-used.xbel", g_get_user_data_dir());
	gchar *xml = NULL;
	if (!g_file_get_contents(caminho, &xml, NULL, NULL)) {
		return;
	}
	struct recente todos[256];
	int n = 0;
	for (char *p = strstr(xml, "<bookmark "); p && n < 256; p = strstr(p + 1, "<bookmark ")) {
		char *href = strstr(p, "href=\"");
		char *fim = strchr(p, '>');
		if (!href || !fim || href > fim) {
			continue;
		}
		href += 6;
		char *aspa = strchr(href, '"');
		gchar *uri = g_strndup(href, aspa - href);
		gchar *arquivo = g_filename_from_uri(uri, NULL, NULL);
		g_free(uri);
		if (!arquivo) {
			continue;
		}
		struct stat st;
		if (stat(arquivo, &st) != 0) {
			g_free(arquivo);
			continue;
		}
		struct recente *r = &todos[n++];
		memset(r, 0, sizeof(*r));
		snprintf(r->caminho, sizeof(r->caminho), "%s", arquivo);
		gchar *base = g_path_get_basename(arquivo);
		gchar *pai = g_path_get_dirname(arquivo);
		gchar *onde = g_path_get_basename(pai);
		snprintf(r->nome, sizeof(r->nome), "%s", base);
		snprintf(r->onde, sizeof(r->onde), "%s", onde);
		g_free(base);
		g_free(pai);
		g_free(onde);
		r->pasta = S_ISDIR(st.st_mode);
		/* a data: visited, ou modified (ISO 8601) */
		const char *chaves[] = { "visited=\"", "modified=\"" };
		for (int k = 0; k < 2 && !r->quando; k++) {
			char *d = strstr(p, chaves[k]);
			if (d && d < fim) {
				d += strlen(chaves[k]);
				char *ate = strchr(d, '"');
				gchar *iso = g_strndup(d, ate ? (gsize)(ate - d) : strlen(d));
				GDateTime *dt = g_date_time_new_from_iso8601(iso, NULL);
				g_free(iso);
				if (dt) {
					r->quando = g_date_time_to_unix(dt);
					g_date_time_unref(dt);
				}
			}
		}
		char *mime = strstr(fim, "mime:mime-type type=\"");
		char *prox = strstr(fim, "</bookmark>");
		const char *tipo = mime && (!prox || mime < prox) ? mime + 21 : "";
		r->icone = r->pasta ? (!strcasecmp(r->nome, "Downloads") ? "download" : "folder")
			: !strncmp(tipo, "image/", 6) ? "image" : !strncmp(tipo, "audio/", 6) ? "music"
			: !strncmp(tipo, "video/", 6) ? "video" : "document";
		g_free(arquivo);
	}
	g_free(xml);
	qsort(todos, n, sizeof(todos[0]), por_data);
	nrecentes = n < MAX_RECENTES ? n : MAX_RECENTES;
	memcpy(recentes, todos, nrecentes * sizeof(recentes[0]));
}

static const char *ha_quanto(time_t t, char *buf, size_t tam) {
	long s = (long)(time(NULL) - t);
	if (s < 60) {
		return "agora";
	}
	if (s < 3600) {
		snprintf(buf, tam, "há %ld min", s / 60);
	} else if (s < 86400) {
		snprintf(buf, tam, "há %ld hora%s", s / 3600, s / 3600 > 1 ? "s" : "");
	} else if (s < 2 * 86400) {
		return "ontem";
	} else {
		snprintf(buf, tam, "há %ld dias", s / 86400);
	}
	return buf;
}

/* ---- fechar ---- */

static void soltar_marca(void) {
	char caminho[1024];
	snprintf(caminho, sizeof(caminho), "%s/lumna-lancador.pid", g_get_user_runtime_dir());
	unlink(caminho);
}

/* sai, e larga já a marca de aberto: quem abrir logo a seguir abre um novo, em vez de achar que
 * é um toque para fechar este (que ainda está a fechar) */
static void sair(void) {
	soltar_marca();
	gv_app_sair(app_lancador);
}

/* ---- abrir ---- */

static void executar(const char *comando) {
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

static void abrir_app(const struct app *a) {
	printf("abrir %s: %s\n", a->id, a->comando);
	fflush(stdout);
	executar(a->comando);
	sair();
}

static void abrir_arquivo(const struct recente *r) {
	gchar *q = g_shell_quote(r->caminho);
	char cmd[1200];
	snprintf(cmd, sizeof(cmd), "xdg-open %s", q);
	g_free(q);
	printf("abrir %s\n", r->caminho);
	fflush(stdout);
	executar(cmd);
	sair();
}

/* ---- a busca ---- */

static bool combina(const struct app *a) {
	return !busca[0] || strcasestr(a->nome, busca) || strcasestr(a->descricao, busca) || strcasestr(a->palavras, busca) ||
		strcasestr(a->id, busca);
}

/* ---- os cartões ---- */

/* o cartão grande dos favoritos: ícone e nome ao centro */
static bool cartao_grande(int i, float largura) {
	const struct app *a = &apps[i];
	Clay_ElementId id = CLAY_IDI("fav", i);
	bool tocado = gv_tocado(id);
	bool sobre = gv_por_cima(id);
	float marca = gv_anim(id.id ^ 0x77u, sobre || gv_focado(id) ? 1 : 0, gv_t.duracao_normal);
	CLAY(id, {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIXED(gv_app_altura(app_lancador) >= 900 ? 200 : 170) },
			.childGap = 18, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = sobre ? gv_t.selecionado : gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = sobre ? gv_alfa(gv_t.acento, 0.35f) : gv_t.borda, .width = GV_BORDA(1) },
	}) {
		tocado |= gv_focavel(id, gv_t.raio_grande);
		icone_app(a, 56, sobre);
		gv_texto(gv_str(a->nome), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_NORMAL);
		/* o sublinhado violeta de baixo, no que está debaixo do rato */
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(3) } },
			.backgroundColor = gv_alfa(gv_t.acento, marca),
			.floating = { .attachTo = CLAY_ATTACH_TO_PARENT,
				.attachPoints = { CLAY_ATTACH_POINT_LEFT_BOTTOM, CLAY_ATTACH_POINT_LEFT_BOTTOM },
				.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
	}
	return tocado;
}

/* o cartão da grade: ícone em cima, nome em baixo; o rato mostra os detalhes, segurar fixa-os */
static bool cartao(int i, float largura) {
	const struct app *a = &apps[i];
	Clay_ElementId id = CLAY_IDI("app", i);
	if (gv_segurou(id) || gv_menu_contexto(id)) {
		detalhe = i;
		fixado = true;
		confirmar_remocao = false;
	}
	bool tocado = gv_tocado(id);
	bool sobre = gv_por_cima(id);
	if ((sobre || gv_focado(id)) && !fixado && detalhe != i) {
		detalhe = i;
		confirmar_remocao = false;
	}
	bool esta = detalhe == i;
	float marca = gv_anim(id.id ^ 0x77u, esta ? 1 : 0, gv_t.duracao_normal);
	CLAY(id, {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIXED(122) },
			.padding = { 10, 10, 0, 0 }, .childGap = 12, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = esta ? gv_t.selecionado : sobre ? gv_t.sob_cursor : gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = esta ? gv_alfa(gv_t.acento, 0.35f) : gv_t.borda, .width = GV_BORDA(1) },
	}) {
		tocado |= gv_focavel(id, gv_t.raio_grande);
		icone_app(a, 44, esta);
		gv_texto(gv_str(a->nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(3) } },
			.backgroundColor = gv_alfa(gv_t.acento, marca),
			.floating = { .attachTo = CLAY_ATTACH_TO_PARENT,
				.attachPoints = { CLAY_ATTACH_POINT_LEFT_BOTTOM, CLAY_ATTACH_POINT_LEFT_BOTTOM },
				.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
	}
	return tocado;
}

/* uma grade de cartões que preenche a largura dada: quantas colunas couberem */
static void grade(Clay_ElementId id, const int *lista, int n, float largura, float largura_min, bool grandes) {
	if (largura <= 0) {
		largura = 1000;
		gv_app_redesenhar(NULL);
	}
	const float vao = 22;
	int colunas = (int)((largura + vao) / (largura_min + vao));
	colunas = colunas < 1 ? 1 : colunas;
	float w = (largura - vao * (colunas - 1)) / colunas;
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16 } }) {
		for (int linha = 0; linha * colunas < n; linha++) {
			CLAY(CLAY_IDI("linha", (int)(id.id % 1000) * 100 + linha), { .layout = { .childGap = (uint16_t)vao } }) {
				for (int c = 0; c < colunas && linha * colunas + c < n; c++) {
					int i = lista[linha * colunas + c];
					if (grandes ? cartao_grande(i, w) : cartao(i, w)) {
						abrir_app(&apps[i]);
					}
				}
			}
		}
	}
}

static void recentes_desenhar(void) {
	int mostrados = 0;
	for (int i = 0; i < nrecentes && mostrados < 3; i++) {
		if (busca[0] && !strcasestr(recentes[i].nome, busca)) {
			continue;
		}
		mostrados++;
	}
	if (!mostrados) {
		return;
	}
	CLAY(CLAY_ID("recentes-titulo"), { .layout = { .padding = { 0, 0, 30, 4 } } }) {
		gv_texto(CLAY_STRING("Recentes"), gv_t.tamanho_destaque + 3, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	CLAY(CLAY_ID("recentes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 28 } }) {
		mostrados = 0;
		for (int i = 0; i < nrecentes && mostrados < 3; i++) {
			const struct recente *r = &recentes[i];
			if (busca[0] && !strcasestr(r->nome, busca)) {
				continue;
			}
			mostrados++;
			Clay_ElementId id = CLAY_IDI("recente", i);
			bool tocado = gv_tocado(id);
			char quando[48];
			CLAY(id, {
				.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(88) }, .padding = { 24, 24, 0, 0 },
					.childGap = 22, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : gv_t.superficie,
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
				.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
			}) {
				tocado |= gv_focavel(id, gv_t.raio_grande);
				gv_icone(r->icone, 32, gv_t.icone);
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
					gv_texto(gv_str(r->nome), gv_t.tamanho_destaque + 1, gv_t.texto, GV_PESO_NORMAL);
					gv_texto(gv_fmt("%s  •  %s", r->pasta ? "Pasta" : r->onde, ha_quanto(r->quando, quando, sizeof(quando))),
						gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				gv_icone("forward", 20, gv_t.texto_secundario);
			}
			if (tocado) {
				abrir_arquivo(r);
			}
		}
	}
}

/* ---- os detalhes ---- */

static bool acao(Clay_ElementId id, const char *icone, const char *texto, bool perigo) {
	bool tocado = gv_tocado(id);
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 10, 10, 0, 0 }, .childGap = 14,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? (perigo ? gv_alfa(gv_t.perigo, 0.12f) : gv_t.sob_cursor) : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
	}) {
		tocado |= gv_focavel(id, gv_t.raio_medio);
		gv_icone(icone, 22, perigo ? gv_t.perigo : gv_t.icone);
		gv_texto(gv_str(texto), gv_t.tamanho_corpo + 1, perigo ? gv_t.perigo : gv_t.texto, GV_PESO_NORMAL);
	}
	return tocado;
}

static void detalhes(void) {
	if (detalhe < 0 || detalhe >= napps) {
		return;
	}
	struct app *a = &apps[detalhe];
	/* o painel rola por dentro quando a tela é baixa (a descrição longa não se espreme) */
	Clay_ElementId painel = CLAY_ID("detalhes");
	CLAY(painel, gv_rolagem(painel, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(400), CLAY_SIZING_FIT(0, gv_caixa(CLAY_ID("meio")).height) },
			.padding = { 24, 20, 22, 18 }, .childGap = 14 },
		.backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = fixado ? gv_alfa(gv_t.acento, 0.5f) : gv_t.borda, .width = GV_BORDA(1) },
	})) {
		CLAY(CLAY_ID("det-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 18,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			icone_app(a, 48, true);
			CLAY(CLAY_ID("det-nome"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
				.childGap = 4 } }) {
				gv_texto(gv_str(a->nome), gv_t.tamanho_titulo + 2, gv_t.texto, GV_PESO_NORMAL);
				gv_texto(gv_str(a->tipo), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			/* o ✕ só existe com os detalhes fixos: solta-os */
			if (fixado && gv_botao(CLAY_ID("det-soltar"), "close", NULL, GV_BOTAO_SIMPLES)) {
				fixado = false;
				confirmar_remocao = false;
			}
		}
		gv_separador(false, 0);
		if (a->descricao[0]) {
			gv_texto(gv_str(a->descricao), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("det-estado"), { .layout = { .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
			.padding = { 0, 0, 4, 4 } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8) } }, .backgroundColor = gv_t.sucesso,
				.cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
			gv_texto(CLAY_STRING("Instalado"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (gv_botao_ext(CLAY_ID("det-abrir"), &(gv_botao_opcoes){ .icone = "media-play", .texto = "Abrir",
				.estilo = GV_BOTAO_CHEIO, .crescer = true })) {
			abrir_app(a);
		}
		CLAY(CLAY_ID("det-acoes1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
			bool fav = e_favorito(a);
			if (acao(CLAY_ID("det-fav"), "star", fav ? "Desfavoritar" : "Favoritar", false)) {
				alternar_favorito(a);
			}
			bool dock = na_dock(a);
			if (acao(CLAY_ID("det-dock"), "pin", dock ? "Já está na dock" : "Fixar na dock", false) && !dock) {
				fixar_na_dock(a);
				printf("fixar na dock: %s\n", a->id);
			}
		}
		gv_separador(false, 0);
		if (confirmar_remocao) {
			CLAY(CLAY_ID("det-confirmar"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
				.childGap = 10 } }) {
				gv_texto(gv_fmt("Desinstalar o %s?", a->nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
				CLAY(CLAY_ID("det-conf-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
					if (gv_botao_ext(CLAY_ID("det-nao"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO,
							.crescer = true })) {
						confirmar_remocao = false;
					}
					if (gv_botao_ext(CLAY_ID("det-sim"), &(gv_botao_opcoes){ .icone = "trash", .texto = "Desinstalar",
							.estilo = GV_BOTAO_PERIGO, .crescer = true })) {
						/* quem desinstala é o gestor de pacotes do LUMNA, pelo .desktop do app */
						gchar *q = g_shell_quote(a->caminho);
						char cmd[1200];
						snprintf(cmd, sizeof(cmd), "lumna-pkg desinstalar %s", q);
						g_free(q);
						printf("desinstalar: %s\n", cmd);
						fflush(stdout);
						executar(cmd);
						confirmar_remocao = false;
					}
				}
			}
		} else {
			CLAY(CLAY_ID("det-acoes2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
				if (acao(CLAY_ID("det-prop"), "settings", "Propriedades", false)) {
					gchar *q = g_shell_quote(a->caminho);
					char cmd[1200];
					snprintf(cmd, sizeof(cmd), "xdg-open %s", q);
					g_free(q);
					printf("propriedades: %s\n", a->caminho);
					executar(cmd);
				}
				if (acao(CLAY_ID("det-remover"), "trash", "Desinstalar", true)) {
					confirmar_remocao = true;
				}
			}
		}
	}
}

/* ---- a tela ---- */

static void marca_tempo(const char *etapa);

static void desenhar(gv_app *app, void *dados) {
	static int quadros;
	if (quadros < 3) {
		quadros++;
		marca_tempo(quadros == 1 ? "primeiro quadro" : quadros == 2 ? "segundo quadro" : "terceiro quadro");
	}
	(void)dados;
	static int lista[MAX_APPS];
	int n = 0;
	bool buscando = busca[0] != '\0';
	if (aba == FAVORITOS && !buscando) {
		for (int f = 0; f < nfavoritos; f++) {
			for (int i = 0; i < napps; i++) {
				if (!strcmp(apps[i].id, favoritos[f])) {
					lista[n++] = i;
				}
			}
		}
	} else {
		for (int i = 0; i < napps; i++) {
			if (buscando ? combina(&apps[i]) : (int)apps[i].aba == aba) {
				lista[n++] = i;
			}
		}
	}
	bool com_detalhes = aba != FAVORITOS || buscando;
	if (detalhe < 0 && n && com_detalhes) {
		detalhe = lista[0];
	}

	Clay_ElementId tela = CLAY_ID("tela");
	/* as margens crescem com a tela (100 px numa de 1600, 48 numa de 1280) */
	float largura_tela = gv_app_largura(app);
	uint16_t lado = (uint16_t)(largura_tela >= 1600 ? 100 : largura_tela >= 1200 ? 60 + (largura_tela - 1200) / 10 : 32);
	CLAY(tela, {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.padding = { lado, lado, 56, 24 } },
		.backgroundColor = gv_alfa(gv_t.fundo, 0.985f),
	}) {
		/* o título e a busca */
		CLAY(CLAY_ID("topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("titulo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Início"), 56, gv_t.texto, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("busca-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(556) } } }) {
				if (gv_campo(CLAY_ID("busca"), busca, sizeof(busca), "Buscar aplicativos e arquivos", GV_CAMPO_BUSCA) &
					GV_CAMPO_ENTER) {
					if (n) {
						abrir_app(&apps[detalhe >= 0 && combina(&apps[detalhe]) ? detalhe : lista[0]]);
					}
				}
			}
		}
		CLAY(CLAY_ID("abas-faixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 40, 26 } } }) {
			if (buscando) {
				gv_texto(gv_fmt("Resultados para “%s”", busca), gv_t.tamanho_destaque + 2, gv_t.texto_secundario, GV_PESO_NORMAL);
			} else if (gv_abas(CLAY_ID("abas"), nomes_abas, NABAS, &aba)) {
				fixado = false;
				detalhe = -1;
				confirmar_remocao = false;
			}
		}
		/* o meio: a grade (e os detalhes ao lado), a rolar */
		CLAY(CLAY_ID("meio"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 26 },
			.clip = { .horizontal = true } }) {
			Clay_ElementId rolar = CLAY_ID("rolar");
			CLAY(rolar, gv_rolagem(rolar, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
				.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
					.padding = { 2, 2, 2, 2 } } })) {
				if (!n) {
					gv_texto(buscando ? gv_fmt("Nenhum aplicativo com “%s”.", busca)
						: aba == FAVORITOS ? CLAY_STRING("Nenhum favorito ainda: segure um app nas outras abas e toque em Favoritar.")
						: CLAY_STRING("Nenhum aplicativo nesta categoria."), gv_t.tamanho_destaque, gv_t.texto_secundario,
						GV_PESO_NORMAL);
				} else {
					/* a largura do meio (do quadro anterior) menos os detalhes e o vão */
					float largura = gv_caixa(CLAY_ID("meio")).width - (com_detalhes ? 400 + 26 : 0) - 4;
					grade(CLAY_ID("grade"), lista, n, largura, com_detalhes ? 200 : 190, !com_detalhes);
				}
				if (aba == FAVORITOS || buscando) {
					recentes_desenhar();
				}
			}
			if (com_detalhes) {
				detalhes();
			}
		}
		/* em baixo: bloquear, desligar, a Emma e a dica da busca */
		/* três partes: as duas de fora dividem o espaço por igual, e o Fechar fica mesmo ao centro */
		CLAY(CLAY_ID("rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 26, 0 }, .childGap = 10,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("rodape-esq"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10,
				.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao(CLAY_ID("bloquear"), "lock", "Bloquear", GV_BOTAO_SIMPLES)) {
				executar("lumna-bloqueio");
				sair();
			}
			gv_separador(true, 28);
			if (gv_botao(CLAY_ID("desligar"), "power", "Desligar", GV_BOTAO_SIMPLES)) {
				gv_popup_alternar(CLAY_ID("energia"));
			}
			GV_POPUP(CLAY_ID("energia"), CLAY_ID("desligar"), .lado = GV_POPUP_ACIMA, .alinhar = GV_ALINHAR_INICIO, .largura = 230) {
				if (gv_menu_item(CLAY_ID("en-suspender"), "moon", "Suspender", NULL)) {
					executar("systemctl suspend");
					sair();
				}
				if (gv_menu_item(CLAY_ID("en-reiniciar"), "refresh", "Reiniciar", NULL)) {
					executar("systemctl reboot");
				}
				if (gv_menu_item(CLAY_ID("en-desligar"), "power", "Desligar", NULL)) {
					executar("systemctl poweroff");
				}
			}
			}
			/* fechar sem teclado (com o rato ou o dedo) */
			if (gv_botao(CLAY_ID("fechar"), "chevron-down", "Fechar", GV_BOTAO_ELEVADO)) {
				sair();
			}
			CLAY(CLAY_ID("rodape-dir"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10,
				.childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao(CLAY_ID("emma"), "mic", "Perguntar à Emma", GV_BOTAO_SIMPLES)) {
				executar("lumna-emma");
				sair();
			}
			gv_separador(true, 28);
			CLAY(CLAY_ID("dica"), { .layout = { .childGap = 12, .padding = { 14, 8, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY(CLAY_ID("tecla"), { .layout = { .sizing = { CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(32) },
					.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
					.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
					gv_texto(CLAY_STRING("/"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
				gv_texto(CLAY_STRING("para buscar"), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			}
		}
	}
	fflush(stdout);

	/* teclado: Esc limpa a busca e depois fecha; "/" vai à busca */
	if (gv_tecla_carregada(XKB_KEY_Escape)) {
		if (busca[0]) {
			busca[0] = '\0';
		} else if (fixado) {
			fixado = false;
		} else {
			sair();
		}
	}
	if (gv_tecla_carregada(XKB_KEY_slash) && !gv_focado(CLAY_ID("busca"))) {
		gv_focar(CLAY_ID("busca"));
	}
}

/* Cada abertura começa limpa: os detalhes soltos (o último fixado não fica preso), sem busca, e a
 * busca pronta para escrever. Hoje cada abertura é um processo novo; se o lançador passar a ficar
 * residente (para abrir mais depressa), é aqui que se volta ao começo. */
static void ao_abrir(void) {
	fixado = false;
	detalhe = -1;
	confirmar_remocao = false;
	busca[0] = '\0';
	gv_focar(CLAY_ID("busca"));
}

/* chamar outra vez fecha o aberto (a dock abre e fecha com o mesmo toque) */
static bool alternar_instancia(void) {
	char caminho[1024];
	const char *run = g_get_user_runtime_dir();
	snprintf(caminho, sizeof(caminho), "%s/lumna-lancador.pid", run);
	FILE *f = fopen(caminho, "r");
	if (f) {
		int pid = 0;
		if (fscanf(f, "%d", &pid) == 1 && pid > 0 && kill(pid, 0) == 0) {
			fclose(f);
			kill(pid, SIGTERM);
			return true;
		}
		fclose(f);
	}
	f = fopen(caminho, "w");
	if (f) {
		fprintf(f, "%d\n", (int)getpid());
		fclose(f);
	}
	return false;
}

static void ao_terminar(int s) {
	(void)s;
	if (app_lancador) {
		gv_app_sair(app_lancador); /* (num sinal: a marca sai no fim do main) */
	}
}

/* LUMNA_TEMPO=1: quanto leva cada etapa da abertura, no terminal (para achar o que deixa o launcher lento) */
static double t_inicio;
static bool medir;
static double agora_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}
static void marca_tempo(const char *etapa) {
	if (medir) {
		fprintf(stderr, "lumna-lancador: %-24s %7.1f ms\n", etapa, agora_ms() - t_inicio);
	}
}

int main(void) {
	t_inicio = agora_ms();
	medir = getenv("LUMNA_TEMPO") != NULL;
	if (alternar_instancia()) {
		return 0;
	}
	snprintf(pasta_config, sizeof(pasta_config), "%s/lumna", g_get_user_config_dir());
	carregar_apps();
	marca_tempo("apps lidos");
	carregar_favoritos();
	carregar_recentes();
	marca_tempo("favoritos e recentes");
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Início", .app_id = "lumna-lancador", .tipo = GV_CAMADA,
		.nivel = GV_NIVEL_CIMA, .ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1, .teclado_exclusivo = true,
		.margens = { 40, 0, 0, 0 } });
	if (!app) {
		return 1;
	}
	marca_tempo("janela criada");
	app_lancador = app;
	signal(SIGTERM, ao_terminar);
	ao_abrir();
	int r = gv_app_correr(app, desenhar, NULL);
	soltar_marca();
	return r;
}
