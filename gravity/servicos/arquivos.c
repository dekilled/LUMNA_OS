#define _GNU_SOURCE
#include "arquivos.h"

#include <dirent.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

/* ---- tipos ---- */

static const struct { const char *ext; enum ar_tipo tipo; } EXTENSOES[] = {
	{ "pdf", AR_PDF },
	{ "png", AR_IMAGEM }, { "jpg", AR_IMAGEM }, { "jpeg", AR_IMAGEM }, { "gif", AR_IMAGEM }, { "webp", AR_IMAGEM }, { "bmp", AR_IMAGEM }, { "svg", AR_IMAGEM },
	{ "avif", AR_IMAGEM }, { "tif", AR_IMAGEM }, { "tiff", AR_IMAGEM }, { "ico", AR_IMAGEM }, { "heic", AR_IMAGEM },
	{ "mp3", AR_AUDIO }, { "flac", AR_AUDIO }, { "ogg", AR_AUDIO }, { "opus", AR_AUDIO }, { "wav", AR_AUDIO }, { "m4a", AR_AUDIO }, { "aac", AR_AUDIO },
	{ "mp4", AR_VIDEO }, { "mkv", AR_VIDEO }, { "webm", AR_VIDEO }, { "avi", AR_VIDEO }, { "mov", AR_VIDEO }, { "m4v", AR_VIDEO },
	{ "txt", AR_TEXTO }, { "log", AR_TEXTO }, { "rtf", AR_TEXTO },
	{ "md", AR_MARKDOWN }, { "markdown", AR_MARKDOWN },
	{ "ods", AR_PLANILHA }, { "xls", AR_PLANILHA }, { "xlsx", AR_PLANILHA }, { "csv", AR_PLANILHA }, { "tsv", AR_PLANILHA },
	{ "odp", AR_APRESENTACAO }, { "ppt", AR_APRESENTACAO }, { "pptx", AR_APRESENTACAO },
	{ "odt", AR_DOCUMENTO }, { "doc", AR_DOCUMENTO }, { "docx", AR_DOCUMENTO }, { "epub", AR_DOCUMENTO },
	{ "zip", AR_COMPACTADO }, { "tar", AR_COMPACTADO }, { "gz", AR_COMPACTADO }, { "xz", AR_COMPACTADO }, { "bz2", AR_COMPACTADO }, { "zst", AR_COMPACTADO },
	{ "7z", AR_COMPACTADO }, { "rar", AR_COMPACTADO },
	{ "c", AR_CODIGO }, { "h", AR_CODIGO }, { "cpp", AR_CODIGO }, { "py", AR_CODIGO }, { "js", AR_CODIGO }, { "ts", AR_CODIGO }, { "rs", AR_CODIGO },
	{ "go", AR_CODIGO }, { "java", AR_CODIGO }, { "sh", AR_CODIGO }, { "json", AR_CODIGO }, { "xml", AR_CODIGO }, { "html", AR_CODIGO }, { "css", AR_CODIGO },
	{ "toml", AR_CODIGO }, { "yaml", AR_CODIGO }, { "yml", AR_CODIGO }, { "ini", AR_CODIGO }, { "lua", AR_CODIGO },
};

static const char *extensao(const char *nome) {
	const char *p = strrchr(nome, '.');
	return p && p != nome && p[1] ? p + 1 : NULL;
}

enum ar_tipo ar_tipo_do_nome(const char *nome) {
	const char *ext = extensao(nome);
	if (!ext) {
		return AR_OUTRO;
	}
	for (size_t i = 0; i < sizeof(EXTENSOES) / sizeof(EXTENSOES[0]); i++) {
		if (!g_ascii_strcasecmp(ext, EXTENSOES[i].ext)) {
			return EXTENSOES[i].tipo;
		}
	}
	return AR_OUTRO;
}

const char *ar_tipo_nome(const ar_entrada *e) {
	switch (e->tipo) {
	case AR_PASTA: return "Pasta";
	case AR_PDF: return "PDF";
	case AR_IMAGEM: return "Imagem";
	case AR_AUDIO: return "Áudio";
	case AR_VIDEO: return "Vídeo";
	case AR_TEXTO: return "Texto";
	case AR_MARKDOWN: return "Markdown";
	case AR_PLANILHA: return "Planilha";
	case AR_APRESENTACAO: return "Apresentação";
	case AR_DOCUMENTO: return "Documento";
	case AR_COMPACTADO: return "Arquivo compactado";
	case AR_CODIGO: return "Código";
	default: return e->link ? "Atalho" : "Arquivo";
	}
}

void ar_selo(const ar_entrada *e, char *saida, size_t tam) {
	const char *ext = extensao(e->nome);
	if (e->pasta || !ext) {
		snprintf(saida, tam, "%s", e->pasta ? "" : "·");
		return;
	}
	char *maiusc = g_ascii_strup(ext, -1);
	snprintf(saida, tam, "%.4s", maiusc);
	g_free(maiusc);
}

