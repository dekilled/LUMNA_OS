/*
 * O papel de parede do desktop novo: uma camada no fundo de cada tela, com a imagem a cobrir a
 * tela (ou como o papel.ini mandar). A imagem descodifica no tamanho da tela (um 4K num ecrã 1080
 * gasta a memória de 1080).
 *   ./papel [imagem]          (com uma imagem: essa, em todas as telas)
 *
 * O que vai em cada tela vem do ~/.config/lumna/papel.ini (servicos/papelini.h): o mesmo em todas
 * ou um por tela, um por modo, e trocar sozinho entre os da coleção. O papel da tela principal
 * abre os das outras (./papel --saida NOME), como a dock. Mudar o arquivo (o Painel, no Aplicar)
 * troca o papel na hora. O botão direito (ou o dedo parado) abre o menu do desktop.
 */
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "gravity.h"
#include "papelini.h"

#define MAX_FILHAS 8

static gv_app *app_papel;
static const char *minha_saida;  /* NULL: o da tela principal (que abre os outros) */
static char forcada[1024];        /* a da linha de comando */
static char imagem[1024];
static int ajuste;
static double proxima_troca;      /* com trocar: quando muda de novo */
static Clay_Vector2 onde_menu;    /* onde se pediu o menu */
static bool abrir_menu;           /* abre no quadro seguinte, com a âncora já no sítio novo */

static struct {
	char saida[64];
	pid_t pid;
} filhas[MAX_FILHAS];
static int nfilhas;

/* o nome da tela desta camada */
static const char *esta_tela(void) {
	if (minha_saida) {
		return minha_saida;
	}
	const char *saidas[8];
	return gv_saidas(app_papel, saidas, 8) ? saidas[0] : "";
}

static int indice_tela(void) {
	const char *saidas[8];
	int n = gv_saidas(app_papel, saidas, 8);
	for (int i = 0; i < n; i++) {
		if (!strcmp(saidas[i], esta_tela())) {
			return i;
		}
	}
	return 0;
}

/* decide a imagem desta tela agora */
static void escolher(void) {
	if (forcada[0]) {
		snprintf(imagem, sizeof(imagem), "%s", forcada);
		ajuste = PAPEL_PREENCHER;
		return;
	}
	struct papel_cfg cfg;
	papel_ler(&cfg);
	const struct papel_item *it = papel_para(&cfg, papel_modo_atual(), esta_tela());
	snprintf(imagem, sizeof(imagem), "%s", it && access(it->imagem, R_OK) == 0 ? it->imagem : papel_padrao());
	ajuste = it ? it->ajuste : PAPEL_PREENCHER;
	proxima_troca = 0;
	if (cfg.trocar) {
		/* todas as telas pela mesma conta do relógio: trocam juntas; com um por tela, cada uma
		 * vai numa imagem diferente da coleção */
		static char colecao[PAPEL_COLECAO_MAX][512];
		int n = papel_colecao(colecao, PAPEL_COLECAO_MAX);
		if (n > 0) {
			long periodo = (long)cfg.intervalo * 60;
			long agora = (long)time(NULL);
			long passo = agora / periodo + (cfg.por_tela ? indice_tela() : 0);
			snprintf(imagem, sizeof(imagem), "%s", colecao[passo % n]);
			proxima_troca = gv_agora() + (double)(periodo - agora % periodo) + 0.5;
		}
	}
}

/* o Painel trocou o papel (ou o modo): o arquivo mudou */
static void mudou(gv_app *app, int fd, void *dados) {
	char buf[4096];
	while (read(fd, buf, sizeof(buf)) > 0) {
	}
	escolher();
	gv_app_redesenhar(app);
}

/* ---- um papel por tela (o da principal abre os outros) ---- */

static void filha_abrir(const char *saida) {
	pid_t p = fork();
	if (p == 0) {
		prctl(PR_SET_PDEATHSIG, SIGTERM); /* o principal fechou: este também */
		execl("/proc/self/exe", "papel", "--saida", saida, (char *)NULL);
		_exit(127);
	}
	if (p > 0 && nfilhas < MAX_FILHAS) {
		snprintf(filhas[nfilhas].saida, sizeof(filhas[0].saida), "%s", saida);
		filhas[nfilhas++].pid = p;
	}
}

