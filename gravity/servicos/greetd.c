/* A conversa com o greetd (greetd.h). */
#define _DEFAULT_SOURCE
#include "greetd.h"

#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* ---- quem e o quê ---- */

int greetd_usuarios(struct greetd_usuario *lista, int max) {
	FILE *f = fopen("/etc/passwd", "r");
	if (!f) {
		return 0;
	}
	int n = 0;
	char linha[1024];
	while (n < max && fgets(linha, sizeof(linha), f)) {
		char *campos[7] = { 0 };
		char *p = linha;
		for (int i = 0; i < 7 && p; i++) {
			campos[i] = p;
			p = strchr(p, ':');
			if (p) {
				*p++ = '\0';
			}
		}
		if (!campos[6]) {
			continue;
		}
		campos[6][strcspn(campos[6], "\r\n")] = '\0';
		long uid = strtol(campos[2], NULL, 10);
		if (uid < 1000 || uid >= 60000 || g_str_has_suffix(campos[6], "nologin") || g_str_has_suffix(campos[6], "false")) {
			continue;
		}
		snprintf(lista[n].login, sizeof(lista[n].login), "%s", campos[0]);
		snprintf(lista[n].nome, sizeof(lista[n].nome), "%.*s", (int)strcspn(campos[4], ","), campos[4]);
		n++;
	}
	fclose(f);
	return n;
}

static char *campo_desktop(const char *texto, const char *chave) {
	char **linhas = g_strsplit(texto, "\n", -1);
	char *valor = NULL;
	size_t l = strlen(chave);
	for (char **x = linhas; *x && !valor; x++) {
		if (!strncmp(*x, chave, l) && (*x)[l] == '=') {
			valor = g_strstrip(g_strdup(*x + l + 1));
		}
	}
	g_strfreev(linhas);
	return valor;
}

static int ordem(const void *a, const void *b) {
	const struct greetd_sessao *x = a, *y = b;
	/* as do LUMNA primeiro (a do Orbit antes de todas), depois por nome */
	int px = !strcmp(x->id, "lumna-orbit") ? 0 : g_str_has_prefix(x->id, "lumna") ? 1 : 2;
	int py = !strcmp(y->id, "lumna-orbit") ? 0 : g_str_has_prefix(y->id, "lumna") ? 1 : 2;
	return px != py ? px - py : g_utf8_collate(x->nome, y->nome);
}

int greetd_sessoes(struct greetd_sessao *lista, int max) {
	int n = 0;
	GDir *d = g_dir_open("/usr/share/wayland-sessions", 0, NULL);
	const char *nome;
	while (d && n < max && (nome = g_dir_read_name(d))) {
		if (!g_str_has_suffix(nome, ".desktop")) {
			continue;
		}
		char *caminho = g_build_filename("/usr/share/wayland-sessions", nome, NULL);
		char *texto = NULL;
		if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
			char *escondida = campo_desktop(texto, "Hidden"), *nodisplay = campo_desktop(texto, "NoDisplay");
			char *exec = campo_desktop(texto, "Exec"), *titulo = campo_desktop(texto, "Name");
			if (exec && !(escondida && !strcmp(escondida, "true")) && !(nodisplay && !strcmp(nodisplay, "true"))) {
				struct greetd_sessao *s = &lista[n++];
				snprintf(s->id, sizeof(s->id), "%.*s", (int)(strlen(nome) - 8), nome);
				snprintf(s->nome, sizeof(s->nome), "%s", titulo ? titulo : s->id);
				/* sem os %U e companhia */
				s->exec[0] = '\0';
				char **partes = g_strsplit(exec, " ", -1);
				for (char **p = partes; *p; p++) {
					if (**p && **p != '%') {
						if (s->exec[0]) {
							g_strlcat(s->exec, " ", sizeof(s->exec));
						}
						g_strlcat(s->exec, *p, sizeof(s->exec));
					}
				}
				g_strfreev(partes);
			}
			g_free(escondida);
			g_free(nodisplay);
			g_free(exec);
			g_free(titulo);
			g_free(texto);
		}
		g_free(caminho);
	}
	if (d) {
		g_dir_close(d);
	}
	if (n == 0 && max > 0) {
		snprintf(lista[0].id, sizeof(lista[0].id), "lumna-orbit");
		snprintf(lista[0].nome, sizeof(lista[0].nome), "LUMNA · Orbit");
		snprintf(lista[0].exec, sizeof(lista[0].exec), "lumna-orbit-session");
		n = 1;
	}
	qsort(lista, (size_t)n, sizeof(lista[0]), ordem);
	return n;
}

