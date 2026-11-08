/* O terminal do LUMNA: libvterm + pty. Veja terminal.h. */
#define _GNU_SOURCE
#include "terminal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vterm.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define HISTORICO_MAX 5000
#define LER_MAX (256 * 1024) /* o mais que se lê de uma vez: um programa que despeja sem parar não trava o app */

struct linha_hist {
	int colunas;
	VTermScreenCell celulas[];
};

struct terminal {
	VTerm *vt;
	VTermScreen *tela;
	VTermState *estado;
	int fd;
	pid_t pid;
	bool vivo;
	int status;
	int colunas, linhas;
	uint64_t versao;
	/* o histórico: um anel das linhas que saíram por cima */
	struct linha_hist **hist;
	int nhist;
	/* a saída para o processo que ainda não coube no pty */
	char *saida;
	size_t saida_n, saida_cap;
	/* o que o programa pediu */
	char titulo[256];
	bool titulo_aberto;
	bool cursor_visivel;
	bool alt;
	int mouse; /* VTermMouseProperty */
	bool campainha;
	int cursor_l, cursor_c;
	uint32_t fg, bg;
	char pasta[4096];
};

/* ---- libvterm: os retornos ---- */

static int cb_damage(VTermRect rect, void *u) {
	((struct terminal *)u)->versao++;
	return 1;
}

static int cb_moverect(VTermRect dest, VTermRect src, void *u) {
	((struct terminal *)u)->versao++;
	return 1;
}

static int cb_movecursor(VTermPos pos, VTermPos antiga, int visivel, void *u) {
	struct terminal *t = u;
	t->cursor_l = pos.row;
	t->cursor_c = pos.col;
	t->versao++;
	return 1;
}

static int cb_prop(VTermProp prop, VTermValue *val, void *u) {
	struct terminal *t = u;
	switch (prop) {
	case VTERM_PROP_CURSORVISIBLE:
		t->cursor_visivel = val->boolean;
		break;
	case VTERM_PROP_ALTSCREEN:
		t->alt = val->boolean;
		break;
	case VTERM_PROP_MOUSE:
		t->mouse = val->number;
		break;
	case VTERM_PROP_TITLE: {
		/* o título chega em pedaços */
		if (val->string.initial) {
			t->titulo[0] = '\0';
		}
		size_t usado = strlen(t->titulo);
		size_t cabe = sizeof(t->titulo) - 1 - usado;
		size_t n = val->string.len < cabe ? val->string.len : cabe;
		memcpy(t->titulo + usado, val->string.str, n);
		t->titulo[usado + n] = '\0';
		break;
	}
	default:
		break;
	}
	t->versao++;
	return 1;
}

static int cb_bell(void *u) {
	((struct terminal *)u)->campainha = true;
	return 1;
}

static int cb_resize(int linhas, int colunas, void *u) {
	return 1;
}

static int cb_pushline(int colunas, const VTermScreenCell *celulas, void *u) {
	struct terminal *t = u;
	struct linha_hist *l = malloc(sizeof(*l) + sizeof(VTermScreenCell) * (size_t)colunas);
	if (!l) {
		return 0;
	}
	l->colunas = colunas;
	memcpy(l->celulas, celulas, sizeof(VTermScreenCell) * (size_t)colunas);
	if (t->nhist == HISTORICO_MAX) {
		free(t->hist[0]);
		memmove(t->hist, t->hist + 1, sizeof(*t->hist) * (HISTORICO_MAX - 1));
		t->nhist--;
	}
	t->hist[t->nhist++] = l;
	t->versao++;
	return 1;
}

static int cb_popline(int colunas, VTermScreenCell *celulas, void *u) {
	struct terminal *t = u;
	if (t->nhist == 0) {
		return 0;
	}
	struct linha_hist *l = t->hist[--t->nhist];
	for (int c = 0; c < colunas; c++) {
		if (c < l->colunas) {
			celulas[c] = l->celulas[c];
		} else {
			memset(&celulas[c], 0, sizeof(celulas[c]));
			celulas[c].width = 1;
			vterm_state_get_default_colors(vterm_obtain_state(t->vt), &celulas[c].fg, &celulas[c].bg);
		}
	}
	free(l);
	t->versao++;
	return 1;
}

