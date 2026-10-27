#define _GNU_SOURCE
#include "pacotes.h"

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

struct nopac {
	struct pacote p;
	GPtrArray *deps;   /* nomes de que depende (sem versão) */
	GPtrArray *opt;    /* nomes dos opcionais */
	GPtrArray *prov;   /* o que ele fornece (inclui o próprio nome) */
	GArray *req;       /* índices de quem depende dele */
	bool app;
};

struct pacotes_bd {
	struct nopac *v;
	int n;
	GHashTable *por_nome;     /* nome -> índice+1 */
	GHashTable *quem_provem;  /* nome fornecido -> GArray de índices */
	const struct pacote **apps;
	int napps;
};

static const char *banco(void) {
	const char *e = g_getenv("LUMNA_PACMAN_DB");
	return e && *e ? e : "/var/lib/pacman/local";
}

static char *na_raiz(const char *relativo) {
	const char *r = g_getenv("LUMNA_PACMAN_RAIZ");
	return g_strdup_printf("%s/%s", r && *r ? r : "", relativo);
}

/* "glibc>=2.35" → "glibc"; "foo: para quê" → "foo" */
static char *so_nome(const char *dep) {
	size_t n = strcspn(dep, "<>=: \t");
	return g_strndup(dep, n);
}

/* as linhas de uma seção "%CHAVE%" (até a linha em branco) */
static GPtrArray *secao(const char *texto, const char *chave) {
	GPtrArray *r = g_ptr_array_new_with_free_func(g_free);
	char *marca = g_strdup_printf("\n%%%s%%\n", chave);
	const char *p = strstr(texto, marca + 1) == texto ? texto : strstr(texto, marca);
	g_free(marca);
	if (!p) {
		return r;
	}
	p = strchr(p + (p == texto ? 0 : 1), '\n');
	while (p && *p == '\n') {
		p++;
		const char *fim = strchr(p, '\n');
		size_t len = fim ? (size_t)(fim - p) : strlen(p);
		if (len == 0) {
			break;
		}
		g_ptr_array_add(r, g_strndup(p, len));
		p = fim;
	}
	return r;
}

static void primeira(const char *texto, const char *chave, char *saida, size_t tam) {
	GPtrArray *l = secao(texto, chave);
	if (l->len) {
		g_strlcpy(saida, l->pdata[0], tam);
	}
	g_ptr_array_free(l, TRUE);
}

static int64_t primeira_num(const char *texto, const char *chave) {
	char b[32] = "";
	primeira(texto, chave, b, sizeof(b));
	return (int64_t)atoll(b);
}

/* ---- .desktop ---- */

struct desktop {
	char nome[96], exec[256], icone[128];
	bool visivel;
};

static bool ler_desktop(const char *caminho, struct desktop *d) {
	memset(d, 0, sizeof(*d));
	char *t = NULL;
	if (!g_file_get_contents(caminho, &t, NULL, NULL)) {
		return false;
	}
	bool entrada = false, app = false, escondido = false;
	char **linhas = g_strsplit(t, "\n", -1);
	for (char **l = linhas; *l; l++) {
		char *s = g_strstrip(*l);
		if (*s == '[') {
			entrada = !strcmp(s, "[Desktop Entry]");
			continue;
		}
		char *igual = strchr(s, '=');
		if (!entrada || !igual) {
			continue;
		}
		*igual = '\0';
		const char *k = g_strstrip(s), *v = g_strstrip(igual + 1);
		if (!strcmp(k, "Name")) {
			g_strlcpy(d->nome, v, sizeof(d->nome));
		} else if (!strcmp(k, "Exec")) {
			g_strlcpy(d->exec, v, sizeof(d->exec));
		} else if (!strcmp(k, "Icon")) {
			g_strlcpy(d->icone, v, sizeof(d->icone));
		} else if (!strcmp(k, "Type")) {
			app = !strcmp(v, "Application");
		} else if (!strcmp(k, "NoDisplay") || !strcmp(k, "Hidden")) {
			escondido = escondido || !strcmp(v, "true");
		}
	}
	g_strfreev(linhas);
	g_free(t);
	d->visivel = app && !escondido && d->nome[0];
	return true;
}

