#define _GNU_SOURCE
#include "galeria.h"

#include <dirent.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "arquivos.h"

/* ================= ajustes ================= */

static char *arq_cfg(const char *nome) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "galeria", nome, NULL);
}

static bool gravar_texto(const char *nome, const char *conteudo, gsize n) {
	char *a = arq_cfg(nome);
	char *pasta = g_path_get_dirname(a);
	g_mkdir_with_parents(pasta, 0700);
	char *tmp = g_strconcat(a, ".novo", NULL);
	bool ok = g_file_set_contents(tmp, conteudo, (gssize)n, NULL) && g_rename(tmp, a) == 0;
	g_free(tmp);
	g_free(pasta);
	g_free(a);
	return ok;
}

void gl_cfg_padrao(gl_cfg *c) {
	memset(c, 0, sizeof(*c));
	const char *casa = g_get_home_dir();
	const char *imagens = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	const char *videos = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
	char *a = imagens ? g_strdup(imagens) : g_build_filename(casa, "Imagens", NULL);
	char *b = videos ? g_strdup(videos) : g_build_filename(casa, "Vídeos", NULL);
	char *capturas = g_build_filename(casa, "Capturas", NULL);
	g_snprintf(c->pastas[c->npastas++], sizeof(c->pastas[0]), "%s", a);
	g_snprintf(c->pastas[c->npastas++], sizeof(c->pastas[0]), "%s", b);
	g_snprintf(c->pastas[c->npastas++], sizeof(c->pastas[0]), "%s", capturas);
	g_free(a);
	g_free(b);
	g_free(capturas);
	c->buscar_auto = true;
	c->subpastas = true;
	c->agrupar = GL_AGRUPAR_CAPTURA;
	c->qualidade = GL_QUAL_EQUILIBRADA;
	c->duplicados = GL_DUP_PERGUNTAR;
	c->manter_original = true;
	c->ordem = GL_ORDEM_RECENTES;
	c->vista = GL_VISTA_GRADE;
	c->tam = 168;
	c->nomes = false;
	c->ajustar_tela = true;
	c->formato = GL_FORMATO_ORIGINAL;
	c->qualidade_jpeg = 90;
	c->video_mpv = true;
	c->volume = 80;
	c->repetir = false;
	c->cache = true;
	c->lixeira_na_barra = true;
	c->confirmar_apagar = true;
}

static int dentro(int v, int a, int b) {
	return v < a ? a : v > b ? b : v;
}

void gl_cfg_ler(gl_cfg *c) {
	gl_cfg_padrao(c);
	GKeyFile *kf = g_key_file_new();
	char *a = arq_cfg("config.ini");
	bool ok = g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL);
	g_free(a);
	if (!ok) {
		g_key_file_free(kf);
		return;
	}
#define INT(chave, campo, min, max) if (g_key_file_has_key(kf, "galeria", chave, NULL)) c->campo = dentro(g_key_file_get_integer(kf, "galeria", chave, NULL), min, max)
#define BOOL(chave, campo) if (g_key_file_has_key(kf, "galeria", chave, NULL)) c->campo = g_key_file_get_boolean(kf, "galeria", chave, NULL)
	gsize n = 0;
	char **lista = g_key_file_has_key(kf, "galeria", "pastas", NULL) ? g_key_file_get_string_list(kf, "galeria", "pastas", &n, NULL) : NULL;
	if (lista) { /* a pessoa mexeu na lista (mesmo que tenha ficado vazia): vale a dela */
		c->npastas = 0;
		for (gsize i = 0; i < n && c->npastas < GL_PASTAS_MAX; i++) {
			if (lista[i][0] && strlen(lista[i]) < sizeof(c->pastas[0])) {
				g_strlcpy(c->pastas[c->npastas++], lista[i], sizeof(c->pastas[0]));
			}
		}
		g_strfreev(lista);
	}
	BOOL("buscar_auto", buscar_auto);
	BOOL("subpastas", subpastas);
	INT("agrupar", agrupar, 0, 3);
	INT("qualidade", qualidade, 0, 2);
	INT("duplicados", duplicados, 0, 3);
	BOOL("manter_original", manter_original);
	INT("ordem", ordem, 0, 3);
	INT("vista", vista, 0, 1);
	INT("tam", tam, 96, 260);
	BOOL("nomes", nomes);
	BOOL("ajustar_tela", ajustar_tela);
	INT("formato", formato, 0, 2);
	INT("qualidade_jpeg", qualidade_jpeg, 50, 100);
	BOOL("video_mpv", video_mpv);
	INT("volume", volume, 0, 100);
	BOOL("repetir", repetir);
	BOOL("cache", cache);
	BOOL("lixeira_na_barra", lixeira_na_barra);
	BOOL("confirmar_apagar", confirmar_apagar);
#undef INT
#undef BOOL
	g_key_file_free(kf);
}

