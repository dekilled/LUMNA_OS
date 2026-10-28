/*
 * orbit-msg: fala com o Orbit pelo socket dele.
 *   orbit-msg saidas
 *   orbit-msg -t get_outputs
 *   orbit-msg espaco 2
 *   orbit-msg subscrever        (fica a imprimir os eventos, um JSON por linha)
 * O socket vem de $ORBIT_SOCK (o Orbit põe-no no ambiente dos programas que lança) ou de
 * $XDG_RUNTIME_DIR/orbit.$WAYLAND_DISPLAY.sock. -s CAMINHO escolhe outro.
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void usage(const char *self) {
	fprintf(stderr,
		"uso: %s [-s socket] pedido [argumentos]\n"
		"  saidas | janelas | espacos | foco ID | fechar [ID] | espaco N [SAIDA] | mover ID N\n"
		"  ecra [ID] | flutuar [ID] | dividir | acao NOME | exec COMANDO\n"
		"  modo [seguinte|anterior|desktop|gaming|midia [SAIDA]] | modo_layout tela|estender\n"
		"  toque baixo|move|cima DEDO X Y | subscrever\n"
		"  tela_cheia [sim|nao|alternar] | copiados | copiar N | limpar_copiados\n"
		"  (também -t get_outputs, -t get_tree, -t get_workspaces)\n",
		self);
}

int main(int argc, char *argv[]) {
	const char *path = getenv("ORBIT_SOCK");
	char fallback[108];
	int first = 1;
	if (argc > 2 && strcmp(argv[1], "-s") == 0) {
		path = argv[2];
		first = 3;
	}
	if (first >= argc || strcmp(argv[first], "-h") == 0 || strcmp(argv[first], "--help") == 0) {
		usage(argv[0]);
		return first >= argc ? 1 : 0;
	}
	if (!path || !*path) {
		const char *runtime = getenv("XDG_RUNTIME_DIR");
		const char *display = getenv("WAYLAND_DISPLAY");
		if (!runtime || !display) {
			fprintf(stderr, "orbit-msg: sem ORBIT_SOCK nem XDG_RUNTIME_DIR/WAYLAND_DISPLAY\n");
			return 1;
		}
		snprintf(fallback, sizeof(fallback), "%s/orbit.%s.sock", runtime, display);
		path = fallback;
	}

	char line[1024] = "";
	size_t len = 0;
	for (int i = first; i < argc; i++) {
		int n = snprintf(line + len, sizeof(line) - len, "%s%s", i > first ? " " : "", argv[i]);
		if (n < 0 || (size_t)n >= sizeof(line) - len - 1) {
			fprintf(stderr, "orbit-msg: pedido longo demais\n");
			return 1;
		}
		len += n;
	}
	line[len++] = '\n';

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "orbit-msg: o Orbit não respondeu em %s\n", path);
		return 1;
	}
	if (write(fd, line, len) != (ssize_t)len) {
		fprintf(stderr, "orbit-msg: falha ao mandar o pedido\n");
		return 1;
	}
	int subscribe = strcmp(argv[first], "subscrever") == 0 || strcmp(argv[first], "subscribe") == 0;
	/* a resposta é uma linha; assinando, as linhas seguem até o Orbit fechar */
	char buf[4096];
	int ok = 1;
	int first_line = 1;
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		fwrite(buf, 1, n, stdout);
		fflush(stdout);
		if (first_line && strstr(buf, "\"ok\":false")) {
			ok = 0;
		}
		if (memchr(buf, '\n', n)) {
			first_line = 0;
			if (!subscribe) {
				break;
			}
		}
	}
	close(fd);
	return ok ? 0 : 2;
}
