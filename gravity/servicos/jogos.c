#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "jogos.h"
#include "json.h"

/* ---------------- um VDF pequeno (o formato dos arquivos da Steam) ---------------- */

struct vdf {
	char *k, *v;
	GPtrArray *f; /* os filhos (NULL: é um valor) */
};

static void vdf_liberar(struct vdf *n) {
	if (!n) {
		return;
	}
	for (guint i = 0; n->f && i < n->f->len; i++) {
		vdf_liberar(n->f->pdata[i]);
	}
	if (n->f) {
		g_ptr_array_free(n->f, TRUE);
	}
	g_free(n->k);
	g_free(n->v);
	g_free(n);
}

static char *vdf_token(const char **p) {
	while (**p && g_ascii_isspace(**p)) {
		(*p)++;
	}
	if (**p != '"') {
		return NULL;
	}
	(*p)++;
	GString *s = g_string_new(NULL);
	while (**p && **p != '"') {
		if (**p == '\\' && (*p)[1]) {
			(*p)++;
		}
		g_string_append_c(s, **p);
		(*p)++;
	}
	if (**p == '"') {
		(*p)++;
	}
	return g_string_free(s, FALSE);
}

static void vdf_filhos(const char **p, struct vdf *pai) {
	for (;;) {
		while (**p && g_ascii_isspace(**p)) {
			(*p)++;
		}
		if (**p == '}' || !**p) {
			if (**p == '}') {
				(*p)++;
			}
			return;
		}
		char *k = vdf_token(p);
		if (!k) {
			(*p)++;
			continue;
		}
		while (**p && g_ascii_isspace(**p)) {
			(*p)++;
		}
		struct vdf *n = g_new0(struct vdf, 1);
		n->k = k;
		if (**p == '{') {
			(*p)++;
			n->f = g_ptr_array_new();
			vdf_filhos(p, n);
		} else {
			n->v = vdf_token(p);
		}
		g_ptr_array_add(pai->f, n);
	}
}

static struct vdf *vdf_ler(const char *texto) {
	struct vdf *raiz = g_new0(struct vdf, 1);
	raiz->f = g_ptr_array_new();
	const char *p = texto;
	vdf_filhos(&p, raiz);
	return raiz;
}

static const struct vdf *vdf_no(const struct vdf *n, const char *k) {
	for (guint i = 0; n && n->f && i < n->f->len; i++) {
		const struct vdf *c = n->f->pdata[i];
		if (!g_ascii_strcasecmp(c->k, k)) {
			return c;
		}
	}
	return NULL;
}

static const char *vdf_val(const struct vdf *n, const char *k) {
	const struct vdf *c = vdf_no(n, k);
	return c && c->v ? c->v : "";
}

/* ---------------- os arquivos nossos ---------------- */

static char *dir_jogos(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "jogos", NULL);
}

static char *arq_jogos(const char *nome) {
	char *d = dir_jogos(), *a = g_build_filename(d, nome, NULL);
	g_free(d);
	return a;
}

static struct json *json_arquivo(const char *caminho) {
	char *t = NULL;
	struct json *j = NULL;
	if (g_file_get_contents(caminho, &t, NULL, NULL)) {
		j = json_ler(t);
		g_free(t);
	}
	return j;
}

static bool gravar_arquivo(const char *caminho, const char *texto) {
	char *dir = g_path_get_dirname(caminho), *tmp = g_strconcat(caminho, ".tmp", NULL);
	g_mkdir_with_parents(dir, 0700);
	bool ok = g_file_set_contents(tmp, texto, -1, NULL) && g_rename(tmp, caminho) == 0;
	g_free(tmp);
	g_free(dir);
	return ok;
}

const char *jogos_origem_nome(enum jogo_origem o) {
	static const char *const n[ORIGEM_N] = { "Steam", "Lutris", "Heroic", "Avulso" };
	return o >= 0 && o < ORIGEM_N ? n[o] : "";
}

bool jogos_props_editaveis(const struct jogo *j) {
	return j->origem == ORIGEM_AVULSO || j->origem == ORIGEM_LUTRIS;
}

/* ---------------- a Steam ---------------- */

