#include "arquivos_prop.h"

#include <dirent.h>
#include <glib/gstdio.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *const AR_NIVEIS_PASTA[4] = { "Sem acesso", "Somente acessar", "Ler e acessar", "Ler, gravar e acessar" };
const char *const AR_NIVEIS_ARQ[3] = { "Sem acesso", "Somente leitura", "Ler e gravar" };

int ar_nivel_pasta_de(int rwx) {
	switch (rwx & 7) {
	case 0: return AR_NP_NENHUM;
	case 1: return AR_NP_ACESSAR;
	case 5: return AR_NP_LER_ACESSAR;
	case 7: return AR_NP_TUDO;
	default: return AR_NP_OUTRO;
	}
}

int ar_rwx_da_pasta(int nivel) {
	static const int bits[4] = { 0, 1, 5, 7 };
	return nivel >= 0 && nivel < 4 ? bits[nivel] : 0;
}

int ar_nivel_arq_de(int rwx) {
	switch (rwx & 6) {
	case 0: return AR_NA_NENHUM;
	case 4: return AR_NA_LER;
	case 6: return AR_NA_LER_GRAVAR;
	default: return AR_NA_OUTRO; /* só escrever */
	}
}

int ar_rwx_do_arq(int nivel) {
	static const int bits[3] = { 0, 4, 6 };
	return nivel >= 0 && nivel < 3 ? bits[nivel] : 0;
}

static void grupo_adicionar(ar_grupo *g, int *n, int max, gid_t gid) {
	for (int i = 0; i < *n; i++) {
		if (g[i].gid == gid) {
			return;
		}
	}
	if (*n >= max) {
		return;
	}
	struct group *gr = getgrgid(gid);
	g[*n].gid = gid;
	if (gr) {
		snprintf(g[*n].nome, sizeof(g[*n].nome), "%s", gr->gr_name);
	} else {
		snprintf(g[*n].nome, sizeof(g[*n].nome), "%u", (unsigned)gid);
	}
	(*n)++;
}

int ar_grupos_da_pessoa(ar_grupo *g, int max, gid_t atual) {
	int n = 0;
	grupo_adicionar(g, &n, max, atual); /* o do arquivo primeiro: é o que está escolhido */
	grupo_adicionar(g, &n, max, getegid());
	gid_t outros[64];
	int k = getgroups(64, outros);
	for (int i = 0; i < k; i++) {
		grupo_adicionar(g, &n, max, outros[i]);
	}
	return n;
}

static bool aplicar_um(const char *caminho, mode_t modo, bool trocar_grupo, gid_t gid, bool eh_arquivo_recursivo, ar_prop_resultado *r) {
	struct stat st;
	if (lstat(caminho, &st) != 0 || S_ISLNK(st.st_mode)) {
		return false;
	}
	mode_t novo = modo & 07777;
	if (eh_arquivo_recursivo && !S_ISDIR(st.st_mode)) {
		novo = (mode_t)((st.st_mode & 07111) | (modo & 0666));
	}
	bool ok = true;
	if ((st.st_mode & 07777) != novo) {
		ok = chmod(caminho, novo) == 0;
	}
	if (ok && trocar_grupo && st.st_gid != gid) {
		ok = chown(caminho, (uid_t)-1, gid) == 0;
	}
	if (ok) {
		r->alterados++;
	} else {
		r->erros++;
	}
	return ok;
}

static void aplicar_dentro(const char *pasta, mode_t modo, bool trocar_grupo, gid_t gid, ar_prop_resultado *r) {
	GDir *d = g_dir_open(pasta, 0, NULL);
	if (!d) {
		r->erros++;
		return;
	}
	const char *nome;
	while ((nome = g_dir_read_name(d))) {
		char *c = g_build_filename(pasta, nome, NULL);
		struct stat st;
		if (lstat(c, &st) == 0 && !S_ISLNK(st.st_mode)) {
			aplicar_um(c, modo, trocar_grupo, gid, true, r);
			if (S_ISDIR(st.st_mode)) {
				aplicar_dentro(c, modo, trocar_grupo, gid, r);
			}
		}
		g_free(c);
	}
	g_dir_close(d);
}

