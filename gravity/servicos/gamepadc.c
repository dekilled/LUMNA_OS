#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "gamepadc.h"

char *gamepadc_socket_caminho(void) {
	const char *run = g_get_user_runtime_dir();
	return g_build_filename(run ? run : "/tmp", "lumna-gamepad.sock", NULL);
}

static int ligar(void) {
	char *caminho = gamepadc_socket_caminho();
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	g_strlcpy(a.sun_path, caminho, sizeof(a.sun_path));
	g_free(caminho);
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		return -1;
	}
	if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

bool gamepadc_pedido(const char *pedido, char *resposta, size_t tam) {
	int fd = ligar();
	if (fd < 0 || !tam) {
		return false;
	}
	struct timeval tv = { .tv_sec = 3 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	char *linha = g_strdup_printf("%s\n", pedido);
	bool ok = write(fd, linha, strlen(linha)) == (ssize_t)strlen(linha);
	g_free(linha);
	size_t n = 0;
	while (ok && n + 1 < tam) {
		ssize_t r = read(fd, resposta + n, tam - 1 - n);
		if (r <= 0) {
			break;
		}
		n += (size_t)r;
		if (memchr(resposta, '\n', n)) {
			break;
		}
	}
	resposta[n] = '\0';
	char *nl = strchr(resposta, '\n');
	if (nl) {
		*nl = '\0';
	}
	close(fd);
	return ok && n > 0;
}

int gamepadc_assinar(void) {
	int fd = ligar();
	if (fd < 0) {
		return -1;
	}
	if (write(fd, "assinar\n", 8) != 8) {
		close(fd);
		return -1;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	return fd;
}

char *gamepadc_ler_linha(int fd, bool *caiu) {
	static GString *pendente[64]; /* o que chegou de cada fd e ainda não fechou uma linha */
	if (fd < 0 || fd >= 64) {
		if (caiu) {
			*caiu = true;
		}
		return NULL;
	}
	if (!pendente[fd]) {
		pendente[fd] = g_string_new(NULL);
	}
	char buf[1024];
	for (;;) {
		char *nl = memchr(pendente[fd]->str, '\n', pendente[fd]->len);
		if (nl) {
			char *l = g_strndup(pendente[fd]->str, (gsize)(nl - pendente[fd]->str));
			g_string_erase(pendente[fd], 0, nl - pendente[fd]->str + 1);
			return l;
		}
		ssize_t r = read(fd, buf, sizeof(buf));
		if (r > 0) {
			g_string_append_len(pendente[fd], buf, r);
			continue;
		}
		if (r == 0 || (errno != EAGAIN && errno != EINTR)) {
			g_string_set_size(pendente[fd], 0);
			if (caiu) {
				*caiu = true;
			}
		}
		return NULL;
	}
}