static char *dir_apps_do_usuario(void) {
	return g_build_filename(g_get_user_data_dir(), "applications", NULL);
}

/* o .desktop do usuário que cobre este aplicativo (o do Painel), se existir */
static bool desativado_pelo_painel(const char *app_id) {
	char *dir = dir_apps_do_usuario(), *c = g_build_filename(dir, app_id, NULL), *t = NULL;
	bool r = g_file_get_contents(c, &t, NULL, NULL) && strstr(t, "X-LUMNA-Desativado=true") != NULL;
	g_free(t);
	g_free(c);
	g_free(dir);
	return r;
}

/* ---- um pacote ---- */

static void instalar_dir(struct nopac *n, GPtrArray *arquivos) {
	const char *nome = n->p.nome;
	char *lib = g_strdup_printf("usr/lib/%s/", nome), *share = g_strdup_printf("usr/share/%s/", nome);
	const char *achado = NULL;
	char opt[96] = "";
	for (guint i = 0; i < arquivos->len; i++) {
		const char *a = arquivos->pdata[i];
		if (!strncmp(a, "opt/", 4) && a[4] && !opt[0]) {
			size_t seg = strcspn(a + 4, "/");
			snprintf(opt, sizeof(opt), "/opt/%.*s", (int)seg, a + 4);
		}
		if (!achado && (!strcmp(a, lib) || !strcmp(a, share))) {
			achado = a;
		}
	}
	if (opt[0]) {
		g_strlcpy(n->p.dir, opt, sizeof(n->p.dir));
	} else if (achado) {
		snprintf(n->p.dir, sizeof(n->p.dir), "/%.*s", (int)strlen(achado) - 1, achado);
	} else {
		g_strlcpy(n->p.dir, "/usr (programas em /usr/bin)", sizeof(n->p.dir));
	}
	g_free(lib);
	g_free(share);
}

