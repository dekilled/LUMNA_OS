#include "musica.h"

#include <ctype.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "json.h"

/* ---- caminhos ---- */

char *mu_pasta_config(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "musica", NULL);
}

static char *pasta_cache(void) {
	return g_build_filename(g_get_user_cache_dir(), "lumna", "musica", NULL);
}

/* ---- ajustes ---- */

void mu_cfg_padrao(mu_cfg *c) {
	memset(c, 0, sizeof(*c));
	c->normalizar = true;
	c->sem_intervalos = true;
	c->restaurar_fila = true;
	c->emma_no_app = true;
	c->segundo_plano = true;
	c->subpastas = true;
	c->buscar_auto = true;
	c->letras_mostrar = true;
	c->letras_seguir = true;
	c->letras_tamanho = 1;
	c->capas = true;
	c->fundo_capa = true;
	c->tam_capa = 200;
	c->volume = 80;
	const char *m = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC);
	g_strlcpy(c->pastas[0], m ? m : "", sizeof(c->pastas[0]));
	if (!c->pastas[0][0]) {
		char *p = g_build_filename(g_get_home_dir(), "Music", NULL);
		g_strlcpy(c->pastas[0], p, sizeof(c->pastas[0]));
		g_free(p);
	}
	c->npastas = 1;
}

static int dentro(int v, int min, int max) {
	return v < min ? min : v > max ? max : v;
}

void mu_cfg_ler(mu_cfg *c) {
	mu_cfg_padrao(c);
	char *d = mu_pasta_config(), *arq = g_build_filename(d, "config.ini", NULL);
	GKeyFile *kf = g_key_file_new();
	if (g_key_file_load_from_file(kf, arq, G_KEY_FILE_NONE, NULL)) {
#define S "musica"
#define BOOL(k, f) if (g_key_file_has_key(kf, S, k, NULL)) c->f = g_key_file_get_boolean(kf, S, k, NULL)
#define INT(k, f, lo, hi) if (g_key_file_has_key(kf, S, k, NULL)) c->f = dentro(g_key_file_get_integer(kf, S, k, NULL), lo, hi)
		gsize n = 0;
		char **lista = g_key_file_has_key(kf, S, "pastas", NULL) ? g_key_file_get_string_list(kf, S, "pastas", &n, NULL) : NULL;
		if (lista) {
			c->npastas = 0;
			for (gsize i = 0; i < n && c->npastas < MU_PASTAS_MAX; i++) {
				if (lista[i][0]) {
					g_strlcpy(c->pastas[c->npastas++], lista[i], sizeof(c->pastas[0]));
				}
			}
			g_strfreev(lista);
		}
		char *saida = g_key_file_get_string(kf, S, "saida", NULL);
		if (saida) {
			g_strlcpy(c->saida, saida, sizeof(c->saida));
			g_free(saida);
		}
		BOOL("normalizar", normalizar); BOOL("sem_intervalos", sem_intervalos); INT("transicao", transicao, 0, 12);
		BOOL("restaurar_fila", restaurar_fila); BOOL("emma_no_app", emma_no_app); BOOL("segundo_plano", segundo_plano); BOOL("subpastas", subpastas); BOOL("buscar_auto", buscar_auto);
		BOOL("letras_mostrar", letras_mostrar); BOOL("letras_seguir", letras_seguir); INT("letras_tamanho", letras_tamanho, 0, 2);
		BOOL("capas", capas); BOOL("fundo_capa", fundo_capa); INT("tam_capa", tam_capa, 120, 240);
		INT("volume", volume, 0, 100); INT("repetir", repetir, 0, 2); BOOL("aleatorio", aleatorio); INT("vista_albuns", vista_albuns, 0, 1); INT("ordem_albuns", ordem_albuns, 0, 2); BOOL("fila_aberta", fila_aberta);
#undef BOOL
#undef INT
#undef S
	}
	g_key_file_free(kf);
	g_free(d);
	g_free(arq);
}

bool mu_cfg_gravar(const mu_cfg *c) {
	char *d = mu_pasta_config();
	g_mkdir_with_parents(d, 0700);
	char *arq = g_build_filename(d, "config.ini", NULL);
	GKeyFile *kf = g_key_file_new();
	const char *ps[MU_PASTAS_MAX];
	for (int i = 0; i < c->npastas; i++) {
		ps[i] = c->pastas[i];
	}
	g_key_file_set_string_list(kf, "musica", "pastas", ps, (gsize)c->npastas);
	g_key_file_set_string(kf, "musica", "saida", c->saida);
#define BOOL(k, f) g_key_file_set_boolean(kf, "musica", k, c->f)
#define INT(k, f) g_key_file_set_integer(kf, "musica", k, c->f)
	BOOL("normalizar", normalizar); BOOL("sem_intervalos", sem_intervalos); INT("transicao", transicao);
	BOOL("restaurar_fila", restaurar_fila); BOOL("emma_no_app", emma_no_app); BOOL("segundo_plano", segundo_plano); BOOL("subpastas", subpastas); BOOL("buscar_auto", buscar_auto);
	BOOL("letras_mostrar", letras_mostrar); BOOL("letras_seguir", letras_seguir); INT("letras_tamanho", letras_tamanho);
	BOOL("capas", capas); BOOL("fundo_capa", fundo_capa); INT("tam_capa", tam_capa);
	INT("volume", volume); INT("repetir", repetir); BOOL("aleatorio", aleatorio); INT("vista_albuns", vista_albuns); INT("ordem_albuns", ordem_albuns); BOOL("fila_aberta", fila_aberta);
#undef BOOL
#undef INT
	gsize n = 0;
	char *txt = g_key_file_to_data(kf, &n, NULL);
	bool ok = g_file_set_contents(arq, txt, (gssize)n, NULL);
	g_free(txt);
	g_key_file_free(kf);
	g_free(d);
	g_free(arq);
	return ok;
}

/* ---- texto ---- */