bool ar_no_filtro(const ar_entrada *e, enum ar_filtro f) {
	switch (f) {
	case AR_F_TUDO: return true;
	case AR_F_PASTAS: return e->pasta;
	case AR_F_DOCUMENTOS:
		return e->tipo == AR_PDF || e->tipo == AR_TEXTO || e->tipo == AR_MARKDOWN || e->tipo == AR_PLANILHA || e->tipo == AR_APRESENTACAO ||
			e->tipo == AR_DOCUMENTO;
	case AR_F_IMAGENS: return e->tipo == AR_IMAGEM;
	case AR_F_MIDIA: return e->tipo == AR_AUDIO || e->tipo == AR_VIDEO;
	case AR_F_OUTROS: return !e->pasta && !ar_no_filtro(e, AR_F_DOCUMENTOS) && !ar_no_filtro(e, AR_F_IMAGENS) && !ar_no_filtro(e, AR_F_MIDIA);
	}
	return true;
}

/* ---- listar ---- */

bool ar_entrada_de(const char *caminho, ar_entrada *e) {
	memset(e, 0, sizeof(*e));
	char *base = g_path_get_basename(caminho);
	bool cabe = strlen(base) < sizeof(e->nome);
	if (cabe) {
		snprintf(e->nome, sizeof(e->nome), "%s", base);
	}
	g_free(base);
	if (!cabe) {
		return false;
	}
	e->oculto = e->nome[0] == '.';
	e->itens = -1;
	struct stat st, lst;
	if (lstat(caminho, &lst) != 0) {
		return false;
	}
	e->link = S_ISLNK(lst.st_mode);
	if (e->link && stat(caminho, &st) != 0) {
		st = lst; /* um atalho quebrado: mostra o próprio atalho */
	} else if (!e->link) {
		st = lst;
	}
	e->pasta = S_ISDIR(st.st_mode);
	e->tamanho = e->pasta ? 0 : st.st_size;
	e->mtime = st.st_mtime;
	e->modo = st.st_mode;
	e->uid = st.st_uid;
	e->tipo = e->pasta ? AR_PASTA : ar_tipo_do_nome(e->nome);
	return true;
}

bool ar_listar(const char *pasta, bool ocultos, ar_lista *saida, char **erro) {
	memset(saida, 0, sizeof(*saida));
	GError *err = NULL;
	GDir *dir = g_dir_open(pasta, 0, &err);
	if (!dir) {
		if (erro) {
			*erro = g_strdup(err && err->message ? err->message : "não abriu");
		}
		g_clear_error(&err);
		return false;
	}
	saida->pasta = g_strdup(pasta);
	const char *nome;
	while ((nome = g_dir_read_name(dir))) {
		bool oculto = nome[0] == '.';
		if (oculto && !ocultos) {
			continue;
		}
		if (strlen(nome) >= sizeof(saida->v[0].nome)) {
			continue;
		}
		if (saida->n == saida->cap) {
			saida->cap = saida->cap ? saida->cap * 2 : 64;
			saida->v = g_realloc(saida->v, (size_t)saida->cap * sizeof(ar_entrada));
		}
		ar_entrada *e = &saida->v[saida->n++];
		char *caminho = g_build_filename(pasta, nome, NULL);
		if (!ar_entrada_de(caminho, e)) {
			saida->n--;
		}
		g_free(caminho);
	}
	g_dir_close(dir);
	return true;
}

void ar_liberar(ar_lista *l) {
	g_free(l->pasta);
	g_free(l->v);
	memset(l, 0, sizeof(*l));
}

/* ---- ordem ---- */

/* sem acento e sem caixa, com números por valor: a chave de comparação do nome */
static char *chave_nome(const char *s) {
	/* um nome de arquivo pode não ser UTF-8 (pendrives antigos, zips, NTFS: "caf\xe9.txt"): g_utf8_normalize devolve NULL e o Atlas caía ao ordenar */
	char *valido = g_utf8_make_valid(s, -1);
	char *norm = g_utf8_normalize(valido, -1, G_NORMALIZE_NFD);
	if (!norm) {
		norm = g_strdup(valido);
	}
	g_free(valido);
	GString *g = g_string_new(NULL);
	for (const char *p = norm; *p; p = g_utf8_next_char(p)) {
		gunichar c = g_utf8_get_char(p);
		GUnicodeType t = g_unichar_type(c);
		if (t == G_UNICODE_NON_SPACING_MARK) {
			continue;
		}
		g_string_append_unichar(g, g_unichar_tolower(c));
	}
	g_free(norm);
	return g_string_free(g, FALSE);
}

bool ar_busca_combina(const char *nome, const char *termo) {
	if (!termo || !*termo) {
		return true;
	}
	char *a = chave_nome(nome), *b = chave_nome(termo);
	bool ok = strstr(a, b) != NULL;
	g_free(a);
	g_free(b);
	return ok;
}

