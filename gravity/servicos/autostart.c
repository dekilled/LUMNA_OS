#include "autostart.h"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *desktop_atual(void) {
	const char *d = getenv("XDG_CURRENT_DESKTOP");
	return d && *d ? d : "LUMNA";
}

static char *pasta_usuario(void) {
	const char *c = getenv("XDG_CONFIG_HOME");
	return c && *c ? g_build_filename(c, "autostart", NULL) : g_build_filename(g_get_home_dir(), ".config", "autostart", NULL);
}

static char **pastas_sistema(void) {
	const char *d = getenv("XDG_CONFIG_DIRS");
	char **v = g_strsplit(d && *d ? d : "/etc/xdg", ":", -1);
	for (char **p = v; *p; p++) {
		char *c = g_build_filename(*p, "autostart", NULL);
		g_free(*p);
		*p = c;
	}
	return v;
}

static bool na_lista(char **lista, const char *valor) {
	for (; lista && *lista; lista++) {
		if (!strcmp(*lista, valor)) {
			return true;
		}
	}
	return false;
}

/* o desktop atual aparece em uma lista "A;B;" (XDG_CURRENT_DESKTOP também pode ser "A:B") */
static bool desktop_em(char **lista) {
	char **atuais = g_strsplit(desktop_atual(), ":", -1);
	bool achou = false;
	for (char **a = atuais; *a; a++) {
		achou |= na_lista(lista, *a);
	}
	g_strfreev(atuais);
	return achou;
}

/* lê um .desktop; devolve falso se não é um app de autostart que se possa mostrar */
static bool ler(const char *caminho, struct autostart_item *it) {
	GKeyFile *kf = g_key_file_new();
	if (!g_key_file_load_from_file(kf, caminho, G_KEY_FILE_NONE, NULL)) {
		g_key_file_free(kf);
		return false;
	}
	const char *g = "Desktop Entry";
	char *tipo = g_key_file_get_string(kf, g, "Type", NULL);
	bool ok = !tipo || !strcmp(tipo, "Application");
	g_free(tipo);
	if (!ok) {
		g_key_file_free(kf);
		return false;
	}
	char *nome = g_key_file_get_locale_string(kf, g, "Name", NULL, NULL);
	char *exec = g_key_file_get_string(kf, g, "Exec", NULL);
	char *icone = g_key_file_get_string(kf, g, "Icon", NULL);
	g_strlcpy(it->nome, nome ? nome : "", sizeof(it->nome));
	g_strlcpy(it->comando, exec ? exec : "", sizeof(it->comando));
	g_strlcpy(it->icone, icone ? icone : "", sizeof(it->icone));
	g_free(nome);
	g_free(exec);
	g_free(icone);
	bool escondido = g_key_file_get_boolean(kf, g, "Hidden", NULL);
	bool gnome_desligado = g_key_file_has_key(kf, g, "X-GNOME-Autostart-enabled", NULL) && !g_key_file_get_boolean(kf, g, "X-GNOME-Autostart-enabled", NULL);
	char **so = g_key_file_get_string_list(kf, g, "OnlyShowIn", NULL, NULL);
	char **nao = g_key_file_get_string_list(kf, g, "NotShowIn", NULL, NULL);
	bool serve = (!so || desktop_em(so)) && !(nao && desktop_em(nao));
	g_strfreev(so);
	g_strfreev(nao);
	char *tentar = g_key_file_get_string(kf, g, "TryExec", NULL);
	if (tentar && *tentar) {
		char *achado = g_path_is_absolute(tentar) ? (g_file_test(tentar, G_FILE_TEST_IS_EXECUTABLE) ? g_strdup(tentar) : NULL) : g_find_program_in_path(tentar);
		serve = serve && achado;
		g_free(achado);
	}
	g_free(tentar);
	it->ligado = !escondido && !gnome_desligado && serve;
	if (!it->nome[0]) {
		g_strlcpy(it->nome, it->comando, sizeof(it->nome));
	}
	g_key_file_free(kf);
	return it->comando[0] != '\0';
}