bool gl_cfg_gravar(const gl_cfg *c) {
	GKeyFile *kf = g_key_file_new();
	const char *pastas[GL_PASTAS_MAX + 1];
	for (int i = 0; i < c->npastas; i++) {
		pastas[i] = c->pastas[i];
	}
	g_key_file_set_string_list(kf, "galeria", "pastas", pastas, (gsize)c->npastas);
#define INT(chave, campo) g_key_file_set_integer(kf, "galeria", chave, c->campo)
#define BOOL(chave, campo) g_key_file_set_boolean(kf, "galeria", chave, c->campo)
	BOOL("buscar_auto", buscar_auto);
	BOOL("subpastas", subpastas);
	INT("agrupar", agrupar);
	INT("qualidade", qualidade);
	INT("duplicados", duplicados);
	BOOL("manter_original", manter_original);
	INT("ordem", ordem);
	INT("vista", vista);
	INT("tam", tam);
	BOOL("nomes", nomes);
	BOOL("ajustar_tela", ajustar_tela);
	INT("formato", formato);
	INT("qualidade_jpeg", qualidade_jpeg);
	BOOL("video_mpv", video_mpv);
	INT("volume", volume);
	BOOL("repetir", repetir);
	BOOL("cache", cache);
	BOOL("lixeira_na_barra", lixeira_na_barra);
	BOOL("confirmar_apagar", confirmar_apagar);
#undef INT
#undef BOOL
	gsize n;
	char *dados = g_key_file_to_data(kf, &n, NULL);
	bool ok = gravar_texto("config.ini", dados, n);
	g_free(dados);
	g_key_file_free(kf);
	return ok;
}

/* ================= tipos e datas ================= */

static bool tem_extensao(const char *nome, const char *const *lista) {
	const char *ponto = strrchr(nome, '.');
	if (!ponto || ponto == nome || !ponto[1]) {
		return false;
	}
	for (int i = 0; lista[i]; i++) {
		if (!strcasecmp(ponto + 1, lista[i])) {
			return true;
		}
	}
	return false;
}

bool gl_eh_foto(const char *nome) {
	static const char *const e[] = { "jpg", "jpeg", "png", "webp", "gif", "bmp", "tif", "tiff", "heic", "avif", NULL };
	return tem_extensao(nome, e);
}

bool gl_eh_video(const char *nome) {
	static const char *const e[] = { "mp4", "mkv", "webm", "mov", "avi", "m4v", "mpg", "mpeg", "3gp", "ogv", NULL };
	return tem_extensao(nome, e);
}

static uint32_t u16(const unsigned char *p, bool le) {
	return le ? (uint32_t)(p[0] | p[1] << 8) : (uint32_t)(p[1] | p[0] << 8);
}