static const VTermScreenCallbacks callbacks = {
	.damage = cb_damage,
	.moverect = cb_moverect,
	.movecursor = cb_movecursor,
	.settermprop = cb_prop,
	.bell = cb_bell,
	.resize = cb_resize,
	.sb_pushline = cb_pushline,
	.sb_popline = cb_popline,
};

/* ---- a saída para o processo ---- */

static void enfileirar(struct terminal *t, const char *dados, size_t n) {
	if (t->saida_n + n > t->saida_cap) {
		size_t novo = (t->saida_n + n) * 2 + 4096;
		char *p = realloc(t->saida, novo);
		if (!p) {
			return;
		}
		t->saida = p;
		t->saida_cap = novo;
	}
	memcpy(t->saida + t->saida_n, dados, n);
	t->saida_n += n;
}

bool terminal_enviar(terminal *t) {
	while (t->saida_n > 0 && t->fd >= 0) {
		ssize_t n = write(t->fd, t->saida, t->saida_n);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			break; /* EAGAIN: o processo ainda não leu; tenta de novo no próximo quadro */
		}
		memmove(t->saida, t->saida + n, t->saida_n - (size_t)n);
		t->saida_n -= (size_t)n;
	}
	return t->saida_n > 0;
}

static void cb_saida(const char *dados, size_t n, void *u) {
	struct terminal *t = u;
	enfileirar(t, dados, n);
	terminal_enviar(t);
}

/* ---- abrir e fechar ---- */