static void ler_pacote(struct pacotes_bd *bd, const char *pasta) {
	char *desc_c = g_build_filename(pasta, "desc", NULL), *desc = NULL;
	if (!g_file_get_contents(desc_c, &desc, NULL, NULL)) {
		g_free(desc_c);
		return;
	}
	g_free(desc_c);
	bd->v = g_renew(struct nopac, bd->v, (size_t)bd->n + 1);
	struct nopac *n = &bd->v[bd->n++];
	memset(n, 0, sizeof(*n));
	struct pacote *p = &n->p;
	primeira(desc, "NAME", p->nome, sizeof(p->nome));
	primeira(desc, "VERSION", p->versao, sizeof(p->versao));
	primeira(desc, "DESC", p->descricao, sizeof(p->descricao));
	primeira(desc, "URL", p->url, sizeof(p->url));
	primeira(desc, "PACKAGER", p->empacotador, sizeof(p->empacotador));
	GPtrArray *lic = secao(desc, "LICENSE");
	for (guint i = 0; i < lic->len; i++) {
		char *s = g_strdup_printf("%s%s", p->licenca[0] ? ", " : "", (char *)lic->pdata[i]);
		g_strlcat(p->licenca, s, sizeof(p->licenca));
		g_free(s);
	}
	g_ptr_array_free(lic, TRUE);
	p->tamanho = primeira_num(desc, "SIZE");
	p->instalado_em = primeira_num(desc, "INSTALLDATE");
	p->compilado_em = primeira_num(desc, "BUILDDATE");
	p->dependencia = primeira_num(desc, "REASON") == 1;
	n->deps = g_ptr_array_new_with_free_func(g_free);
	n->opt = g_ptr_array_new_with_free_func(g_free);
	n->prov = g_ptr_array_new_with_free_func(g_free);
	n->req = g_array_new(FALSE, FALSE, sizeof(int));
	GPtrArray *l = secao(desc, "DEPENDS");
	for (guint i = 0; i < l->len; i++) {
		g_ptr_array_add(n->deps, so_nome(l->pdata[i]));
	}
	g_ptr_array_free(l, TRUE);
	l = secao(desc, "OPTDEPENDS");
	for (guint i = 0; i < l->len; i++) {
		g_ptr_array_add(n->opt, so_nome(l->pdata[i]));
	}
	g_ptr_array_free(l, TRUE);
	g_ptr_array_add(n->prov, g_strdup(p->nome));
	l = secao(desc, "PROVIDES");
	for (guint i = 0; i < l->len; i++) {
		g_ptr_array_add(n->prov, so_nome(l->pdata[i]));
	}
	g_ptr_array_free(l, TRUE);
	g_free(desc);

	/* os arquivos: o .desktop, a pasta de instalação e a data do mais novo */
	char *arq_c = g_build_filename(pasta, "files", NULL), *arq = NULL;
	if (g_file_get_contents(arq_c, &arq, NULL, NULL)) {
		GPtrArray *arquivos = secao(arq, "FILES");
		int amostra = 0;
		for (guint i = 0; i < arquivos->len; i++) {
			const char *a = arquivos->pdata[i];
			size_t len = strlen(a);
			if (!p->app_id[0] && !strncmp(a, "usr/share/applications/", 23) && len > 31 && !strcmp(a + len - 8, ".desktop")) {
				char *c = na_raiz(a);
				struct desktop d;
				if (ler_desktop(c, &d) && d.visivel) {
					g_strlcpy(p->app_id, a + 23, sizeof(p->app_id));
					g_strlcpy(p->app_nome, d.nome, sizeof(p->app_nome));
					g_strlcpy(p->exec, d.exec, sizeof(p->exec));
					g_strlcpy(p->icone, d.icone, sizeof(p->icone));
					n->app = true;
				}
				g_free(c);
			}
			if (len && a[len - 1] != '/' && amostra < 400) {
				amostra++;
				char *c = na_raiz(a);
				struct stat st;
				if (lstat(c, &st) == 0 && (int64_t)st.st_mtime > p->modificado_em) {
					p->modificado_em = (int64_t)st.st_mtime;
				}
				g_free(c);
			}
		}
		instalar_dir(n, arquivos);
		g_ptr_array_free(arquivos, TRUE);
		g_free(arq);
	}
	g_free(arq_c);
	if (!p->modificado_em) {
		p->modificado_em = p->instalado_em;
	}
	g_strlcpy(p->fonte, "pacman", sizeof(p->fonte));
	if (n->app) {
		p->desativado = desativado_pelo_painel(p->app_id);
	}
}

/* ---- AppImages ---- */

static int por_nome_apps(gconstpointer a, gconstpointer b) {
	const struct pacote *x = *(const struct pacote *const *)a, *y = *(const struct pacote *const *)b;
	return g_utf8_collate(x->app_nome[0] ? x->app_nome : x->nome, y->app_nome[0] ? y->app_nome : y->nome);
}

