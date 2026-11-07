#define _DEFAULT_SOURCE 1 /* explicit_bzero */
#include "polkitag.h"

#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define SOCKET_PADRAO "/run/polkit/agent-helper.socket"
#define HELPER_PADRAO "/usr/lib/polkit-1/polkit-agent-helper-1"

int polkitag_escolher(const uint32_t *uids, int n, uint32_t proprio, uint32_t *uid) {
	for (int i = 0; i < n; i++) {
		if (uids[i] == proprio) {
			*uid = proprio;
			return 1;
		}
	}
	if (n > 0) {
		*uid = uids[0];
		return 1;
	}
	return 0;
}

char *polkitag_desescapar(const char *linha) {
	GString *s = g_string_sized_new(strlen(linha));
	for (const char *p = linha; *p; p++) {
		if (*p != '\\') {
			g_string_append_c(s, *p);
			continue;
		}
		p++;
		if (!*p) {
			break;
		}
		g_string_append_c(s, *p == 'n' ? '\n' : *p == 't' ? '\t' : *p);
	}
	return g_string_free(s, FALSE);
}

char *polkitag_nome(uint32_t uid) {
	const char *etc = g_getenv("LUMNA_ETC");
	char *caminho = g_build_filename(etc && *etc ? etc : "/etc", "passwd", NULL), *texto = NULL, *nome = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l && !nome; l++) {
			char **c = g_strsplit(*l, ":", 4);
			if (c[0] && c[1] && c[2] && (uint32_t)strtoul(c[2], NULL, 10) == uid && *c[0]) {
				nome = g_strdup(c[0]);
			}
			g_strfreev(c);
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(caminho);
	return nome;
}

static int escrever_tudo(int fd, const char *texto) {
	size_t n = strlen(texto);
	while (n) {
		ssize_t r = write(fd, texto, n);
		if (r <= 0) {
			return -1;
		}
		texto += r;
		n -= (size_t)r;
	}
	return 0;
}

int polkitag_conferir(const char *usuario, const char *cookie, const char *senha, char *erro, size_t n) {
	const char *sock_env = g_getenv("LUMNA_POLKIT_SOCKET"), *help_env = g_getenv("LUMNA_POLKIT_HELPER");
	const char *sock = sock_env && *sock_env ? sock_env : SOCKET_PADRAO;
	int entrada = -1, saida = -1;
	GPid filho = 0;
	if (!help_env && g_file_test(sock, G_FILE_TEST_EXISTS)) {
		int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		struct sockaddr_un a = { .sun_family = AF_UNIX };
		g_strlcpy(a.sun_path, sock, sizeof(a.sun_path));
		if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
			g_snprintf(erro, n, "%s: %s", sock, g_strerror(errno));
			if (fd >= 0) {
				close(fd);
			}
			return POLKITAG_FALHA;
		}
		entrada = saida = fd;
		/* no modo por socket o helper lê primeiro a conta, depois o cookie */
		char *linha = g_strdup_printf("%s\n", usuario);
		escrever_tudo(fd, linha);
		g_free(linha);
	} else {
		const char *helper = help_env && *help_env ? help_env : HELPER_PADRAO;
		char *argv[] = { (char *)helper, (char *)usuario, NULL };
		GError *e = NULL;
		if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &filho, &entrada, &saida, NULL, &e)) {
			g_snprintf(erro, n, "%s: %s", helper, e->message);
			g_error_free(e);
			return POLKITAG_FALHA;
		}
	}
	char *linha = g_strdup_printf("%s\n", cookie);
	int resultado = POLKITAG_FALHA;
	g_snprintf(erro, n, "o helper do polkit não respondeu");
	if (escrever_tudo(entrada, linha) == 0) {
		FILE *f = fdopen(saida == entrada ? dup(saida) : saida, "r");
		char buf[1024];
		bool respondeu = false;
		while (f && fgets(buf, sizeof(buf), f)) {
			buf[strcspn(buf, "\r\n")] = '\0';
			char *l = polkitag_desescapar(buf);
			if (g_str_has_prefix(l, "PAM_PROMPT_ECHO_OFF") || g_str_has_prefix(l, "PAM_PROMPT_ECHO_ON")) {
				if (respondeu) { /* só sabemos responder a senha: um segundo pedido (código de dois fatores) falha */
					resultado = POLKITAG_ERRADA;
					g_free(l);
					break;
				}
				char *s = g_strdup_printf("%s\n", senha);
				escrever_tudo(entrada, s);
				explicit_bzero(s, strlen(s));
				g_free(s);
				respondeu = true;
			} else if (g_str_has_prefix(l, "SUCCESS")) {
				resultado = POLKITAG_OK;
				g_free(l);
				break;
			} else if (g_str_has_prefix(l, "FAILURE")) {
				resultado = POLKITAG_ERRADA;
				g_free(l);
				break;
			}
			g_free(l);
		}
		if (f) {
			fclose(f);
		}
	}
	g_free(linha);
	if (entrada >= 0) {
		close(entrada);
	}
	if (saida >= 0 && saida != entrada) {
		close(saida);
	}
	if (filho) {
		waitpid(filho, NULL, 0);
		g_spawn_close_pid(filho);
	}
	return resultado;
}