static int comparar_naturais(const char *a, const char *b) {
	while (*a && *b) {
		if (g_ascii_isdigit(*a) && g_ascii_isdigit(*b)) {
			char *fa, *fb;
			unsigned long long na = strtoull(a, &fa, 10), nb = strtoull(b, &fb, 10);
			if (na != nb) {
				return na < nb ? -1 : 1;
			}
			a = fa;
			b = fb;
			continue;
		}
		if (*a != *b) {
			return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
		}
		a++;
		b++;
	}
	return *a ? 1 : *b ? -1 : 0;
}

static bool pastas_primeiro = true;
void ar_ordenar_pastas_primeiro(bool sim) { pastas_primeiro = sim; }

static enum ar_ordem ordem_atual;
static bool desc_atual;

/* a entrada com a chave de comparação do nome já calculada: a chave (normalizar, tirar acentos) é cara, e calculada a cada comparação dava
 * milhões de normalizações numa pasta de 20 mil arquivos (segundos de CPU parado). Agora é uma por entrada. */
struct chaveada {
	const ar_entrada *e;
	char *k;
};

static int cmp_entrada(const void *pa, const void *pb) {
	const struct chaveada *ca = pa, *cb = pb;
	const ar_entrada *a = ca->e, *b = cb->e;
	if (pastas_primeiro && a->pasta != b->pasta) {
		return a->pasta ? -1 : 1; /* as pastas vêm primeiro (a menos que a pessoa tenha desligado) */
	}
	int r = 0;
	switch (ordem_atual) {
	case AR_O_DATA: r = a->mtime < b->mtime ? -1 : a->mtime > b->mtime ? 1 : 0; break;
	case AR_O_TAMANHO: r = a->tamanho < b->tamanho ? -1 : a->tamanho > b->tamanho ? 1 : 0; break;
	case AR_O_TIPO: r = (int)a->tipo - (int)b->tipo; break;
	default: break;
	}
	if (!r) {
		r = comparar_naturais(ca->k, cb->k);
		if (ordem_atual != AR_O_NOME) {
			return r; /* o desempate é sempre A-Z */
		}
	}
	return desc_atual ? -r : r;
}

void ar_ordenar(ar_lista *l, enum ar_ordem ordem, bool decrescente) {
	if (l->n < 2 || !l->v) {
		return;
	}
	ordem_atual = ordem;
	desc_atual = decrescente;
	struct chaveada *c = g_new(struct chaveada, (gsize)l->n);
	for (int i = 0; i < l->n; i++) {
		c[i].e = &l->v[i];
		c[i].k = chave_nome(l->v[i].nome);
	}
	qsort(c, (size_t)l->n, sizeof(*c), cmp_entrada);
	ar_entrada *novo = g_new(ar_entrada, (gsize)l->cap > (gsize)l->n ? (gsize)l->cap : (gsize)l->n);
	for (int i = 0; i < l->n; i++) {
		novo[i] = *c[i].e;
		g_free(c[i].k);
	}
	g_free(c);
	g_free(l->v);
	l->v = novo;
}

int ar_filtrar(const ar_lista *l, const char *busca, enum ar_filtro filtro, int *indices) {
	int n = 0;
	char *chave = busca && *busca ? chave_nome(busca) : NULL;
	for (int i = 0; i < l->n; i++) {
		if (!ar_no_filtro(&l->v[i], filtro)) {
			continue;
		}
		if (chave) {
			char *k = chave_nome(l->v[i].nome);
			bool ok = strstr(k, chave) != NULL;
			g_free(k);
			if (!ok) {
				continue;
			}
		}
		indices[n++] = i;
	}
	g_free(chave);
	return n;
}

int ar_contar_itens(const char *pasta, bool ocultos) {
	GDir *dir = g_dir_open(pasta, 0, NULL);
	if (!dir) {
		return -1;
	}
	int n = 0;
	const char *nome;
	while ((nome = g_dir_read_name(dir))) {
		if (nome[0] != '.' || ocultos) {
			n++;
		}
	}
	g_dir_close(dir);
	return n;
}

bool ar_tamanho_pasta(const char *pasta, int teto, int64_t *bytes, int *itens) {
	*bytes = 0;
	*itens = 0;
	GQueue *fila = g_queue_new();
	g_queue_push_tail(fila, g_strdup(pasta));
	bool completo = true;
	while (!g_queue_is_empty(fila)) {
		char *atual = g_queue_pop_head(fila);
		GDir *dir = g_dir_open(atual, 0, NULL);
		const char *nome;
		while (dir && (nome = g_dir_read_name(dir))) {
			if (*itens >= teto) {
				completo = false;
				break;
			}
			char *caminho = g_build_filename(atual, nome, NULL);
			struct stat st;
			if (lstat(caminho, &st) == 0) {
				(*itens)++;
				if (S_ISDIR(st.st_mode)) {
					g_queue_push_tail(fila, caminho);
					continue;
				}
				*bytes += st.st_size;
			}
			g_free(caminho);
		}
		if (dir) {
			g_dir_close(dir);
		}
		g_free(atual);
		if (!completo) {
			break;
		}
	}
	while (!g_queue_is_empty(fila)) {
		g_free(g_queue_pop_head(fila));
	}
	g_queue_free(fila);
	return completo;
}

/* ---- texto ---- */

