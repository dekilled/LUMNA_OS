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

/* quem entrega o resultado de um pedido assíncrono na linha principal: por padrão o laço do GLib; os apps do Gravity (que não rodam o do GLib) apontam
 * isto para gv_na_linha_principal */
void (*emmac_entregar)(void (*fn)(void *), void *dados);

struct pedido_async {
	void (*pronto)(const char *, void *);
	void *dados;
	char *pedido;
	char *resposta; /* NULL: o serviço não respondeu */
};

static void pa_entregue(void *d) {
	struct pedido_async *p = d;
	p->pronto(p->resposta, p->dados);
	g_free(p->resposta);
	g_free(p->pedido);
	g_free(p);
}

static gboolean pa_entregue_glib(gpointer d) {
	pa_entregue(d);
	return G_SOURCE_REMOVE;
}

static gpointer pa_thread(gpointer d) {
	struct pedido_async *p = d;
	char *buf = g_malloc(65536);
	if (emmac_pedido(p->pedido, buf, 65536)) {
		p->resposta = buf;
	} else {
		g_free(buf);
	}
	if (emmac_entregar) {
		emmac_entregar(pa_entregue, p);
	} else {
		g_idle_add(pa_entregue_glib, p);
	}
	return NULL;
}

void emmac_pedido_async(const char *pedido, void (*pronto)(const char *, void *), void *dados) {
	struct pedido_async *p = g_new0(struct pedido_async, 1);
	p->pronto = pronto;
	p->dados = dados;
	p->pedido = g_strdup(pedido);
	g_thread_unref(g_thread_new("emmac-pedido", pa_thread, p));
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