/* ---- o protocolo ---- */

static bool mandar(int fd, const char *json) {
	uint32_t n = (uint32_t)strlen(json);
	return write(fd, &n, sizeof(n)) == sizeof(n) && write(fd, json, n) == (ssize_t)n;
}

static bool ler_tudo(int fd, void *buf, size_t n) {
	size_t lido = 0;
	while (lido < n) {
		ssize_t r = read(fd, (char *)buf + lido, n - lido);
		if (r <= 0) {
			return false;
		}
		lido += (size_t)r;
	}
	return true;
}

static char *receber(int fd) {
	uint32_t n;
	if (!ler_tudo(fd, &n, sizeof(n)) || n > 1 << 20) {
		return NULL;
	}
	char *texto = g_malloc(n + 1);
	if (!ler_tudo(fd, texto, n)) {
		g_free(texto);
		return NULL;
	}
	texto[n] = '\0';
	return texto;
}

/* o valor de "chave":"..." numa resposta (as respostas do greetd são pequenas e planas) */
static char *valor(const char *json, const char *chave) {
	char padrao[64];
	snprintf(padrao, sizeof(padrao), "\"%s\"", chave);
	const char *p = strstr(json, padrao);
	if (!p) {
		return NULL;
	}
	p = strchr(p + strlen(padrao), ':');
	if (!p) {
		return NULL;
	}
	p++;
	while (*p == ' ') {
		p++;
	}
	if (*p != '"') {
		return NULL;
	}
	GString *s = g_string_new(NULL);
	for (p++; *p && *p != '"'; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			g_string_append_c(s, *p == 'n' ? '\n' : *p);
		} else {
			g_string_append_c(s, *p);
		}
	}
	return g_string_free(s, FALSE);
}

/* uma string JSON ("..." com os escapes) */
static char *aspas(const char *s) {
	GString *g = g_string_new("\"");
	for (; *s; s++) {
		if (*s == '"' || *s == '\\') {
			g_string_append_c(g, '\\');
			g_string_append_c(g, *s);
		} else if ((unsigned char)*s < 0x20) {
			g_string_append_printf(g, "\\u%04x", *s);
		} else {
			g_string_append_c(g, *s);
		}
	}
	g_string_append_c(g, '"');
	return g_string_free(g, FALSE);
}

static const char *descrever(const char *resposta) {
	static char erro[256];
	char *tipo = valor(resposta, "error_type");
	char *desc = valor(resposta, "description");
	if (tipo && !strcmp(tipo, "auth_error")) {
		snprintf(erro, sizeof(erro), "Senha incorreta.");
	} else {
		snprintf(erro, sizeof(erro), "%s", desc && *desc ? desc : "Não deu para entrar.");
	}
	g_free(tipo);
	g_free(desc);
	return erro;
}