static char *steam_raiz(void) {
	const char *e = g_getenv("LUMNA_STEAM_DIR");
	if (e && *e) {
		return g_strdup(e);
	}
	char *a = g_build_filename(g_get_home_dir(), ".local", "share", "Steam", NULL), *b = g_build_filename(g_get_home_dir(), ".steam", "steam", NULL);
	char *r = g_file_test(a, G_FILE_TEST_IS_DIR) ? a : b;
	if (r == a) {
		g_free(b);
	} else {
		g_free(a);
	}
	return r;
}

static bool nome_de_ferramenta(const char *n) {
	return g_str_has_prefix(n, "Proton") || g_str_has_prefix(n, "Steam Linux Runtime") || g_str_has_prefix(n, "Steamworks Common") || g_str_has_prefix(n, "Steam Controller");
}

static void arte_steam(const char *raiz, const char *appid, struct jogo *j) {
	const char *cache = "appcache/librarycache";
	const char *capas[] = { "%s/%s/%s/library_600x900.jpg", "%s/%s/%s_library_600x900.jpg", "%s/%s/%s/portrait.png", NULL };
	const char *fundos[] = { "%s/%s/%s/library_hero.jpg", "%s/%s/%s_library_hero.jpg", "%s/%s/%s/header.jpg", "%s/%s/%s_header.jpg", NULL };
	for (int pass = 0; pass < 2; pass++) {
		const char *const *m = pass ? fundos : capas;
		char *dest = pass ? j->fundo : j->capa;
		for (int i = 0; m[i]; i++) {
			char *c = g_strdup_printf(m[i], raiz, cache, appid);
			if (!dest[0] && g_file_test(c, G_FILE_TEST_IS_REGULAR)) {
				g_strlcpy(dest, c, 400);
			}
			g_free(c);
		}
	}
	if (!j->capa[0]) {
		g_strlcpy(j->capa, j->fundo, sizeof(j->capa));
	}
	if (!j->fundo[0]) {
		g_strlcpy(j->fundo, j->capa, sizeof(j->fundo));
	}
}

static void steam_tempo(const char *raiz, GHashTable *horas, GHashTable *ultima) {
	char *ud = g_build_filename(raiz, "userdata", NULL);
	GDir *d = g_dir_open(ud, 0, NULL);
	const char *u;
	while (d && (u = g_dir_read_name(d))) {
		char *lc = g_build_filename(ud, u, "config", "localconfig.vdf", NULL), *t = NULL;
		if (g_file_get_contents(lc, &t, NULL, NULL)) {
			struct vdf *r = vdf_ler(t);
			const struct vdf *apps = vdf_no(vdf_no(vdf_no(vdf_no(vdf_no(r, "UserLocalConfigStore"), "Software"), "Valve"), "Steam"), "apps");
			if (!apps) {
				apps = vdf_no(vdf_no(vdf_no(vdf_no(vdf_no(r, "UserLocalConfigStore"), "Software"), "valve"), "Steam"), "apps");
			}
			for (guint i = 0; apps && apps->f && i < apps->f->len; i++) {
				const struct vdf *a = apps->f->pdata[i];
				double h = atof(vdf_val(a, "Playtime")) / 60.0;
				gint64 l = g_ascii_strtoll(vdf_val(a, "LastPlayed"), NULL, 10);
				if (h > 0 || l > 0) {
					g_hash_table_insert(horas, g_strdup(a->k), g_memdup2(&h, sizeof(h)));
					g_hash_table_insert(ultima, g_strdup(a->k), g_memdup2(&l, sizeof(l)));
				}
			}
			vdf_liberar(r);
			g_free(t);
		}
		g_free(lc);
	}
	if (d) {
		g_dir_close(d);
	}
	g_free(ud);
}