static void filhas_rever(void) {
	const char *saidas[8];
	int n = gv_saidas(app_papel, saidas, 8);
	for (int i = 0; i < nfilhas; i++) {
		bool existe = false;
		for (int k = 1; k < n; k++) {
			existe |= !strcmp(saidas[k], filhas[i].saida);
		}
		bool morreu = waitpid(filhas[i].pid, NULL, WNOHANG) == filhas[i].pid;
		if (!existe || morreu) {
			if (!morreu) {
				kill(filhas[i].pid, SIGTERM);
				waitpid(filhas[i].pid, NULL, 0);
			}
			filhas[i--] = filhas[--nfilhas];
		}
	}
	for (int k = 1; k < n; k++) {
		bool tem = false;
		for (int i = 0; i < nfilhas; i++) {
			tem |= !strcmp(filhas[i].saida, saidas[k]);
		}
		if (!tem) {
			filha_abrir(saidas[k]);
		}
	}
}

/* ---- o menu do desktop ---- */

static void executar(const char *comando) {
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) {
			setsid();
			execl("/bin/sh", "sh", "-c", comando, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}

static void menu(void) {
	Clay_ElementId m = CLAY_ID("menu");
	GV_POPUP(m, CLAY_ID("ancora"), .largura = 240, .distancia = 2) {
		if (gv_menu_item(CLAY_ID("terminal"), "terminal", "Abrir terminal", NULL)) {
			executar("${TERMINAL:-foot}");
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("arquivos"), "folder", "Arquivos", NULL)) {
			executar("lumna-arquivos");
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("apps"), "grid", "Todos os apps", NULL)) {
			executar("lumna-lancador");
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("emma"), "sparkle", "Perguntar à Emma", NULL)) {
			executar("lumna-emma");
			gv_popup_fechar(m);
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("papel"), "image", "Papel de parede…", NULL)) {
			executar("lumna-painel --papel");
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("config"), "settings", "Configurações", NULL)) {
			executar("lumna-painel");
			gv_popup_fechar(m);
		}
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	if (!minha_saida && gv_saidas_mudaram(app)) {
		filhas_rever();
		escolher(); /* a principal pode ter mudado de nome */
	}
	if (proxima_troca > 0) {
		if (gv_agora() >= proxima_troca) {
			escolher();
		}
		gv_acordar_em(proxima_troca - gv_agora());
	}
	if (abrir_menu) {
		abrir_menu = false;
		gv_popup_abrir(CLAY_ID("menu"));
	}
	Clay_ElementId id = CLAY_ID("fundo");
	if (gv_menu_contexto(id)) {
		onde_menu = gv_ponteiro();
		gv_popup_fechar(CLAY_ID("menu"));
		abrir_menu = true;
		gv_app_redesenhar(NULL);
	}
	/* a cor por trás é o que se vê se a imagem faltar (ou nas sobras do Ajustar) */
	static const enum gv_ajuste ajustes[] = { GV_AJUSTE_COBRIR, GV_AJUSTE_CONTER, GV_AJUSTE_ESTICAR };
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.fundo }) {
		Clay_ElementId img = CLAY_ID("imagem");
		CLAY(img, gv_imagem(img, imagem, ajustes[ajuste], (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {}
		/* a âncora do menu: um ponto onde se clicou */
		CLAY(CLAY_ID("ancora"), { .layout = { .sizing = { CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(1) } },
			.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .offset = onde_menu,
				.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
	}
	menu();
}

int main(int argc, char **argv) {
	if (argc >= 3 && !strcmp(argv[1], "--saida")) {
		minha_saida = argv[2];
	} else if (argc > 1) {
		snprintf(forcada, sizeof(forcada), "%s", argv[1]);
	}
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Papel de parede", .app_id = "lumna-papel", .tipo = GV_CAMADA,
		.nivel = GV_NIVEL_FUNDO, .ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1,
		.saida = minha_saida ? minha_saida : "@principal" });
	if (!app) {
		return 1;
	}
	app_papel = app;
	escolher();
	if (!minha_saida) {
		filhas_rever();
	}
	/* vigia a pasta (o arquivo pode ainda não existir, ou ser trocado por outro ao gravar) */
	char ini[600];
	papel_caminho_ini(ini, sizeof(ini));
	char *pasta = g_path_get_dirname(ini);
	g_mkdir_with_parents(pasta, 0755);
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd >= 0 && inotify_add_watch(fd, pasta, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE) >= 0) {
		gv_app_vigiar(app, fd, mudou, NULL);
	}
	g_free(pasta);
	return gv_app_correr(app, desenhar, NULL);
}