static void acrescentar_appimage(struct pacotes_bd *bd, const char *caminho, const struct desktop *d, const char *app_id) {
	for (int i = 0; i < bd->n; i++) {
		if (bd->v[i].p.origem == PAC_APPIMAGE && !strcmp(bd->v[i].p.caminho, caminho)) {
			if (d && !bd->v[i].p.app_id[0]) {
				g_strlcpy(bd->v[i].p.app_id, app_id, sizeof(bd->v[i].p.app_id));
				g_strlcpy(bd->v[i].p.app_nome, d->nome, sizeof(bd->v[i].p.app_nome));
				g_strlcpy(bd->v[i].p.icone, d->icone, sizeof(bd->v[i].p.icone));
			}
			return;
		}
	}
	struct stat st;
	if (stat(caminho, &st) != 0 || !S_ISREG(st.st_mode)) {
		return;
	}
	bd->v = g_renew(struct nopac, bd->v, (size_t)bd->n + 1);
	struct nopac *n = &bd->v[bd->n++];
	memset(n, 0, sizeof(*n));
	n->deps = g_ptr_array_new_with_free_func(g_free);
	n->opt = g_ptr_array_new_with_free_func(g_free);
	n->prov = g_ptr_array_new_with_free_func(g_free);
	n->req = g_array_new(FALSE, FALSE, sizeof(int));
	struct pacote *p = &n->p;
	p->origem = PAC_APPIMAGE;
	g_strlcpy(p->caminho, caminho, sizeof(p->caminho));
	g_strlcpy(p->nome, caminho, sizeof(p->nome));
	char *base = g_path_get_basename(caminho);
	char *sem = g_strdup(base);
	char *ponto = strcasestr(sem, ".appimage");
	if (ponto) {
		*ponto = '\0';
	}
	g_strdelimit(sem, "_", ' ');
	g_strlcpy(p->app_nome, d && d->nome[0] ? d->nome : sem, sizeof(p->app_nome));
	if (d) {
		g_strlcpy(p->app_id, app_id, sizeof(p->app_id));
		g_strlcpy(p->icone, d->icone, sizeof(p->icone));
		p->desativado = desativado_pelo_painel(app_id);
	}
	g_strlcpy(p->exec, caminho, sizeof(p->exec));
	p->tamanho = (int64_t)st.st_size;
	p->instalado_em = p->modificado_em = (int64_t)st.st_mtime;
	char *dir = g_path_get_dirname(caminho);
	g_strlcpy(p->dir, dir, sizeof(p->dir));
	g_strlcpy(p->fonte, "AppImage", sizeof(p->fonte));
	g_strlcpy(p->descricao, "Um arquivo AppImage: roda sem instalar, fora do pacman.", sizeof(p->descricao));
	g_free(dir);
	g_free(sem);
	g_free(base);
	n->app = true;
}

static void procurar_appimages(struct pacotes_bd *bd) {
	const char *home = g_get_home_dir();
	char *dirs[] = { g_build_filename(home, "Applications", NULL), g_build_filename(home, "AppImages", NULL), g_build_filename(home, "Apps", NULL),
		g_build_filename(home, ".local", "bin", NULL), g_build_filename(home, ".local", "share", "AppImages", NULL),
		g_build_filename(home, "Downloads", NULL), g_strdup("/opt") };
	for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		DIR *d = opendir(dirs[i]);
		struct dirent *e;
		while (d && (e = readdir(d))) {
			size_t n = strlen(e->d_name);
			if (n > 9 && !strcasecmp(e->d_name + n - 9, ".appimage")) {
				char *c = g_build_filename(dirs[i], e->d_name, NULL);
				acrescentar_appimage(bd, c, NULL, NULL);
				g_free(c);
			}
		}
		if (d) {
			closedir(d);
		}
		g_free(dirs[i]);
	}
	/* os integrados (AppImageLauncher, Gear Lever, à mão): um .desktop do usuário que roda um .AppImage */
	char *apps = dir_apps_do_usuario();
	DIR *d = opendir(apps);
	struct dirent *e;
	while (d && (e = readdir(d))) {
		size_t n = strlen(e->d_name);
		if (n < 9 || strcmp(e->d_name + n - 8, ".desktop") != 0) {
			continue;
		}
		char *c = g_build_filename(apps, e->d_name, NULL);
		struct desktop dk;
		char *t = NULL;
		if (g_file_get_contents(c, &t, NULL, NULL) && strcasestr(t, ".appimage") && ler_desktop(c, &dk)) {
			/* o primeiro argumento do Exec, com ou sem aspas */
			char *exec = g_strdup(dk.exec);
			char *ini = exec[0] == '"' ? exec + 1 : exec;
			char *fim = exec[0] == '"' ? strchr(ini, '"') : strchr(ini, ' ');
			if (fim) {
				*fim = '\0';
			}
			if (strcasestr(ini, ".appimage") && !strstr(t, "X-LUMNA-Criado=true")) {
				dk.visivel = true;
				acrescentar_appimage(bd, ini, &dk, e->d_name);
			}
			g_free(exec);
		}
		g_free(t);
		g_free(c);
	}
	if (d) {
		closedir(d);
	}
	g_free(apps);
}

