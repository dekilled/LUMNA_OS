/* O serviço do terminal (servicos/terminal.c): a grade, as cores, o histórico, o tamanho, as teclas e o colar, contra processos de verdade num pty. */
#define _GNU_SOURCE
#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "terminal.h"

static int falhas;
#define CHECA(cond, ...) do { if (!(cond)) { fprintf(stderr, "FALHOU (%s:%d): ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); falhas++; } } while (0)

static const uint32_t ANSI[16] = { 0x000000, 0xcc0000, 0x00cc00, 0xcccc00, 0x0000cc, 0xcc00cc, 0x00cccc, 0xcccccc,
	0x555555, 0xff5555, 0x55ff55, 0xffff55, 0x5555ff, 0xff55ff, 0x55ffff, 0xffffff };
#define FG 0xe0e0e0
#define BG 0x101010

static terminal *abrir(int c, int l, const char *cmd) {
	terminal *t = terminal_abrir(c, l, "/tmp", cmd);
	assert(t);
	terminal_cores(t, FG, BG, ANSI);
	return t;
}

/* lê até o processo acabar (ou 4 s) */
static void ate_acabar(terminal *t) {
	for (int i = 0; i < 80; i++) {
		struct pollfd p = { .fd = terminal_fd(t), .events = POLLIN };
		poll(&p, 1, 50);
		if (!terminal_ler(t)) {
			return;
		}
	}
}

static void bombear(terminal *t, int ms) {
	for (int i = 0; i < ms / 20; i++) {
		struct pollfd p = { .fd = terminal_fd(t), .events = POLLIN };
		poll(&p, 1, 20);
		terminal_ler(t);
		terminal_enviar(t);
	}
}

static char *linha_texto(terminal *t, int l) {
	return terminal_texto(t, l, 0, l, terminal_colunas(t) - 1);
}

static void teste_grade(void) {
	terminal *t = abrir(40, 6, "printf 'ola \\033[31mvermelho\\033[0m \\033[1mnegrito\\033[0m\\r\\n\\303\\251 \\346\\227\\245x\\r\\n'");
	ate_acabar(t);
	char *l0 = linha_texto(t, 0);
	CHECA(!strcmp(l0, "ola vermelho negrito"), "linha 0: '%s'", l0);
	free(l0);
	struct term_celula c;
	terminal_celula(t, 0, 0, &c);
	CHECA(!strcmp(c.texto, "o") && c.fg == FG && c.bg_padrao && c.bg == BG, "célula 0: '%s' fg %06x", c.texto, c.fg);
	terminal_celula(t, 0, 4, &c);
	CHECA(!strcmp(c.texto, "v") && c.fg == 0xcc0000, "o 'v' devia ser vermelho: %06x", c.fg);
	terminal_celula(t, 0, 13, &c);
	CHECA(!strcmp(c.texto, "n") && c.negrito, "o 'n' devia ser negrito");
	terminal_celula(t, 1, 0, &c);
	CHECA(!strcmp(c.texto, "\xc3\xa9"), "o é: '%s'", c.texto);
	terminal_celula(t, 1, 2, &c);
	CHECA(c.largura == 2 && !strcmp(c.texto, "\xe6\x97\xa5"), "o 日 devia ocupar 2 células: largura %d '%s'", c.largura, c.texto);
	terminal_celula(t, 1, 4, &c);
	CHECA(!strcmp(c.texto, "x"), "o x depois do caractere largo: '%s'", c.texto);
	char *dois = terminal_texto(t, 0, 0, 1, 5);
	CHECA(!strcmp(dois, "ola vermelho negrito\n\xc3\xa9 \xe6\x97\xa5x"), "texto de duas linhas: '%s'", dois);
	free(dois);
	terminal_fechar(t);
}

static void teste_saida_e_titulo(void) {
	terminal *t = abrir(30, 4, "printf '\\033]0;meu titulo\\007'; exit 3");
	ate_acabar(t);
	CHECA(!terminal_vivo(t), "o processo devia ter acabado");
	CHECA(terminal_status(t) == 3, "o código de saída devia ser 3: %d", terminal_status(t));
	CHECA(!strcmp(terminal_titulo(t), "meu titulo"), "o título: '%s'", terminal_titulo(t));
	terminal_fechar(t);
}