static uint32_t u32(const unsigned char *p, bool le) {
	return le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24
		  : (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24;
}

/* procura a tag no IFD em tiff+off: devolve o ponteiro para a entrada de 12 bytes */
static const unsigned char *ifd_tag(const unsigned char *tiff, size_t tam, uint32_t off, bool le, uint32_t tag) {
	if ((size_t)off + 2 > tam) {
		return NULL;
	}
	uint32_t n = u16(tiff + off, le);
	for (uint32_t i = 0; i < n; i++) {
		size_t e = (size_t)off + 2 + (size_t)i * 12;
		if (e + 12 > tam) {
			return NULL;
		}
		if (u16(tiff + e, le) == tag) {
			return tiff + e;
		}
	}
	return NULL;
}

static bool texto_para_data(const char *s, time_t *data) {
	struct tm tm = { 0 };
	int a, me, d, h, mi, se;
	if (sscanf(s, "%d:%d:%d %d:%d:%d", &a, &me, &d, &h, &mi, &se) != 6 || a < 1971 || me < 1 || me > 12 || d < 1 || d > 31) {
		return false;
	}
	tm.tm_year = a - 1900;
	tm.tm_mon = me - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = se;
	tm.tm_isdst = -1;
	time_t t = mktime(&tm);
	if (t == (time_t)-1) {
		return false;
	}
	*data = t;
	return true;
}

bool gl_data_exif(const char *caminho, time_t *data) {
	FILE *f = fopen(caminho, "rb");
	if (!f) {
		return false;
	}
	unsigned char buf[96 * 1024];
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (n < 12 || buf[0] != 0xff || buf[1] != 0xd8) {
		return false;
	}
	size_t p = 2;
	while (p + 4 <= n && buf[p] == 0xff) {
		unsigned char marca = buf[p + 1];
		if (marca == 0xd8 || marca == 0x01 || (marca >= 0xd0 && marca <= 0xd7)) {
			p += 2;
			continue;
		}
		size_t len = (size_t)(buf[p + 2] << 8 | buf[p + 3]);
		if (len < 2 || marca == 0xda) {
			break; /* o começo da imagem: acabaram os cabeçalhos */
		}
		if (marca == 0xe1 && p + 4 + len - 2 <= n && len > 8 && !memcmp(buf + p + 4, "Exif\0\0", 6)) {
			const unsigned char *tiff = buf + p + 10;
			size_t tam = len - 8;
			if (tam < 8 || (memcmp(tiff, "II", 2) && memcmp(tiff, "MM", 2))) {
				return false;
			}
			bool le = tiff[0] == 'I';
			uint32_t ifd0 = u32(tiff + 4, le);
			const unsigned char *e = ifd_tag(tiff, tam, ifd0, le, 0x8769); /* o ponteiro para o IFD Exif */
			if (e) {
				const unsigned char *o = ifd_tag(tiff, tam, u32(e + 8, le), le, 0x9003); /* DateTimeOriginal */
				if (o && u16(o + 2, le) == 2 && u32(o + 4, le) >= 19) {
					uint32_t off = u32(o + 8, le);
					if ((size_t)off + 19 <= tam) {
						char txt[24] = { 0 };
						memcpy(txt, tiff + off, 19);
						if (texto_para_data(txt, data)) {
							return true;
						}
					}
				}
			}
			const unsigned char *d = ifd_tag(tiff, tam, ifd0, le, 0x0132); /* DateTime */
			if (d && u16(d + 2, le) == 2 && u32(d + 4, le) >= 19) {
				uint32_t off = u32(d + 8, le);
				if ((size_t)off + 19 <= tam) {
					char txt[24] = { 0 };
					memcpy(txt, tiff + off, 19);
					return texto_para_data(txt, data);
				}
			}
			return false;
		}
		p += 2 + len;
	}
	return false;
}

/* ================= a biblioteca ================= */

static void acervo_juntar(gl_acervo *a, gl_midia m) {
	if (a->n == a->cap) {
		a->cap = a->cap ? a->cap * 2 : 256;
		a->v = g_realloc(a->v, (size_t)a->cap * sizeof(gl_midia));
	}
	a->v[a->n++] = m;
}

static void ler_pasta(const char *pasta, bool subpastas, bool usar_mtime, gl_acervo *a, int fundo) {
	GDir *d = g_dir_open(pasta, 0, NULL);
	if (!d) {
		return;
	}
	const char *nome;
	while ((nome = g_dir_read_name(d))) {
		if (nome[0] == '.') {
			continue;
		}
		char *caminho = g_build_filename(pasta, nome, NULL);
		struct stat st;
		if (stat(caminho, &st) != 0) {
			g_free(caminho);
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			if (subpastas && fundo < 12) {
				ler_pasta(caminho, true, usar_mtime, a, fundo + 1);
			}
			g_free(caminho);
			continue;
		}
		bool foto = gl_eh_foto(nome), video = !foto && gl_eh_video(nome);
		if (!S_ISREG(st.st_mode) || (!foto && !video)) {
			g_free(caminho);
			continue;
		}
		gl_midia m = { .caminho = caminho, .nome = g_strdup(nome), .tipo = foto ? GL_FOTO : GL_VIDEO, .mtime = st.st_mtime, .data = st.st_mtime,
			.tamanho = st.st_size, .duracao = -1 };
		if (foto && !usar_mtime) {
			const char *ponto = strrchr(nome, '.');
			time_t t;
			if (ponto && (!strcasecmp(ponto, ".jpg") || !strcasecmp(ponto, ".jpeg")) && gl_data_exif(caminho, &t)) {
				m.data = t;
			}
		}
		if (video) {
			m.duracao = gl_duracao_conhecida(caminho, st.st_mtime);
		}
		acervo_juntar(a, m);
	}
	g_dir_close(d);
}

void gl_varrer(const gl_cfg *c, gl_acervo *saida) {
	memset(saida, 0, sizeof(*saida));
	for (int i = 0; i < c->npastas; i++) {
		/* a mesma mídia por duas pastas (uma dentro da outra) entra uma vez só */
		ler_pasta(c->pastas[i], c->subpastas, c->agrupar == GL_AGRUPAR_MODIFICACAO, saida, 0);
	}
	GHashTable *vistos = g_hash_table_new(g_str_hash, g_str_equal);
	int j = 0;
	for (int i = 0; i < saida->n; i++) {
		if (g_hash_table_contains(vistos, saida->v[i].caminho)) {
			g_free(saida->v[i].caminho);
			g_free(saida->v[i].nome);
			continue;
		}
		g_hash_table_add(vistos, saida->v[i].caminho);
		saida->v[j++] = saida->v[i];
	}
	saida->n = j;
	g_hash_table_destroy(vistos);
	gl_fav_marcar(saida);
	gl_acervo_ordenar(saida, c->ordem, c->agrupar);
}

void gl_varrer_lixeira(gl_acervo *saida) {
	memset(saida, 0, sizeof(*saida));
	char *pasta = g_build_filename(g_get_user_data_dir(), "Trash", "files", NULL);
	ler_pasta(pasta, false, true, saida, 0);
	g_free(pasta);
	gl_acervo_ordenar(saida, GL_ORDEM_RECENTES, GL_AGRUPAR_NENHUM);
}

void gl_acervo_liberar(gl_acervo *a) {
	for (int i = 0; i < a->n; i++) {
		g_free(a->v[i].caminho);
		g_free(a->v[i].nome);
	}
	g_free(a->v);
	memset(a, 0, sizeof(*a));
}

static int ordem_cmp_ordem, ordem_cmp_agrupar; /* só para o qsort: o ordenar roda numa thread por vez */
static GMutex ordem_trinco;

static int comparar(const void *pa, const void *pb) {
	const gl_midia *a = pa, *b = pb;
	if (ordem_cmp_agrupar == GL_AGRUPAR_PASTA) {
		char *da = g_path_get_dirname(a->caminho), *db = g_path_get_dirname(b->caminho);
		int r = g_utf8_collate(da, db);
		g_free(da);
		g_free(db);
		if (r) {
			return r;
		}
	}
	int r = 0;
	switch (ordem_cmp_ordem) {
	case GL_ORDEM_ANTIGAS: r = a->data < b->data ? -1 : a->data > b->data ? 1 : 0; break;
	case GL_ORDEM_NOME: r = g_utf8_collate(a->nome, b->nome); break;
	case GL_ORDEM_TAMANHO: r = a->tamanho < b->tamanho ? 1 : a->tamanho > b->tamanho ? -1 : 0; break; /* as maiores primeiro */
	default: r = a->data < b->data ? 1 : a->data > b->data ? -1 : 0; break;
	}
	return r ? r : g_utf8_collate(a->nome, b->nome);
}

void gl_acervo_ordenar(gl_acervo *a, int ordem, int agrupar) {
	if (a->n < 2) {
		return;
	}
	g_mutex_lock(&ordem_trinco);
	ordem_cmp_ordem = ordem;
	ordem_cmp_agrupar = agrupar;
	qsort(a->v, (size_t)a->n, sizeof(gl_midia), comparar);
	g_mutex_unlock(&ordem_trinco);
}

int gl_filtrar(const gl_acervo *a, const gl_filtro *f, int *indices) {
	int n = 0;
	GHashTable *album = NULL;
	if (f->tipo == GL_F_ALBUM) {
		album = g_hash_table_new(g_str_hash, g_str_equal);
		for (int i = 0; i < f->album_n; i++) {
			g_hash_table_add(album, f->album_caminhos[i]);
		}
	}
	time_t agora = time(NULL);
	bool busca = f->busca && f->busca[0];
	for (int i = 0; i < a->n; i++) {
		const gl_midia *m = &a->v[i];
		bool ok = true;
		switch (f->tipo) {
		case GL_F_FOTOS: ok = m->tipo == GL_FOTO; break;
		case GL_F_VIDEOS: ok = m->tipo == GL_VIDEO; break;
		case GL_F_FAVORITOS: ok = m->favorita; break;
		case GL_F_RECENTES: ok = agora - m->data <= 30 * 24 * 3600; break; /* os últimos 30 dias */
		case GL_F_ALBUM: ok = g_hash_table_contains(album, m->caminho); break;
		default: break;
		}
		if (ok && busca && !ar_busca_combina(m->nome, f->busca)) {
			ok = false;
		}
		if (ok) {
			indices[n++] = i;
		}
	}
	if (album) {
		g_hash_table_destroy(album);
	}
	return n;
}

/* ================= grupos ================= */

static const char *const MESES[12] = { "Janeiro", "Fevereiro", "Março", "Abril", "Maio", "Junho", "Julho", "Agosto", "Setembro", "Outubro", "Novembro", "Dezembro" };

static void rotulo_da_data(time_t t, time_t agora, char *saida, size_t tam) {
	struct tm a, b;
	localtime_r(&t, &a);
	localtime_r(&agora, &b);
	if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) {
		g_strlcpy(saida, "Hoje", tam);
		return;
	}
	time_t ontem = agora - 24 * 3600;
	struct tm o;
	localtime_r(&ontem, &o);
	if (a.tm_year == o.tm_year && a.tm_yday == o.tm_yday) {
		g_strlcpy(saida, "Ontem", tam);
		return;
	}
	g_snprintf(saida, tam, "%s de %d", MESES[a.tm_mon], a.tm_year + 1900);
}