static VTermColor cor_rgb(uint32_t c) {
	VTermColor v;
	vterm_color_rgb(&v, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
	return v;
}

terminal *terminal_abrir(int colunas, int linhas, const char *cwd, const char *comando) {
	if (colunas < 2) {
		colunas = 2;
	}
	if (linhas < 1) {
		linhas = 1;
	}
	struct terminal *t = calloc(1, sizeof(*t));
	if (!t) {
		return NULL;
	}
	t->hist = calloc(HISTORICO_MAX, sizeof(*t->hist));
	t->colunas = colunas;
	t->linhas = linhas;
	t->cursor_visivel = true;
	t->fd = -1;
	t->vt = vterm_new(linhas, colunas);
	if (!t->hist || !t->vt) {
		terminal_fechar(t);
		return NULL;
	}
	vterm_set_utf8(t->vt, 1);
	vterm_output_set_callback(t->vt, cb_saida, t);
	t->tela = vterm_obtain_screen(t->vt);
	t->estado = vterm_obtain_state(t->vt);
	vterm_screen_enable_altscreen(t->tela, 1);
	vterm_screen_set_callbacks(t->tela, &callbacks, t);
	vterm_screen_set_damage_merge(t->tela, VTERM_DAMAGE_SCROLL);
	vterm_screen_reset(t->tela, 1);

	struct winsize ws = { .ws_row = (unsigned short)linhas, .ws_col = (unsigned short)colunas };
	pid_t pid = forkpty(&t->fd, NULL, NULL, &ws);
	if (pid < 0) {
		terminal_fechar(t);
		return NULL;
	}
	if (pid == 0) {
		/* o filho: o shell, numa pasta e com o TERM de um terminal moderno */
		const char *pasta = cwd && *cwd ? cwd : getenv("HOME");
		if (pasta && chdir(pasta) != 0) {
			(void)!chdir("/");
		}
		setenv("TERM", "xterm-256color", 1);
		setenv("COLORTERM", "truecolor", 1);
		setenv("TERM_PROGRAM", "lumna-console", 1);
		unsetenv("COLUMNS");
		unsetenv("LINES");
		/* os sinais voltam ao normal: quem abriu o terminal pode tê-los ignorado (um app aberto em segundo plano por um sh ignora o SIGINT, e o Ctrl+C
		 * não mataria nada dentro do terminal) */
		static const int sinais[] = { SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD, SIGALRM };
		for (size_t i = 0; i < sizeof(sinais) / sizeof(sinais[0]); i++) {
			signal(sinais[i], SIG_DFL);
		}
		sigset_t vazio;
		sigemptyset(&vazio);
		sigprocmask(SIG_SETMASK, &vazio, NULL);
		if (comando && *comando) {
			execl("/bin/sh", "sh", "-c", comando, (char *)NULL);
		} else {
			const char *shell = getenv("SHELL");
			if (!shell || !*shell) {
				shell = "/bin/bash";
			}
			const char *nome = strrchr(shell, '/');
			char arg0[128];
			snprintf(arg0, sizeof(arg0), "-%s", nome ? nome + 1 : shell); /* o traço: um shell de login, como o foot/gnome-terminal */
			execl(shell, arg0, (char *)NULL);
			execl("/bin/sh", "-sh", (char *)NULL);
		}
		_exit(127);
	}
	t->pid = pid;
	t->vivo = true;
	int flags = fcntl(t->fd, F_GETFL);
	fcntl(t->fd, F_SETFL, flags | O_NONBLOCK);
	fcntl(t->fd, F_SETFD, FD_CLOEXEC);
	return t;
}

void terminal_fechar(terminal *t) {
	if (!t) {
		return;
	}
	if (t->fd >= 0) {
		close(t->fd);
	}
	if (t->vivo && t->pid > 0) {
		kill(t->pid, SIGHUP);
		int st;
		if (waitpid(t->pid, &st, WNOHANG) == 0) {
			/* não saiu na hora: deixa o init recolher (um filho que ignora o SIGHUP não pode travar a janela) */
			signal(SIGCHLD, SIG_IGN);
		}
	}
	for (int i = 0; t->hist && i < t->nhist; i++) {
		free(t->hist[i]);
	}
	free(t->hist);
	free(t->saida);
	if (t->vt) {
		vterm_free(t->vt);
	}
	free(t);
}

int terminal_fd(const terminal *t) {
	return t->fd;
}

static void recolher(struct terminal *t) {
	if (!t->vivo) {
		return;
	}
	int st = 0;
	pid_t r = waitpid(t->pid, &st, WNOHANG);
	if (r == t->pid) {
		t->vivo = false;
		t->status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
	} else if (r < 0 && errno == ECHILD) {
		t->vivo = false;
	}
}

bool terminal_ler(terminal *t) {
	char buf[16384];
	size_t total = 0;
	bool fim = false;
	while (total < LER_MAX) {
		ssize_t n = read(t->fd, buf, sizeof(buf));
		if (n > 0) {
			vterm_input_write(t->vt, buf, (size_t)n);
			total += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n < 0 && errno == EAGAIN) {
			break;
		}
		fim = true; /* 0 ou EIO: o outro lado do pty fechou */
		break;
	}
	vterm_screen_flush_damage(t->tela);
	if (total > 0) {
		t->versao++;
	}
	if (fim) {
		/* o processo pode demorar um instante para ser recolhido depois de fechar o pty */
		for (int i = 0; i < 20 && t->vivo; i++) {
			recolher(t);
			if (t->vivo) {
				usleep(5000);
			}
		}
		if (t->vivo) {
			kill(t->pid, SIGHUP);
			recolher(t);
		}
		return false;
	}
	return true;
}

bool terminal_vivo(const terminal *t) {
	return t->vivo;
}

int terminal_status(const terminal *t) {
	return t->status;
}

pid_t terminal_pid(const terminal *t) {
	return t->pid;
}

const char *terminal_pasta(terminal *t) {
	char caminho[64];
	snprintf(caminho, sizeof(caminho), "/proc/%d/cwd", (int)t->pid);
	ssize_t n = readlink(caminho, t->pasta, sizeof(t->pasta) - 1);
	t->pasta[n > 0 ? n : 0] = '\0';
	return t->pasta;
}

void terminal_redimensionar(terminal *t, int colunas, int linhas) {
	if (colunas < 2) {
		colunas = 2;
	}
	if (linhas < 1) {
		linhas = 1;
	}
	if (colunas == t->colunas && linhas == t->linhas) {
		return;
	}
	t->colunas = colunas;
	t->linhas = linhas;
	vterm_set_size(t->vt, linhas, colunas);
	vterm_screen_flush_damage(t->tela);
	struct winsize ws = { .ws_row = (unsigned short)linhas, .ws_col = (unsigned short)colunas };
	ioctl(t->fd, TIOCSWINSZ, &ws);
	t->versao++;
}

int terminal_colunas(const terminal *t) {
	return t->colunas;
}

int terminal_linhas(const terminal *t) {
	return t->linhas;
}

int terminal_historico(const terminal *t) {
	return t->alt ? 0 : t->nhist;
}

uint64_t terminal_versao(const terminal *t) {
	return t->versao;
}

/* ---- a grade ---- */

void terminal_cores(terminal *t, uint32_t fg, uint32_t bg, const uint32_t ansi[16]) {
	t->fg = fg;
	t->bg = bg;
	VTermColor f = cor_rgb(fg), b = cor_rgb(bg);
	vterm_state_set_default_colors(t->estado, &f, &b);
	for (int i = 0; i < 16; i++) {
		VTermColor c = cor_rgb(ansi[i]);
		vterm_state_set_palette_color(t->estado, i, &c);
	}
	t->versao++;
}

static uint32_t rgb_de(terminal *t, VTermColor c, bool fundo) {
	if (fundo ? VTERM_COLOR_IS_DEFAULT_BG(&c) : VTERM_COLOR_IS_DEFAULT_FG(&c)) {
		return fundo ? t->bg : t->fg; /* as do tema (a libvterm guarda as dela nas células já escritas) */
	}
	vterm_screen_convert_color_to_rgb(t->tela, &c);
	return ((uint32_t)c.rgb.red << 16) | ((uint32_t)c.rgb.green << 8) | c.rgb.blue;
}

static void vazia(terminal *t, struct term_celula *s) {
	memset(s, 0, sizeof(*s));
	s->largura = 1;
	s->fg = t->fg;
	s->bg = t->bg;
	s->bg_padrao = true;
}

bool terminal_celula(terminal *t, int linha, int coluna, struct term_celula *saida) {
	vazia(t, saida);
	if (coluna < 0 || coluna >= t->colunas) {
		return false;
	}
	VTermScreenCell c;
	if (linha >= 0) {
		if (linha >= t->linhas) {
			return false;
		}
		vterm_screen_get_cell(t->tela, (VTermPos){ .row = linha, .col = coluna }, &c);
	} else {
		int i = t->nhist + linha;
		if (t->alt || i < 0 || i >= t->nhist) {
			return false;
		}
		struct linha_hist *l = t->hist[i];
		if (coluna >= l->colunas) {
			return true; /* a linha era mais estreita: o resto é vazio */
		}
		c = l->celulas[coluna];
	}
	/* o texto: os pontos de código da célula (a base e as marcas combinantes) */
	size_t u = 0;
	for (int k = 0; k < VTERM_MAX_CHARS_PER_CELL && c.chars[k] && c.chars[k] != (uint32_t)-1; k++) {
		uint32_t cp = c.chars[k];
		char tmp[8];
		int n = 0;
		if (cp < 0x80) {
			tmp[n++] = (char)cp;
		} else if (cp < 0x800) {
			tmp[n++] = (char)(0xc0 | (cp >> 6));
			tmp[n++] = (char)(0x80 | (cp & 0x3f));
		} else if (cp < 0x10000) {
			tmp[n++] = (char)(0xe0 | (cp >> 12));
			tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
			tmp[n++] = (char)(0x80 | (cp & 0x3f));
		} else {
			tmp[n++] = (char)(0xf0 | (cp >> 18));
			tmp[n++] = (char)(0x80 | ((cp >> 12) & 0x3f));
			tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
			tmp[n++] = (char)(0x80 | (cp & 0x3f));
		}
		if (u + (size_t)n < sizeof(saida->texto)) {
			memcpy(saida->texto + u, tmp, (size_t)n);
			u += (size_t)n;
		}
	}
	saida->texto[u] = '\0';
	saida->largura = c.width > 0 ? c.width : 1;
	uint32_t fg = rgb_de(t, c.fg, false), bg = rgb_de(t, c.bg, true);
	bool bg_padrao = VTERM_COLOR_IS_DEFAULT_BG(&c.bg);
	if (c.attrs.reverse) {
		uint32_t x = fg;
		fg = bg;
		bg = x;
		bg_padrao = false;
		if (VTERM_COLOR_IS_DEFAULT_FG(&c.fg) && VTERM_COLOR_IS_DEFAULT_BG(&c.bg)) {
			fg = t->bg;
			bg = t->fg;
		}
	}
	saida->fg = fg;
	saida->bg = bg;
	saida->bg_padrao = bg_padrao;
	saida->negrito = c.attrs.bold;
	saida->italico = c.attrs.italic;
	saida->sublinhado = c.attrs.underline != 0;
	saida->riscado = c.attrs.strike;
	return true;
}

void terminal_cursor(const terminal *t, int *linha, int *coluna, bool *visivel) {
	if (linha) {
		*linha = t->cursor_l;
	}
	if (coluna) {
		*coluna = t->cursor_c;
	}
	if (visivel) {
		*visivel = t->cursor_visivel;
	}
}

const char *terminal_titulo(const terminal *t) {
	return t->titulo;
}

bool terminal_tela_alternativa(const terminal *t) {
	return t->alt;
}

bool terminal_campainha(terminal *t) {
	bool b = t->campainha;
	t->campainha = false;
	return b;
}

void terminal_foco(terminal *t, bool com_foco) {
	if (com_foco) {
		vterm_state_focus_in(t->estado);
	} else {
		vterm_state_focus_out(t->estado);
	}
}

/* ---- o teclado ---- */

static VTermModifier mods_vterm(uint32_t m) {
	return (m & TERM_SHIFT ? VTERM_MOD_SHIFT : 0) | (m & TERM_CTRL ? VTERM_MOD_CTRL : 0) | (m & TERM_ALT ? VTERM_MOD_ALT : 0);
}

static uint32_t proximo_codigo(const char **p) {
	const unsigned char *s = (const unsigned char *)*p;
	uint32_t cp;
	int n;
	if (s[0] < 0x80) {
		cp = s[0];
		n = 1;
	} else if ((s[0] & 0xe0) == 0xc0) {
		cp = s[0] & 0x1f;
		n = 2;
	} else if ((s[0] & 0xf0) == 0xe0) {
		cp = s[0] & 0x0f;
		n = 3;
	} else if ((s[0] & 0xf8) == 0xf0) {
		cp = s[0] & 0x07;
		n = 4;
	} else {
		*p += 1;
		return 0xfffd;
	}
	for (int i = 1; i < n; i++) {
		if ((s[i] & 0xc0) != 0x80) {
			*p += i;
			return 0xfffd;
		}
		cp = (cp << 6) | (s[i] & 0x3f);
	}
	*p += n;
	return cp;
}

void terminal_tecla(terminal *t, uint32_t sym, const char *utf8, uint32_t mods) {
	VTermModifier vm = mods_vterm(mods);
	VTermKey tecla = VTERM_KEY_NONE;
	switch (sym) {
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		tecla = VTERM_KEY_ENTER;
		break;
	case XKB_KEY_Tab:
		tecla = VTERM_KEY_TAB;
		break;
	case XKB_KEY_ISO_Left_Tab:
		tecla = VTERM_KEY_TAB;
		vm |= VTERM_MOD_SHIFT;
		break;
	case XKB_KEY_BackSpace:
		tecla = VTERM_KEY_BACKSPACE;
		break;
	case XKB_KEY_Escape:
		tecla = VTERM_KEY_ESCAPE;
		break;
	case XKB_KEY_Up:
		tecla = VTERM_KEY_UP;
		break;
	case XKB_KEY_Down:
		tecla = VTERM_KEY_DOWN;
		break;
	case XKB_KEY_Left:
		tecla = VTERM_KEY_LEFT;
		break;
	case XKB_KEY_Right:
		tecla = VTERM_KEY_RIGHT;
		break;
	case XKB_KEY_Insert:
		tecla = VTERM_KEY_INS;
		break;
	case XKB_KEY_Delete:
		tecla = VTERM_KEY_DEL;
		break;
	case XKB_KEY_Home:
		tecla = VTERM_KEY_HOME;
		break;
	case XKB_KEY_End:
		tecla = VTERM_KEY_END;
		break;
	case XKB_KEY_Page_Up:
		tecla = VTERM_KEY_PAGEUP;
		break;
	case XKB_KEY_Page_Down:
		tecla = VTERM_KEY_PAGEDOWN;
		break;
	default:
		if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
			tecla = VTERM_KEY_FUNCTION(1 + (int)(sym - XKB_KEY_F1));
		}
		break;
	}
	if (tecla != VTERM_KEY_NONE) {
		vterm_keyboard_key(t->vt, tecla, vm);
		return;
	}
	if (utf8 && *utf8 && !(mods & TERM_CTRL)) {
		/* o que a tecla escreve (já com os acentos): o Alt vira o Esc antes, como nos outros terminais */
		const char *p = utf8;
		while (*p) {
			uint32_t cp = proximo_codigo(&p);
			vterm_keyboard_unichar(t->vt, cp, vm & ~VTERM_MOD_SHIFT);
		}
		return;
	}
	if (mods & (TERM_CTRL | TERM_ALT)) {
		/* Ctrl+letra, Alt+letra e companhia: o keysym (o Gravity não manda o texto quando há Ctrl ou Alt); o libvterm faz o código de controle
		 * (Ctrl) ou o Esc na frente (Alt) */
		uint32_t c = xkb_keysym_to_utf32(sym);
		if ((mods & TERM_CTRL) && c >= 'A' && c <= 'Z') {
			c += 32;
		}
		if (c >= 0x20 && c != 0x7f) {
			vterm_keyboard_unichar(t->vt, c, vm);
		}
	}
}

