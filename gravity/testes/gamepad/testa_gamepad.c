/* A leitura dos controles (servicos/gamepad.c) com controles de mentira: um FIFO por controle, cheio de `struct input_event`. */
#include <fcntl.h>
#include <glib.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gamepad.h"

static int falhas;
#define CONFERE(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

static char registro[256];

static void emite(const struct gp_disp *d, const struct gp_evento *e, void *dados) {
	char t[24];
	g_snprintf(t, sizeof(t), "%s%s ", gp_botao_nome(e->botao), e->pressionado ? "+" : "-");
	g_strlcat(registro, t, sizeof(registro));
}

static void manda(int fd, int tipo, int codigo, int valor) {
	struct input_event e = { .type = (uint16_t)tipo, .code = (uint16_t)codigo, .value = valor };
	if (write(fd, &e, sizeof(e)) < 0) {
	}
}

static const char *le(struct gp_disp *d, const struct gp_cfg *c) {
	registro[0] = '\0';
	gp_ler(d, c, emite, NULL);
	return registro;
}

int main(void) {
	const char *pasta = g_getenv("LUMNA_GAMEPAD_FALSOS"), *energia = g_getenv("LUMNA_POWER_DIR");
	char *dev = g_strdup_printf("%s/xbox.dev", pasta), *ev = g_strdup_printf("%s/xbox.ev", pasta);
	g_file_set_contents(dev, "[controle]\nnome=Xbox Wireless Controller\nuniq=AA:BB:CC:DD:EE:01\nbus=bluetooth\nvendor=1118\nproduct=2834\nrumble=sim\n", -1, NULL);
	mkfifo(ev, 0600);
	char *dev2 = g_strdup_printf("%s/ps.dev", pasta), *ev2 = g_strdup_printf("%s/ps.ev", pasta);
	g_file_set_contents(dev2, "[controle]\nnome=DualSense\nuniq=\nbus=usb\nrumble=nao\nbateria=77\n", -1, NULL);
	mkfifo(ev2, 0600);
	char *bat = g_strdup_printf("%s/ps-controller-battery-aa:bb:cc:dd:ee:01", energia);
	g_mkdir_with_parents(bat, 0700);
	char *cap = g_strdup_printf("%s/capacity", bat);
	g_file_set_contents(cap, "64\n", -1, NULL);

	struct gp_cfg c;
	gp_cfg_ler(&c);
	CONFERE(c.zona_morta == 15 && c.vibracao && !c.trocar_ab && !strcmp(c.guia, "biblioteca") && !strcmp(c.energia, "performance") && !c.fps, "configuração padrão");

	static struct gp_disp l[GP_MAX];
	int n = gp_varrer(l, 0, GP_MAX);
	CONFERE(n == 2, "acha os 2 controles");
	struct gp_disp *x = NULL, *ps = NULL;
	for (int i = 0; i < n; i++) {
		if (!strcmp(l[i].id, "AA:BB:CC:DD:EE:01")) {
			x = &l[i];
		} else {
			ps = &l[i];
		}
	}
	CONFERE(x && x->bluetooth && x->vendor == 1118 && x->rumble && x->bateria == 64, "o Xbox: Bluetooth, vendor, vibra e a bateria vem do /sys (64%)");
	CONFERE(ps && !ps->bluetooth && !ps->rumble && ps->bateria == 77 && !strcmp(ps->id, "ps"), "o DualSense: USB, sem vibração, bateria do .dev, sem uniq usa o nome do arquivo");
	int fd = open(ev, O_WRONLY | O_NONBLOCK);

	/* botões */
	manda(fd, EV_KEY, BTN_SOUTH, 1);
	manda(fd, EV_KEY, BTN_SOUTH, 0);
	manda(fd, EV_KEY, BTN_EAST, 1);
	manda(fd, EV_KEY, BTN_MODE, 1);
	manda(fd, EV_KEY, BTN_TL, 1);
	manda(fd, EV_KEY, BTN_TR, 1);
	manda(fd, EV_KEY, BTN_SOUTH, 2); /* repetição do kernel: ignorada */
	manda(fd, EV_SYN, 0, 0);
	CONFERE(!strcmp(le(x, &c), "A+ A- B+ GUIA+ LB+ RB+ "), "A, B, Guia, LB e RB (a repetição do kernel não conta)");

	/* trocar A e B */
	c.trocar_ab = true;
	manda(fd, EV_KEY, BTN_SOUTH, 1);
	manda(fd, EV_KEY, BTN_EAST, 1);
	CONFERE(!strcmp(le(x, &c), "B+ A+ "), "trocar A↔B (estilo Nintendo)");
	c.trocar_ab = false;

	/* o direcional (hat) */
	manda(fd, EV_ABS, ABS_HAT0X, 1);
	manda(fd, EV_ABS, ABS_HAT0X, 0);
	manda(fd, EV_ABS, ABS_HAT0Y, -1);
	manda(fd, EV_ABS, ABS_HAT0Y, 0);
	CONFERE(!strcmp(le(x, &c), "DIR+ DIR- CIMA+ CIMA- "), "o direcional vira setas");

	/* o analógico: dentro da zona morta nada; além, direção; com histerese ao voltar */
	manda(fd, EV_ABS, ABS_X, 4000);
	CONFERE(!strcmp(le(x, &c), ""), "analógico dentro da zona morta: nada");
	manda(fd, EV_ABS, ABS_X, 30000);
	CONFERE(!strcmp(le(x, &c), "DIR+ "), "analógico para a direita: DIR");
	manda(fd, EV_ABS, ABS_X, 15000);
	CONFERE(!strcmp(le(x, &c), ""), "histerese: perto do limite, continua apertado");
	manda(fd, EV_ABS, ABS_X, 0);
	CONFERE(!strcmp(le(x, &c), "DIR- "), "volta ao centro: solta");
	manda(fd, EV_ABS, ABS_Y, -30000);
	manda(fd, EV_ABS, ABS_Y, 0);
	CONFERE(!strcmp(le(x, &c), "CIMA+ CIMA- "), "analógico para cima: CIMA");
	/* uma zona morta maior pede mais curso */
	c.zona_morta = 50;
	manda(fd, EV_ABS, ABS_X, 14000);
	CONFERE(!strcmp(le(x, &c), ""), "zona morta de 50%: 14000 (43% do curso) não conta");
	c.zona_morta = 15;
	manda(fd, EV_ABS, ABS_X, 14100);
	CONFERE(!strcmp(le(x, &c), "DIR+ "), "com 15% o mesmo curso já conta");
	manda(fd, EV_ABS, ABS_X, 0);
	le(x, &c);
	/* os gatilhos analógicos */
	manda(fd, EV_ABS, ABS_Z, 255);
	manda(fd, EV_ABS, ABS_Z, 0);
	manda(fd, EV_ABS, ABS_RZ, 200);
	CONFERE(!strcmp(le(x, &c), "LT+ LT- RT+ "), "os gatilhos analógicos viram LT e RT");
	/* um evento que não conhecemos não derruba nada */
	manda(fd, EV_KEY, BTN_THUMBL, 1);
	manda(fd, EV_REL, REL_X, 5);
	CONFERE(!strcmp(le(x, &c), ""), "eventos desconhecidos são ignorados");

	/* apelido e ignorar */
	CONFERE(gp_apelido_gravar(x->id, "Controle da sala"), "grava o apelido");
	CONFERE(!gp_apelido_gravar("a=b", "x") && !gp_apelido_gravar("", "x"), "id com lixo é recusado");
	CONFERE(gp_ignorar_gravar(ps->id, true), "ignora o DualSense");
	n = gp_varrer(l, n, GP_MAX);
	for (int i = 0; i < n; i++) {
		if (!strcmp(l[i].id, "AA:BB:CC:DD:EE:01")) {
			x = &l[i];
		} else {
			ps = &l[i];
		}
	}
	CONFERE(!strcmp(x->apelido, "Controle da sala") && !x->ignorado && ps->ignorado, "o apelido e o ignorado voltam do arquivo");
	CONFERE(gp_apelido_gravar(x->id, "  "), "apelido vazio remove");
	gp_apelidos_ler(l, n);
	CONFERE(!x->apelido[0], "o apelido removido some");

	/* configurações */
	struct gp_cfg g;
	gp_cfg_ler(&g);
	g.zona_morta = 99;
	g.trocar_ab = true;
	g.vibracao = false;
	g.fps = true;
	g_strlcpy(g.guia, "emma", sizeof(g.guia));
	g_strlcpy(g.energia, "balanced", sizeof(g.energia));
	CONFERE(gp_cfg_gravar(&g), "grava a configuração");
	gp_cfg_ler(&g);
	CONFERE(g.zona_morta == 50 && g.trocar_ab && !g.vibracao && g.fps && !strcmp(g.guia, "emma") && !strcmp(g.energia, "balanced"), "a configuração volta (a zona morta limitada a 50)");
	char *ini = gp_cfg_caminho();
	GKeyFile *kf = g_key_file_new();
	g_key_file_load_from_file(kf, ini, G_KEY_FILE_NONE, NULL);
	g_key_file_set_string(kf, "geral", "guia", "rm -rf");
	g_key_file_set_string(kf, "gaming", "energia", "xyz");
	g_key_file_save_to_file(kf, ini, NULL);
	gp_cfg_ler(&g);
	CONFERE(!strcmp(g.guia, "biblioteca") && !strcmp(g.energia, "performance"), "valores desconhecidos no arquivo voltam ao padrão");
	g_key_file_free(kf);
	g_free(ini);

	/* vibrar */
	CONFERE(gp_vibrar(x, 100), "o Xbox vibra");
	CONFERE(!gp_vibrar(ps, 100), "o DualSense (sem vibração) recusa");
	char *vlog = g_strdup_printf("%s/vibrou.log", pasta), *vt = NULL;
	g_file_get_contents(vlog, &vt, NULL, NULL);
	CONFERE(vt && strstr(vt, "AA:BB:CC:DD:EE:01 100") != NULL, "o tranco foi pedido ao controle certo");
	g_free(vt);
	g_free(vlog);

	/* desconectar: o arquivo some e o controle sai da lista, fechado */
	int fd_ps = ps->fd;
	unlink(dev2);
	unlink(ev2);
	n = gp_varrer(l, n, GP_MAX);
	CONFERE(n == 1 && !strcmp(l[0].id, "AA:BB:CC:DD:EE:01"), "o controle desconectado sai da lista");
	CONFERE(fcntl(fd_ps, F_GETFD) < 0, "e o descritor dele foi fechado");
	close(fd);
	for (int i = 0; i < n; i++) {
		gp_fechar(&l[i]);
	}

	g_free(dev); g_free(ev); g_free(dev2); g_free(ev2); g_free(bat); g_free(cap);
	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("gamepad (leitura): ok\n");
	return 0;
}