int gl_agrupar(const gl_acervo *a, const int *indices, int n, int agrupar, time_t agora, gl_grupo *grupos, int max) {
	int g = 0;
	for (int k = 0; k < n && g <= max; k++) {
		const gl_midia *m = &a->v[indices[k]];
		char rotulo[96] = "";
		switch (agrupar) {
		case GL_AGRUPAR_PASTA: {
			char *d = g_path_get_dirname(m->caminho);
			char *b = g_path_get_basename(d);
			g_strlcpy(rotulo, b, sizeof(rotulo));
			g_free(d);
			g_free(b);
			break;
		}
		case GL_AGRUPAR_NENHUM: break;
		default: rotulo_da_data(agrupar == GL_AGRUPAR_MODIFICACAO ? m->mtime : m->data, agora, rotulo, sizeof(rotulo)); break;
		}
		if (g == 0 || strcmp(grupos[g - 1].rotulo, rotulo)) {
			if (g == max) {
				break;
			}
			g_strlcpy(grupos[g].rotulo, rotulo, sizeof(grupos[g].rotulo));
			grupos[g].inicio = k;
			grupos[g].n = 0;
			g++;
		}
		grupos[g - 1].n++;
	}
	return g;
}

/* ================= favoritos ================= */

static GHashTable *favs;
static GMutex favs_trinco;

static void favs_gravar(void) {
	GString *s = g_string_new(NULL);
	GHashTableIter it;
	gpointer k;
	g_hash_table_iter_init(&it, favs);
	while (g_hash_table_iter_next(&it, &k, NULL)) {
		g_string_append_printf(s, "%s\n", (char *)k);
	}
	gravar_texto("favoritos.txt", s->str, s->len);
	g_string_free(s, TRUE);
}

static void favs_garantir(void) {
	if (favs) {
		return;
	}
	favs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	char *a = arq_cfg("favoritos.txt"), *txt = NULL;
	if (g_file_get_contents(a, &txt, NULL, NULL)) {
		char **ls = g_strsplit(txt, "\n", -1);
		for (int i = 0; ls[i]; i++) {
			if (ls[i][0]) {
				g_hash_table_add(favs, g_strdup(ls[i]));
			}
		}
		g_strfreev(ls);
		g_free(txt);
	}
	g_free(a);
}

bool gl_fav_tem(const char *caminho) {
	g_mutex_lock(&favs_trinco);
	favs_garantir();
	bool sim = g_hash_table_contains(favs, caminho);
	g_mutex_unlock(&favs_trinco);
	return sim;
}

void gl_fav_definir(const char *caminho, bool sim) {
	g_mutex_lock(&favs_trinco);
	favs_garantir();
	bool tem = g_hash_table_contains(favs, caminho);
	if (sim && !tem) {
		g_hash_table_add(favs, g_strdup(caminho));
		favs_gravar();
	} else if (!sim && tem) {
		g_hash_table_remove(favs, caminho);
		favs_gravar();
	}
	g_mutex_unlock(&favs_trinco);
}