/* ---- o banco inteiro ---- */

static void montar_grafo(struct pacotes_bd *bd) {
	bd->por_nome = g_hash_table_new(g_str_hash, g_str_equal);
	bd->quem_provem = g_hash_table_new(g_str_hash, g_str_equal);
	for (int i = 0; i < bd->n; i++) {
		if (bd->v[i].p.origem == PAC_APPIMAGE) {
			continue;
		}
		g_hash_table_insert(bd->por_nome, bd->v[i].p.nome, GINT_TO_POINTER(i + 1));
		for (guint k = 0; k < bd->v[i].prov->len; k++) {
			const char *pv = bd->v[i].prov->pdata[k];
			GArray *l = g_hash_table_lookup(bd->quem_provem, pv);
			if (!l) {
				l = g_array_new(FALSE, FALSE, sizeof(int));
				g_hash_table_insert(bd->quem_provem, (gpointer)pv, l);
			}
			g_array_append_val(l, i);
		}
	}
	for (int i = 0; i < bd->n; i++) {
		for (guint k = 0; k < bd->v[i].deps->len; k++) {
			GArray *l = g_hash_table_lookup(bd->quem_provem, bd->v[i].deps->pdata[k]);
			for (guint m = 0; l && m < l->len; m++) {
				int alvo = g_array_index(l, int, m);
				if (alvo != i) {
					g_array_append_val(bd->v[alvo].req, i);
				}
			}
		}
	}
}

static void marcar_origens(struct pacotes_bd *bd) {
	char *argv[] = { "pacman", "-Qmq", NULL };
	char *saida = NULL;
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL) || !saida) {
		g_free(saida);
		return;
	}
	GHashTable *de_fora = g_hash_table_new(g_str_hash, g_str_equal);
	char **linhas = g_strsplit(saida, "\n", -1);
	for (char **l = linhas; *l; l++) {
		if (**l) {
			g_hash_table_add(de_fora, *l);
		}
	}
	char *cache = (char *)g_get_user_cache_dir();
	for (int i = 0; i < bd->n; i++) {
		struct pacote *p = &bd->v[i].p;
		if (p->origem == PAC_APPIMAGE || !g_hash_table_contains(de_fora, p->nome)) {
			continue;
		}
		p->origem = PAC_AUR;
		char *yay = g_build_filename(cache, "yay", p->nome, NULL), *paru = g_build_filename(cache, "paru", "clone", p->nome, NULL);
		g_strlcpy(p->fonte, g_file_test(yay, G_FILE_TEST_IS_DIR) ? "AUR (yay)" : g_file_test(paru, G_FILE_TEST_IS_DIR) ? "AUR (paru)" : "AUR ou compilado à mão", sizeof(p->fonte));
		g_free(yay);
		g_free(paru);
	}
	g_strfreev(linhas);
	g_hash_table_destroy(de_fora);
	g_free(saida);
}

struct pacotes_bd *pacotes_carregar(void) {
	struct pacotes_bd *bd = g_new0(struct pacotes_bd, 1);
	DIR *d = opendir(banco());
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.' || !strcmp(e->d_name, "ALPM_DB_VERSION")) {
			continue;
		}
		char *pasta = g_build_filename(banco(), e->d_name, NULL);
		ler_pacote(bd, pasta);
		g_free(pasta);
	}
	if (d) {
		closedir(d);
	}
	montar_grafo(bd);
	marcar_origens(bd);
	procurar_appimages(bd);
	/* a lista de aplicativos (ponteiros estáveis: o vetor não cresce mais) */
	GPtrArray *apps = g_ptr_array_new();
	for (int i = 0; i < bd->n; i++) {
		if (bd->v[i].app) {
			g_ptr_array_add(apps, &bd->v[i].p);
		}
	}
	g_ptr_array_sort(apps, por_nome_apps);
	bd->napps = (int)apps->len;
	bd->apps = (const struct pacote **)g_ptr_array_free(apps, FALSE);
	if (!bd->n) {
		pacotes_liberar(bd);
		return NULL;
	}
	return bd;
}

