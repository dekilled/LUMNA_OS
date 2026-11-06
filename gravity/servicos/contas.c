#include "contas.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *etc(void) {
	const char *e = getenv("LUMNA_ETC");
	return e && *e ? e : "/etc";
}

static bool no_grupo(const char *grupo, const char *login) {
	char *c = g_strdup_printf("%s/group", etc()), *texto = NULL;
	bool achou = false;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l && !achou; l++) {
			char **p = g_strsplit(*l, ":", 4);
			if (p[0] && p[1] && p[2] && p[3] && !strcmp(p[0], grupo)) {
				char **membros = g_strsplit(p[3], ",", -1);
				for (char **m = membros; *m; m++) {
					achou |= !strcmp(g_strstrip(*m), login);
				}
				g_strfreev(membros);
			}
			g_strfreev(p);
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(c);
	return achou;
}

const char *contas_eu(void) {
	return getenv("LUMNA_USUARIO") ? getenv("LUMNA_USUARIO") : g_get_user_name(); /* LUMNA_USUARIO: testes */
}

int contas_listar(struct conta *lista, int max) {
	char *c = g_strdup_printf("%s/passwd", etc()), *texto = NULL;
	int n = 0;
	const char *eu = contas_eu();
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l && n < max; l++) {
			char **p = g_strsplit(*l, ":", 7);
			if (p[0] && p[1] && p[2] && p[3] && p[4] && p[5] && p[6]) {
				int uid = atoi(p[2]);
				if (uid >= 1000 && uid < 60000 && !g_str_has_suffix(p[6], "nologin") && !g_str_has_suffix(p[6], "false")) {
					struct conta *k = &lista[n++];
					memset(k, 0, sizeof(*k));
					g_strlcpy(k->login, p[0], sizeof(k->login));
					char *virg = strchr(p[4], ',');
					if (virg) {
						*virg = '\0';
					}
					g_strlcpy(k->nome, p[4], sizeof(k->nome));
					k->uid = uid;
					k->admin = no_grupo("wheel", p[0]) || no_grupo("sudo", p[0]);
					k->atual = !strcmp(p[0], eu);
				}
			}
			g_strfreev(p);
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(c);
	/* quem está logado primeiro */
	for (int i = 1; i < n; i++) {
		if (lista[i].atual) {
			struct conta t = lista[i];
			memmove(&lista[1], &lista[0], (size_t)i * sizeof(lista[0]));
			lista[0] = t;
			break;
		}
	}
	return n;
}

/* o ajudante com root; entrada recebe o que vai na entrada padrão (a senha) */
static bool ajudante(char **args, const char *entrada, char *erro, size_t tam) {
	const char *h = getenv("LUMNA_USUARIOS_AJUDANTE") ? getenv("LUMNA_USUARIOS_AJUDANTE") : "/usr/lib/lumna/lumna-usuarios";
	char *argv[12];
	int k = 0;
	if (!getenv("LUMNA_SEM_PKEXEC")) {
		argv[k++] = "pkexec";
	}
	argv[k++] = (char *)h;
	for (int i = 0; args[i] && k < 11; i++) {
		argv[k++] = args[i];
	}
	argv[k] = NULL;
	GPid pid;
	gint in = -1, err = -1;
	GError *e = NULL;
	if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, &pid, &in, NULL, &err, &e)) {
		g_strlcpy(erro, e->message, tam);
		g_error_free(e);
		return false;
	}
	if (entrada && *entrada) {
		size_t n = strlen(entrada);
		if (write(in, entrada, n) < 0 || write(in, "\n", 1) < 0) {
		}
	}
	close(in);
	char buf[300] = "";
	ssize_t r = read(err, buf, sizeof(buf) - 1);
	buf[r > 0 ? r : 0] = '\0';
	close(err);
	int estado = 0;
	waitpid(pid, &estado, 0);
	g_spawn_close_pid(pid);
	bool ok = WIFEXITED(estado) && WEXITSTATUS(estado) == 0;
	if (!ok) {
		char *t = g_strstrip(buf);
		g_strlcpy(erro, *t ? t : "não deu (faltou a senha do administrador?)", tam);
	}
	return ok;
}