char *mu_normalizar(const char *texto) {
	char *d = g_utf8_normalize(texto ? texto : "", -1, G_NORMALIZE_NFD);
	GString *s = g_string_new(NULL);
	for (const char *p = d; *p; p = g_utf8_next_char(p)) {
		gunichar c = g_utf8_get_char(p);
		GUnicodeType t = g_unichar_type(c);
		if (t == G_UNICODE_NON_SPACING_MARK || t == G_UNICODE_SPACING_MARK || t == G_UNICODE_ENCLOSING_MARK) {
			continue;
		}
		g_string_append_unichar(s, g_unichar_tolower(c));
	}
	g_free(d);
	return g_string_free(s, FALSE);
}

void mu_duracao_texto(int s, char *saida, size_t tam) {
	if (s < 0) {
		s = 0;
	}
	if (s >= 3600) {
		g_snprintf(saida, tam, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
	} else {
		g_snprintf(saida, tam, "%d:%02d", s / 60, s % 60);
	}
}

void mu_duracao_longa(int s, char *saida, size_t tam) {
	int min = (s + 30) / 60;
	if (min >= 60) {
		if (min % 60) {
			g_snprintf(saida, tam, "%d h %d min", min / 60, min % 60);
		} else {
			g_snprintf(saida, tam, "%d h", min / 60);
		}
	} else {
		g_snprintf(saida, tam, "%d min", MAX(min, s > 0 ? 1 : 0));
	}
}

static char *limpo(const char *s) {
	char *r = g_strdup(s ? s : "");
	for (char *p = r; *p; p++) {
		if (*p == '\t' || *p == '\n' || *p == '\r') {
			*p = ' ';
		}
	}
	return g_strstrip(r);
}

/* ---- faixas ---- */

bool mu_extensao_de_audio(const char *caminho) {
	const char *p = strrchr(caminho, '.');
	if (!p) {
		return false;
	}
	static const char *const E[] = { "mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "wav", "wma", "ape", "aiff", "aif", "mka", "wv", NULL };
	char *e = g_ascii_strdown(p + 1, -1);
	bool r = false;
	for (int i = 0; E[i] && !r; i++) {
		r = !strcmp(e, E[i]);
	}
	g_free(e);
	return r;
}

void mu_faixa_liberar(mu_faixa *f) {
	g_free(f->caminho); g_free(f->pasta); g_free(f->titulo); g_free(f->artista); g_free(f->album); g_free(f->album_artista); g_free(f->genero);
	memset(f, 0, sizeof(*f));
}

void mu_faixa_copiar(mu_faixa *p, const mu_faixa *d) {
	*p = *d;
	p->caminho = g_strdup(d->caminho); p->pasta = g_strdup(d->pasta); p->titulo = g_strdup(d->titulo); p->artista = g_strdup(d->artista);
	p->album = g_strdup(d->album); p->album_artista = g_strdup(d->album_artista); p->genero = g_strdup(d->genero);
}

void mu_acervo_liberar(mu_acervo *a) {
	for (int i = 0; i < a->n; i++) {
		mu_faixa_liberar(&a->v[i]);
	}
	g_free(a->v);
	a->v = NULL;
	a->n = 0;
}

static bool rodar_saida(char **argv, int segundos, GString *saida) {
	char **a = g_new0(char *, g_strv_length(argv) + 4);
	a[0] = (char *)"timeout";
	char tmo[16];
	g_snprintf(tmo, sizeof(tmo), "%d", segundos);
	a[1] = tmo;
	for (int i = 0; argv[i]; i++) {
		a[i + 2] = argv[i];
	}
	gchar *out = NULL;
	gint st = 0;
	gboolean ok = g_spawn_sync(NULL, a, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &st, NULL);
	g_free(a);
	bool bom = ok && WIFEXITED(st) && WEXITSTATUS(st) == 0;
	if (bom && saida && out) {
		g_string_append(saida, out);
	}
	g_free(out);
	return bom;
}

/* a tag `nome` (sem diferenciar maiúsculas) no objeto de tags */
static const char *tag(const struct json *tags, const char *nome) {
	if (!tags) {
		return NULL;
	}
	for (int i = 0; i < tags->nfilhos; i++) {
		if (tags->filhos[i]->chave && !g_ascii_strcasecmp(tags->filhos[i]->chave, nome) && tags->filhos[i]->tipo == JSON_TEXTO) {
			return tags->filhos[i]->s;
		}
	}
	return NULL;
}

static const char *tag_em(const struct json *fmt_tags, const struct json *streams, const char *nome) {
	const char *r = tag(fmt_tags, nome);
	for (int i = 0; !r && streams && i < streams->nfilhos; i++) {
		r = tag(json_chave(streams->filhos[i], "tags"), nome);
	}
	return r;
}

static int inteiro_inicial(const char *s) {
	return s ? atoi(s) : 0;
}

static char *json_do_arquivo(const char *caminho) {
	char *argv[] = { "ffprobe", "-v", "error", "-show_entries", "format=duration:format_tags:stream=codec_type:stream_tags:stream_disposition=attached_pic", "-of", "json", (char *)caminho, NULL };
	GString *s = g_string_new(NULL);
	bool ok = rodar_saida(argv, 20, s);
	return ok ? g_string_free(s, FALSE) : (g_string_free(s, TRUE), NULL);
}

bool mu_ler_faixa(const char *caminho, mu_faixa *f) {
	memset(f, 0, sizeof(*f));
	if (!mu_extensao_de_audio(caminho)) {
		return false;
	}
	char *txt = json_do_arquivo(caminho);
	if (!txt) {
		return false;
	}
	struct json *j = json_ler(txt);
	g_free(txt);
	if (!j) {
		return false;
	}
	const struct json *fmt = json_chave(j, "format"), *ftags = json_chave(fmt, "tags"), *streams = json_chave(j, "streams");
	bool tem_audio = false;
	for (int i = 0; streams && i < streams->nfilhos; i++) {
		const struct json *s = streams->filhos[i];
		const char *tp = json_chave_texto(s, "codec_type");
		if (!strcmp(tp, "audio")) {
			tem_audio = true;
		} else if (!strcmp(tp, "video") && json_numero(json_chave(json_chave(s, "disposition"), "attached_pic"), 0) > 0) {
			f->capa_embutida = true;
		}
	}
	if (!tem_audio) {
		json_liberar(j);
		return false;
	}
	f->caminho = g_strdup(caminho);
	f->pasta = g_path_get_dirname(caminho);
	char *base = g_path_get_basename(caminho);
	char *ponto = strrchr(base, '.');
	if (ponto && ponto != base) {
		*ponto = 0;
	}
	const char *t = tag_em(ftags, streams, "title");
	f->titulo = t && *t ? limpo(t) : limpo(base);
	g_free(base);
	const char *ar = tag_em(ftags, streams, "artist"), *aa = tag_em(ftags, streams, "album_artist"), *al = tag_em(ftags, streams, "album");
	if (!aa) {
		aa = tag_em(ftags, streams, "albumartist");
	}
	f->artista = ar && *ar ? limpo(ar) : (aa && *aa ? limpo(aa) : g_strdup("Artista desconhecido"));
	f->album_artista = aa && *aa ? limpo(aa) : g_strdup("");
	f->album = al && *al ? limpo(al) : g_strdup("Álbum desconhecido");
	const char *g = tag_em(ftags, streams, "genre");
	f->genero = limpo(g);
	f->faixa = inteiro_inicial(tag_em(ftags, streams, "track"));
	f->disco = inteiro_inicial(tag_em(ftags, streams, "disc"));
	const char *d = tag_em(ftags, streams, "date");
	if (!d) {
		d = tag_em(ftags, streams, "year");
	}
	f->ano = d && strlen(d) >= 4 ? atoi(d) : 0;
	f->duracao = (int)(json_numero(json_chave(fmt, "duration"), 0) + 0.5);
	if (!f->duracao) {
		const char *ds = json_chave_texto(fmt, "duration");
		f->duracao = (int)(g_ascii_strtod(ds, NULL) + 0.5);
	}
	/* a letra na tag: lyrics, lyrics-xxx, unsyncedlyrics */
	for (int pass = 0; pass < 2 && !f->letra_tag; pass++) {
		const struct json *tt = pass == 0 ? ftags : (streams && streams->nfilhos ? json_chave(streams->filhos[0], "tags") : NULL);
		for (int i = 0; tt && i < tt->nfilhos; i++) {
			const char *k = tt->filhos[i]->chave;
			if (k && (!g_ascii_strncasecmp(k, "lyrics", 6) || !g_ascii_strcasecmp(k, "unsyncedlyrics") || !g_ascii_strcasecmp(k, "unsynced lyrics")) && tt->filhos[i]->s && *tt->filhos[i]->s) {
				f->letra_tag = true;
			}
		}
	}
	struct stat st;
	if (stat(caminho, &st) == 0) {
		f->mtime = st.st_mtime;
		f->tamanho = st.st_size;
	}
	json_liberar(j);
	return true;
}

/* ---- o cache da varredura ---- */

static char *cache_arquivo(void) {
	char *d = pasta_cache();
	g_mkdir_with_parents(d, 0700);
	char *a = g_build_filename(d, "indice.tsv", NULL);
	g_free(d);
	return a;
}

static GHashTable *cache_ler(void) {
	GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	char *a = cache_arquivo(), *txt = NULL;
	if (g_file_get_contents(a, &txt, NULL, NULL)) {
		char **ls = g_strsplit(txt, "\n", -1);
		if (ls[0] && !strcmp(ls[0], "lumna-musica-v1")) {
			for (int i = 1; ls[i]; i++) {
				char *tab = strchr(ls[i], '\t');
				if (tab) {
					g_hash_table_insert(h, g_strndup(ls[i], (gsize)(tab - ls[i])), g_strdup(tab + 1));
				}
			}
		}
		g_strfreev(ls);
	}
	g_free(txt);
	g_free(a);
	return h;
}

static bool faixa_de_linha(const char *caminho, const char *linha, const struct stat *st, mu_faixa *f) {
	char **c = g_strsplit(linha, "\t", -1);
	bool ok = g_strv_length(c) >= 13 && atol(c[0]) == (long)st->st_mtime && atoll(c[1]) == (long long)st->st_size;
	if (ok) {
		memset(f, 0, sizeof(*f));
		f->caminho = g_strdup(caminho);
		f->pasta = g_path_get_dirname(caminho);
		f->titulo = g_strdup(c[2]); f->artista = g_strdup(c[3]); f->album = g_strdup(c[4]); f->album_artista = g_strdup(c[5]); f->genero = g_strdup(c[6]);
		f->faixa = atoi(c[7]); f->disco = atoi(c[8]); f->ano = atoi(c[9]); f->duracao = atoi(c[10]);
		f->capa_embutida = c[11][0] == '1'; f->letra_tag = c[12][0] == '1';
		f->mtime = st->st_mtime;
		f->tamanho = st->st_size;
	}
	g_strfreev(c);
	return ok;
}

static void cache_gravar(const mu_acervo *a) {
	GString *s = g_string_new("lumna-musica-v1\n");
	for (int i = 0; i < a->n; i++) {
		const mu_faixa *f = &a->v[i];
		char *t = limpo(f->titulo), *ar = limpo(f->artista), *al = limpo(f->album), *aa = limpo(f->album_artista), *g = limpo(f->genero);
		g_string_append_printf(s, "%s\t%ld\t%lld\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\n", f->caminho, (long)f->mtime, (long long)f->tamanho, t, ar, al, aa, g,
			f->faixa, f->disco, f->ano, f->duracao, f->capa_embutida, f->letra_tag);
		g_free(t); g_free(ar); g_free(al); g_free(aa); g_free(g);
	}
	char *arq = cache_arquivo();
	g_file_set_contents(arq, s->str, (gssize)s->len, NULL);
	g_free(arq);
	g_string_free(s, TRUE);
}

typedef struct {
	GMutex m;
	mu_acervo *saida;
	int feitos, total;
	void (*progresso)(int, int, void *);
	void *dados;
} varredura;

static void ler_um(gpointer dado, gpointer user) {
	char *caminho = dado;
	varredura *v = user;
	mu_faixa f;
	bool ok = mu_ler_faixa(caminho, &f);
	g_mutex_lock(&v->m);
	if (ok) {
		v->saida->v = g_renew(mu_faixa, v->saida->v, (guint)v->saida->n + 1);
		v->saida->v[v->saida->n++] = f;
	}
	int feitos = ++v->feitos, total = v->total;
	g_mutex_unlock(&v->m);
	if (v->progresso) {
		v->progresso(feitos, total, v->dados);
	}
	g_free(caminho);
}

static void listar(const char *pasta, bool sub, GPtrArray *arquivos, int profundidade) {
	GDir *d = g_dir_open(pasta, 0, NULL);
	if (!d) {
		return;
	}
	const char *n;
	while ((n = g_dir_read_name(d))) {
		if (n[0] == '.') {
			continue;
		}
		char *c = g_build_filename(pasta, n, NULL);
		struct stat st;
		if (stat(c, &st) == 0 && S_ISDIR(st.st_mode)) {
			if (sub && profundidade < 12) {
				listar(c, sub, arquivos, profundidade + 1);
			}
			g_free(c);
		} else if (S_ISREG(st.st_mode) && mu_extensao_de_audio(c)) {
			g_ptr_array_add(arquivos, c);
		} else {
			g_free(c);
		}
	}
	g_dir_close(d);
}

static int cmp_faixas(const void *x, const void *y) {
	const mu_faixa *a = x, *b = y;
	return strcmp(a->caminho, b->caminho);
}

void mu_varrer(const mu_cfg *c, mu_acervo *a, void (*progresso)(int, int, void *), void *dados) {
	memset(a, 0, sizeof(*a));
	GPtrArray *arquivos = g_ptr_array_new_with_free_func(g_free);
	for (int i = 0; i < c->npastas; i++) {
		listar(c->pastas[i], c->subpastas, arquivos, 0);
	}
	GHashTable *cache = cache_ler();
	varredura v = { .saida = a, .total = arquivos->len, .progresso = progresso, .dados = dados };
	g_mutex_init(&v.m);
	GThreadPool *pool = g_thread_pool_new(ler_um, &v, 4, FALSE, NULL);
	GHashTable *vistos = g_hash_table_new(g_str_hash, g_str_equal);
	for (guint i = 0; i < arquivos->len; i++) {
		char *caminho = arquivos->pdata[i];
		if (g_hash_table_contains(vistos, caminho)) {
			v.total--; /* a mesma música por duas pastas da biblioteca */
			continue;
		}
		g_hash_table_add(vistos, caminho);
		struct stat st;
		const char *linha = g_hash_table_lookup(cache, caminho);
		mu_faixa f;
		if (linha && stat(caminho, &st) == 0 && faixa_de_linha(caminho, linha, &st, &f)) {
			g_mutex_lock(&v.m);
			a->v = g_renew(mu_faixa, a->v, (guint)a->n + 1);
			a->v[a->n++] = f;
			v.feitos++;
			g_mutex_unlock(&v.m);
		} else {
			g_thread_pool_push(pool, g_strdup(caminho), NULL);
		}
	}
	g_thread_pool_free(pool, FALSE, TRUE); /* espera as leituras acabarem */
	g_hash_table_destroy(vistos);
	g_hash_table_destroy(cache);
	g_ptr_array_free(arquivos, TRUE);
	g_mutex_clear(&v.m);
	if (a->n > 1) {
		qsort(a->v, (size_t)a->n, sizeof(mu_faixa), cmp_faixas);
	}
	mu_ordenar_faixas(a);
	cache_gravar(a);
	if (progresso) {
		progresso(v.total, v.total, dados);
	}
}

/* ---- ordens e grupos ---- */

static int cmp_texto(const char *a, const char *b) {
	char *x = mu_normalizar(a), *y = mu_normalizar(b);
	int r = strcmp(x, y);
	g_free(x);
	g_free(y);
	return r;
}

static int cmp_album_ordem(const mu_faixa *a, const mu_faixa *b) {
	if (a->disco != b->disco) {
		return a->disco - b->disco;
	}
	if (a->faixa != b->faixa) {
		return a->faixa - b->faixa;
	}
	return cmp_texto(a->titulo, b->titulo);
}

static int cmp_biblioteca(const void *x, const void *y) {
	const mu_faixa *a = x, *b = y;
	int r = cmp_texto(a->album_artista[0] ? a->album_artista : a->artista, b->album_artista[0] ? b->album_artista : b->artista);
	if (!r) {
		r = cmp_texto(a->album, b->album);
	}
	return r ? r : cmp_album_ordem(a, b);
}

void mu_ordenar_faixas(mu_acervo *a) {
	if (a->n > 1) {
		qsort(a->v, (size_t)a->n, sizeof(mu_faixa), cmp_biblioteca);
	}
}

static const mu_acervo *ordena_ctx;
static int cmp_idx_album(const void *x, const void *y) {
	return cmp_album_ordem(&ordena_ctx->v[*(const int *)x], &ordena_ctx->v[*(const int *)y]);
}

void mu_grupos_liberar(mu_grupos *g) {
	for (int i = 0; i < g->n; i++) {
		g_free(g->v[i].nome);
		g_free(g->v[i].artista);
		g_free(g->v[i].idx);
	}
	g_free(g->v);
	memset(g, 0, sizeof(*g));
}

static mu_grupo *grupo_novo(mu_grupos *g, const char *nome, const char *artista) {
	g->v = g_renew(mu_grupo, g->v, (guint)g->n + 1);
	mu_grupo *r = &g->v[g->n++];
	memset(r, 0, sizeof(*r));
	r->nome = g_strdup(nome);
	r->artista = g_strdup(artista);
	r->primeira = -1;
	return r;
}

static void grupo_somar(mu_grupo *g, const mu_acervo *a, int i) {
	g->idx = g_renew(int, g->idx, (guint)g->n + 1);
	g->idx[g->n++] = i;
	g->ntotal = g->n;
	g->duracao += a->v[i].duracao;
	if (a->v[i].ano && (!g->ano || a->v[i].ano < g->ano)) {
		g->ano = a->v[i].ano;
	}
}

static void grupo_ordenar_faixas(mu_grupos *g, const mu_acervo *a) {
	ordena_ctx = a;
	for (int i = 0; i < g->n; i++) {
		if (g->v[i].n > 1) {
			qsort(g->v[i].idx, (size_t)g->v[i].n, sizeof(int), cmp_idx_album);
		}
		g->v[i].primeira = g->v[i].n ? g->v[i].idx[0] : -1;
	}
}

static int g_ordem_atual;
static int cmp_grupo(const void *x, const void *y) {
	const mu_grupo *a = x, *b = y;
	int r = 0;
	if (g_ordem_atual == MU_ORDEM_ARTISTA) {
		r = cmp_texto(a->artista, b->artista);
	} else if (g_ordem_atual == MU_ORDEM_ANO) {
		r = b->ano - a->ano; /* os mais novos primeiro */
	}
	return r ? r : cmp_texto(a->nome, b->nome);
}

void mu_albuns(const mu_acervo *a, int ordem, mu_grupos *g) {
	memset(g, 0, sizeof(*g));
	GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (int i = 0; i < a->n; i++) {
		const mu_faixa *f = &a->v[i];
		char *an = mu_normalizar(f->album), *aa = mu_normalizar(f->album_artista);
		char *chave = g_strdup_printf("%s\x01%s", an, aa);
		g_free(an);
		g_free(aa);
		mu_grupo *gr = NULL;
		gpointer p = g_hash_table_lookup(h, chave);
		if (p) {
			gr = &g->v[GPOINTER_TO_INT(p) - 1];
			g_free(chave);
		} else {
			gr = grupo_novo(g, f->album, f->album_artista[0] ? f->album_artista : f->artista);
			g_hash_table_insert(h, chave, GINT_TO_POINTER(g->n)); /* o índice + 1 (os ponteiros de v mudam ao crescer) */
		}
		grupo_somar(gr, a, i);
		if (gr->artista && !f->album_artista[0] && strcmp(gr->artista, f->artista)) {
			g_free(gr->artista);
			gr->artista = g_strdup("Vários artistas");
		}
	}
	g_hash_table_destroy(h);
	grupo_ordenar_faixas(g, a);
	g_ordem_atual = ordem;
	if (g->n > 1) {
		qsort(g->v, (size_t)g->n, sizeof(mu_grupo), cmp_grupo);
	}
}

void mu_artistas(const mu_acervo *a, mu_grupos *g) {
	memset(g, 0, sizeof(*g));
	GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (int i = 0; i < a->n; i++) {
		char *chave = mu_normalizar(a->v[i].artista);
		gpointer p = g_hash_table_lookup(h, chave);
		mu_grupo *gr;
		if (p) {
			gr = &g->v[GPOINTER_TO_INT(p) - 1];
			g_free(chave);
		} else {
			gr = grupo_novo(g, a->v[i].artista, a->v[i].artista);
			g_hash_table_insert(h, chave, GINT_TO_POINTER(g->n));
		}
		grupo_somar(gr, a, i);
	}
	g_hash_table_destroy(h);
	/* as faixas de um artista, por álbum */
	ordena_ctx = a;
	for (int i = 0; i < g->n; i++) {
		g->v[i].primeira = g->v[i].idx[0];
	}
	g_ordem_atual = MU_ORDEM_TITULO;
	if (g->n > 1) {
		qsort(g->v, (size_t)g->n, sizeof(mu_grupo), cmp_grupo);
	}
}

void mu_pastas(const mu_acervo *a, mu_grupos *g) {
	memset(g, 0, sizeof(*g));
	GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (int i = 0; i < a->n; i++) {
		const char *pasta = a->v[i].pasta;
		gpointer p = g_hash_table_lookup(h, pasta);
		mu_grupo *gr;
		if (p) {
			gr = &g->v[GPOINTER_TO_INT(p) - 1];
		} else {
			char *base = g_path_get_basename(pasta);
			gr = grupo_novo(g, base, pasta);
			g_free(base);
			g_hash_table_insert(h, g_strdup(pasta), GINT_TO_POINTER(g->n));
		}
		grupo_somar(gr, a, i);
	}
	g_hash_table_destroy(h);
	grupo_ordenar_faixas(g, a);
	g_ordem_atual = MU_ORDEM_TITULO;
	if (g->n > 1) {
		qsort(g->v, (size_t)g->n, sizeof(mu_grupo), cmp_grupo);
	}
}

int mu_buscar(const mu_acervo *a, const char *texto, int *idx) {
	char *t = mu_normalizar(texto);
	char **palavras = g_strsplit_set(t, " \t", -1);
	int n = 0;
	for (int i = 0; i < a->n; i++) {
		const mu_faixa *f = &a->v[i];
		char *pasta = g_path_get_basename(f->pasta);
		char *tudo = g_strdup_printf("%s %s %s %s %s", f->titulo, f->artista, f->album, f->album_artista, pasta);
		char *norm = mu_normalizar(tudo);
		bool todas = true;
		for (int k = 0; palavras[k]; k++) {
			if (palavras[k][0] && !strstr(norm, palavras[k])) {
				todas = false;
				break;
			}
		}
		if (todas) {
			idx[n++] = i;
		}
		g_free(pasta);
		g_free(tudo);
		g_free(norm);
	}
	g_strfreev(palavras);
	g_free(t);
	return n;
}

/* ---- capas ---- */

static char *capa_cache_arquivo(const mu_faixa *f) {
	char *chave = g_strdup_printf("%s\x01%s\x01%s", f->pasta, f->album, f->album_artista);
	char *h = g_compute_checksum_for_string(G_CHECKSUM_MD5, chave, -1);
	char *d = pasta_cache();
	char *r = g_build_filename(d, "capas", NULL);
	g_mkdir_with_parents(r, 0700);
	g_free(r);
	char *nome = g_strdup_printf("%s.jpg", h);
	r = g_build_filename(d, "capas", nome, NULL);
	g_free(nome);
	g_free(h);
	g_free(chave);
	g_free(d);
	return r;
}

char *mu_capa_pronta(const mu_faixa *f) {
	static const char *const NOMES[] = { "cover", "folder", "front", "album", "capa", NULL };
	static const char *const EXTS[] = { "jpg", "jpeg", "png", NULL };
	for (int i = 0; NOMES[i]; i++) {
		for (int k = 0; EXTS[k]; k++) {
			char *n = g_strdup_printf("%s.%s", NOMES[i], EXTS[k]);
			char *c = g_build_filename(f->pasta, n, NULL);
			g_free(n);
			if (g_file_test(c, G_FILE_TEST_IS_REGULAR)) {
				return c;
			}
			g_free(c);
		}
	}
	if (f->capa_embutida) {
		char *c = capa_cache_arquivo(f);
		if (g_file_test(c, G_FILE_TEST_IS_REGULAR)) {
			return c;
		}
		g_free(c);
	}
	return NULL;
}

char *mu_capa_gerar(const mu_faixa *f) {
	char *pronta = mu_capa_pronta(f);
	if (pronta || !f->capa_embutida) {
		return pronta;
	}
	char *dest = capa_cache_arquivo(f), *tmp = g_strdup_printf("%s.tmp.jpg", dest);
	char *argv[] = { "ffmpeg", "-v", "error", "-nostdin", "-y", "-i", f->caminho, "-an", "-frames:v", "1", "-vf", "scale='min(500,iw)':-2", "-q:v", "3", tmp, NULL };
	if (rodar_saida(argv, 30, NULL) && g_rename(tmp, dest) == 0) {
		g_free(tmp);
		return dest;
	}
	g_unlink(tmp);
	g_free(tmp);
	g_free(dest);
	return NULL;
}

void mu_capas_limpar(void) {
	char *d = pasta_cache(), *c = g_build_filename(d, "capas", NULL);
	GDir *dir = g_dir_open(c, 0, NULL);
	const char *n;
	while (dir && (n = g_dir_read_name(dir))) {
		char *a = g_build_filename(c, n, NULL);
		g_unlink(a);
		g_free(a);
	}
	if (dir) {
		g_dir_close(dir);
	}
	g_free(c);
	g_free(d);
}

gint64 mu_capas_tamanho(void) {
	char *d = pasta_cache(), *c = g_build_filename(d, "capas", NULL);
	GDir *dir = g_dir_open(c, 0, NULL);
	const char *n;
	gint64 t = 0;
	while (dir && (n = g_dir_read_name(dir))) {
		char *a = g_build_filename(c, n, NULL);
		struct stat st;
		if (stat(a, &st) == 0) {
			t += st.st_size;
		}
		g_free(a);
	}
	if (dir) {
		g_dir_close(dir);
	}
	g_free(c);
	g_free(d);
	return t;
}

/* ---- letras ---- */

void mu_letra_liberar(mu_letra *l) {
	for (int i = 0; i < l->n; i++) {
		g_free(l->v[i].texto);
	}
	g_free(l->v);
	memset(l, 0, sizeof(*l));
}

static int cmp_linha(const void *x, const void *y) {
	double a = ((const mu_linha *)x)->tempo, b = ((const mu_linha *)y)->tempo;
	return a < b ? -1 : a > b;
}

static void linha_somar(mu_letra *l, double t, const char *texto) {
	l->v = g_renew(mu_linha, l->v, (guint)l->n + 1);
	l->v[l->n].tempo = t;
	l->v[l->n].texto = g_strdup(texto);
	l->n++;
}

/* [mm:ss.xx] ou [mm:ss] ou [hh:mm:ss.xx]: o tempo, ou -1 */
static double carimbo(const char *s, const char *fim) {
	int partes = 0;
	char *e;
	const char *p = s;
	double frac = 0;
	long v[3] = { 0 };
	while (p < fim && partes < 3) {
		if (!isdigit((unsigned char)*p)) {
			return -1;
		}
		v[partes++] = strtol(p, &e, 10);
		p = e;
		if (p < fim && *p == ':') {
			p++;
			continue;
		}
		break;
	}
	if (p < fim && (*p == '.' || *p == ',')) {
		char digitos[16] = "0.";
		int nd = 2;
		for (p++; p < fim && isdigit((unsigned char)*p) && nd < 14; p++) {
			digitos[nd++] = *p;
		}
		digitos[nd] = 0;
		frac = g_ascii_strtod(digitos, NULL);
	}
	if (p != fim || partes < 2) {
		return -1;
	}
	double t = partes == 2 ? (double)v[0] * 60 + (double)v[1] : (double)v[0] * 3600 + (double)v[1] * 60 + (double)v[2];
	return t + frac;
}

bool mu_letra_de_texto(const char *texto, mu_letra *l) {
	memset(l, 0, sizeof(*l));
	if (!texto || !*texto) {
		return false;
	}
	char **ls = g_strsplit_set(texto, "\r\n", -1);
	double offset = 0;
	bool achou_tempo = false;
	/* primeiro passo: com tempo */
	for (int i = 0; ls[i]; i++) {
		const char *p = ls[i];
		double ts[16];
		int nt = 0;
		while (*p == '[') {
			const char *f = strchr(p, ']');
			if (!f) {
				break;
			}
			double t = carimbo(p + 1, f);
			if (t >= 0 && nt < 16) {
				ts[nt++] = t;
			} else if (t < 0 && !g_ascii_strncasecmp(p + 1, "offset:", 7)) {
				offset = atof(p + 8) / 1000.0;
			}
			p = f + 1;
		}
		if (nt > 0) {
			GString *tx = g_string_new(NULL);
			for (const char *q = p; *q;) { /* tira as marcas de palavra <mm:ss.xx> */
				if (*q == '<' && strchr(q, '>')) {
					q = strchr(q, '>') + 1;
				} else {
					g_string_append_c(tx, *q++);
				}
			}
			char *t = g_strstrip(g_string_free(tx, FALSE));
			for (int k = 0; k < nt; k++) {
				linha_somar(l, fmax(ts[k] - offset, 0), t);
			}
			g_free(t);
			achou_tempo = true;
		}
	}
	if (achou_tempo) {
		qsort(l->v, (size_t)l->n, sizeof(mu_linha), cmp_linha);
		l->sincronizada = true;
	} else {
		/* sem tempo: o texto todo, uma linha por linha (as vazias seguram o espaço) */
		bool algum = false;
		for (int i = 0; ls[i]; i++) {
			char *t = g_strstrip(g_strdup(ls[i]));
			if (ls[i][0] == '[' && strchr(ls[i], ']') && strlen(ls[i]) < 40 && strchr(ls[i], ':')) { /* [ar:...] [ti:...] */
				g_free(t);
				continue;
			}
			if (*t) {
				algum = true;
			}
			if (*t || (l->n > 0 && *l->v[l->n - 1].texto)) {
				linha_somar(l, -1, t);
			}
			g_free(t);
		}
		if (!algum) {
			mu_letra_liberar(l);
		}
	}
	g_strfreev(ls);
	return l->n > 0;
}

bool mu_letra_ler(const char *caminho, mu_letra *l) {
	memset(l, 0, sizeof(*l));
	char *base = g_strdup(caminho);
	char *ponto = strrchr(base, '.');
	if (ponto) {
		*ponto = 0;
	}
	char *lrc = g_strconcat(base, ".lrc", NULL), *txt = NULL;
	g_free(base);
	bool ok = false;
	if (g_file_get_contents(lrc, &txt, NULL, NULL)) {
		ok = mu_letra_de_texto(txt, l);
		g_free(txt);
	}
	g_free(lrc);
	if (ok) {
		return true;
	}
	char *js = json_do_arquivo(caminho);
	if (!js) {
		return false;
	}
	struct json *j = json_ler(js);
	g_free(js);
	if (!j) {
		return false;
	}
	const char *achada = NULL;
	for (int pass = 0; pass < 2 && !achada; pass++) {
		const struct json *streams = json_chave(j, "streams");
		const struct json *tt = pass == 0 ? json_chave(json_chave(j, "format"), "tags") : (streams && streams->nfilhos ? json_chave(streams->filhos[0], "tags") : NULL);
		for (int i = 0; tt && i < tt->nfilhos && !achada; i++) {
			const char *k = tt->filhos[i]->chave;
			if (k && (!g_ascii_strncasecmp(k, "lyrics", 6) || !g_ascii_strcasecmp(k, "unsyncedlyrics")) && tt->filhos[i]->s && *tt->filhos[i]->s) {
				achada = tt->filhos[i]->s;
			}
		}
	}
	ok = achada && mu_letra_de_texto(achada, l);
	json_liberar(j);
	return ok;
}

int mu_letra_linha(const mu_letra *l, double t) {
	if (!l->sincronizada) {
		return -1;
	}
	int r = -1;
	for (int i = 0; i < l->n && l->v[i].tempo <= t + 0.3; i++) {
		r = i;
	}
	return r;
}

/* ---- playlists ---- */

static char *pasta_playlists(void) {
	char *d = mu_pasta_config(), *p = g_build_filename(d, "playlists", NULL);
	g_free(d);
	g_mkdir_with_parents(p, 0700);
	return p;
}

static char *arquivo_playlist(const char *nome) {
	char *d = pasta_playlists(), *n = g_strconcat(nome, ".m3u8", NULL), *a = g_build_filename(d, n, NULL);
	g_free(d);
	g_free(n);
	return a;
}

bool mu_playlist_nome_valido(const char *nome) {
	if (!nome || !*nome || strlen(nome) > 80 || nome[0] == '.' || !g_utf8_validate(nome, -1, NULL)) {
		return false;
	}
	for (const char *p = nome; *p; p++) {
		if (*p == '/' || *p == '\\' || (unsigned char)*p < 0x20 || *p == 0x7f) {
			return false;
		}
	}
	char *t = g_strstrip(g_strdup(nome));
	bool ok = *t != 0;
	g_free(t);
	return ok;
}

static int cmp_nomes(gconstpointer a, gconstpointer b) {
	return cmp_texto(*(char *const *)a, *(char *const *)b);
}

void mu_playlists_listar(GPtrArray *nomes) {
	char *d = pasta_playlists();
	GDir *dir = g_dir_open(d, 0, NULL);
	const char *n;
	GPtrArray *outros = g_ptr_array_new_with_free_func(g_free);
	while (dir && (n = g_dir_read_name(dir))) {
		if (g_str_has_suffix(n, ".m3u8") && n[0] != '.') {
			char *nome = g_strndup(n, strlen(n) - 5);
			if (strcmp(nome, MU_FAVORITAS)) {
				g_ptr_array_add(outros, nome);
			} else {
				g_free(nome);
			}
		}
	}
	if (dir) {
		g_dir_close(dir);
	}
	g_free(d);
	g_ptr_array_sort(outros, cmp_nomes);
	g_ptr_array_add(nomes, g_strdup(MU_FAVORITAS));
	for (guint i = 0; i < outros->len; i++) {
		g_ptr_array_add(nomes, outros->pdata[i]);
	}
	g_free(g_ptr_array_free(outros, FALSE));
}

bool mu_playlist_ler(const char *nome, GPtrArray *caminhos) {
	char *a = arquivo_playlist(nome), *txt = NULL;
	bool ok = g_file_get_contents(a, &txt, NULL, NULL);
	g_free(a);
	if (ok) {
		char **ls = g_strsplit(txt, "\n", -1);
		for (int i = 0; ls[i]; i++) {
			char *l = g_strstrip(ls[i]);
			if (*l && *l != '#') {
				g_ptr_array_add(caminhos, g_strdup(l));
			}
		}
		g_strfreev(ls);
	}
	g_free(txt);
	return ok || !strcmp(nome, MU_FAVORITAS);
}

bool mu_playlist_gravar(const char *nome, char *const *caminhos, int n) {
	if (!mu_playlist_nome_valido(nome)) {
		return false;
	}
	GString *s = g_string_new("#EXTM3U\n");
	for (int i = 0; i < n; i++) {
		g_string_append_printf(s, "%s\n", caminhos[i]);
	}
	char *a = arquivo_playlist(nome);
	bool ok = g_file_set_contents(a, s->str, (gssize)s->len, NULL);
	g_free(a);
	g_string_free(s, TRUE);
	return ok;
}

bool mu_playlist_criar(const char *nome) {
	if (!mu_playlist_nome_valido(nome)) {
		return false;
	}
	char *a = arquivo_playlist(nome);
	bool existe = g_file_test(a, G_FILE_TEST_EXISTS) || !g_ascii_strcasecmp(nome, MU_FAVORITAS);
	g_free(a);
	return !existe && mu_playlist_gravar(nome, NULL, 0);
}

bool mu_playlist_renomear(const char *nome, const char *novo) {
	if (!strcmp(nome, MU_FAVORITAS) || !mu_playlist_nome_valido(novo) || !g_ascii_strcasecmp(novo, MU_FAVORITAS)) {
		return false;
	}
	char *de = arquivo_playlist(nome), *para = arquivo_playlist(novo);
	bool ok = g_file_test(de, G_FILE_TEST_EXISTS) && !g_file_test(para, G_FILE_TEST_EXISTS) && g_rename(de, para) == 0;
	g_free(de);
	g_free(para);
	return ok;
}

bool mu_playlist_apagar(const char *nome) {
	if (!strcmp(nome, MU_FAVORITAS) || !mu_playlist_nome_valido(nome)) {
		return false;
	}
	char *a = arquivo_playlist(nome);
	bool ok = g_unlink(a) == 0;
	g_free(a);
	return ok;
}

bool mu_playlist_tem(const char *nome, const char *caminho) {
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	mu_playlist_ler(nome, v);
	bool r = false;
	for (guint i = 0; i < v->len && !r; i++) {
		r = !strcmp(v->pdata[i], caminho);
	}
	g_ptr_array_free(v, TRUE);
	return r;
}

bool mu_playlist_adicionar(const char *nome, char *const *caminhos, int n) {
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	if (!mu_playlist_ler(nome, v)) {
		g_ptr_array_free(v, TRUE);
		return false;
	}
	for (int i = 0; i < n; i++) {
		bool tem = false;
		for (guint k = 0; k < v->len && !tem; k++) {
			tem = !strcmp(v->pdata[k], caminhos[i]);
		}
		if (!tem) {
			g_ptr_array_add(v, g_strdup(caminhos[i]));
		}
	}
	bool ok = mu_playlist_gravar(nome, (char *const *)v->pdata, (int)v->len);
	g_ptr_array_free(v, TRUE);
	return ok;
}

bool mu_playlist_remover(const char *nome, const char *caminho) {
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	if (!mu_playlist_ler(nome, v)) {
		g_ptr_array_free(v, TRUE);
		return false;
	}
	for (guint i = 0; i < v->len; i++) {
		if (!strcmp(v->pdata[i], caminho)) {
			g_ptr_array_remove_index(v, i);
			break;
		}
	}
	bool ok = mu_playlist_gravar(nome, (char *const *)v->pdata, (int)v->len);
	g_ptr_array_free(v, TRUE);
	return ok;
}

/* ---- a fila salva ---- */

static char *arquivo_fila(void) {
	char *d = mu_pasta_config();
	g_mkdir_with_parents(d, 0700);
	char *a = g_build_filename(d, "fila.m3u8", NULL);
	g_free(d);
	return a;
}

void mu_fila_gravar(char *const *caminhos, int n, int atual, double posicao) {
	GString *s = g_string_new("#EXTM3U\n");
	g_string_append_printf(s, "#LUMNA-ATUAL:%d\n#LUMNA-POSICAO:%.2f\n", atual, posicao);
	for (int i = 0; i < n; i++) {
		g_string_append_printf(s, "%s\n", caminhos[i]);
	}
	char *a = arquivo_fila();
	g_file_set_contents(a, s->str, (gssize)s->len, NULL);
	g_free(a);
	g_string_free(s, TRUE);
}

bool mu_fila_ler(mu_fila_salva *f) {
	memset(f, 0, sizeof(*f));
	f->caminhos = g_ptr_array_new_with_free_func(g_free);
	char *a = arquivo_fila(), *txt = NULL;
	bool ok = g_file_get_contents(a, &txt, NULL, NULL);
	g_free(a);
	if (ok) {
		char **ls = g_strsplit(txt, "\n", -1);
		for (int i = 0; ls[i]; i++) {
			if (g_str_has_prefix(ls[i], "#LUMNA-ATUAL:")) {
				f->atual = atoi(ls[i] + 13);
			} else if (g_str_has_prefix(ls[i], "#LUMNA-POSICAO:")) {
				f->posicao = g_ascii_strtod(ls[i] + 15, NULL);
			} else if (ls[i][0] && ls[i][0] != '#') {
				g_ptr_array_add(f->caminhos, g_strdup(ls[i]));
			}
		}
		g_strfreev(ls);
	}
	g_free(txt);
	if (!ok || f->caminhos->len == 0) {
		mu_fila_liberar(f);
		return false;
	}
	f->atual = CLAMP(f->atual, 0, (int)f->caminhos->len - 1);
	return true;
}

void mu_fila_liberar(mu_fila_salva *f) {
	if (f->caminhos) {
		g_ptr_array_free(f->caminhos, TRUE);
	}
	memset(f, 0, sizeof(*f));
}