void pacotes_liberar(struct pacotes_bd *bd) {
	if (!bd) {
		return;
	}
	for (int i = 0; i < bd->n; i++) {
		g_ptr_array_free(bd->v[i].deps, TRUE);
		g_ptr_array_free(bd->v[i].opt, TRUE);
		g_ptr_array_free(bd->v[i].prov, TRUE);
		g_array_free(bd->v[i].req, TRUE);
	}
	if (bd->quem_provem) {
		GHashTableIter it;
		gpointer k, v;
		g_hash_table_iter_init(&it, bd->quem_provem);
		while (g_hash_table_iter_next(&it, &k, &v)) {
			g_array_free(v, TRUE);
		}
		g_hash_table_destroy(bd->quem_provem);
	}
	if (bd->por_nome) {
		g_hash_table_destroy(bd->por_nome);
	}
	g_free(bd->v);
	g_free(bd->apps);
	g_free(bd);
}

int pacotes_apps(const struct pacotes_bd *bd, const struct pacote ***lista) {
	*lista = bd->apps;
	return bd->napps;
}

const struct pacote *pacotes_achar(const struct pacotes_bd *bd, const char *nome) {
	gpointer i = g_hash_table_lookup(bd->por_nome, nome);
	if (i) {
		return &bd->v[GPOINTER_TO_INT(i) - 1].p;
	}
	for (int k = 0; k < bd->n; k++) {
		if (bd->v[k].p.origem == PAC_APPIMAGE && !strcmp(bd->v[k].p.caminho, nome)) {
			return &bd->v[k].p;
		}
	}
	return NULL;
}

int64_t pacotes_apps_tamanho(const struct pacotes_bd *bd) {
	int64_t t = 0;
	for (int i = 0; i < bd->napps; i++) {
		t += bd->apps[i]->tamanho;
	}
	return t;
}

static struct nopac *no_de(const struct pacotes_bd *bd, const struct pacote *p) {
	return (struct nopac *)((char *)p - offsetof(struct nopac, p));
}

static int indice(const struct pacotes_bd *bd, const struct nopac *n) {
	return (int)(n - bd->v);
}

/* os pacotes (nomes) a que cada dependência de n leva */
GPtrArray *pacotes_depende_de(const struct pacotes_bd *bd, const struct pacote *p) {
	GPtrArray *r = g_ptr_array_new_with_free_func(g_free);
	const struct nopac *n = no_de(bd, p);
	for (guint k = 0; k < n->deps->len; k++) {
		GArray *l = g_hash_table_lookup(bd->quem_provem, n->deps->pdata[k]);
		if (l && l->len) {
			g_ptr_array_add(r, g_strdup(bd->v[g_array_index(l, int, 0)].p.nome));
		} else {
			g_ptr_array_add(r, g_strdup(n->deps->pdata[k]));
		}
	}
	return r;
}

GPtrArray *pacotes_necessario_por(const struct pacotes_bd *bd, const struct pacote *p) {
	GPtrArray *r = g_ptr_array_new_with_free_func(g_free);
	const struct nopac *n = no_de(bd, p);
	for (guint k = 0; k < n->req->len; k++) {
		const char *nome = bd->v[g_array_index(n->req, int, k)].p.nome;
		bool ja = false;
		for (guint m = 0; m < r->len; m++) {
			ja = ja || !strcmp(r->pdata[m], nome);
		}
		if (!ja) {
			g_ptr_array_add(r, g_strdup(nome));
		}
	}
	return r;
}

GPtrArray *pacotes_opcional_para(const struct pacotes_bd *bd, const struct pacote *p) {
	GPtrArray *r = g_ptr_array_new_with_free_func(g_free);
	const struct nopac *alvo = no_de(bd, p);
	for (int i = 0; i < bd->n; i++) {
		for (guint k = 0; k < bd->v[i].opt->len; k++) {
			for (guint m = 0; m < alvo->prov->len; m++) {
				if (!strcmp(bd->v[i].opt->pdata[k], alvo->prov->pdata[m])) {
					g_ptr_array_add(r, g_strdup(bd->v[i].p.nome));
					k = bd->v[i].opt->len;
					break;
				}
			}
		}
	}
	return r;
}