void gl_fav_marcar(gl_acervo *a) {
	g_mutex_lock(&favs_trinco);
	favs_garantir();
	for (int i = 0; i < a->n; i++) {
		a->v[i].favorita = g_hash_table_contains(favs, a->v[i].caminho);
	}
	g_mutex_unlock(&favs_trinco);
}

/* ================= álbuns ================= */

static GMutex albuns_trinco;

static GKeyFile *albuns_kf(void) {
	GKeyFile *kf = g_key_file_new();
	char *a = arq_cfg("albuns.ini");
	g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL);
	g_free(a);
	return kf;
}

static void albuns_salvar(GKeyFile *kf) {
	gsize n;
	char *d = g_key_file_to_data(kf, &n, NULL);
	gravar_texto("albuns.ini", d, n);
	g_free(d);
}

void gl_albuns_ler(gl_albuns *a) {
	memset(a, 0, sizeof(*a));
	g_mutex_lock(&albuns_trinco);
	GKeyFile *kf = albuns_kf();
	gsize ng;
	char **grupos = g_key_file_get_groups(kf, &ng);
	a->v = g_new0(gl_album, ng ? ng : 1);
	for (gsize i = 0; i < ng; i++) {
		gl_album *al = &a->v[a->n++];
		g_strlcpy(al->nome, grupos[i], sizeof(al->nome));
		al->caminhos = g_ptr_array_new_with_free_func(g_free);
		gsize nc = 0;
		char **cs = g_key_file_get_string_list(kf, grupos[i], "itens", &nc, NULL);
		for (gsize k = 0; cs && k < nc; k++) {
			g_ptr_array_add(al->caminhos, g_strdup(cs[k]));
		}
		g_strfreev(cs);
	}
	g_strfreev(grupos);
	g_key_file_free(kf);
	g_mutex_unlock(&albuns_trinco);
}

void gl_albuns_liberar(gl_albuns *a) {
	for (int i = 0; i < a->n; i++) {
		g_ptr_array_free(a->v[i].caminhos, TRUE);
	}
	g_free(a->v);
	memset(a, 0, sizeof(*a));
}

bool gl_album_criar(const char *nome) {
	char *n = g_strstrip(g_strdup(nome ? nome : ""));
	bool ok = n[0] && strlen(n) < 96 && !strchr(n, '[') && !strchr(n, ']');
	if (ok) {
		g_mutex_lock(&albuns_trinco);
		GKeyFile *kf = albuns_kf();
		if (g_key_file_has_group(kf, n)) {
			ok = false;
		} else {
			g_key_file_set_string_list(kf, n, "itens", NULL, 0);
			albuns_salvar(kf);
		}
		g_key_file_free(kf);
		g_mutex_unlock(&albuns_trinco);
	}
	g_free(n);
	return ok;
}

bool gl_album_renomear(const char *nome, const char *novo) {
	char *n = g_strstrip(g_strdup(novo ? novo : ""));
	bool ok = n[0] && strlen(n) < 96 && !strchr(n, '[') && !strchr(n, ']') && strcmp(nome, n);
	if (ok) {
		g_mutex_lock(&albuns_trinco);
		GKeyFile *kf = albuns_kf();
		if (!g_key_file_has_group(kf, nome) || g_key_file_has_group(kf, n)) {
			ok = false;
		} else {
			gsize nc = 0;
			char **cs = g_key_file_get_string_list(kf, nome, "itens", &nc, NULL);
			g_key_file_remove_group(kf, nome, NULL);
			g_key_file_set_string_list(kf, n, "itens", (const char *const *)cs, nc);
			g_strfreev(cs);
			albuns_salvar(kf);
		}
		g_key_file_free(kf);
		g_mutex_unlock(&albuns_trinco);
	}
	g_free(n);
	return ok;
}

void gl_album_apagar(const char *nome) {
	g_mutex_lock(&albuns_trinco);
	GKeyFile *kf = albuns_kf();
	if (g_key_file_remove_group(kf, nome, NULL)) {
		albuns_salvar(kf);
	}
	g_key_file_free(kf);
	g_mutex_unlock(&albuns_trinco);
}

int gl_album_adicionar(const char *nome, char *const *caminhos, int n) {
	int novos = 0;
	g_mutex_lock(&albuns_trinco);
	GKeyFile *kf = albuns_kf();
	if (g_key_file_has_group(kf, nome)) {
		gsize nc = 0;
		char **cs = g_key_file_get_string_list(kf, nome, "itens", &nc, NULL);
		GPtrArray *todos = g_ptr_array_new();
		GHashTable *ja = g_hash_table_new(g_str_hash, g_str_equal);
		for (gsize i = 0; cs && i < nc; i++) {
			g_ptr_array_add(todos, cs[i]);
			g_hash_table_add(ja, cs[i]);
		}
		for (int i = 0; i < n; i++) {
			if (!g_hash_table_contains(ja, caminhos[i])) {
				g_ptr_array_add(todos, caminhos[i]);
				g_hash_table_add(ja, caminhos[i]);
				novos++;
			}
		}
		g_key_file_set_string_list(kf, nome, "itens", (const char *const *)todos->pdata, todos->len);
		if (novos) {
			albuns_salvar(kf);
		}
		g_hash_table_destroy(ja);
		g_ptr_array_free(todos, TRUE);
		g_strfreev(cs);
	}
	g_key_file_free(kf);
	g_mutex_unlock(&albuns_trinco);
	return novos;
}