void terminal_colar(terminal *t, const char *texto) {
	if (!texto || !*texto) {
		return;
	}
	vterm_keyboard_start_paste(t->vt);
	/* o texto vai como veio, sem passar pelas teclas (um \n colado não é um Enter apertado) */
	/* nas linhas, o terminal manda \r como o Enter faz */
	size_t n = strlen(texto);
	char *copia = malloc(n + 1);
	if (copia) {
		size_t j = 0;
		for (size_t i = 0; i < n; i++) {
			if (texto[i] == '\n') {
				copia[j++] = '\r';
			} else if (texto[i] == '\r' && texto[i + 1] == '\n') {
				continue;
			} else {
				copia[j++] = texto[i];
			}
		}
		enfileirar(t, copia, j);
		free(copia);
		terminal_enviar(t);
	}
	vterm_keyboard_end_paste(t->vt);
}

/* ---- o mouse ---- */

bool terminal_mouse_ativo(const terminal *t) {
	return t->mouse != VTERM_PROP_MOUSE_NONE;
}

void terminal_mouse_mover(terminal *t, int linha, int coluna, uint32_t mods) {
	vterm_mouse_move(t->vt, linha, coluna, mods_vterm(mods));
}

void terminal_mouse_botao(terminal *t, int linha, int coluna, int botao, bool apertou, uint32_t mods) {
	vterm_mouse_move(t->vt, linha, coluna, mods_vterm(mods));
	vterm_mouse_button(t->vt, botao, apertou, mods_vterm(mods));
}