bool ar_prop_aplicar(const char *caminho, mode_t modo, bool trocar_grupo, gid_t gid, bool recursivo, ar_prop_resultado *r) {
	ar_prop_resultado local = { 0, 0 };
	if (!r) {
		r = &local;
	}
	r->alterados = 0;
	r->erros = 0;
	struct stat st;
	if (lstat(caminho, &st) != 0) {
		r->erros++;
		return false;
	}
	/* a pasta antes de entrar nela: tirar o x dela depois impediria de mudar o que há dentro, e o contrário (dar o x) tem de vir primeiro */
	aplicar_um(caminho, modo, trocar_grupo, gid, false, r);
	if (recursivo && S_ISDIR(st.st_mode)) {
		aplicar_dentro(caminho, modo, trocar_grupo, gid, r);
	}
	return r->erros == 0;
}

/* ---- .desktop ---- */

static bool saida_do_comando(char **argv, char *saida, size_t tam) {
	gchar *out = NULL;
	gint status = 0;
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &status, NULL) || status != 0 || !out) {
		g_free(out);
		return false;
	}
	snprintf(saida, tam, "%s", g_strstrip(out));
	g_free(out);
	return saida[0] != '\0';
}

bool ar_prop_desktop(const char *caminho, ar_prop_app *a) {
	memset(a, 0, sizeof(*a));
	GKeyFile *kf = g_key_file_new();
	if (!g_key_file_load_from_file(kf, caminho, G_KEY_FILE_NONE, NULL) || !g_key_file_has_group(kf, "Desktop Entry")) {
		g_key_file_free(kf);
		return false;
	}
	const char *chaves[] = { "Name", "Comment", "Icon", "Exec", "Version" };
	char *dest[] = { a->nome, a->comentario, a->icone, a->exec, a->versao };
	size_t tams[] = { sizeof(a->nome), sizeof(a->comentario), sizeof(a->icone), sizeof(a->exec), sizeof(a->versao) };
	for (int i = 0; i < 5; i++) {
		char *v = g_key_file_get_locale_string(kf, "Desktop Entry", chaves[i], NULL, NULL);
		if (v) {
			snprintf(dest[i], tams[i], "%s", v);
			g_free(v);
		}
	}
	g_key_file_free(kf);
	/* o programa: a primeira palavra do Exec= (sem %U e afins) */
	char *copia = g_strdup(a->exec);
	char *esp = copia ? strchr(copia, ' ') : NULL;
	if (esp) {
		*esp = '\0';
	}
	if (copia && copia[0]) {
		char *achado = copia[0] == '/' ? g_strdup(copia) : g_find_program_in_path(copia);
		if (achado) {
			snprintf(a->executavel, sizeof(a->executavel), "%s", achado);
			g_free(achado);
		}
	}
	g_free(copia);
	/* de que pacote vem (pacman, se houver): o pacote do .desktop, a versão e o repositório dele */
	char pkg[128] = "";
	char *cmd1[] = { "pacman", "-Qoq", (char *)caminho, NULL };
	if (saida_do_comando(cmd1, pkg, sizeof(pkg))) {
		snprintf(a->pacote, sizeof(a->pacote), "%s", pkg);
		char qv[256] = "";
		char *cmd2[] = { "pacman", "-Q", pkg, NULL };
		if (saida_do_comando(cmd2, qv, sizeof(qv))) {
			char *esp2 = strchr(qv, ' ');
			if (esp2 && !a->versao[0]) {
				snprintf(a->versao, sizeof(a->versao), "%s", esp2 + 1);
			}
		}
		char si[2048] = "";
		char *cmd3[] = { "pacman", "-Si", pkg, NULL };
		if (saida_do_comando(cmd3, si, sizeof(si))) {
			char *rp = strstr(si, "Repository");
			if (rp) {
				char *dp = strchr(rp, ':');
				if (dp) {
					char *fim = strchr(dp, '\n');
					if (fim) {
						*fim = '\0';
					}
					snprintf(a->origem, sizeof(a->origem), "Repositório %s", g_strstrip(dp + 1));
				}
			}
		}
	}
	return true;
}
