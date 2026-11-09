/*
 * Relatório de queda: se um app do Gravity morrer por SIGSEGV, SIGBUS, SIGABRT, SIGFPE ou SIGILL, escreve em ~/.cache/lumna/crash/<app>-<pid>.log o sinal, o
 * endereço que falhou, a pilha e o mapa de memória do executável. Quem usa só precisa mandar esse arquivo: com o binário (que o pacote não tira os símbolos),
 * `addr2line -f -C -e /usr/bin/lumna-<app> <endereço - base>` diz a função e a linha.
 * Só chamadas seguras dentro de um handler de sinal (open/write); o backtrace() é aquecido na partida para não carregar a libgcc na hora da queda. O handler
 * roda numa pilha própria (sigaltstack): um estouro de pilha também deixa relatório.
 */
#define _GNU_SOURCE
#include "gv_interno.h"

#include <execinfo.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char caminho_rel[PATH_MAX + 80];
static char nome_app[64];

static void escreve(int fd, const char *s) {
	size_t n = strlen(s);
	while (n > 0) {
		ssize_t w = write(fd, s, n);
		if (w <= 0) {
			return;
		}
		s += w;
		n -= (size_t)w;
	}
}

static void hex(int fd, unsigned long v) {
	char b[2 + 2 * sizeof(v) + 1];
	int i = (int)sizeof(b) - 1;
	b[i] = '\0';
	do {
		b[--i] = "0123456789abcdef"[v & 15];
		v >>= 4;
	} while (v);
	b[--i] = 'x';
	b[--i] = '0';
	escreve(fd, b + i);
}

static void ao_cair(int sig, siginfo_t *info, void *ctx) {
	(void)ctx;
	int fd = open(caminho_rel, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) {
		fd = 2;
	}
	escreve(fd, "lumna-");
	escreve(fd, nome_app);
	escreve(fd, " caiu: sinal ");
	hex(fd, (unsigned long)sig);
	escreve(fd, sig == SIGSEGV ? " (SIGSEGV)" : sig == SIGBUS ? " (SIGBUS)" : sig == SIGABRT ? " (SIGABRT)" : sig == SIGFPE ? " (SIGFPE)" : " (SIGILL)");
	escreve(fd, ", endereço ");
	hex(fd, (unsigned long)info->si_addr);
	escreve(fd, "\npilha (endereços):\n");
	void *pilha[64];
	int n = backtrace(pilha, 64);
	for (int i = 0; i < n; i++) {
		hex(fd, (unsigned long)pilha[i]);
		escreve(fd, "\n");
	}
	escreve(fd, "\npilha (arquivo+deslocamento: addr2line -f -C -e /usr/bin/lumna-<app> <deslocamento>):\n");
	backtrace_symbols_fd(pilha, n, fd);
	escreve(fd, "\nmapa do executável (a base está na primeira linha):\n");
	int m = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
	if (m >= 0) {
		char buf[4096];
		ssize_t lido;
		size_t total = 0;
		while ((lido = read(m, buf, sizeof(buf))) > 0 && total < 16384) {
			if (write(fd, buf, (size_t)lido) < 0) {
				break;
			}
			total += (size_t)lido;
		}
		close(m);
	}
	if (fd != 2) {
		close(fd);
	}
	signal(sig, SIG_DFL); /* o resto (o core dump, o aviso do systemd) segue como sempre */
	raise(sig);
}

void gv__crash_iniciar(const char *app) {
#ifdef __SANITIZE_ADDRESS__
	(void)app; /* o ASAN tem o relatório dele: este handler tomaria o lugar */
	return;
#endif
	static bool feito;
	if (feito) {
		return;
	}
	feito = true;
	const char *nome = app && *app ? app : "app";
	const char *barra = strrchr(nome, '.'); /* "org.lumna.atlas" -> "atlas" */
	nome = barra ? barra + 1 : nome;
	snprintf(nome_app, sizeof(nome_app), "%s", g_str_has_prefix(nome, "lumna-") ? nome + 6 : nome); /* "lumna-atlas" -> "atlas" */
	const char *cache = g_get_user_cache_dir();
	char pasta[PATH_MAX];
	snprintf(pasta, sizeof(pasta), "%s/lumna/crash", cache);
	g_mkdir_with_parents(pasta, 0700);
	snprintf(caminho_rel, sizeof(caminho_rel), "%s/%s.log", pasta, nome_app); /* o último relatório de cada app (não enche o disco) */
	void *aquece[2];
	backtrace(aquece, 2);
	static char pilha_alt[64 * 1024];
	stack_t ss = { .ss_sp = pilha_alt, .ss_size = sizeof(pilha_alt) };
	sigaltstack(&ss, NULL);
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = ao_cair;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	int sinais[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL };
	for (size_t i = 0; i < sizeof(sinais) / sizeof(sinais[0]); i++) {
		sigaction(sinais[i], &sa, NULL);
	}
}