static int ler_steam(struct jogo *lista, int n, int max) {
	char *raiz = steam_raiz();
	GPtrArray *pastas = g_ptr_array_new_with_free_func(g_free);
	char *lf = g_build_filename(raiz, "steamapps", "libraryfolders.vdf", NULL), *t = NULL;
	g_ptr_array_add(pastas, g_build_filename(raiz, "steamapps", NULL));
	if (g_file_get_contents(lf, &t, NULL, NULL)) {
		struct vdf *r = vdf_ler(t);
		const struct vdf *libs = vdf_no(r, "libraryfolders");
		for (guint i = 0; libs && libs->f && i < libs->f->len; i++) {
			const char *p = vdf_val(libs->f->pdata[i], "path");
			if (*p) {
				char *sa = g_build_filename(p, "steamapps", NULL);
				bool dup = false;
				for (guint k = 0; k < pastas->len; k++) {
					dup |= !strcmp(pastas->pdata[k], sa);
				}
				if (dup) {
					g_free(sa);
				} else {
					g_ptr_array_add(pastas, sa);
				}
			}
		}
		vdf_liberar(r);
		g_free(t);
	}
	g_free(lf);
	GHashTable *horas = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free), *ultima = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	steam_tempo(raiz, horas, ultima);
	for (guint p = 0; p < pastas->len && n < max; p++) {
		GDir *d = g_dir_open(pastas->pdata[p], 0, NULL);
		const char *nome;
		while (d && (nome = g_dir_read_name(d)) && n < max) {
			if (!g_str_has_prefix(nome, "appmanifest_") || !g_str_has_suffix(nome, ".acf")) {
				continue;
			}
			char *ac = g_build_filename(pastas->pdata[p], nome, NULL), *tx = NULL;
			if (g_file_get_contents(ac, &tx, NULL, NULL)) {
				struct vdf *r = vdf_ler(tx);
				const struct vdf *st = vdf_no(r, "AppState");
				const char *appid = vdf_val(st, "appid"), *nm = vdf_val(st, "name");
				/* só o que está instalado por inteiro (StateFlags tem o bit 4) e não é ferramenta da Steam */
				if (*appid && *nm && (atoi(vdf_val(st, "StateFlags")) & 4) && !nome_de_ferramenta(nm)) {
					struct jogo *j = &lista[n++];
					memset(j, 0, sizeof(*j));
					g_snprintf(j->id, sizeof(j->id), "steam:%s", appid);
					g_strlcpy(j->nome, nm, sizeof(j->nome));
					j->origem = ORIGEM_STEAM;
					g_strlcpy(j->ref, appid, sizeof(j->ref));
					char *pj = g_build_filename(pastas->pdata[p], "common", vdf_val(st, "installdir"), NULL);
					g_strlcpy(j->pasta, pj, sizeof(j->pasta));
					g_free(pj);
					j->tamanho = g_ascii_strtoll(vdf_val(st, "SizeOnDisk"), NULL, 10);
					g_strlcpy(j->versao, vdf_val(st, "buildid"), sizeof(j->versao));
					const double *h = g_hash_table_lookup(horas, appid);
					const gint64 *u = g_hash_table_lookup(ultima, appid);
					j->horas = h ? *h : 0;
					j->ultima = u ? *u : 0;
					j->compat = true;
					g_strlcpy(j->janela, "cheia", sizeof(j->janela));
					arte_steam(raiz, appid, j);
				}
				vdf_liberar(r);
				g_free(tx);
			}
			g_free(ac);
		}
		if (d) {
			g_dir_close(d);
		}
	}
	g_hash_table_destroy(horas);
	g_hash_table_destroy(ultima);
	g_ptr_array_free(pastas, TRUE);
	g_free(raiz);
	return n;
}

/* ---------------- o Lutris ---------------- */