void gl_album_remover(const char *nome, const char *caminho) {
	g_mutex_lock(&albuns_trinco);
	GKeyFile *kf = albuns_kf();
	if (g_key_file_has_group(kf, nome)) {
		gsize nc = 0;
		char **cs = g_key_file_get_string_list(kf, nome, "itens", &nc, NULL);
		GPtrArray *resto = g_ptr_array_new();
		for (gsize i = 0; cs && i < nc; i++) {
			if (strcmp(cs[i], caminho)) {
				g_ptr_array_add(resto, cs[i]);
			}
		}
		if (resto->len != nc) {
			g_key_file_set_string_list(kf, nome, "itens", (const char *const *)resto->pdata, resto->len);
			albuns_salvar(kf);
		}
		g_ptr_array_free(resto, TRUE);
		g_strfreev(cs);
	}
	g_key_file_free(kf);
	g_mutex_unlock(&albuns_trinco);
}

void gl_limpar_orfaos(void) {
	g_mutex_lock(&albuns_trinco);
	GKeyFile *kf = albuns_kf();
	bool mudou = false;
	gsize ng;
	char **grupos = g_key_file_get_groups(kf, &ng);
	for (gsize i = 0; i < ng; i++) {
		gsize nc = 0;
		char **cs = g_key_file_get_string_list(kf, grupos[i], "itens", &nc, NULL);
		GPtrArray *vivos = g_ptr_array_new();
		for (gsize k = 0; cs && k < nc; k++) {
			if (g_file_test(cs[k], G_FILE_TEST_EXISTS)) {
				g_ptr_array_add(vivos, cs[k]);
			}
		}
		if (vivos->len != nc) {
			g_key_file_set_string_list(kf, grupos[i], "itens", (const char *const *)vivos->pdata, vivos->len);
			mudou = true;
		}
		g_ptr_array_free(vivos, TRUE);
		g_strfreev(cs);
	}
	g_strfreev(grupos);
	if (mudou) {
		albuns_salvar(kf);
	}
	g_key_file_free(kf);
	g_mutex_unlock(&albuns_trinco);
	g_mutex_lock(&favs_trinco);
	favs_garantir();
	GPtrArray *sumidos = g_ptr_array_new();
	GHashTableIter it;
	gpointer k;
	g_hash_table_iter_init(&it, favs);
	while (g_hash_table_iter_next(&it, &k, NULL)) {
		if (!g_file_test(k, G_FILE_TEST_EXISTS)) {
			g_ptr_array_add(sumidos, k);
		}
	}
	for (guint i = 0; i < sumidos->len; i++) {
		g_hash_table_remove(favs, sumidos->pdata[i]);
	}
	if (sumidos->len) {
		favs_gravar();
	}
	g_ptr_array_free(sumidos, TRUE);
	g_mutex_unlock(&favs_trinco);
}

/* ================= miniaturas e vídeos ================= */

static bool cache_disco = true;
static GMutex cache_trinco;
static GHashTable *cache_pronta;   /* chave → caminho do arquivo da miniatura (g_strdup) */
static GHashTable *cache_falhou;   /* chave → 1 */
static GHashTable *cache_fila;     /* chave → 1: já pedida, ainda sendo feita */
static GHashTable *duracoes;       /* "caminho|mtime" → GINT_TO_POINTER(segundos + 1) */
static GThreadPool *pool;
static unsigned geracao;

void gl_cache_usar_disco(bool sim) {
	cache_disco = sim;
}

static char *cache_pasta(void) {
	char *p;
	if (cache_disco) {
		p = g_build_filename(g_get_user_cache_dir(), "lumna", "galeria", NULL);
	} else {
		char *nome = g_strdup_printf("lumna-galeria-%u", (unsigned)getuid());
		p = g_build_filename(g_get_tmp_dir(), nome, NULL);
		g_free(nome);
	}
	g_mkdir_with_parents(p, 0700);
	return p;
}

static char *chave_da(const gl_midia *m, int qualidade) {
	char *base = g_strdup_printf("%s|%lld|%d", m->caminho, (long long)m->mtime, qualidade);
	char *sha = g_compute_checksum_for_string(G_CHECKSUM_SHA1, base, -1);
	g_free(base);
	return sha;
}

static int lado_da_qualidade(int q) {
	return q == GL_QUAL_RAPIDA ? 192 : q == GL_QUAL_ALTA ? 512 : 320;
}

typedef struct {
	char *chave, *caminho, *pasta;
	enum gl_tipo tipo;
	time_t mtime;
	int lado;
	unsigned geracao;
	void (*pronta)(void *);
	void *dados;
} trabalho;

/* ffprobe: a duração em segundos, -1 se não deu */
static int ffprobe_duracao(const char *caminho) {
	char *argv[] = { "timeout", "20", "ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "default=nw=1:nk=1", (char *)caminho, NULL };
	char *saida = NULL;
	int estado = 0;
	int d = -1;
	if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, &estado, NULL) && saida) {
		double v = g_ascii_strtod(saida, NULL);
		if (v > 0) {
			d = (int)(v + 0.5);
		}
	}
	g_free(saida);
	return d;
}