static void teste_historico(void) {
	terminal *t = abrir(20, 5, "seq 1 40");
	ate_acabar(t);
	CHECA(terminal_historico(t) >= 35, "o histórico devia ter as linhas que rolaram: %d", terminal_historico(t));
	char *u = linha_texto(t, 3);
	CHECA(!strcmp(u, "40"), "a última linha com texto devia ser 40: '%s'", u);
	free(u);
	char *h = linha_texto(t, -1);
	CHECA(!strcmp(h, "36"), "a linha -1 do histórico devia ser 36: '%s'", h);
	free(h);
	char *p = linha_texto(t, -terminal_historico(t));
	CHECA(!strcmp(p, "1"), "a primeira do histórico devia ser 1: '%s'", p);
	free(p);
	struct term_celula c;
	CHECA(!terminal_celula(t, -terminal_historico(t) - 1, 0, &c), "antes do histórico não há célula");
	terminal_fechar(t);
}

static void teste_tamanho(void) {
	terminal *t = abrir(30, 5, "sleep 0.2; stty size; trap 'stty size' WINCH; i=0; while [ $i -lt 40 ]; do sleep 0.05; i=$((i+1)); done");
	bombear(t, 300);
	terminal_redimensionar(t, 50, 9);
	CHECA(terminal_colunas(t) == 50 && terminal_linhas(t) == 9, "o tamanho: %dx%d", terminal_colunas(t), terminal_linhas(t));
	bombear(t, 600);
	char *a = linha_texto(t, 0);
	CHECA(!strcmp(a, "5 30"), "o stty size do início: '%s'", a);
	free(a);
	char *todo = terminal_texto(t, 0, 0, 8, 49);
	CHECA(strstr(todo, "9 50") != NULL, "depois de redimensionar o filho devia ver 9 50: '%s'", todo);
	free(todo);
	terminal_fechar(t);
}

/* as teclas: o que o processo recebe no pty cru */
static void recebe(const char *rotulo, void (*enviar)(terminal *), int bytes, const char *esperado) {
	char saida[] = "/tmp/lumna-terminal-teste-XXXXXX";
	int fd = mkstemp(saida);
	close(fd);
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "stty raw -echo; dd bs=1 count=%d 2>/dev/null | od -An -tx1 | tr -d ' \\n' > %s", bytes, saida);
	terminal *t = abrir(40, 5, cmd);
	bombear(t, 300); /* o stty precisa já ter acontecido */
	enviar(t);
	ate_acabar(t);
	FILE *f = fopen(saida, "r");
	char lido[256] = "";
	if (f) {
		if (!fgets(lido, sizeof(lido), f)) {
			lido[0] = '\0';
		}
		fclose(f);
	}
	CHECA(!strcmp(lido, esperado), "%s: recebeu '%s', esperava '%s'", rotulo, lido, esperado);
	unlink(saida);
	terminal_fechar(t);
}

static void env_teclas(terminal *t) {
	terminal_tecla(t, 'a', "a", 0);
	terminal_tecla(t, 'c', "", TERM_CTRL);
	terminal_tecla(t, XKB_KEY_Return, "", 0);
	terminal_tecla(t, XKB_KEY_Up, "", 0);
	terminal_tecla(t, XKB_KEY_ccedilla, "\xc3\xa7", 0);
	terminal_tecla(t, 'x', "", TERM_ALT); /* o Gravity não manda o texto com o Alt */
	terminal_tecla(t, XKB_KEY_Tab, "", 0);
	terminal_tecla(t, XKB_KEY_BackSpace, "", 0);
}

static void env_ctrl(terminal *t) {
	terminal_tecla(t, 'C', "", TERM_CTRL | TERM_SHIFT); /* Ctrl+Shift+C é do app (copiar); aqui só confere que Ctrl+maiúscula vira o mesmo controle */
	terminal_tecla(t, 'd', "", TERM_CTRL);
	terminal_tecla(t, XKB_KEY_Left, "", TERM_CTRL);
}

static void env_f(terminal *t) {
	terminal_tecla(t, XKB_KEY_F1, "", 0);
	terminal_tecla(t, XKB_KEY_Home, "", 0);
	terminal_tecla(t, XKB_KEY_Delete, "", 0);
	terminal_tecla(t, XKB_KEY_ISO_Left_Tab, "", TERM_SHIFT);
}

static void env_colar(terminal *t) {
	terminal_colar(t, "ab\ncd");
}

static void env_colar_longo(terminal *t) {
	char *g = malloc(200001);
	memset(g, 'z', 200000);
	g[200000] = '\0';
	terminal_colar(t, g);
	free(g);
	while (terminal_enviar(t)) {
		bombear(t, 20);
	}
}

static void teste_teclas(void) {
	recebe("teclas", env_teclas, 12, "6103" "0d" "1b5b41" "c3a7" "1b78" "09" "7f");
	recebe("ctrl", env_ctrl, 8, "0304" "1b5b313b3544");
	recebe("f/home/del", env_f, 13, "1b4f50" "1b5b48" "1b5b337e" "1b5b5a");
}

