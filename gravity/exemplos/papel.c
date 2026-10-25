/*
 * O papel de parede do desktop novo: uma camada no fundo de cada saída, com a imagem a cobrir a
 * tela. A imagem descodifica no tamanho da tela (um 4K num ecrã 1080 gasta a memória de 1080).
 *   ./papel [imagem]
 *
 * Sem imagem: a de ~/.config/lumna/papel (um caminho), senão a do LUMNA (/usr/share/lumna, ou a
 * do repositório, ao lado do programa, para testar sem instalar). Trocar o arquivo de config troca
 * o papel na hora (o Painel escreve lá). O botão direito (ou o dedo parado) abre o menu do desktop.
 */
#include <glib.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gravity.h"

static char imagem[1024];
static char config[1100];
static char pasta_config[1024];
static bool da_linha_de_comando;
static Clay_Vector2 onde_menu; /* onde se pediu o menu */
static bool abrir_menu;        /* abre no quadro seguinte, com a âncora já no sítio novo */

static bool existe(const char *caminho) {
	return access(caminho, R_OK) == 0;
}

static void escolher(void) {
	if (da_linha_de_comando) {
		return;
	}
	imagem[0] = '\0';
	FILE *f = fopen(config, "r");
	if (f && fgets(imagem, sizeof(imagem), f)) {
		imagem[strcspn(imagem, "\r\n")] = '\0';
	}
	if (f) {
		fclose(f);
	}
	if (imagem[0] && existe(imagem)) {
		return;
	}
	/* o do LUMNA: instalado, ou o do repositório (gravity/../session/) */
	if (existe("/usr/share/lumna/wallpaper.png")) {
		snprintf(imagem, sizeof(imagem), "/usr/share/lumna/wallpaper.png");
		return;
	}
	char exe[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = '\0';
		snprintf(imagem, sizeof(imagem), "%s/../session/wallpaper.png", dirname(exe));
	}
}

/* o Painel trocou o papel: o arquivo mudou */
static void mudou(gv_app *app, int fd, void *dados) {
	char buf[4096];
	while (read(fd, buf, sizeof(buf)) > 0) {
	}
	escolher();
	gv_app_redesenhar(app);
}

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
	(void)app;
	(void)dados;
	Clay_ElementId id = CLAY_ID("fundo");
	if (abrir_menu) {
		abrir_menu = false;
		gv_popup_abrir(CLAY_ID("menu"));
	}
	if (gv_menu_contexto(id)) {
		onde_menu = gv_ponteiro();
		gv_popup_fechar(CLAY_ID("menu"));
		abrir_menu = true;
		gv_app_redesenhar(NULL);
	}
	/* a cor por trás é o que se vê se a imagem faltar */
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.fundo }) {
		Clay_ElementId img = CLAY_ID("imagem");
		CLAY(img, gv_imagem(img, imagem, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {}
		/* a âncora do menu: um ponto onde se clicou */
		CLAY(CLAY_ID("ancora"), { .layout = { .sizing = { CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(1) } },
			.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .offset = onde_menu,
				.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
	}
	menu();
}

int main(int argc, char **argv) {
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *casa = getenv("HOME");
	if (xdg && *xdg) {
		snprintf(pasta_config, sizeof(pasta_config), "%s/lumna", xdg);
	} else {
		snprintf(pasta_config, sizeof(pasta_config), "%s/.config/lumna", casa ? casa : "");
	}
	snprintf(config, sizeof(config), "%s/papel", pasta_config);
	if (argc > 1) {
		snprintf(imagem, sizeof(imagem), "%s", argv[1]);
		da_linha_de_comando = true;
	}
	escolher();
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Papel de parede", .app_id = "lumna-papel", .tipo = GV_CAMADA,
		.nivel = GV_NIVEL_FUNDO, .ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1 });
	if (!app) {
		return 1;
	}
	/* vigia a pasta (o arquivo pode ainda não existir, ou ser trocado por outro ao gravar) */
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd >= 0) {
		g_mkdir_with_parents(pasta_config, 0755);
		if (inotify_add_watch(fd, pasta_config, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE) >= 0) {
			gv_app_vigiar(app, fd, mudou, NULL);
		}
	}
	return gv_app_correr(app, desenhar, NULL);
}
