#include "emmac.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <glib.h>

char *emmac_socket_caminho(void) {
	const char *env = g_getenv("LUMNA_EMMA_SOCK");
	return g_strdup(env && *env ? env : NULL) ?: g_build_filename(g_get_user_runtime_dir(), "lumna-emma.sock", NULL);
}

static int ligar(void) {
	char *c = emmac_socket_caminho();
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	g_strlcpy(a.sun_path, c, sizeof(a.sun_path));
	g_free(c);
	if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		return -1;
	}
	return fd;
}

bool emmac_pedido(const char *pedido, char *resposta, size_t tam) {
	int fd = ligar();
	if (fd < 0) {
		return false;
	}
	/* uma conversa inteira pode levar um bom tempo (o celular espera a resposta) */
	struct timeval tv = { .tv_sec = 120 };
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

struct pedido_async {
	void (*pronto)(const char *, void *);
	void *dados;
	GSocketConnection *con;
	GDataInputStream *in;
	char *pedido;
};

static void pa_fim(struct pedido_async *p, const char *resposta) {
	p->pronto(resposta, p->dados);
	if (p->con) {
		g_io_stream_close(G_IO_STREAM(p->con), NULL, NULL);
		g_object_unref(p->con);
	}
	if (p->in) {
		g_object_unref(p->in);
	}
	g_free(p->pedido);
	g_free(p);
}

static void pa_lido(GObject *o, GAsyncResult *r, gpointer d) {
	struct pedido_async *p = d;
	char *linha = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(o), r, NULL, NULL);
	pa_fim(p, linha);
	g_free(linha);
}

static void pa_escrito(GObject *o, GAsyncResult *r, gpointer d) {
	struct pedido_async *p = d;
	if (g_output_stream_write_all_finish(G_OUTPUT_STREAM(o), r, NULL, NULL)) {
		g_data_input_stream_read_line_async(p->in, G_PRIORITY_DEFAULT, NULL, pa_lido, p);
	} else {
		pa_fim(p, NULL);
	}
}

static void pa_ligado(GObject *o, GAsyncResult *r, gpointer d) {
	struct pedido_async *p = d;
	p->con = g_socket_client_connect_finish(G_SOCKET_CLIENT(o), r, NULL);
	g_object_unref(o);
	if (!p->con) {
		pa_fim(p, NULL);
		return;
	}
	p->in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(p->con)));
	g_output_stream_write_all_async(g_io_stream_get_output_stream(G_IO_STREAM(p->con)), p->pedido, strlen(p->pedido), G_PRIORITY_DEFAULT, NULL, pa_escrito, p);
}

void emmac_pedido_async(const char *pedido, void (*pronto)(const char *, void *), void *dados) {
	struct pedido_async *p = g_new0(struct pedido_async, 1);
	p->pronto = pronto;
	p->dados = dados;
	p->pedido = g_strdup_printf("%s\n", pedido);
	char *c = emmac_socket_caminho();
	GSocketAddress *a = g_unix_socket_address_new(c);
	g_free(c);
	GSocketClient *cli = g_socket_client_new();
	g_socket_client_connect_async(cli, G_SOCKET_CONNECTABLE(a), NULL, pa_ligado, p);
	g_object_unref(a);
}

int emmac_assinar(void) {
	int fd = ligar();
	if (fd < 0) {
		return -1;
	}
	const char *p = "assinar\n";
	if (write(fd, p, strlen(p)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

char *emmac_assinar_ler(int fd, bool *caiu) {
	static GString *resto;
	static int dono = -1;
	if (!resto || dono != fd) {
		if (resto) {
			g_string_free(resto, TRUE);
		}
		resto = g_string_new(NULL);
		dono = fd;
	}
	*caiu = false;
	/* o descritor é bloqueante: só lê o que o poll disse que há (um pedaço) */
	char buf[4096];
	char *nl = strchr(resto->str, '\n');
	if (!nl) {
		ssize_t r = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
			*caiu = true;
			return NULL;
		}
		if (r > 0) {
			g_string_append_len(resto, buf, r);
		}
		nl = strchr(resto->str, '\n');
	}
	if (!nl) {
		return NULL;
	}
	char *linha = g_strndup(resto->str, (gsize)(nl - resto->str));
	g_string_erase(resto, 0, (gssize)(nl - resto->str) + 1);
	return linha;
}