static void teste_colar(void) {
	recebe("colar", env_colar, 5, "6162" "0d" "6364");
}

static void teste_colar_grande(void) {
	/* mais do que cabe de uma vez no pty: sai pela fila */
	char saida[] = "/tmp/lumna-terminal-teste-XXXXXX";
	int fd = mkstemp(saida);
	close(fd);
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "stty raw -echo; dd bs=1000 count=200 iflag=fullblock 2>/dev/null | wc -c > %s", saida);
	terminal *t = abrir(40, 5, cmd);
	bombear(t, 300);
	env_colar_longo(t);
	ate_acabar(t);
	FILE *f = fopen(saida, "r");
	int n = 0;
	if (f) {
		if (fscanf(f, "%d", &n) != 1) {
			n = -1;
		}
		fclose(f);
	}
	CHECA(n == 200000, "um colar de 200000 bytes devia chegar inteiro: %d", n);
	unlink(saida);
	terminal_fechar(t);
}

static void teste_cursor_e_palavra(void) {
	terminal *t = abrir(40, 5, "printf 'um dois/tres.txt  quatro'");
	ate_acabar(t);
	int l, c;
	bool v;
	terminal_cursor(t, &l, &c, &v);
	CHECA(l == 0 && c == 24 && v, "o cursor devia estar em 0,24: %d,%d", l, c);
	int ini, fim;
	terminal_palavra(t, 0, 5, &ini, &fim);
	CHECA(ini == 3 && fim == 15, "a palavra 'dois/tres.txt' devia ir de 3 a 15: %d a %d", ini, fim);
	terminal_palavra(t, 0, 20, &ini, &fim);
	CHECA(ini == 18 && fim == 23, "'quatro': %d a %d", ini, fim);
	terminal_fechar(t);
}

static void teste_mouse(void) {
	/* o programa liga o relatório do mouse (1000 + 1006) e lê o clique que o terminal manda */
	char saida[] = "/tmp/lumna-terminal-teste-XXXXXX";
	int fd = mkstemp(saida);
	close(fd);
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "stty raw -echo; printf '\\033[?1000h\\033[?1006h'; dd bs=1 count=9 2>/dev/null > %s", saida);
	terminal *t = abrir(40, 5, cmd);
	CHECA(!terminal_mouse_ativo(t), "antes de o programa pedir, o mouse não é dele");
	bombear(t, 400);
	CHECA(terminal_mouse_ativo(t), "o programa pediu o mouse");
	terminal_mouse_botao(t, 2, 4, 1, true, 0);
	ate_acabar(t);
	FILE *f = fopen(saida, "r");
	char lido[64] = "";
	if (f) {
		size_t n = fread(lido, 1, sizeof(lido) - 1, f);
		lido[n] = '\0';
		fclose(f);
	}
	CHECA(!strcmp(lido, "\033[<0;5;3M"), "o clique na linha 2, coluna 4: '%s'", lido);
	unlink(saida);
	terminal_fechar(t);
}


/* ---- a integração com o shell (bash de verdade + pulse-shell/integracao.bash) ---- */

static void digita(terminal *t, const char *txt) {
	for (const char *p = txt; *p; p++) {
		char u[2] = { *p, 0 };
		terminal_tecla(t, (uint32_t)*p, u, 0);
		bombear(t, 20);
	}
}

static void enter(terminal *t) {
	terminal_tecla(t, XKB_KEY_Return, "", 0);
}

/* lê até chegar um evento do tipo (ou 5 s); false se não veio */
static bool espera_evento(terminal *t, enum term_evento_tipo tipo, struct term_evento *e) {
	for (int i = 0; i < 250; i++) {
		struct term_evento x;
		while (terminal_evento(t, &x)) {
			if (x.tipo == tipo) {
				*e = x;
				return true;
			}
			terminal_evento_limpar(&x);
		}
		bombear(t, 20);
	}
	return false;
}