/* ---- a prévia da remoção ---- */

void pacotes_previa_remocao(const struct pacotes_bd *bd, const struct pacote *p, bool cascata, struct pacotes_remocao *r) {
	r->removidos = g_ptr_array_new_with_free_func(g_free);
	r->quebrariam = pacotes_necessario_por(bd, p);
	r->liberado = 0;
	if (p->origem == PAC_APPIMAGE) {
		g_ptr_array_add(r->removidos, g_strdup(p->nome));
		r->liberado = p->tamanho;
		return;
	}
	bool *fora = g_new0(bool, bd->n);
	int alvo = indice(bd, no_de(bd, p));
	GQueue fila = G_QUEUE_INIT;
	fora[alvo] = true;
	g_queue_push_tail(&fila, GINT_TO_POINTER(alvo + 1));
	/* -c: quem depende do alvo vai junto, e quem depende deles... */
	while (cascata && !g_queue_is_empty(&fila)) {
		int i = GPOINTER_TO_INT(g_queue_pop_head(&fila)) - 1;
		for (guint k = 0; k < bd->v[i].req->len; k++) {
			int q = g_array_index(bd->v[i].req, int, k);
			if (!fora[q]) {
				fora[q] = true;
				g_queue_push_tail(&fila, GINT_TO_POINTER(q + 1));
			}
		}
	}
	/* -s: as dependências instaladas como dependência que ninguém de fora do conjunto usa mais */
	bool mudou = true;
	while (mudou) {
		mudou = false;
		for (int i = 0; i < bd->n; i++) {
			if (!fora[i]) {
				continue;
			}
			for (guint k = 0; k < bd->v[i].deps->len; k++) {
				GArray *l = g_hash_table_lookup(bd->quem_provem, bd->v[i].deps->pdata[k]);
				for (guint m = 0; l && m < l->len; m++) {
					int d = g_array_index(l, int, m);
					if (fora[d] || !bd->v[d].p.dependencia) {
						continue;
					}
					bool livre = true;
					for (guint u = 0; u < bd->v[d].req->len && livre; u++) {
						livre = fora[g_array_index(bd->v[d].req, int, u)];
					}
					if (livre) {
						fora[d] = true;
						mudou = true;
					}
				}
			}
		}
	}
	g_ptr_array_add(r->removidos, g_strdup(p->nome));
	r->liberado = p->tamanho;
	for (int i = 0; i < bd->n; i++) {
		if (fora[i] && i != alvo) {
			g_ptr_array_add(r->removidos, g_strdup(bd->v[i].p.nome));
			r->liberado += bd->v[i].p.tamanho;
		}
	}
	g_free(fora);
	g_queue_clear(&fila);
}

void pacotes_remocao_liberar(struct pacotes_remocao *r) {
	if (r->removidos) {
		g_ptr_array_free(r->removidos, TRUE);
	}
	if (r->quebrariam) {
		g_ptr_array_free(r->quebrariam, TRUE);
	}
	r->removidos = r->quebrariam = NULL;
}

void pacotes_marcar_atualizacao(struct pacotes_bd *bd, const char *nome, const char *nova) {
	gpointer i = g_hash_table_lookup(bd->por_nome, nome);
	if (i) {
		struct pacote *p = &bd->v[GPOINTER_TO_INT(i) - 1].p;
		p->tem_atualizacao = true;
		g_strlcpy(p->nova_versao, nova, sizeof(p->nova_versao));
	}
}

/* ---- ações ---- */