static int por_nome(const void *a, const void *b) {
	return g_utf8_collate(((const struct autostart_item *)a)->nome, ((const struct autostart_item *)b)->nome);
}

int autostart_listar(struct autostart_item *lista, int max) {
	int n = 0;
	char *usuario = pasta_usuario();
	char **sistema = pastas_sistema();
	/* o do usuário primeiro (ele cobre o do sistema), e depois o que só o sistema tem */
	for (int fase = 0; fase < 2; fase++) {
		char *pastas_fase[2] = { usuario, NULL };
		char **pastas = fase == 0 ? pastas_fase : sistema;
		for (char **p = pastas; *p; p++) {
			GDir *dir = g_dir_open(*p, 0, NULL);
			const char *nome;
			while (dir && (nome = g_dir_read_name(dir)) && n < max) {
				if (!g_str_has_suffix(nome, ".desktop")) {
					continue;
				}
				int existente = -1;
				for (int i = 0; i < n; i++) {
					if (!strcmp(lista[i].arquivo, nome)) {
						existente = i;
					}
				}
				char *caminho = g_build_filename(*p, nome, NULL);
				if (existente >= 0) {
					if (fase == 1) {
						lista[existente].do_sistema = true;
					}
				} else {
					struct autostart_item it = { 0 };
					g_strlcpy(it.arquivo, nome, sizeof(it.arquivo));
					if (ler(caminho, &it)) {
						it.do_usuario = fase == 0;
						it.do_sistema = fase == 1;
						lista[n++] = it;
					}
				}
				g_free(caminho);
			}
			if (dir) {
				g_dir_close(dir);
			}
		}
	}
	g_free(usuario);
	g_strfreev(sistema);
	qsort(lista, (size_t)n, sizeof(lista[0]), por_nome);
	return n;
}

/* acha o .desktop de um nome (o do usuário, ou o do sistema): o caminho, para g_free */
static char *achar(const char *arquivo) {
	char *usuario = pasta_usuario();
	char *c = g_build_filename(usuario, arquivo, NULL);
	g_free(usuario);
	if (g_file_test(c, G_FILE_TEST_EXISTS)) {
		return c;
	}
	g_free(c);
	char **sistema = pastas_sistema();
	char *achado = NULL;
	for (char **p = sistema; *p && !achado; p++) {
		char *s = g_build_filename(*p, arquivo, NULL);
		if (g_file_test(s, G_FILE_TEST_EXISTS)) {
			achado = s;
		} else {
			g_free(s);
		}
	}
	g_strfreev(sistema);
	return achado;
}

static bool nome_valido(const char *arquivo) {
	return arquivo && *arquivo && !strchr(arquivo, '/') && g_str_has_suffix(arquivo, ".desktop") && arquivo[0] != '.';
}