/* ---- copiar ---- */

char *terminal_texto(terminal *t, int l1, int c1, int l2, int c2) {
	if (l1 > l2 || (l1 == l2 && c1 > c2)) {
		int x = l1;
		l1 = l2;
		l2 = x;
		x = c1;
		c1 = c2;
		c2 = x;
	}
	size_t cap = 256, n = 0;
	char *s = malloc(cap);
	if (!s) {
		return NULL;
	}
	s[0] = '\0';
	for (int l = l1; l <= l2; l++) {
		int ini = l == l1 ? c1 : 0;
		int fim = l == l2 ? c2 : t->colunas - 1;
		size_t inicio_linha = n;
		size_t fim_util = n;
		for (int c = ini; c <= fim && c < t->colunas; c++) {
			struct term_celula cel;
			terminal_celula(t, l, c, &cel);
			const char *txt = cel.texto[0] ? cel.texto : (cel.largura == 2 ? "" : " ");
			if (!cel.texto[0] && cel.largura == 1 && c > 0) {
				/* a segunda metade de um caractere largo não escreve nada; uma célula vazia é um espaço */
				struct term_celula ant;
				terminal_celula(t, l, c - 1, &ant);
				if (ant.largura == 2 && ant.texto[0]) {
					txt = "";
				}
			}
			size_t k = strlen(txt);
			if (n + k + 2 > cap) {
				cap = (n + k + 2) * 2;
				s = realloc(s, cap);
				if (!s) {
					return NULL;
				}
			}
			memcpy(s + n, txt, k);
			n += k;
			if (cel.texto[0]) {
				fim_util = n;
			}
		}
		n = fim_util > inicio_linha ? fim_util : inicio_linha; /* sem os espaços do fim da linha */
		if (l < l2) {
			if (n + 2 > cap) {
				cap = (n + 2) * 2;
				s = realloc(s, cap);
				if (!s) {
					return NULL;
				}
			}
			s[n++] = '\n';
		}
		s[n] = '\0';
	}
	return s;
}