static bool texto_seguro(const char *t) {
	/* a senha e o nome vão por argumento ou entrada: uma quebra de linha quebraria o protocolo */
	return t && !strchr(t, '\n') && !strchr(t, '\r');
}

bool contas_adicionar(const char *login, const char *nome, const char *senha, bool admin, char *erro, size_t tam) {
	if (!login || !*login || !senha || !*senha || !texto_seguro(nome) || !texto_seguro(senha)) {
		g_strlcpy(erro, "faltou o nome de usuário ou a senha", tam);
		return false;
	}
	char *args[] = { "adicionar", (char *)login, (char *)(nome ? nome : ""), admin ? "admin" : NULL, NULL };
	return ajudante(args, senha, erro, tam);
}

bool contas_remover(const char *login, bool apagar_pasta, char *erro, size_t tam) {
	char *args[] = { "remover", (char *)login, apagar_pasta ? "apagar" : NULL, NULL };
	return ajudante(args, NULL, erro, tam);
}

bool contas_admin(const char *login, bool admin, char *erro, size_t tam) {
	char *args[] = { "admin", (char *)login, admin ? "sim" : "nao", NULL };
	return ajudante(args, NULL, erro, tam);
}

bool contas_nome(const char *login, const char *nome, char *erro, size_t tam) {
	if (!texto_seguro(nome)) {
		return false;
	}
	char *args[] = { "nome", (char *)login, (char *)nome, NULL };
	return ajudante(args, NULL, erro, tam);
}

bool contas_senha(const char *login, const char *senha, char *erro, size_t tam) {
	if (!senha || !*senha || !texto_seguro(senha)) {
		return false;
	}
	char *args[] = { "senha", (char *)login, NULL };
	return ajudante(args, senha, erro, tam);
}

const char *contas_foto(void) {
	static char c[512];
	g_snprintf(c, sizeof(c), "%s/.face", g_get_home_dir());
	return g_file_test(c, G_FILE_TEST_IS_REGULAR) ? c : NULL;
}

bool contas_foto_definir(const char *imagem, char *erro, size_t tam) {
	GError *e = NULL;
	/* o tamanho de uma vez só: a imagem inteira cabe num quadrado de 256 px, sem esticar, e o que sobra nos lados se corta */
	GdkPixbuf *p = gdk_pixbuf_new_from_file_at_scale(imagem, -1, 256, TRUE, &e);
	if (!p) {
		g_strlcpy(erro, e ? e->message : "não abriu a imagem", tam);
		g_clear_error(&e);
		return false;
	}
	int w = gdk_pixbuf_get_width(p), h = gdk_pixbuf_get_height(p);
	if (w < h) { /* o lado menor define o quadrado: refaz pela largura */
		g_object_unref(p);
		p = gdk_pixbuf_new_from_file_at_scale(imagem, 256, -1, TRUE, NULL);
		if (!p) {
			g_strlcpy(erro, "não abriu a imagem", tam);
			return false;
		}
		w = gdk_pixbuf_get_width(p);
		h = gdk_pixbuf_get_height(p);
	}
	int lado = w < h ? w : h;
	GdkPixbuf *q = gdk_pixbuf_new_subpixbuf(p, (w - lado) / 2, (h - lado) / 2, lado, lado);
	char destino[512], tmp[520];
	g_snprintf(destino, sizeof(destino), "%s/.face", g_get_home_dir());
	g_snprintf(tmp, sizeof(tmp), "%s.novo", destino);
	bool ok = gdk_pixbuf_save(q, tmp, "png", &e, NULL) && g_rename(tmp, destino) == 0;
	if (!ok) {
		g_strlcpy(erro, e ? e->message : "não deu para guardar a foto", tam);
		g_clear_error(&e);
	} else {
		g_chmod(destino, 0644); /* a tela de login (outro usuário) precisa ler */
	}
	g_object_unref(q);
	g_object_unref(p);
	return ok;
}

bool contas_foto_tirar(void) {
	const char *c = contas_foto();
	return !c || g_remove(c) == 0;
}