static void duracoes_garantir(void) {
	if (duracoes) {
		return;
	}
	duracoes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	char *a = g_build_filename(g_get_user_cache_dir(), "lumna", "galeria", "duracao.ini", NULL), *txt = NULL;
	if (g_file_get_contents(a, &txt, NULL, NULL)) {
		char **ls = g_strsplit(txt, "\n", -1);
		for (int i = 0; ls[i]; i++) {
			char *igual = strrchr(ls[i], '=');
			if (igual) {
				*igual = '\0';
				g_hash_table_insert(duracoes, g_strdup(ls[i]), GINT_TO_POINTER(atoi(igual + 1) + 1));
			}
		}
		g_strfreev(ls);
		g_free(txt);
	}
	g_free(a);
}

static void duracoes_gravar(void) {
	if (!cache_disco) {
		return;
	}
	GString *s = g_string_new(NULL);
	GHashTableIter it;
	gpointer k, v;
	g_hash_table_iter_init(&it, duracoes);
	while (g_hash_table_iter_next(&it, &k, &v)) {
		g_string_append_printf(s, "%s=%d\n", (char *)k, GPOINTER_TO_INT(v) - 1);
	}
	char *a = g_build_filename(g_get_user_cache_dir(), "lumna", "galeria", "duracao.ini", NULL);
	char *pasta = g_path_get_dirname(a);
	g_mkdir_with_parents(pasta, 0700);
	g_file_set_contents(a, s->str, (gssize)s->len, NULL);
	g_free(pasta);
	g_free(a);
	g_string_free(s, TRUE);
}

int gl_duracao_conhecida(const char *caminho, time_t mtime) {
	char *k = g_strdup_printf("%s|%lld", caminho, (long long)mtime);
	g_mutex_lock(&cache_trinco);
	duracoes_garantir();
	int d = GPOINTER_TO_INT(g_hash_table_lookup(duracoes, k)) - 1;
	g_mutex_unlock(&cache_trinco);
	g_free(k);
	return d;
}

int gl_video_duracao(const char *caminho, time_t mtime) {
	int d = gl_duracao_conhecida(caminho, mtime);
	if (d >= 0) {
		return d;
	}
	d = ffprobe_duracao(caminho);
	if (d >= 0) {
		char *k = g_strdup_printf("%s|%lld", caminho, (long long)mtime);
		g_mutex_lock(&cache_trinco);
		duracoes_garantir();
		g_hash_table_replace(duracoes, k, GINT_TO_POINTER(d + 1));
		duracoes_gravar();
		g_mutex_unlock(&cache_trinco);
	}
	return d;
}

/* uma foto reduzida, gravada em arquivo; falso se não deu */
static bool miniatura_de_foto(const trabalho *t, char **destino) {
	int w, h;
	GdkPixbufFormat *formato = gdk_pixbuf_get_file_info(t->caminho, &w, &h);
	if (!formato || w <= 0 || h <= 0) {
		return false;
	}
	char *nome = gdk_pixbuf_format_get_name(formato);
	bool jpeg = nome && !strcmp(nome, "jpeg");
	g_free(nome);
	if ((double)w * h > (jpeg ? 4e8 : 6e7)) { /* só o JPEG descodifica já reduzido: o resto abriria a imagem inteira na memória */
		return false;
	}
	GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(t->caminho, t->lado, t->lado, TRUE, NULL);
	if (!pb) {
		return false;
	}
	GdkPixbuf *girado = gdk_pixbuf_apply_embedded_orientation(pb);
	g_object_unref(pb);
	if (!girado) {
		return false;
	}
	bool alfa = gdk_pixbuf_get_has_alpha(girado);
	*destino = g_strdup_printf("%s/%s.%s", t->pasta, t->chave, alfa ? "png" : "jpg");
	char *tmp = g_strconcat(*destino, ".tmp", NULL);
	bool ok = alfa ? gdk_pixbuf_save(girado, tmp, "png", NULL, NULL) : gdk_pixbuf_save(girado, tmp, "jpeg", NULL, "quality", "85", NULL);
	if (ok) {
		ok = g_rename(tmp, *destino) == 0;
	}
	g_free(tmp);
	g_object_unref(girado);
	if (!ok) {
		g_free(*destino);
		*destino = NULL;
	}
	return ok;
}

/* a primeira cena (um pouco depois do começo) de um vídeo */
static bool miniatura_de_video(const trabalho *t, char **destino) {
	int dur = gl_video_duracao(t->caminho, t->mtime);
	*destino = g_strdup_printf("%s/%s.jpg", t->pasta, t->chave);
	char *tmp = g_strdup_printf("%s/%s.tmp.jpg", t->pasta, t->chave);
	char *escala = g_strdup_printf("scale=%d:-2", t->lado);
	char *inicio = g_strdup(dur >= 4 ? "1.5" : "0");
	char *argv[] = { "timeout", "30", "ffmpeg", "-v", "error", "-y", "-ss", inicio, "-i", (char *)t->caminho, "-frames:v", "1", "-vf", escala, "-q:v", "4", tmp, NULL };
	int estado = 0;
	bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &estado, NULL) &&
		  g_spawn_check_wait_status(estado, NULL) && g_file_test(tmp, G_FILE_TEST_EXISTS);
	if (ok) {
		ok = g_rename(tmp, *destino) == 0;
	}
	g_free(inicio);
	g_free(escala);
	g_free(tmp);
	if (!ok) {
		g_free(*destino);
		*destino = NULL;
	}
	return ok;
}