void ar_tamanho_humano(int64_t bytes, char *saida, size_t tam) {
	static const char *const un[] = { "B", "KB", "MB", "GB", "TB" };
	double v = (double)bytes;
	int u = 0;
	while (v >= 1000 && u < 4) {
		v /= 1000;
		u++;
	}
	if (u == 0) {
		snprintf(saida, tam, "%lld B", (long long)bytes);
		return;
	}
	char num[32];
	snprintf(num, sizeof(num), v >= 100 ? "%.0f" : "%.1f", v);
	for (char *p = num; *p; p++) {
		if (*p == '.') {
			*p = ','; /* vírgula decimal */
		}
	}
	size_t n = strlen(num);
	if (n > 2 && !strcmp(num + n - 2, ",0")) {
		num[n - 2] = '\0'; /* "2,0 MB" é "2 MB" */
	}
	snprintf(saida, tam, "%s %s", num, un[u]);
}

void ar_data_humana(time_t t, time_t agora, char *saida, size_t tam) {
	static const char *const meses[] = { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" };
	struct tm a, d;
	localtime_r(&agora, &a);
	localtime_r(&t, &d);
	int dias = (a.tm_year - d.tm_year) * 366 + (a.tm_yday - d.tm_yday);
	if (a.tm_year == d.tm_year && a.tm_yday == d.tm_yday) {
		snprintf(saida, tam, "Hoje, %02d:%02d", d.tm_hour, d.tm_min);
	} else if ((a.tm_year == d.tm_year && a.tm_yday - d.tm_yday == 1) || (a.tm_year == d.tm_year + 1 && a.tm_yday == 0 && d.tm_mon == 11 && d.tm_mday == 31)) {
		snprintf(saida, tam, "Ontem, %02d:%02d", d.tm_hour, d.tm_min);
	} else if (a.tm_year == d.tm_year && dias > 0) {
		snprintf(saida, tam, "%d %s, %02d:%02d", d.tm_mday, meses[d.tm_mon], d.tm_hour, d.tm_min);
	} else {
		snprintf(saida, tam, "%d %s %d", d.tm_mday, meses[d.tm_mon], d.tm_year + 1900);
	}
}

void ar_permissoes_texto(mode_t m, char *saida, size_t tam) {
	char s[10];
	const char *rwx = "rwxrwxrwx";
	for (int i = 0; i < 9; i++) {
		s[i] = m & (1u << (8 - i)) ? rwx[i] : '-';
	}
	s[9] = '\0';
	snprintf(saida, tam, "%s", s);
}

/* ---- histórico ---- */

void ar_hist_ir(ar_hist *h, const char *caminho) {
	if (h->n > 0 && !strcmp(h->v[h->atual], caminho)) {
		return;
	}
	for (int i = h->atual + 1; i < h->n; i++) {
		g_free(h->v[i]);
	}
	h->n = h->n ? h->atual + 1 : 0;
	h->v = g_realloc(h->v, (size_t)(h->n + 1) * sizeof(char *));
	h->v[h->n++] = g_strdup(caminho);
	h->atual = h->n - 1;
}
const char *ar_hist_atual(const ar_hist *h) { return h->n ? h->v[h->atual] : NULL; }
bool ar_hist_pode_voltar(const ar_hist *h) { return h->atual > 0; }
bool ar_hist_pode_avancar(const ar_hist *h) { return h->n && h->atual < h->n - 1; }
const char *ar_hist_voltar(ar_hist *h) { return ar_hist_pode_voltar(h) ? h->v[--h->atual] : NULL; }
const char *ar_hist_avancar(ar_hist *h) { return ar_hist_pode_avancar(h) ? h->v[++h->atual] : NULL; }
void ar_hist_liberar(ar_hist *h) {
	for (int i = 0; i < h->n; i++) {
		g_free(h->v[i]);
	}
	g_free(h->v);
	memset(h, 0, sizeof(*h));
}

/* ---- caminhos ---- */

char *ar_unir(const char *pasta, const char *nome) { return g_build_filename(pasta, nome, NULL); }

char *ar_pai(const char *caminho) {
	if (!strcmp(caminho, "/")) {
		return NULL;
	}
	return g_path_get_dirname(caminho);
}

static const char *rotulo_pasta_do_usuario(const char *caminho) {
	static const struct { GUserDirectory d; const char *rotulo; } U[] = {
		{ G_USER_DIRECTORY_DOCUMENTS, "Documentos" }, { G_USER_DIRECTORY_PICTURES, "Imagens" }, { G_USER_DIRECTORY_MUSIC, "Músicas" },
		{ G_USER_DIRECTORY_DOWNLOAD, "Downloads" }, { G_USER_DIRECTORY_VIDEOS, "Vídeos" }, { G_USER_DIRECTORY_TEMPLATES, "Modelos" },
		{ G_USER_DIRECTORY_DESKTOP, "Área de trabalho" }, { G_USER_DIRECTORY_PUBLIC_SHARE, "Público" },
	};
	for (size_t i = 0; i < sizeof(U) / sizeof(U[0]); i++) {
		const char *d = g_get_user_special_dir(U[i].d);
		if (d && !strcmp(d, caminho) && strcmp(d, g_get_home_dir())) {
			return U[i].rotulo;
		}
	}
	return NULL;
}

int ar_partes(const char *caminho, ar_parte *v, int max) {
	const char *casa = g_get_home_dir();
	size_t lc = strlen(casa);
	int n = 0;
	char *base;
	const char *resto;
	if (!strncmp(caminho, casa, lc) && (caminho[lc] == '/' || caminho[lc] == '\0')) {
		base = g_strdup(casa);
		snprintf(v[n].rotulo, sizeof(v[n].rotulo), "Home");
		v[n++].caminho = g_strdup(casa);
		resto = caminho + lc;
	} else {
		base = g_strdup("/");
		snprintf(v[n].rotulo, sizeof(v[n].rotulo), "Computador");
		v[n++].caminho = g_strdup("/");
		resto = caminho;
	}
	char **partes = g_strsplit(resto, "/", -1);
	char *acumulado = base;
	for (int i = 0; partes[i] && n < max; i++) {
		if (!partes[i][0]) {
			continue;
		}
		char *novo = g_build_filename(acumulado, partes[i], NULL);
		g_free(acumulado);
		acumulado = novo;
		const char *amigo = rotulo_pasta_do_usuario(acumulado);
		snprintf(v[n].rotulo, sizeof(v[n].rotulo), "%s", amigo ? amigo : partes[i]);
		v[n++].caminho = g_strdup(acumulado);
	}
	g_free(acumulado);
	g_strfreev(partes);
	return n;
}

void ar_partes_liberar(ar_parte *v, int n) {
	for (int i = 0; i < n; i++) {
		g_free(v[i].caminho);
	}
}

/* ---- lugares ---- */

static void lugar_novo(ar_lugar *v, int *n, int max, const char *rotulo, const char *caminho, const char *icone) {
	if (*n >= max) {
		return;
	}
	snprintf(v[*n].rotulo, sizeof(v[*n].rotulo), "%s", rotulo);
	v[*n].caminho = g_strdup(caminho);
	v[*n].icone = icone;
	(*n)++;
}

int ar_colecoes(ar_lugar *v, int max) {
	int n = 0;
	lugar_novo(v, &n, max, "Home", g_get_home_dir(), "folder");
	static const struct { GUserDirectory d; const char *rotulo, *icone; } U[] = {
		{ G_USER_DIRECTORY_DOCUMENTS, "Documentos", "document" }, { G_USER_DIRECTORY_PICTURES, "Imagens", "image" }, { G_USER_DIRECTORY_MUSIC, "Músicas", "music" },
		{ G_USER_DIRECTORY_DOWNLOAD, "Downloads", "download" }, { G_USER_DIRECTORY_VIDEOS, "Vídeos", "video" }, { G_USER_DIRECTORY_TEMPLATES, "Templates", "file" },
	};
	for (size_t i = 0; i < sizeof(U) / sizeof(U[0]); i++) {
		const char *d = g_get_user_special_dir(U[i].d);
		if (d && strcmp(d, g_get_home_dir()) && g_file_test(d, G_FILE_TEST_IS_DIR)) {
			lugar_novo(v, &n, max, U[i].rotulo, d, U[i].icone);
		}
	}
	char *emma = g_build_filename(g_get_home_dir(), "Emma", NULL);
	if (g_file_test(emma, G_FILE_TEST_IS_DIR)) {
		lugar_novo(v, &n, max, "Emma", emma, "user");
	}
	g_free(emma);
	return n;
}

void ar_lugares_liberar(ar_lugar *v, int n) {
	for (int i = 0; i < n; i++) {
		g_free(v[i].caminho);
	}
}

static char *arq_favoritos(void) { return g_build_filename(g_get_user_config_dir(), "lumna", "atlas", "favoritos", NULL); }
static char *arq_recentes(void) { return g_build_filename(g_get_user_data_dir(), "lumna", "atlas", "recentes", NULL); }

static char **ler_linhas(const char *arquivo) {
	char *txt = NULL;
	if (!g_file_get_contents(arquivo, &txt, NULL, NULL)) {
		return g_new0(char *, 1);
	}
	char **linhas = g_strsplit(txt, "\n", -1);
	g_free(txt);
	return linhas;
}

static void gravar_linhas(const char *arquivo, char **linhas) {
	char *pasta = g_path_get_dirname(arquivo);
	g_mkdir_with_parents(pasta, 0700);
	g_free(pasta);
	GString *s = g_string_new(NULL);
	for (int i = 0; linhas[i]; i++) {
		if (linhas[i][0]) {
			g_string_append_printf(s, "%s\n", linhas[i]);
		}
	}
	g_file_set_contents(arquivo, s->str, -1, NULL);
	g_string_free(s, TRUE);
}

static int lugares_do_arquivo(const char *arquivo, ar_lugar *v, int max) {
	char **linhas = ler_linhas(arquivo);
	int n = 0;
	for (int i = 0; linhas[i] && n < max; i++) {
		if (linhas[i][0] && g_file_test(linhas[i], G_FILE_TEST_EXISTS)) {
			char *base = g_path_get_basename(linhas[i]);
			bool pasta = g_file_test(linhas[i], G_FILE_TEST_IS_DIR);
			lugar_novo(v, &n, max, base, linhas[i], pasta ? "folder" : "file");
			g_free(base);
		}
	}
	g_strfreev(linhas);
	return n;
}

int ar_favoritos(ar_lugar *v, int max) {
	char *a = arq_favoritos();
	int n = lugares_do_arquivo(a, v, max);
	g_free(a);
	return n;
}

bool ar_eh_favorito(const char *caminho) {
	char *a = arq_favoritos();
	char **linhas = ler_linhas(a);
	bool sim = false;
	for (int i = 0; linhas[i]; i++) {
		sim |= !strcmp(linhas[i], caminho);
	}
	g_strfreev(linhas);
	g_free(a);
	return sim;
}

bool ar_favorito_alternar(const char *caminho) {
	char *a = arq_favoritos();
	char **linhas = ler_linhas(a);
	GPtrArray *novo = g_ptr_array_new();
	bool tinha = false;
	for (int i = 0; linhas[i]; i++) {
		if (!strcmp(linhas[i], caminho)) {
			tinha = true;
		} else if (linhas[i][0]) {
			g_ptr_array_add(novo, linhas[i]);
		}
	}
	if (!tinha) {
		g_ptr_array_add(novo, (gpointer)caminho);
	}
	g_ptr_array_add(novo, NULL);
	gravar_linhas(a, (char **)novo->pdata);
	g_ptr_array_free(novo, TRUE);
	g_strfreev(linhas);
	g_free(a);
	return !tinha;
}

int ar_recentes(ar_lugar *v, int max) {
	char *a = arq_recentes();
	int n = lugares_do_arquivo(a, v, max);
	g_free(a);
	return n;
}

void ar_recente_registrar(const char *caminho) {
	char *a = arq_recentes();
	char **linhas = ler_linhas(a);
	GPtrArray *novo = g_ptr_array_new();
	g_ptr_array_add(novo, (gpointer)caminho);
	for (int i = 0; linhas[i] && novo->len < 50; i++) {
		if (linhas[i][0] && strcmp(linhas[i], caminho)) {
			g_ptr_array_add(novo, linhas[i]);
		}
	}
	g_ptr_array_add(novo, NULL);
	gravar_linhas(a, (char **)novo->pdata);
	g_ptr_array_free(novo, TRUE);
	g_strfreev(linhas);
	g_free(a);
}

/* ---- o caminho digitado ---- */

static char *expandir_til(const char *t) {
	if (t[0] == '~' && (t[1] == '/' || t[1] == '\0')) {
		return g_strconcat(g_get_home_dir(), t + 1, NULL);
	}
	return g_strdup(t);
}

char *ar_caminho_digitado(const char *texto, const char *pasta_atual) {
	char *t = g_strstrip(g_strdup(texto));
	if (!*t) {
		g_free(t);
		return NULL;
	}
	char *x = expandir_til(t);
	g_free(t);
	char *abs = x[0] == '/' ? g_strdup(x) : g_build_filename(pasta_atual ? pasta_atual : g_get_home_dir(), x, NULL);
	g_free(x);
	char *limpo = g_canonicalize_filename(abs, NULL);
	g_free(abs);
	if (!g_file_test(limpo, G_FILE_TEST_IS_DIR)) {
		g_free(limpo);
		return NULL;
	}
	return limpo;
}

static void sugestao_nova(ar_sugestao *v, int *n, int max, const char *nome, const char *caminho, bool recente) {
	if (*n >= max) {
		return;
	}
	for (int i = 0; i < *n; i++) {
		if (!strcmp(v[i].caminho, caminho)) {
			return;
		}
	}
	snprintf(v[*n].nome, sizeof(v[*n].nome), "%s", nome);
	v[*n].caminho = g_strdup(caminho);
	v[*n].recente = recente;
	(*n)++;
}

static int cmp_sugestao(const void *a, const void *b) {
	const ar_sugestao *x = a, *y = b;
	char *kx = chave_nome(x->nome), *ky = chave_nome(y->nome);
	int r = strcmp(kx, ky);
	g_free(kx);
	g_free(ky);
	return r;
}

int ar_sugestoes(const char *texto, ar_sugestao *v, int max) {
	int n = 0;
	char *t = g_strdup(texto ? texto : "");
	g_strstrip(t);
	if (!*t) {
		g_free(t);
		return 0;
	}
	char *x = expandir_til(t);
	char *dir, *prefixo;
	if (g_str_has_suffix(x, "/")) {
		dir = g_strdup(x);
		prefixo = g_strdup("");
	} else if (strchr(x, '/')) {
		dir = g_path_get_dirname(x);
		prefixo = g_path_get_basename(x);
	} else {
		dir = NULL; /* só um nome: procura nos recentes */
		prefixo = g_strdup(x);
	}
	char *chave = chave_nome(prefixo);
	if (dir && g_file_test(dir, G_FILE_TEST_IS_DIR)) {
		/* acha as pastas que combinam (até 512), ordena e fica com as primeiras do que cabe em v */
		ar_sugestao *achadas = g_new(ar_sugestao, 512);
		int na = 0;
		GDir *d = g_dir_open(dir, 0, NULL);
		const char *nome;
		/* uma pasta com milhares de itens (/usr/lib, /tmp) não pode travar a digitação: o caminho rápido compara o começo sem alocar nada;
		 * só nome com acento (que precisa de normalização) pega o caminho lento */
		size_t lp = strlen(prefixo);
		bool prefixo_ascii = g_str_is_ascii(prefixo);
		gint64 limite = g_get_monotonic_time() + 40 * 1000;
		int lidos = 0;
		while (d && (nome = g_dir_read_name(d)) && na < 512) {
			if ((++lidos & 255) == 0 && g_get_monotonic_time() > limite) {
				break;
			}
			if (nome[0] == '.' && prefixo[0] != '.') {
				continue;
			}
			bool combina;
			if (prefixo_ascii && g_str_is_ascii(nome)) {
				combina = !g_ascii_strncasecmp(nome, prefixo, lp);
			} else {
				char *k = chave_nome(nome);
				combina = g_str_has_prefix(k, chave);
				g_free(k);
			}
			if (!combina) {
				continue;
			}
			char *c = g_build_filename(dir, nome, NULL);
			if (g_file_test(c, G_FILE_TEST_IS_DIR)) {
				int antes = na;
				sugestao_nova(achadas, &na, 512, nome, c, false);
				(void)antes;
			}
			g_free(c);
		}
		if (d) {
			g_dir_close(d);
		}
		qsort(achadas, (size_t)na, sizeof(ar_sugestao), cmp_sugestao);
		int levar = na < 6 ? na : 6;
		for (int i = 0; i < na; i++) {
			if (i < levar && n < max) {
				v[n++] = achadas[i];
			} else {
				g_free(achadas[i].caminho);
			}
		}
		g_free(achadas);
	}
	/* os recentes que combinam */
	ar_lugar rec[50];
	int nr = ar_recentes(rec, 50);
	for (int i = 0; i < nr && n < max; i++) {
		if (g_file_test(rec[i].caminho, G_FILE_TEST_IS_DIR) && ar_busca_combina(rec[i].caminho, x)) {
			sugestao_nova(v, &n, max, rec[i].caminho, rec[i].caminho, true);
		}
	}
	ar_lugares_liberar(rec, nr);
	g_free(chave);
	g_free(prefixo);
	g_free(dir);
	g_free(x);
	g_free(t);
	return n;
}

void ar_sugestoes_liberar(ar_sugestao *v, int n) {
	for (int i = 0; i < n; i++) {
		g_free(v[i].caminho);
	}
}

/* ---- cor e ícone de uma pasta ---- */

const struct ar_cor AR_CORES[AR_CORES_N] = {
	{ "Padrão", 0 }, { "Azul", 0x3E63DD }, { "Turquesa", 0x00A2C7 }, { "Verde", 0x30A46C }, { "Amarelo", 0xE5A100 },
	{ "Laranja", 0xF76B15 }, { "Vermelho", 0xE5484D }, { "Rosa", 0xD6409F }, { "Cinza", 0x8B8D98 },
};
const char *const AR_ICONES[] = { "star", "heart", "music", "image", "video", "camera", "code", "document", "download", "gamepad", "globe", "bag", "mail", "lock", "user", "leaf", NULL };

static GKeyFile *estilos;
static char *estilos_arq;

static GKeyFile *estilos_ler(void) {
	char *a = g_build_filename(g_get_user_config_dir(), "lumna", "atlas", "pastas.ini", NULL);
	if (estilos && estilos_arq && !strcmp(estilos_arq, a)) {
		g_free(a);
		return estilos;
	}
	if (estilos) {
		g_key_file_free(estilos);
	}
	g_free(estilos_arq);
	estilos_arq = a;
	estilos = g_key_file_new();
	g_key_file_load_from_file(estilos, a, G_KEY_FILE_NONE, NULL);
	return estilos;
}

void ar_pasta_estilo(const char *caminho, uint32_t *rgb, char *icone, size_t tam) {
	GKeyFile *kf = estilos_ler();
	*rgb = 0;
	icone[0] = '\0';
	if (!g_key_file_has_group(kf, caminho)) {
		return;
	}
	char *cor = g_key_file_get_string(kf, caminho, "cor", NULL), *ic = g_key_file_get_string(kf, caminho, "icone", NULL);
	if (cor && cor[0] == '#' && strlen(cor) == 7) {
		*rgb = (uint32_t)strtoul(cor + 1, NULL, 16);
	}
	if (ic) {
		/* só os emblemas conhecidos (um arquivo editado à mão não escolhe o que desenhar) */
		for (int i = 0; AR_ICONES[i]; i++) {
			if (!strcmp(ic, AR_ICONES[i])) {
				snprintf(icone, tam, "%s", ic);
			}
		}
	}
	g_free(cor);
	g_free(ic);
}

void ar_pasta_estilo_definir(const char *caminho, uint32_t rgb, const char *icone) {
	GKeyFile *kf = estilos_ler();
	g_key_file_remove_group(kf, caminho, NULL);
	if (rgb || (icone && *icone)) {
		char cor[16];
		snprintf(cor, sizeof(cor), "#%06X", rgb & 0xFFFFFF);
		if (rgb) {
			g_key_file_set_string(kf, caminho, "cor", cor);
		}
		if (icone && *icone) {
			g_key_file_set_string(kf, caminho, "icone", icone);
		}
	}
	char *pasta = g_path_get_dirname(estilos_arq);
	g_mkdir_with_parents(pasta, 0700);
	g_free(pasta);
	g_key_file_save_to_file(kf, estilos_arq, NULL);
}

/* ---- unidades ---- */

int ar_unidades(ar_unidade *v, int max) {
	char *txt = NULL;
	int n = 0;
	if (!g_file_get_contents("/proc/self/mountinfo", &txt, NULL, NULL)) {
		return 0;
	}
	char **linhas = g_strsplit(txt, "\n", -1);
	g_free(txt);
	for (int i = 0; linhas[i] && n < max; i++) {
		/* id pai maj:min raiz PONTO opções ... - TIPO fonte superopções */
		char **c = g_strsplit(linhas[i], " ", -1);
		int nc = (int)g_strv_length(c);
		int traco = -1;
		for (int k = 6; k < nc; k++) {
			if (!strcmp(c[k], "-")) {
				traco = k;
				break;
			}
		}
		if (traco < 0 || traco + 2 >= nc) {
			g_strfreev(c);
			continue;
		}
		char *ponto = g_strcompress(c[4]); /* \040 vira espaço */
		const char *tipo = c[traco + 1], *fonte = c[traco + 2];
		bool raiz = !strcmp(ponto, "/");
		bool removivel = g_str_has_prefix(ponto, "/media/") || g_str_has_prefix(ponto, "/run/media/");
		bool montagem = raiz || removivel || g_str_has_prefix(ponto, "/mnt/");
		bool falso = !strcmp(tipo, "tmpfs") || !strcmp(tipo, "proc") || !strcmp(tipo, "sysfs") || !strcmp(tipo, "devtmpfs") || !strcmp(tipo, "overlay") || !strcmp(tipo, "squashfs") ||
			g_str_has_prefix(tipo, "fuse.") || !strcmp(tipo, "cgroup2") || !strncmp(fonte, "/dev/loop", 9);
		if (montagem && (!falso || (raiz && strcmp(tipo, "squashfs")))) {
			bool repetido = false;
			for (int k = 0; k < n; k++) {
				repetido |= !strcmp(v[k].ponto, ponto);
			}
			struct statvfs sv;
			if (!repetido && statvfs(ponto, &sv) == 0 && sv.f_blocks > 0) {
				ar_unidade *u = &v[n++];
				memset(u, 0, sizeof(*u));
				g_strlcpy(u->ponto, ponto, sizeof(u->ponto));
				u->total = (int64_t)sv.f_blocks * (int64_t)sv.f_frsize;
				u->usado = u->total - (int64_t)sv.f_bfree * (int64_t)sv.f_frsize;
				u->removivel = removivel;
				if (raiz) {
					g_strlcpy(u->rotulo, "Sistema", sizeof(u->rotulo));
				} else {
					char *b = g_path_get_basename(ponto);
					g_strlcpy(u->rotulo, b, sizeof(u->rotulo));
					g_free(b);
				}
			}
		}
		g_free(ponto);
		g_strfreev(c);
	}
	g_strfreev(linhas);
	return n;
}

/* ---- prévia do conteúdo de uma pasta ---- */

int ar_previa_pasta(const char *caminho, enum ar_tipo *tipos, int max) {
	GDir *d = g_dir_open(caminho, 0, NULL);
	if (!d) {
		return 0;
	}
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	const char *n;
	while ((n = g_dir_read_name(d)) && nomes->len < 300) {
		if (n[0] != '.') {
			g_ptr_array_add(nomes, g_strdup(n));
		}
	}
	g_dir_close(d);
	g_ptr_array_sort(nomes, (GCompareFunc)strcmp);
	int k = 0;
	for (int passo = 0; passo < 2 && k < max; passo++) { /* primeiro os arquivos, depois as pastas */
		for (guint i = 0; i < nomes->len && k < max; i++) {
			char *c2 = g_build_filename(caminho, g_ptr_array_index(nomes, i), NULL);
			bool pasta = g_file_test(c2, G_FILE_TEST_IS_DIR);
			if (pasta == (passo == 1)) {
				tipos[k++] = pasta ? AR_PASTA : ar_tipo_do_nome(g_ptr_array_index(nomes, i));
			}
			g_free(c2);
		}
	}
	g_ptr_array_free(nomes, TRUE);
	return k;
}
