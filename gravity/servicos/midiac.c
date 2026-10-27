#include "midiac.h"

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "json.h"

static int ligar(void) {
	const char *run = g_get_user_runtime_dir();
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		return -1;
	}
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	snprintf(a.sun_path, sizeof(a.sun_path), "%s/lumna-midia.sock", run);
	if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

bool midiac_pedido(const char *pedido, char *resposta, size_t tam) {
	int fd = ligar();
	if (fd < 0) {
		return false;
	}
	struct timeval tv = { .tv_sec = 2 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	char *linha = g_strdup_printf("%s\n", pedido);
	bool ok = write(fd, linha, strlen(linha)) == (ssize_t)strlen(linha);
	g_free(linha);
	size_t len = 0;
	while (ok && len + 1 < tam) {
		ssize_t r = read(fd, resposta + len, tam - 1 - len);
		if (r <= 0) {
			break;
		}
		len += (size_t)r;
		if (memchr(resposta, '\n', len)) {
			break;
		}
	}
	resposta[len] = '\0';
	close(fd);
	return ok && len > 0;
}

static void copiar(char *dest, size_t tam, const struct json *j, const char *chave) {
	g_strlcpy(dest, json_chave_texto(j, chave), tam);
}

bool midiac_estado(struct midia_estado *e) {
	memset(e, 0, sizeof(*e));
	char *r = g_malloc(65536);
	bool ok = false;
	if (midiac_pedido("estado", r, 65536)) {
		struct json *j = json_ler(r);
		if (j && json_bool(json_chave(j, "ok"), false)) {
			ok = e->servico = true;
			e->firefox = json_bool(json_chave(j, "firefox"), false);
			e->extensao = json_bool(json_chave(j, "extensao"), true);
			copiar(e->ativo, sizeof(e->ativo), j, "ativo");
			const struct json *lista = json_chave(j, "servicos");
			for (int i = 0; lista && i < lista->nfilhos && e->n < MIDIA_MAX_SERVICOS; i++) {
				const struct json *s = lista->filhos[i];
				struct midia_vivo *v = &e->serv[e->n++];
				copiar(v->s.id, sizeof(v->s.id), s, "id");
				copiar(v->s.nome, sizeof(v->s.nome), s, "nome");
				copiar(v->s.url, sizeof(v->s.url), s, "url");
				copiar(v->s.cor, sizeof(v->s.cor), s, "cor");
				v->s.padrao = json_bool(json_chave(s, "padrao"), false);
				v->s.ativo = json_bool(json_chave(s, "ativo"), true);
				v->aberto = json_bool(json_chave(s, "aberto"), false);
				v->carregando = json_bool(json_chave(s, "carregando"), false);
			}
			lista = json_chave(j, "recentes");
			for (int i = 0; lista && i < lista->nfilhos && e->nrec < MIDIA_MAX_RECENTES; i++) {
				const struct json *s = lista->filhos[i];
				struct midia_recente *v = &e->rec[e->nrec++];
				copiar(v->servico, sizeof(v->servico), s, "servico");
				copiar(v->url, sizeof(v->url), s, "url");
				copiar(v->titulo, sizeof(v->titulo), s, "titulo");
				copiar(v->miniatura, sizeof(v->miniatura), s, "miniatura");
				v->quando = (gint64)json_numero(json_chave(s, "quando"), 0);
			}
			const struct json *capa = json_chave(j, "capa");
			copiar(e->capa, sizeof(e->capa), capa, "servico");
			e->capa_visivel = json_bool(json_chave(capa, "visivel"), false);
		}
		json_liberar(j);
	}
	g_free(r);
	return ok;
}

int midiac_assinar(void) {
	int fd = ligar();
	if (fd < 0) {
		return -1;
	}
	if (write(fd, "assinar\n", 8) != 8) {
		close(fd);
		return -1;
	}
	return fd;
}

void midiac_assinar_ler(int fd) {
	char buf[1024];
	while (recv(fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {
	}
}

/* o serviço caiu? (o descritor acordou e já não há quem escreva) */
bool midiac_assinar_vivo(int fd) {
	char buf[1024];
	for (;;) {
		ssize_t r = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (r == 0) {
			return false;
		}
		if (r < 0) {
			return true;
		}
	}
}

const struct midia_vivo *midiac_achar(const struct midia_estado *e, const char *id) {
	for (int i = 0; i < e->n; i++) {
		if (!strcmp(e->serv[i].s.id, id)) {
			return &e->serv[i];
		}
	}
	return NULL;
}