void terminal_palavra(terminal *t, int linha, int coluna, int *ini, int *fim) {
	struct term_celula c;
	terminal_celula(t, linha, coluna, &c);
	bool espaco = !c.texto[0] || c.texto[0] == ' ';
	bool tipo_alnum = !espaco && ((unsigned char)c.texto[0] >= 0x80 || (c.texto[0] >= '0' && c.texto[0] <= '9') || (c.texto[0] >= 'a' && c.texto[0] <= 'z') ||
		(c.texto[0] >= 'A' && c.texto[0] <= 'Z') || c.texto[0] == '_' || c.texto[0] == '-' || c.texto[0] == '.' || c.texto[0] == '/' || c.texto[0] == '~');
	int a = coluna, b = coluna;
	for (int i = coluna - 1; i >= 0; i--) {
		struct term_celula x;
		terminal_celula(t, linha, i, &x);
		bool e = !x.texto[0] || x.texto[0] == ' ';
		bool al = !e && ((unsigned char)x.texto[0] >= 0x80 || (x.texto[0] >= '0' && x.texto[0] <= '9') || (x.texto[0] >= 'a' && x.texto[0] <= 'z') ||
			(x.texto[0] >= 'A' && x.texto[0] <= 'Z') || x.texto[0] == '_' || x.texto[0] == '-' || x.texto[0] == '.' || x.texto[0] == '/' || x.texto[0] == '~');
		if (e != espaco || (!espaco && al != tipo_alnum)) {
			break;
		}
		a = i;
	}
	for (int i = coluna + 1; i < t->colunas; i++) {
		struct term_celula x;
		terminal_celula(t, linha, i, &x);
		bool e = !x.texto[0] || x.texto[0] == ' ';
		bool al = !e && ((unsigned char)x.texto[0] >= 0x80 || (x.texto[0] >= '0' && x.texto[0] <= '9') || (x.texto[0] >= 'a' && x.texto[0] <= 'z') ||
			(x.texto[0] >= 'A' && x.texto[0] <= 'Z') || x.texto[0] == '_' || x.texto[0] == '-' || x.texto[0] == '.' || x.texto[0] == '/' || x.texto[0] == '~');
		if (e != espaco || (!espaco && al != tipo_alnum)) {
			break;
		}
		b = i;
	}
	*ini = a;
	*fim = b;
}