static int rodar(char **argv, char *erro, size_t tam) {
	char *err = NULL;
	int estado = 0;
	GError *ge = NULL;
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &err, &estado, &ge)) {
		g_strlcpy(erro, ge ? ge->message : "não consegui rodar", tam);
		g_clear_error(&ge);
		g_free(err);
		return -1;
	}
	int codigo = WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
	if (codigo != 0 && err) {
		/* o fim do texto é o que importa ("error: failed to prepare transaction (could not satisfy dependencies)") */
		const char *p = err;
		size_t len = strlen(err);
		if (len > tam - 1) {
			p = err + len - (tam - 1);
		}
		g_strlcpy(erro, g_strstrip((char *)p), tam);
	}
	g_free(err);
	return codigo;
}

int pacotes_remover(const struct pacote *p, bool cascata, char *erro, size_t tam) {
	erro[0] = '\0';
	/* o pkexec pede a senha numa janela; em teste ($LUMNA_SEM_PKEXEC) o pacman falso roda direto */
	const char *sem = g_getenv("LUMNA_SEM_PKEXEC");
	char *argv_pk[] = { "pkexec", "pacman", cascata ? "-Rcns" : "-Rns", "--noconfirm", (char *)p->nome, NULL };
	char *argv_sem[] = { "pacman", cascata ? "-Rcns" : "-Rns", "--noconfirm", (char *)p->nome, NULL };
	return rodar(sem && *sem ? argv_sem : argv_pk, erro, tam);
}

int pacotes_remover_appimage(const struct pacote *p, char *erro, size_t tam) {
	erro[0] = '\0';
	if (p->origem != PAC_APPIMAGE || !p->caminho[0]) {
		g_strlcpy(erro, "isto não é um AppImage", tam);
		return -1;
	}
	if (unlink(p->caminho) != 0) {
		g_strlcpy(erro, g_strerror(errno), tam);
		return -1;
	}
	if (p->app_id[0]) {
		char *dir = dir_apps_do_usuario(), *c = g_build_filename(dir, p->app_id, NULL);
		unlink(c);
		g_free(c);
		g_free(dir);
	}
	return 0;
}

bool pacotes_definir_ativo(const struct pacote *p, bool ativo) {
	if (!p->app_id[0]) {
		return false;
	}
	char *dir = dir_apps_do_usuario();
	g_mkdir_with_parents(dir, 0755);
	char *c = g_build_filename(dir, p->app_id, NULL), *t = NULL;
	bool existe = g_file_get_contents(c, &t, NULL, NULL);
	bool ok = false;
	if (!ativo) {
		if (existe && !strstr(t, "X-LUMNA-Criado=true")) {
			/* o .desktop é da pessoa (um AppImage integrado): só ganha a marca e o Hidden */
			if (!strstr(t, "X-LUMNA-Desativado=true")) {
				char *novo = g_strdup_printf("%s%sHidden=true\nX-LUMNA-Desativado=true\n", t, t[0] && t[strlen(t) - 1] != '\n' ? "\n" : "");
				ok = g_file_set_contents(c, novo, -1, NULL);
				g_free(novo);
			} else {
				ok = true;
			}
		} else {
			char *novo = g_strdup_printf("[Desktop Entry]\nType=Application\nName=%s\nHidden=true\nNoDisplay=true\nX-LUMNA-Desativado=true\nX-LUMNA-Criado=true\n",
				p->app_nome[0] ? p->app_nome : p->nome);
			ok = g_file_set_contents(c, novo, -1, NULL);
			g_free(novo);
		}
	} else if (existe && strstr(t, "X-LUMNA-Criado=true")) {
		ok = unlink(c) == 0;
	} else if (existe) {
		/* tira só o que o Painel pôs */
		GString *s = g_string_new(NULL);
		char **linhas = g_strsplit(t, "\n", -1);
		for (char **l = linhas; *l; l++) {
			if (strcmp(*l, "Hidden=true") != 0 && strcmp(*l, "X-LUMNA-Desativado=true") != 0 && (**l || *(l + 1))) {
				g_string_append_printf(s, "%s\n", *l);
			}
		}
		g_strfreev(linhas);
		ok = g_file_set_contents(c, s->str, (gssize)s->len, NULL);
		g_string_free(s, TRUE);
	} else {
		ok = true;
	}
	g_free(t);
	g_free(c);
	g_free(dir);
	return ok;
}