static int ler_lutris(struct jogo *lista, int n, int max) {
	const char *e = g_getenv("LUMNA_LUTRIS_DB");
	char *db = e && *e ? g_strdup(e) : g_build_filename(g_get_home_dir(), ".local", "share", "lutris", "pga.db", NULL);
	sqlite3 *h = NULL;
	if (g_file_test(db, G_FILE_TEST_EXISTS) && sqlite3_open_v2(db, &h, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
		sqlite3_stmt *st = NULL;
		if (sqlite3_prepare_v2(h, "SELECT slug, name, runner, directory, lastplayed, playtime FROM games WHERE installed = 1", -1, &st, NULL) == SQLITE_OK) {
			while (sqlite3_step(st) == SQLITE_ROW && n < max) {
				const char *slug = (const char *)sqlite3_column_text(st, 0), *nome = (const char *)sqlite3_column_text(st, 1);
				if (!slug || !nome) {
					continue;
				}
				struct jogo *j = &lista[n++];
				memset(j, 0, sizeof(*j));
				g_snprintf(j->id, sizeof(j->id), "lutris:%s", slug);
				g_strlcpy(j->nome, nome, sizeof(j->nome));
				j->origem = ORIGEM_LUTRIS;
				g_strlcpy(j->ref, slug, sizeof(j->ref));
				const char *dir = (const char *)sqlite3_column_text(st, 3);
				g_strlcpy(j->pasta, dir ? dir : "", sizeof(j->pasta));
				j->ultima = sqlite3_column_int64(st, 4);
				j->horas = sqlite3_column_double(st, 5);
				j->compat = true;
				g_strlcpy(j->janela, "cheia", sizeof(j->janela));
				const char *dirs[] = { "coverart", "banners", NULL };
				for (int i = 0; dirs[i]; i++) {
					char *c = g_build_filename(g_get_home_dir(), ".local", "share", "lutris", dirs[i], NULL);
					for (int x = 0; x < 2; x++) {
						char *f = g_strdup_printf("%s/%s.%s", c, slug, x ? "png" : "jpg");
						if (!j->capa[0] && g_file_test(f, G_FILE_TEST_IS_REGULAR)) {
							g_strlcpy(j->capa, f, sizeof(j->capa));
						}
						g_free(f);
					}
					g_free(c);
				}
				g_strlcpy(j->fundo, j->capa, sizeof(j->fundo));
			}
			sqlite3_finalize(st);
		}
		sqlite3_close(h);
	}
	g_free(db);
	return n;
}

/* ---------------- o Heroic (Epic e GOG) ---------------- */

static int ler_heroic(struct jogo *lista, int n, int max) {
	const char *e = g_getenv("LUMNA_HEROIC_DIR");
	char *base = e && *e ? g_strdup(e) : g_build_filename(g_get_user_config_dir(), "heroic", NULL);
	char *epic = g_build_filename(base, "legendaryConfig", "legendary", "installed.json", NULL), *gog = g_build_filename(base, "gog_store", "installed.json", NULL);
	struct json *j = json_arquivo(epic);
	for (int i = 0; j && i < j->nfilhos && n < max; i++) {
		const struct json *g = j->filhos[i];
		struct jogo *o = &lista[n++];
		memset(o, 0, sizeof(*o));
		g_snprintf(o->id, sizeof(o->id), "heroic:legendary:%s", g->chave ? g->chave : "?");
		g_strlcpy(o->nome, *json_chave_texto(g, "title") ? json_chave_texto(g, "title") : (g->chave ? g->chave : "?"), sizeof(o->nome));
		o->origem = ORIGEM_HEROIC;
		g_snprintf(o->ref, sizeof(o->ref), "legendary/%s", g->chave ? g->chave : "?");
		g_strlcpy(o->pasta, json_chave_texto(g, "install_path"), sizeof(o->pasta));
		g_strlcpy(o->versao, json_chave_texto(g, "version"), sizeof(o->versao));
		o->tamanho = (int64_t)json_numero(json_chave(g, "install_size"), 0);
		o->compat = true;
		g_strlcpy(o->janela, "cheia", sizeof(o->janela));
	}
	json_liberar(j);
	j = json_arquivo(gog);
	const struct json *lista_gog = json_chave(j, "installed");
	for (int i = 0; lista_gog && i < lista_gog->nfilhos && n < max; i++) {
		const struct json *g = json_item(lista_gog, i);
		struct jogo *o = &lista[n++];
		memset(o, 0, sizeof(*o));
		g_snprintf(o->id, sizeof(o->id), "heroic:gog:%s", json_chave_texto(g, "appName"));
		g_strlcpy(o->nome, *json_chave_texto(g, "title") ? json_chave_texto(g, "title") : json_chave_texto(g, "appName"), sizeof(o->nome));
		o->origem = ORIGEM_HEROIC;
		g_snprintf(o->ref, sizeof(o->ref), "gog/%s", json_chave_texto(g, "appName"));
		g_strlcpy(o->pasta, json_chave_texto(g, "install_path"), sizeof(o->pasta));
		g_strlcpy(o->versao, json_chave_texto(g, "version"), sizeof(o->versao));
		o->tamanho = (int64_t)json_numero(json_chave(g, "install_size"), 0);
		o->compat = true;
		g_strlcpy(o->janela, "cheia", sizeof(o->janela));
	}
	json_liberar(j);
	g_free(epic);
	g_free(gog);
	g_free(base);
	return n;
}

/* ---------------- os avulsos ---------------- */

static int ler_avulsos(struct jogo *lista, int n, int max) {
	char *a = arq_jogos("avulsos.json");
	struct json *j = json_arquivo(a);
	const struct json *l = json_chave(j, "jogos");
	for (int i = 0; l && i < l->nfilhos && n < max; i++) {
		const struct json *g = json_item(l, i);
		struct jogo *o = &lista[n++];
		memset(o, 0, sizeof(*o));
		g_snprintf(o->id, sizeof(o->id), "avulso:%s", json_chave_texto(g, "id"));
		g_strlcpy(o->nome, json_chave_texto(g, "nome"), sizeof(o->nome));
		o->origem = ORIGEM_AVULSO;
		g_strlcpy(o->ref, json_chave_texto(g, "id"), sizeof(o->ref));
		g_strlcpy(o->exec, json_chave_texto(g, "exec"), sizeof(o->exec));
		g_strlcpy(o->dir, json_chave_texto(g, "dir"), sizeof(o->dir));
		g_strlcpy(o->pasta, json_chave_texto(g, "dir"), sizeof(o->pasta));
		g_strlcpy(o->capa, json_chave_texto(g, "capa"), sizeof(o->capa));
		g_strlcpy(o->fundo, json_chave_texto(g, "capa"), sizeof(o->fundo));
		o->compat = g_str_has_suffix(o->exec, ".exe") || g_str_has_suffix(o->exec, ".EXE");
		g_strlcpy(o->janela, "cheia", sizeof(o->janela));
	}
	json_liberar(j);
	g_free(a);
	return n;
}

/* ---------------- as propriedades (props.json) ---------------- */

static struct json *props_ler(void) {
	char *a = arq_jogos("props.json");
	struct json *j = json_arquivo(a);
	g_free(a);
	return j;
}

static void props_aplicar(struct jogo *j, const struct json *p) {
	const struct json *g = json_chave(json_chave(p, "jogos"), j->id);
	if (!g) {
		return;
	}
	j->favorito = json_bool(json_chave(g, "favorito"), false);
	int64_t u = (int64_t)json_numero(json_chave(g, "ultima"), 0);
	if (u > j->ultima) {
		j->ultima = u;
	}
	j->horas += json_numero(json_chave(g, "horas_extra"), 0);
	if (!jogos_props_editaveis(j)) {
		return;
	}
	if (json_chave(g, "exec")) {
		g_strlcpy(j->exec, json_chave_texto(g, "exec"), sizeof(j->exec));
	}
	g_strlcpy(j->opcoes, json_chave_texto(g, "opcoes"), sizeof(j->opcoes));
	if (json_chave(g, "dir")) {
		g_strlcpy(j->dir, json_chave_texto(g, "dir"), sizeof(j->dir));
	}
	const struct json *env = json_chave(g, "env");
	for (int i = 0; env && i < env->nfilhos && j->nenv < JOGO_ENV_MAX; i++) {
		g_strlcpy(j->env[j->nenv].k, json_chave_texto(json_item(env, i), "k"), sizeof(j->env[0].k));
		g_strlcpy(j->env[j->nenv].v, json_chave_texto(json_item(env, i), "v"), sizeof(j->env[0].v));
		j->nenv++;
	}
	if (json_chave(g, "compat")) {
		j->compat = json_bool(json_chave(g, "compat"), j->compat);
	}
	j->gpu = json_bool(json_chave(g, "gpu"), false);
	if (*json_chave_texto(g, "janela")) {
		g_strlcpy(j->janela, json_chave_texto(g, "janela"), sizeof(j->janela));
	}
}

/* regrava props.json trocando só o registro deste jogo (ou tirando-o, com j NULL e id) */
static bool props_regravar(const char *id, const struct jogo *j, double horas_extra, int64_t ultima) {
	struct json *p = props_ler();
	const struct json *todos = json_chave(p, "jogos");
	GString *s = g_string_new("{\"jogos\":{");
	bool primeiro = true;
	for (int i = 0; todos && i < todos->nfilhos; i++) {
		if (!todos->filhos[i]->chave || !strcmp(todos->filhos[i]->chave, id)) {
			continue;
		}
		g_string_append(s, primeiro ? "" : ",");
		primeiro = false;
		json_escrever_texto(s, todos->filhos[i]->chave);
		g_string_append_c(s, ':');
		json_escrever(s, todos->filhos[i]);
	}
	if (j) {
		const struct json *antigo = json_chave(todos, id);
		g_string_append(s, primeiro ? "" : ",");
		json_escrever_texto(s, id);
		g_string_append_printf(s, ":{\"favorito\":%s,\"ultima\":%" G_GINT64_FORMAT ",\"horas_extra\":%g,\"exec\":", j->favorito ? "true" : "false", ultima, horas_extra);
		json_escrever_texto(s, j->exec);
		g_string_append(s, ",\"opcoes\":");
		json_escrever_texto(s, j->opcoes);
		g_string_append(s, ",\"dir\":");
		json_escrever_texto(s, j->dir);
		g_string_append(s, ",\"env\":[");
		for (int i = 0; i < j->nenv; i++) {
			g_string_append_printf(s, "%s{\"k\":", i ? "," : "");
			json_escrever_texto(s, j->env[i].k);
			g_string_append(s, ",\"v\":");
			json_escrever_texto(s, j->env[i].v);
			g_string_append_c(s, '}');
		}
		g_string_append_printf(s, "],\"compat\":%s,\"gpu\":%s,\"janela\":", j->compat ? "true" : "false", j->gpu ? "true" : "false");
		json_escrever_texto(s, j->janela);
		g_string_append_c(s, '}');
		(void)antigo;
	}
	g_string_append(s, "}}");
	char *a = arq_jogos("props.json");
	bool ok = gravar_arquivo(a, s->str);
	g_free(a);
	g_string_free(s, TRUE);
	json_liberar(p);
	return ok;
}

/* o registro cru de um jogo no props.json, para mexer num campo sem perder os outros */
static void registro_ler(const char *id, double *horas_extra, int64_t *ultima) {
	struct json *p = props_ler();
	const struct json *g = json_chave(json_chave(p, "jogos"), id);
	*horas_extra = json_numero(json_chave(g, "horas_extra"), 0);
	*ultima = (int64_t)json_numero(json_chave(g, "ultima"), 0);
	json_liberar(p);
}

static void completar_estado_salvo(struct jogo *j, const struct json *p) {
	/* o jogo "puro" (sem o que é nosso), para gravar só o que vale */
	props_aplicar(j, p);
}

static int comparar(const void *a, const void *b) {
	return g_utf8_collate(((const struct jogo *)a)->nome, ((const struct jogo *)b)->nome);
}

int jogos_listar(struct jogo *lista, int max) {
	int n = ler_steam(lista, 0, max);
	n = ler_lutris(lista, n, max);
	n = ler_heroic(lista, n, max);
	n = ler_avulsos(lista, n, max);
	struct json *p = props_ler();
	for (int i = 0; i < n; i++) {
		completar_estado_salvo(&lista[i], p);
	}
	json_liberar(p);
	qsort(lista, (size_t)n, sizeof(lista[0]), comparar);
	return n;
}

bool jogos_favoritar(const struct jogo *j, bool favorito) {
	double h;
	int64_t u;
	registro_ler(j->id, &h, &u);
	struct jogo c = *j;
	c.favorito = favorito;
	return props_regravar(j->id, &c, h, u);
}

bool jogos_props_gravar(const struct jogo *j) {
	double h;
	int64_t u;
	registro_ler(j->id, &h, &u);
	return props_regravar(j->id, j, h, u);
}

void jogos_props_restaurar(struct jogo *j) {
	double h;
	int64_t u;
	registro_ler(j->id, &h, &u);
	struct jogo c = *j;
	c.exec[0] = c.opcoes[0] = c.dir[0] = '\0';
	c.nenv = 0;
	c.gpu = false;
	g_strlcpy(c.janela, "cheia", sizeof(c.janela));
	props_regravar(j->id, &c, h, u);
	j->opcoes[0] = '\0';
	j->nenv = 0;
	j->gpu = false;
	g_strlcpy(j->janela, "cheia", sizeof(j->janela));
}

bool jogos_avulso_adicionar(const char *nome, const char *exec, const char *dir, char *id, size_t tam, char *erro, size_t erro_tam) {
	if (!nome || !*g_strstrip((char *)nome)) {
		g_strlcpy(erro, "Dê um nome ao jogo.", erro_tam);
		return false;
	}
	if (!exec || !g_file_test(exec, G_FILE_TEST_IS_REGULAR)) {
		g_strlcpy(erro, "O executável não existe.", erro_tam);
		return false;
	}
	char *a = arq_jogos("avulsos.json");
	struct json *j = json_arquivo(a);
	const struct json *l = json_chave(j, "jogos");
	GString *s = g_string_new("{\"jogos\":[");
	for (int i = 0; l && i < l->nfilhos; i++) {
		json_escrever(s, l->filhos[i]);
		g_string_append_c(s, ',');
	}
	char novo[16];
	g_snprintf(novo, sizeof(novo), "%08x", g_str_hash(nome) ^ (guint)time(NULL));
	g_string_append(s, "{\"id\":");
	json_escrever_texto(s, novo);
	g_string_append(s, ",\"nome\":");
	json_escrever_texto(s, nome);
	g_string_append(s, ",\"exec\":");
	json_escrever_texto(s, exec);
	g_string_append(s, ",\"dir\":");
	char *pai = g_path_get_dirname(exec);
	json_escrever_texto(s, dir && *dir ? dir : pai);
	g_free(pai);
	g_string_append(s, ",\"capa\":\"\"}]}");
	bool ok = gravar_arquivo(a, s->str);
	if (ok && id) {
		g_snprintf(id, tam, "avulso:%s", novo);
	} else if (!ok) {
		g_strlcpy(erro, "Não consegui gravar a lista de jogos.", erro_tam);
	}
	g_string_free(s, TRUE);
	json_liberar(j);
	g_free(a);
	return ok;
}

/* ---------------- abrir ---------------- */

static char *quote(const char *s) {
	return g_shell_quote(s);
}

char *jogos_comando(const struct jogo *j) {
	switch (j->origem) {
	case ORIGEM_STEAM:
		return g_strdup_printf("steam steam://rungameid/%s", j->ref);
	case ORIGEM_LUTRIS:
		return g_strdup_printf("lutris lutris:rungame/%s", j->ref);
	case ORIGEM_HEROIC: {
		char *r = g_strdup(j->ref), *barra = strchr(r, '/');
		if (barra) {
			*barra = '\0';
		}
		char *c = g_strdup_printf("heroic --no-gui heroic://launch/%s/%s", r, barra ? barra + 1 : "");
		g_free(r);
		return c;
	}
	default:
		break;
	}
	/* avulso: as variáveis, a GPU, a camada de compatibilidade e as opções com %command% */
	GString *c = g_string_new(NULL);
	for (int i = 0; i < j->nenv; i++) {
		bool nome_ok = j->env[i].k[0] && (g_ascii_isalpha(j->env[i].k[0]) || j->env[i].k[0] == '_');
		for (const char *c = j->env[i].k; nome_ok && *c; c++) {
			nome_ok = g_ascii_isalnum(*c) || *c == '_'; /* o nome vai direto para o shell: só letras, números e _ */
		}
		if (nome_ok) {
			char *v = quote(j->env[i].v);
			g_string_append_printf(c, "%s=%s ", j->env[i].k, v);
			g_free(v);
		}
	}
	if (j->gpu) {
		g_string_append(c, "DRI_PRIME=1 __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ");
	}
	char *ex = quote(j->exec);
	/* a camada de compatibilidade para .exe: o umu-run (Proton) se houver, senão o wine */
	char *umu = j->compat ? g_find_program_in_path("umu-run") : NULL;
	char *base = j->compat ? g_strdup_printf("%s %s", umu ? "umu-run" : "wine", ex) : g_strdup(ex);
	g_free(umu);
	if (j->opcoes[0] && strstr(j->opcoes, "%command%")) {
		char **partes = g_strsplit(j->opcoes, "%command%", 2);
		g_string_append_printf(c, "%s%s%s", partes[0], base, partes[1] ? partes[1] : "");
		g_strfreev(partes);
	} else {
		g_string_append_printf(c, "%s%s%s", base, j->opcoes[0] ? " " : "", j->opcoes);
	}
	g_free(base);
	g_free(ex);
	return g_string_free(c, FALSE);
}

static void anotar_sessao(const struct jogo *j) {
	double h;
	int64_t u;
	registro_ler(j->id, &h, &u);
	props_regravar(j->id, j, h, (int64_t)time(NULL));
}

bool jogos_lancar(const struct jogo *j, char *erro, size_t tam) {
	char *cmd = jogos_comando(j);
	const char *dir = j->origem == ORIGEM_AVULSO && j->dir[0] && g_file_test(j->dir, G_FILE_TEST_IS_DIR) ? j->dir : NULL;
	GError *ge = NULL;
	char *argv[] = { "sh", "-c", cmd, NULL };
	bool ok = g_spawn_async(dir, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &ge);
	if (!ok) {
		g_strlcpy(erro, ge ? ge->message : "Não consegui abrir o jogo.", tam);
		g_clear_error(&ge);
	} else {
		anotar_sessao(j);
	}
	g_free(cmd);
	return ok;
}

/* ---------------- desinstalar ---------------- */

const char *jogos_desinstalar_texto(const struct jogo *j) {
	switch (j->origem) {
	case ORIGEM_STEAM: return "A Steam desinstala o jogo e cuida dos arquivos dele. Os saves na nuvem da Steam continuam.";
	case ORIGEM_LUTRIS: return "O Lutris abre para desinstalar este jogo; ele cuida dos arquivos e do prefixo.";
	case ORIGEM_HEROIC: return "O Heroic abre para desinstalar este jogo; ele cuida dos arquivos.";
	default: return "O jogo sai da biblioteca. Os arquivos só são apagados se você marcar a opção abaixo; os saves nunca.";
	}
}

bool jogos_desinstalar(const struct jogo *j, bool apagar_arquivos, bool *saiu_da_lista, char *erro, size_t tam) {
	if (saiu_da_lista) {
		*saiu_da_lista = false;
	}
	GError *ge = NULL;
	if (j->origem == ORIGEM_AVULSO) {
		char *a = arq_jogos("avulsos.json");
		struct json *p = json_arquivo(a);
		const struct json *l = json_chave(p, "jogos");
		GString *s = g_string_new("{\"jogos\":[");
		bool primeiro = true, achou = false;
		for (int i = 0; l && i < l->nfilhos; i++) {
			if (!strcmp(json_chave_texto(l->filhos[i], "id"), j->ref)) {
				achou = true;
				continue;
			}
			g_string_append(s, primeiro ? "" : ",");
			primeiro = false;
			json_escrever(s, l->filhos[i]);
		}
		g_string_append(s, "]}");
		bool ok = achou && gravar_arquivo(a, s->str);
		g_string_free(s, TRUE);
		json_liberar(p);
		g_free(a);
		if (!ok) {
			g_strlcpy(erro, "Esse jogo já não está na lista.", tam);
			return false;
		}
		props_regravar(j->id, NULL, 0, 0);
		/* só apaga a pasta se ela é uma pasta própria do jogo: nunca a casa, a raiz ou uma pasta com pouco caminho */
		if (apagar_arquivos && j->pasta[0] && g_path_is_absolute(j->pasta) && strcmp(j->pasta, g_get_home_dir()) && strlen(j->pasta) > 12 && g_file_test(j->pasta, G_FILE_TEST_IS_DIR)) {
			char *argv[] = { "rm", "-rf", "--", (char *)j->pasta, NULL };
			g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
		}
		if (saiu_da_lista) {
			*saiu_da_lista = true;
		}
		return true;
	}
	char *cmd = j->origem == ORIGEM_STEAM ? g_strdup_printf("steam steam://uninstall/%s", j->ref) : j->origem == ORIGEM_LUTRIS ? g_strdup("lutris") : g_strdup("heroic");
	char *argv[] = { "sh", "-c", cmd, NULL };
	bool ok = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &ge);
	if (!ok) {
		g_strlcpy(erro, ge ? ge->message : "Não consegui abrir o gerenciador do jogo.", tam);
		g_clear_error(&ge);
	}
	g_free(cmd);
	return ok;
}