bool autostart_definir(const char *arquivo, bool ligado) {
	if (!nome_valido(arquivo)) {
		return false;
	}
	char *origem = achar(arquivo);
	if (!origem) {
		return false;
	}
	GKeyFile *kf = g_key_file_new();
	bool ok = g_key_file_load_from_file(kf, origem, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	g_free(origem);
	if (!ok) {
		g_key_file_free(kf);
		return false;
	}
	g_key_file_set_boolean(kf, "Desktop Entry", "Hidden", !ligado);
	if (ligado) {
		g_key_file_remove_key(kf, "Desktop Entry", "X-GNOME-Autostart-enabled", NULL);
		g_key_file_remove_key(kf, "Desktop Entry", "OnlyShowIn", NULL); /* ligar à mão vale mesmo se o app dizia ser de outro desktop */
		g_key_file_remove_key(kf, "Desktop Entry", "NotShowIn", NULL);
	}
	char *pasta = pasta_usuario();
	g_mkdir_with_parents(pasta, 0755);
	char *destino = g_build_filename(pasta, arquivo, NULL);
	ok = g_key_file_save_to_file(kf, destino, NULL);
	g_free(destino);
	g_free(pasta);
	g_key_file_free(kf);
	return ok;
}

bool autostart_adicionar(const char *nome, const char *comando, const char *icone, char *arquivo, size_t tam) {
	if (!nome || !*nome || !comando || !*comando) {
		return false;
	}
	char *base = g_ascii_strdown(nome, -1);
	for (char *p = base; *p; p++) {
		if (!g_ascii_isalnum(*p)) {
			*p = '-';
		}
	}
	g_strstrip(base);
	if (!*base) {
		g_free(base);
		base = g_strdup("app");
	}
	char *pasta = pasta_usuario();
	g_mkdir_with_parents(pasta, 0755);
	char *nome_arq = NULL, *caminho = NULL;
	for (int i = 0; i < 100; i++) {
		g_free(nome_arq);
		g_free(caminho);
		nome_arq = i ? g_strdup_printf("%s-%d.desktop", base, i + 1) : g_strdup_printf("%s.desktop", base);
		caminho = g_build_filename(pasta, nome_arq, NULL);
		if (!g_file_test(caminho, G_FILE_TEST_EXISTS)) {
			break;
		}
	}
	GKeyFile *kf = g_key_file_new();
	g_key_file_set_string(kf, "Desktop Entry", "Type", "Application");
	g_key_file_set_string(kf, "Desktop Entry", "Name", nome);
	g_key_file_set_string(kf, "Desktop Entry", "Exec", comando);
	if (icone && *icone) {
		g_key_file_set_string(kf, "Desktop Entry", "Icon", icone);
	}
	g_key_file_set_boolean(kf, "Desktop Entry", "X-LUMNA-Autostart", true);
	bool ok = g_key_file_save_to_file(kf, caminho, NULL);
	if (ok && arquivo) {
		g_strlcpy(arquivo, nome_arq, tam);
	}
	g_key_file_free(kf);
	g_free(nome_arq);
	g_free(caminho);
	g_free(pasta);
	g_free(base);
	return ok;
}

bool autostart_remover(const char *arquivo) {
	if (!nome_valido(arquivo)) {
		return false;
	}
	char *pasta = pasta_usuario();
	char *meu = g_build_filename(pasta, arquivo, NULL);
	g_free(pasta);
	char **sistema = pastas_sistema();
	bool tem_do_sistema = false;
	for (char **p = sistema; *p; p++) {
		char *s = g_build_filename(*p, arquivo, NULL);
		tem_do_sistema |= g_file_test(s, G_FILE_TEST_EXISTS);
		g_free(s);
	}
	g_strfreev(sistema);
	bool ok = true;
	if (tem_do_sistema) {
		ok = autostart_definir(arquivo, false); /* o do sistema não se apaga: só se desliga */
	} else if (g_file_test(meu, G_FILE_TEST_EXISTS)) {
		ok = g_remove(meu) == 0;
	}
	g_free(meu);
	return ok;
}

int autostart_rodar(void) {
	static struct autostart_item lista[AUTOSTART_MAX];
	int n = autostart_listar(lista, AUTOSTART_MAX), abriu = 0;
	for (int i = 0; i < n; i++) {
		if (!lista[i].ligado) {
			continue;
		}
		char *caminho = achar(lista[i].arquivo);
		GDesktopAppInfo *info = caminho ? g_desktop_app_info_new_from_filename(caminho) : NULL;
		g_free(caminho);
		GError *erro = NULL;
		if (info && g_app_info_launch(G_APP_INFO(info), NULL, NULL, &erro)) {
			abriu++;
		} else {
			fprintf(stderr, "lumna-inicio: não abriu %s: %s\n", lista[i].arquivo, erro ? erro->message : "?");
		}
		g_clear_error(&erro);
		if (info) {
			g_object_unref(info);
		}
	}
	return abriu;
}