static void trabalhar(gpointer dados, gpointer usuario) {
	trabalho *t = dados;
	char *destino = NULL;
	bool ok = false;
	g_mutex_lock(&cache_trinco);
	bool velho = t->geracao != geracao;
	g_mutex_unlock(&cache_trinco);
	if (!velho) {
		ok = t->tipo == GL_VIDEO ? miniatura_de_video(t, &destino) : miniatura_de_foto(t, &destino);
	}
	g_mutex_lock(&cache_trinco);
	g_hash_table_remove(cache_fila, t->chave);
	if (velho) {
		/* saiu da fila antes de começar: pode ser pedida de novo */
	} else if (ok) {
		g_hash_table_replace(cache_pronta, g_strdup(t->chave), destino);
	} else {
		g_hash_table_replace(cache_falhou, g_strdup(t->chave), GINT_TO_POINTER(1));
	}
	g_mutex_unlock(&cache_trinco);
	if (!velho && t->pronta) {
		t->pronta(t->dados);
	}
	g_free(t->chave);
	g_free(t->caminho);
	g_free(t->pasta);
	g_free(t);
}

static void cache_garantir(void) {
	if (cache_pronta) {
		return;
	}
	cache_pronta = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	cache_falhou = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	cache_fila = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	pool = g_thread_pool_new(trabalhar, NULL, 2, FALSE, NULL);
}

const char *gl_miniatura(const gl_midia *m, int qualidade, void (*pronta)(void *), void *dados) {
	char *chave = chave_da(m, qualidade);
	g_mutex_lock(&cache_trinco);
	cache_garantir();
	const char *feita = g_hash_table_lookup(cache_pronta, chave);
	if (feita && !g_file_test(feita, G_FILE_TEST_EXISTS)) { /* o cache foi limpo por fora */
		g_hash_table_remove(cache_pronta, chave);
		feita = NULL;
	}
	if (!feita && !g_hash_table_contains(cache_falhou, chave) && !g_hash_table_contains(cache_fila, chave)) {
		/* já está no disco, de outra vez? */
		char *pasta = cache_pasta();
		char *jpg = g_strdup_printf("%s/%s.jpg", pasta, chave), *png = g_strdup_printf("%s/%s.png", pasta, chave);
		if (g_file_test(jpg, G_FILE_TEST_EXISTS)) {
			g_hash_table_replace(cache_pronta, g_strdup(chave), jpg);
			jpg = NULL;
		} else if (g_file_test(png, G_FILE_TEST_EXISTS)) {
			g_hash_table_replace(cache_pronta, g_strdup(chave), png);
			png = NULL;
		} else {
			trabalho *t = g_new0(trabalho, 1);
			t->chave = g_strdup(chave);
			t->caminho = g_strdup(m->caminho);
			t->pasta = g_strdup(pasta);
			t->tipo = m->tipo;
			t->mtime = m->mtime;
			t->lado = lado_da_qualidade(qualidade);
			t->geracao = geracao;
			t->pronta = pronta;
			t->dados = dados;
			g_hash_table_replace(cache_fila, g_strdup(chave), GINT_TO_POINTER(1));
			g_thread_pool_push(pool, t, NULL);
		}
		g_free(jpg);
		g_free(png);
		g_free(pasta);
		feita = g_hash_table_lookup(cache_pronta, chave);
	}
	g_mutex_unlock(&cache_trinco);
	g_free(chave);
	return feita;
}

bool gl_miniatura_falhou(const gl_midia *m) {
	bool falhou = false;
	for (int q = 0; q < 3 && !falhou; q++) {
		char *chave = chave_da(m, q);
		g_mutex_lock(&cache_trinco);
		cache_garantir();
		falhou = g_hash_table_contains(cache_falhou, chave);
		g_mutex_unlock(&cache_trinco);
		g_free(chave);
	}
	return falhou;
}

void gl_miniaturas_descartar(void) {
	g_mutex_lock(&cache_trinco);
	cache_garantir();
	geracao++;
	g_mutex_unlock(&cache_trinco);
}

static int64_t pasta_tamanho(const char *pasta) {
	int64_t total = 0;
	GDir *d = g_dir_open(pasta, 0, NULL);
	const char *nome;
	while (d && (nome = g_dir_read_name(d))) {
		char *c = g_build_filename(pasta, nome, NULL);
		struct stat st;
		if (stat(c, &st) == 0 && S_ISREG(st.st_mode)) {
			total += st.st_size;
		}
		g_free(c);
	}
	if (d) {
		g_dir_close(d);
	}
	return total;
}

int64_t gl_cache_tamanho(void) {
	char *p = cache_pasta();
	int64_t t = pasta_tamanho(p);
	g_free(p);
	return t;
}

void gl_cache_limpar(void) {
	char *p = cache_pasta();
	GDir *d = g_dir_open(p, 0, NULL);
	const char *nome;
	while (d && (nome = g_dir_read_name(d))) {
		char *c = g_build_filename(p, nome, NULL);
		g_unlink(c);
		g_free(c);
	}
	if (d) {
		g_dir_close(d);
	}
	g_free(p);
	g_mutex_lock(&cache_trinco);
	cache_garantir();
	g_hash_table_remove_all(cache_pronta);
	g_hash_table_remove_all(cache_falhou);
	geracao++;
	if (duracoes) {
		g_hash_table_remove_all(duracoes);
	}
	g_mutex_unlock(&cache_trinco);
}

void gl_duracao_texto(int s, char *saida, size_t tam) {
	if (s < 0) {
		g_strlcpy(saida, "", tam);
	} else if (s >= 3600) {
		g_snprintf(saida, tam, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
	} else {
		g_snprintf(saida, tam, "%d:%02d", s / 60, s % 60);
	}
}