const char *greetd_entrar(const char *login, const char *senha, const char *exec) {
	static char erro[256];
	const char *caminho = getenv("GREETD_SOCK");
	if (!caminho) {
		return "Sem ligação ao greetd (GREETD_SOCK).";
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", caminho);
	if (fd < 0 || connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		return "Não deu para falar com o greetd.";
	}
	const char *resultado = NULL;
	char *u = aspas(login);
	char *msg = g_strdup_printf("{\"type\":\"create_session\",\"username\":%s}", u);
	g_free(u);
	bool ok = mandar(fd, msg);
	g_free(msg);
	bool respondeu = false;
	while (ok) {
		char *r = receber(fd);
		if (!r) {
			resultado = "O greetd não respondeu.";
			break;
		}
		char *tipo = valor(r, "type");
		if (tipo && !strcmp(tipo, "auth_message")) {
			char *pedido = valor(r, "auth_message_type");
			char *resposta;
			if (pedido && !strcmp(pedido, "secret") && !respondeu) {
				respondeu = true;
				resposta = aspas(senha);
			} else if (pedido && (!strcmp(pedido, "secret") || !strcmp(pedido, "visible"))) {
				/* só sabemos responder a senha, uma vez: um segundo pedido (um código) cancela */
				mandar(fd, "{\"type\":\"cancel_session\"}");
				g_free(pedido);
				g_free(tipo);
				g_free(r);
				resultado = "Esta conta pede uma confirmação que a tela ainda não sabe fazer.";
				break;
			} else {
				resposta = g_strdup("null"); /* avisos do PAM: só confirmar */
			}
			char *m = g_strdup_printf("{\"type\":\"post_auth_message_response\",\"response\":%s}", resposta);
			ok = mandar(fd, m);
			/* a senha não fica na memória mais do que precisa */
			memset(resposta, 0, strlen(resposta));
			memset(m, 0, strlen(m));
			g_free(resposta);
			g_free(m);
			g_free(pedido);
		} else if (tipo && !strcmp(tipo, "success")) {
			g_free(tipo);
			g_free(r);
			/* pede a sessão: o comando em partes, como lista JSON */
			GString *cmd = g_string_new("[");
			char **partes = g_strsplit(exec, " ", -1);
			for (char **p = partes; *p; p++) {
				if (!**p) {
					continue;
				}
				char *a = aspas(*p);
				g_string_append_printf(cmd, "%s%s", cmd->len > 1 ? "," : "", a);
				g_free(a);
			}
			g_strfreev(partes);
			g_string_append_c(cmd, ']');
			char *m = g_strdup_printf("{\"type\":\"start_session\",\"cmd\":%s,\"env\":[]}", cmd->str);
			g_string_free(cmd, TRUE);
			ok = mandar(fd, m);
			g_free(m);
			char *fim = ok ? receber(fd) : NULL;
			char *t = fim ? valor(fim, "type") : NULL;
			if (!t || strcmp(t, "success")) {
				snprintf(erro, sizeof(erro), "%s", fim ? descrever(fim) : "O greetd não abriu a sessão.");
				resultado = erro;
			}
			g_free(t);
			g_free(fim);
			close(fd);
			return resultado;
		} else {
			mandar(fd, "{\"type\":\"cancel_session\"}");
			char *x = receber(fd);
			g_free(x);
			snprintf(erro, sizeof(erro), "%s", descrever(r));
			resultado = erro;
			g_free(tipo);
			g_free(r);
			break;
		}
		g_free(tipo);
		g_free(r);
	}
	if (!ok && !resultado) {
		resultado = "A ligação ao greetd caiu.";
	}
	close(fd);
	return resultado;
}

/* ---- em fundo ---- */

static GMutex trinco;
static bool acabou, a_correr;
static const char *erro_fundo;
static char f_login[64], f_senha[256], f_exec[512];

static gpointer linha(gpointer d) {
	const char *e = greetd_entrar(f_login, f_senha, f_exec);
	memset(f_senha, 0, sizeof(f_senha));
	g_mutex_lock(&trinco);
	erro_fundo = e;
	acabou = true;
	a_correr = false;
	g_mutex_unlock(&trinco);
	return NULL;
}

void greetd_entrar_em_fundo(const char *login, const char *senha, const char *exec) {
	g_mutex_lock(&trinco);
	if (a_correr) {
		g_mutex_unlock(&trinco);
		return;
	}
	a_correr = true;
	acabou = false;
	g_mutex_unlock(&trinco);
	snprintf(f_login, sizeof(f_login), "%s", login);
	snprintf(f_senha, sizeof(f_senha), "%s", senha);
	snprintf(f_exec, sizeof(f_exec), "%s", exec);
	g_thread_unref(g_thread_new("greetd", linha, NULL));
}

bool greetd_terminou(const char **erro) {
	g_mutex_lock(&trinco);
	bool fim = acabou;
	if (fim) {
		*erro = erro_fundo;
		acabou = false;
	}
	g_mutex_unlock(&trinco);
	return fim;
}