static void teste_integracao(const char *integracao) {
	char *casa = g_dir_make_tmp("pulse-teste-XXXXXX", NULL);
	setenv("HOME", casa, 1);
	setenv("SHELL", "/bin/bash", 1);
	setenv("LUMNA_PULSE_INTEGRACAO", integracao, 1);
	setenv("HISTCONTROL", "ignorespace", 1);
	setenv("PS1", "$ ", 1);
	setenv("HISTFILE", "/dev/null", 1);
	terminal *t = terminal_abrir(80, 12, casa, NULL);
	assert(t);
	terminal_cores(t, FG, BG, ANSI);
	struct term_evento e;
	CHECA(espera_evento(t, TERM_EV_PROMPT, &e), "o primeiro prompt devia vir com a marca A");
	terminal_evento_limpar(&e);
	CHECA(terminal_integrado(t), "a integração devia estar ligada");

	/* um comando que dá certo */
	digita(t, "echo oi");
	char *dig = terminal_linha_digitada(t);
	CHECA(dig && !strcmp(dig, "echo oi"), "a linha digitada: '%s'", dig ? dig : "(nulo)");
	g_free(dig);
	enter(t);
	CHECA(espera_evento(t, TERM_EV_COMANDO_INICIO, &e), "o comando devia começar (C)");
	terminal_evento_limpar(&e);
	CHECA(espera_evento(t, TERM_EV_COMANDO_FIM, &e), "o comando devia acabar (D)");
	CHECA(e.exit_code == 0 && e.comando && !strcmp(e.comando, "echo oi"), "fim: código %d comando '%s'", e.exit_code, e.comando ? e.comando : "(nulo)");
	CHECA(e.saida && !strcmp(e.saida, "oi"), "a saída do comando: '%s'", e.saida ? e.saida : "(nulo)");
	CHECA(e.pasta && !strcmp(e.pasta, casa), "a pasta do shell: '%s'", e.pasta ? e.pasta : "(nulo)");
	terminal_evento_limpar(&e);

	/* um que falha: o código e a mensagem de erro vêm juntos */
	digita(t, "ls /nao-existe-pulse");
	enter(t);
	CHECA(espera_evento(t, TERM_EV_COMANDO_FIM, &e), "o ls que falha devia acabar");
	CHECA(e.exit_code == 2 && e.comando && !strcmp(e.comando, "ls /nao-existe-pulse"), "fim do ls: código %d comando '%s'", e.exit_code, e.comando ? e.comando : "(nulo)");
	CHECA(e.saida && strstr(e.saida, "nao-existe-pulse"), "a mensagem de erro entra na saída: '%s'", e.saida ? e.saida : "(nulo)");
	terminal_evento_limpar(&e);

	/* saída de várias linhas e só as últimas */
	terminal_saida_linhas(t, 3);
	digita(t, "seq 1 9");
	enter(t);
	CHECA(espera_evento(t, TERM_EV_COMANDO_FIM, &e), "o seq devia acabar");
	CHECA(e.saida && !strcmp(e.saida, "7\n8\n9"), "só as últimas 3 linhas: '%s'", e.saida ? e.saida : "(nulo)");
	terminal_evento_limpar(&e);
	terminal_saida_linhas(t, 40);

	/* começar com espaço: o shell não guarda no histórico e o Pulse não vê (nem registra, nem manda para a Emma) */
	digita(t, " echo segredo");
	enter(t);
	digita(t, "pwd");
	enter(t);
	CHECA(espera_evento(t, TERM_EV_COMANDO_FIM, &e), "o pwd devia acabar");
	CHECA(e.comando && !strcmp(e.comando, "pwd"), "o comando com espaço na frente não pode aparecer: veio '%s'", e.comando ? e.comando : "(nulo)");
	terminal_evento_limpar(&e);

	/* cd muda a pasta do evento seguinte */
	digita(t, "cd /tmp");
	enter(t);
	CHECA(espera_evento(t, TERM_EV_COMANDO_FIM, &e), "o cd devia acabar");
	CHECA(e.pasta && !strcmp(e.pasta, "/tmp"), "depois do cd a pasta é /tmp: '%s'", e.pasta ? e.pasta : "(nulo)");
	terminal_evento_limpar(&e);

	/* um comando com segredo e aspas: o texto chega inteiro (o base64 não perde nada) */
	digita(t, "echo \"a b\" 'c;d' $HOME");
	enter(t);
	CHECA(espera_evento(t, TERM_EV_COMANDO_FIM, &e), "o echo com aspas devia acabar");
	CHECA(e.comando && !strcmp(e.comando, "echo \"a b\" 'c;d' $HOME"), "o comando com aspas e $: '%s'", e.comando ? e.comando : "(nulo)");
	terminal_evento_limpar(&e);
	terminal_fechar(t);
	g_free(casa);
}

int main(int argc, char **argv) {
	teste_grade();
	teste_saida_e_titulo();
	teste_historico();
	teste_tamanho();
	teste_teclas();
	teste_colar();
	teste_colar_grande();
	teste_cursor_e_palavra();
	teste_mouse();
	teste_integracao(argc > 1 ? argv[1] : "pulse-shell/integracao.bash");
	if (falhas) {
		fprintf(stderr, "%d falha(s) no terminal\n", falhas);
		return 1;
	}
	printf("terminal: ok\n");
	return 0;
}
