#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "json.h"
#include "loja_steam.h"

#define TTL_PADRAO (6 * 3600)

/* ---------------- a rede: curl, com cache ---------------- */

static const char *base_loja(void) {
	const char *e = g_getenv("LUMNA_LOJA_STEAM_URL");
	return e && *e ? e : "https://store.steampowered.com";
}

static const char *base_img(void) {
	const char *e = g_getenv("LUMNA_LOJA_IMG_URL");
	return e && *e ? e : "https://cdn.akamai.steamstatic.com";
}

static const char *base_proton(void) {
	const char *e = g_getenv("LUMNA_LOJA_PROTONDB_URL");
	return e && *e ? e : "https://www.protondb.com";
}

static const char *base_api(void) {
	const char *e = g_getenv("LUMNA_LOJA_API_URL");
	return e && *e ? e : "https://api.steampowered.com";
}

static char *cache_dir(void) {
	return g_build_filename(g_get_user_cache_dir(), "lumna", "jogos", "loja", NULL);
}

static char *cache_arquivo(const char *url, const char *ext) {
	char *dir = cache_dir(), *h = g_compute_checksum_for_string(G_CHECKSUM_SHA1, url, -1), *a = g_strdup_printf("%s/%s%s", dir, h, ext);
	g_free(h);
	g_free(dir);
	return a;
}

/* baixa a url para o stdout do curl; a url (que pode levar a chave) vai pela entrada padrão, não pela linha de comando (o `ps` a mostraria) */
static char *curl_corpo(const char *url, char *erro, size_t tam) {
	GError *ge = NULL;
	const char *argv[] = { "curl", "-sS", "-L", "-m", "25", "--fail", "-A", "LUMNA-Gaming/1.0", "-K", "-", NULL };
	GSubprocess *p = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, &ge);
	if (!p) {
		g_strlcpy(erro, "O curl não está instalado.", tam);
		g_clear_error(&ge);
		return NULL;
	}
	char *conf = g_strdup_printf("url = \"%s\"\n", url), *out = NULL, *err = NULL;
	GBytes *in = g_bytes_new_take(conf, strlen(conf)), *saida = NULL, *saida_err = NULL;
	bool ok = g_subprocess_communicate(p, in, NULL, &saida, &saida_err, &ge);
	g_bytes_unref(in);
	if (saida) {
		gsize n;
		const char *d = g_bytes_get_data(saida, &n);
		out = g_strndup(d, n);
		g_bytes_unref(saida);
	}
	if (saida_err) {
		gsize n;
		const char *d = g_bytes_get_data(saida_err, &n);
		err = g_strndup(d, n);
		g_bytes_unref(saida_err);
	}
	bool bom = ok && g_subprocess_get_if_exited(p) && g_subprocess_get_exit_status(p) == 0;
	g_object_unref(p);
	g_clear_error(&ge);
	if (!bom) {
		g_strlcpy(erro, err && *g_strstrip(err) ? err : "Sem conexão com a loja.", tam);
		g_free(out);
		out = NULL;
	}
	g_free(err);
	return out;
}

/* a resposta de uma url, do cache se tem menos de `ttl` segundos (e, sem rede, do cache velho mesmo) */
static char *buscar(const char *url, int ttl, char *erro, size_t tam) {
	char *arq = cache_arquivo(url, ".json"), *t = NULL;
	GStatBuf st;
	bool tem = g_stat(arq, &st) == 0;
	if (tem && time(NULL) - st.st_mtime < ttl && g_file_get_contents(arq, &t, NULL, NULL)) {
		g_free(arq);
		return t;
	}
	char e[160] = "";
	char *corpo = curl_corpo(url, e, sizeof(e));
	if (corpo) {
		char *dir = cache_dir();
		g_mkdir_with_parents(dir, 0700);
		g_file_set_contents(arq, corpo, -1, NULL);
		g_free(dir);
	} else if (tem && g_file_get_contents(arq, &t, NULL, NULL)) {
		corpo = t; /* sem rede: o que se tinha */
	} else if (erro) {
		g_strlcpy(erro, e, tam);
	}
	g_free(arq);
	return corpo;
}

/* ---------------- texto ---------------- */

/* tira as marcas HTML e desfaz as entidades; <br> e </p> viram quebra de linha */
static void sem_html(const char *src, char *dest, size_t tam) {
	GString *s = g_string_new(NULL);
	bool tag = false;
	for (const char *c = src; *c && s->len + 1 < tam; c++) {
		if (*c == '<') {
			tag = true;
			if (!g_ascii_strncasecmp(c, "<br", 3) || !g_ascii_strncasecmp(c, "</p", 3) || !g_ascii_strncasecmp(c, "</li", 4)) {
				g_string_append_c(s, '\n');
			}
		} else if (*c == '>') {
			tag = false;
		} else if (!tag) {
			g_string_append_c(s, *c);
		}
	}
	const struct { const char *de, *para; } ent[] = { { "&amp;", "&" }, { "&quot;", "\"" }, { "&#39;", "'" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&nbsp;", " " }, { "&#x27;", "'" } };
	for (size_t i = 0; i < sizeof(ent) / sizeof(ent[0]); i++) {
		char **partes = g_strsplit(s->str, ent[i].de, -1);
		char *r = g_strjoinv(ent[i].para, partes);
		g_strfreev(partes);
		g_string_assign(s, r);
		g_free(r);
	}
	char *lim = g_strstrip(s->str);
	g_strlcpy(dest, lim, tam);
	g_string_free(s, TRUE);
}

static void preco_txt(int centavos, char *dest, size_t tam) {
	g_snprintf(dest, tam, "R$ %d,%02d", centavos / 100, centavos % 100);
}

static void imagens(int appid, struct ls_item *it) {
	g_snprintf(it->capa, sizeof(it->capa), "%s/steam/apps/%d/capsule_616x353.jpg", base_img(), appid);
	g_snprintf(it->fundo, sizeof(it->fundo), "%s/steam/apps/%d/library_hero.jpg", base_img(), appid);
}

/* o que a Loja da GOG (loja_gog.c) reaproveita */
char *ls_http(const char *url, int ttl, char *erro, size_t tam) { return buscar(url, ttl, erro, tam); }
void ls_sem_html(const char *src, char *dest, size_t tam) { sem_html(src, dest, tam); }
const char *ls_base_img(void) { return base_img(); }

/* ---------------- destaques ---------------- */

static int lista_featured(const char *chave, struct ls_item *l, int max, char *erro, size_t tam) {
	char *url = g_strdup_printf("%s/api/featuredcategories?cc=br&l=portuguese", base_loja());
	char *corpo = buscar(url, 3600, erro, tam);
	g_free(url);
	int n = 0;
	struct json *j = corpo ? json_ler(corpo) : NULL;
	const struct json *itens = json_chave(json_chave(j, chave), "items");
	for (int i = 0; itens && i < itens->nfilhos && n < max; i++) {
		const struct json *x = json_item(itens, i);
		int id = (int)json_numero(json_chave(x, "id"), 0);
		if (!id) {
			continue;
		}
		struct ls_item *it = &l[n++];
		memset(it, 0, sizeof(*it));
		it->appid = id;
		g_strlcpy(it->nome, json_chave_texto(x, "name"), sizeof(it->nome));
		imagens(id, it);
		int final_ = (int)json_numero(json_chave(x, "final_price"), 0), orig = (int)json_numero(json_chave(x, "original_price"), 0);
		it->desconto = (int)json_numero(json_chave(x, "discount_percent"), 0);
		it->gratis = final_ == 0;
		if (it->gratis) {
			g_strlcpy(it->preco, "Grátis", sizeof(it->preco));
		} else {
			preco_txt(final_, it->preco, sizeof(it->preco));
		}
		if (it->desconto > 0 && orig > 0) {
			preco_txt(orig, it->preco_antes, sizeof(it->preco_antes));
		}
		it->linux_nativo = json_bool(json_chave(x, "linux_available"), false);
	}
	json_liberar(j);
	g_free(corpo);
	return n;
}

int ls_ofertas(struct ls_item *l, int max, char *erro, size_t tam) { return lista_featured("specials", l, max, erro, tam); }
int ls_mais_vendidos(struct ls_item *l, int max, char *erro, size_t tam) { return lista_featured("top_sellers", l, max, erro, tam); }
int ls_lancamentos(struct ls_item *l, int max, char *erro, size_t tam) { return lista_featured("new_releases", l, max, erro, tam); }

/* ---------------- busca ---------------- */

static const struct { const char *nome; int tag; } generos[LS_GEN_N] = {
	{ "Todas", 0 }, { "Ação", 19 }, { "Aventura", 21 }, { "RPG", 122 }, { "Corrida", 699 }, { "Estratégia", 9 }, { "Indie", 492 },
	{ "Simulação", 599 }, { "Esportes", 701 }, { "Tiro", 1774 }, { "Terror", 1667 }, { "Luta", 1743 }, { "Plataforma", 1625 },
	{ "Quebra-cabeça", 1664 }, { "Sobrevivência", 1662 }, { "Mundo aberto", 1695 }, { "Casual", 597 },
};

const char *ls_gen_nome(int g) {
	return g >= 0 && g < LS_GEN_N ? generos[g].nome : "";
}

static const char *entre(const char *s, const char *ini, const char *fim, char *dest, size_t tam) {
	const char *a = strstr(s, ini);
	dest[0] = '\0';
	if (!a) {
		return NULL;
	}
	a += strlen(ini);
	const char *b = strstr(a, fim);
	if (!b) {
		return NULL;
	}
	char *t = g_strndup(a, (gsize)(b - a));
	sem_html(t, dest, tam);
	g_free(t);
	return b;
}

int ls_buscar(const struct ls_filtro *f, struct ls_item *l, int max, int *total, char *erro, size_t tam) {
	char *termo = g_uri_escape_string(f->termo, NULL, FALSE);
	GString *u = g_string_new(NULL);
	g_string_append_printf(u, "%s/search/results/?query&start=0&count=%d&infinite=1&json=1&cc=br&l=portuguese&category1=998&term=%s", base_loja(), max > 50 ? 50 : max, termo);
	g_free(termo);
	int tag = f->genero > 0 && f->genero < LS_GEN_N ? generos[f->genero].tag : 0;
	if (tag) {
		g_string_append_printf(u, "&tags=%d", tag);
	}
	if (f->gratis) {
		g_string_append(u, "&maxprice=free");
	}
	if (f->linux_nativo) {
		g_string_append(u, "&os=linux");
	}
	/* as categorias de jogador/controle se somam numa lista só (category3=2,28: uma e outra) */
	char cats[40] = "";
	if (f->um_jogador && !f->cooperativo) {
		g_strlcat(cats, "2,", sizeof(cats));
	} else if (f->cooperativo && !f->um_jogador) {
		g_strlcat(cats, "9,", sizeof(cats));
	}
	if (f->multijogador) {
		g_strlcat(cats, "1,", sizeof(cats));
	}
	if (f->controle) {
		g_strlcat(cats, "28,", sizeof(cats));
	}
	if (cats[0]) {
		cats[strlen(cats) - 1] = '\0';
		g_string_append_printf(u, "&category3=%s", cats);
	}
	if (f->promocao) {
		g_string_append(u, "&specials=1");
	}
	static const char *const ordens[] = { "", "&filter=topsellers", "&sort_by=Released_DESC", "&sort_by=Price_ASC" };
	g_string_append(u, ordens[f->ordem >= 0 && f->ordem < 4 ? f->ordem : 0]);
	char *corpo = buscar(u->str, 1800, erro, tam);
	g_string_free(u, TRUE);
	int n = 0;
	struct json *j = corpo ? json_ler(corpo) : NULL;
	if (total) {
		*total = (int)json_numero(json_chave(j, "total_count"), 0);
	}
	const char *html = json_chave_texto(j, "results_html");
	const char *p = html;
	while (p && (p = strstr(p, "data-ds-appid=\"")) && n < max) {
		int id = atoi(p + strlen("data-ds-appid=\""));
		const char *fim = strstr(p + 10, "data-ds-appid=\"");
		char *bloco = fim ? g_strndup(p, (gsize)(fim - p)) : g_strdup(p);
		p += 10;
		if (id <= 0) {
			g_free(bloco);
			continue;
		}
		struct ls_item *it = &l[n++];
		memset(it, 0, sizeof(*it));
		it->appid = id;
		entre(bloco, "<span class=\"title\">", "</span>", it->nome, sizeof(it->nome));
		imagens(id, it);
		const char *pf = strstr(bloco, "data-price-final=\"");
		int final_ = pf ? atoi(pf + strlen("data-price-final=\"")) : 0;
		const char *dd = strstr(bloco, "data-discount=\"");
		it->desconto = dd ? atoi(dd + strlen("data-discount=\"")) : 0;
		it->gratis = !pf || final_ == 0;
		if (it->gratis) {
			g_strlcpy(it->preco, strstr(bloco, "search_price") ? "Grátis" : "—", sizeof(it->preco));
		} else {
			preco_txt(final_, it->preco, sizeof(it->preco));
		}
		if (it->desconto > 0) {
			entre(bloco, "<strike>", "</strike>", it->preco_antes, sizeof(it->preco_antes));
		}
		it->linux_nativo = strstr(bloco, "platform_img linux") != NULL;
		g_free(bloco);
	}
	json_liberar(j);
	g_free(corpo);
	return n;
}

/* ---------------- detalhes ---------------- */

bool ls_detalhes(int appid, struct ls_detalhe *d, char *erro, size_t tam) {
	memset(d, 0, sizeof(*d));
	char *url = g_strdup_printf("%s/api/appdetails?appids=%d&cc=br&l=portuguese", base_loja(), appid);
	char *corpo = buscar(url, 6 * 3600, erro, tam);
	g_free(url);
	struct json *j = corpo ? json_ler(corpo) : NULL;
	char chave[16];
	g_snprintf(chave, sizeof(chave), "%d", appid);
	const struct json *r = json_chave(j, chave);
	const struct json *x = json_chave(r, "data");
	if (!x || !json_bool(json_chave(r, "success"), false)) {
		json_liberar(j);
		g_free(corpo);
		if (erro && corpo) {
			g_strlcpy(erro, "A loja não tem esse jogo.", tam);
		}
		return false;
	}
	d->appid = appid;
	d->existe = true;
	g_strlcpy(d->nome, json_chave_texto(x, "name"), sizeof(d->nome));
	sem_html(json_chave_texto(x, "short_description"), d->resumo, sizeof(d->resumo));
	sem_html(json_chave_texto(x, "about_the_game"), d->sobre, sizeof(d->sobre));
	g_strlcpy(d->desenvolvedor, json_item(json_chave(x, "developers"), 0) ? json_texto(json_item(json_chave(x, "developers"), 0)) : "", sizeof(d->desenvolvedor));
	g_strlcpy(d->lancamento, json_chave_texto(json_chave(x, "release_date"), "date"), sizeof(d->lancamento));
	g_snprintf(d->capa, sizeof(d->capa), "%s/steam/apps/%d/capsule_616x353.jpg", base_img(), appid);
	g_snprintf(d->fundo, sizeof(d->fundo), "%s/steam/apps/%d/library_hero.jpg", base_img(), appid);
	GString *g = g_string_new(NULL);
	const struct json *gs = json_chave(x, "genres");
	for (int i = 0; gs && i < gs->nfilhos && i < 3; i++) {
		g_string_append_printf(g, "%s%s", i ? " · " : "", json_chave_texto(json_item(gs, i), "description"));
	}
	g_strlcpy(d->generos, g->str, sizeof(d->generos));
	g_string_free(g, TRUE);
	const struct json *sc = json_chave(x, "screenshots");
	for (int i = 0; sc && i < sc->nfilhos && d->ntelas < LS_TELAS; i++) {
		g_strlcpy(d->telas[d->ntelas++], json_chave_texto(json_item(sc, i), "path_thumbnail"), sizeof(d->telas[0]));
	}
	const struct json *mv = json_chave(x, "movies");
	if (mv && mv->nfilhos) {
		/* a Steam trocou os mp4/webm por listas de reprodução (HLS e DASH): o mpv toca; os antigos ainda valem se vierem */
		const struct json *m = json_item(mv, 0);
		const char *cand[] = { json_chave_texto(json_chave(m, "mp4"), "max"), json_chave_texto(json_chave(m, "webm"), "max"), json_chave_texto(m, "hls_h264"), json_chave_texto(m, "dash_h264"), NULL };
		for (int i = 0; cand[i]; i++) {
			if (*cand[i]) {
				g_strlcpy(d->trailer, cand[i], sizeof(d->trailer));
				break;
			}
		}
		g_strlcpy(d->trailer_capa, json_chave_texto(m, "thumbnail"), sizeof(d->trailer_capa));
	}
	d->gratis = json_bool(json_chave(x, "is_free"), false);
	const struct json *po = json_chave(x, "price_overview");
	if (po) {
		g_strlcpy(d->preco, json_chave_texto(po, "final_formatted"), sizeof(d->preco));
		d->desconto = (int)json_numero(json_chave(po, "discount_percent"), 0);
		if (d->desconto > 0) {
			g_strlcpy(d->preco_antes, json_chave_texto(po, "initial_formatted"), sizeof(d->preco_antes));
		}
	} else if (d->gratis) {
		g_strlcpy(d->preco, "Grátis", sizeof(d->preco));
	}
	const struct json *pl = json_chave(x, "platforms");
	d->linux_nativo = json_bool(json_chave(pl, "linux"), false);
	d->windows = json_bool(json_chave(pl, "windows"), false);
	const struct json *cs = json_chave(x, "categories");
	for (int i = 0; cs && i < cs->nfilhos; i++) {
		int id = (int)json_numero(json_chave(json_item(cs, i), "id"), 0);
		d->um_jogador |= id == 2;
		d->multijogador |= id == 1;
		d->cooperativo |= id == 9 || id == 38;
		d->controle |= id == 28 || id == 18;
	}
	const char *req = json_chave_texto(json_chave(x, d->linux_nativo ? "linux_requirements" : "pc_requirements"), "minimum");
	sem_html(req, d->requisitos, sizeof(d->requisitos));
	d->metacritic = (int)json_numero(json_chave(json_chave(x, "metacritic"), "score"), 0);
	json_liberar(j);
	g_free(corpo);
	return true;
}

/* ---------------- ProtonDB ---------------- */

bool ls_proton(int appid, char *nivel, size_t tam) {
	char *url = g_strdup_printf("%s/api/v1/reports/summaries/%d.json", base_proton(), appid), e[80] = "";
	char *corpo = buscar(url, 24 * 3600, e, sizeof(e));
	g_free(url);
	struct json *j = corpo ? json_ler(corpo) : NULL;
	g_strlcpy(nivel, j ? json_chave_texto(j, "tier") : "", tam);
	bool ok = nivel[0] != '\0';
	json_liberar(j);
	g_free(corpo);
	return ok;
}

const char *ls_proton_texto(const char *n) {
	if (!n || !*n) {
		return "";
	}
	if (!strcmp(n, "platinum")) return "Via Proton · roda perfeito";
	if (!strcmp(n, "gold")) return "Via Proton · roda bem";
	if (!strcmp(n, "silver")) return "Via Proton · roda com ressalvas";
	if (!strcmp(n, "bronze")) return "Via Proton · roda mal";
	if (!strcmp(n, "borked")) return "Não roda no Proton";
	return "Via Proton";
}

/* ---------------- a conta (a chave de API) ---------------- */

static char *arq_steam_ini(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "steam.ini", NULL);
}

static void ini_ler(char **chave, char **id) {
	GKeyFile *kf = g_key_file_new();
	char *a = arq_steam_ini();
	*chave = *id = NULL;
	if (g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL)) {
		*chave = g_key_file_get_string(kf, "api", "chave", NULL);
		*id = g_key_file_get_string(kf, "api", "id64", NULL);
	}
	g_free(a);
	g_key_file_free(kf);
}

bool ls_chave_existe(void) {
	char *c, *i;
	ini_ler(&c, &i);
	bool ok = c && *c;
	g_free(c);
	g_free(i);
	return ok;
}

void ls_chave_ler(char *id64, size_t tam) {
	char *c, *i;
	ini_ler(&c, &i);
	g_strlcpy(id64, i ? i : "", tam);
	g_free(c);
	g_free(i);
}

static bool so_alnum(const char *s, size_t min, size_t max) {
	size_t n = s ? strlen(s) : 0;
	for (size_t k = 0; k < n; k++) {
		if (!g_ascii_isalnum(s[k])) {
			return false;
		}
	}
	return n >= min && n <= max;
}

bool ls_chave_gravar(const char *chave, const char *id64) {
	/* a chave da Steam são 32 letras e números; o id64 são 17 dígitos: nada além disso entra no arquivo nem na url */
	if (!so_alnum(chave, 20, 64) || (id64 && *id64 && !so_alnum(id64, 15, 20))) {
		return false;
	}
	GKeyFile *kf = g_key_file_new();
	char *a = arq_steam_ini(), *dir = g_path_get_dirname(a);
	g_key_file_set_string(kf, "api", "chave", chave);
	g_key_file_set_string(kf, "api", "id64", id64 ? id64 : "");
	g_mkdir_with_parents(dir, 0700);
	char *t = g_key_file_to_data(kf, NULL, NULL);
	bool ok = g_file_set_contents(a, t, -1, NULL);
	if (ok) {
		g_chmod(a, 0600);
	}
	g_free(t);
	g_free(dir);
	g_free(a);
	g_key_file_free(kf);
	return ok;
}

void ls_id_local(char *id64, size_t tam) {
	id64[0] = '\0';
	const char *e = g_getenv("LUMNA_STEAM_DIR");
	char *raiz = e && *e ? g_strdup(e) : g_build_filename(g_get_home_dir(), ".local", "share", "Steam", NULL);
	char *a = g_build_filename(raiz, "config", "loginusers.vdf", NULL), *t = NULL;
	if (g_file_get_contents(a, &t, NULL, NULL)) {
		/* o usuário mais recente: o bloco com "MostRecent" "1" cujo título é o id64 */
		const char *p = t;
		while ((p = strstr(p, "\"7656")) != NULL) {
			char id[24] = "";
			const char *q = p + 1;
			size_t n = 0;
			while (*q && *q != '"' && n < sizeof(id) - 1) {
				id[n++] = *q++;
			}
			const char *bloco_fim = strstr(q, "\n\t}");
			const char *mr = strstr(q, "\"MostRecent\"");
			if (mr && (!bloco_fim || mr < bloco_fim) && strstr(mr, "\"1\"") && strstr(mr, "\"1\"") < mr + 24) {
				g_strlcpy(id64, id, tam);
				break;
			}
			if (!id64[0]) {
				g_strlcpy(id64, id, tam); /* o primeiro serve de reserva */
			}
			p = q;
		}
		g_free(t);
	}
	g_free(a);
	g_free(raiz);
}

int ls_possuidos(int *appids, int max, char *erro, size_t tam) {
	char *chave, *id;
	ini_ler(&chave, &id);
	char idl[24];
	if (!chave || !*chave) {
		g_strlcpy(erro, "Falta a chave da API da Steam (Configurações > Steam).", tam);
		g_free(chave);
		g_free(id);
		return -1;
	}
	g_strlcpy(idl, id && *id ? id : "", sizeof(idl));
	if (!idl[0]) {
		ls_id_local(idl, sizeof(idl));
	}
	if (!idl[0]) {
		g_strlcpy(erro, "Não achei o seu id da Steam (entre na Steam uma vez, ou preencha o id nas Configurações).", tam);
		g_free(chave);
		g_free(id);
		return -1;
	}
	char *url = g_strdup_printf("%s/IPlayerService/GetOwnedGames/v1/?key=%s&steamid=%s&include_played_free_games=1&format=json", base_api(), chave, idl);
	char *corpo = buscar(url, 1800, erro, tam);
	g_free(url);
	g_free(chave);
	g_free(id);
	if (!corpo) {
		return -1;
	}
	struct json *j = json_ler(corpo);
	const struct json *g = json_chave(json_chave(j, "response"), "games");
	int n = 0;
	for (int i = 0; g && i < g->nfilhos && n < max; i++) {
		appids[n++] = (int)json_numero(json_chave(json_item(g, i), "appid"), 0);
	}
	if (!g && erro) {
		g_strlcpy(erro, "A Steam não devolveu jogos (o perfil precisa estar público, ou a chave está errada).", tam);
		n = -1;
	}
	json_liberar(j);
	g_free(corpo);
	return n;
}

/* ---------------- imagens ---------------- */

char *ls_imagem(const char *url) {
	if (!url || !g_str_has_prefix(url, "http")) {
		return NULL;
	}
	char *arq = cache_arquivo(url, ".img");
	if (g_file_test(arq, G_FILE_TEST_IS_REGULAR)) {
		return arq;
	}
	char *dir = cache_dir();
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	char *tmp = g_strconcat(arq, ".tmp", NULL);
	GError *ge = NULL;
	const char *argv[] = { "curl", "-sS", "-L", "-m", "30", "--fail", "-A", "LUMNA-Gaming/1.0", "-o", tmp, "-K", "-", NULL };
	GSubprocess *p = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &ge);
	bool ok = false;
	if (p) {
		char *conf = g_strdup_printf("url = \"%s\"\n", url);
		GBytes *in = g_bytes_new_take(conf, strlen(conf));
		ok = g_subprocess_communicate(p, in, NULL, NULL, NULL, NULL) && g_subprocess_get_if_exited(p) && g_subprocess_get_exit_status(p) == 0;
		g_bytes_unref(in);
		g_object_unref(p);
	}
	g_clear_error(&ge);
	if (ok) {
		ok = g_rename(tmp, arq) == 0;
	}
	g_unlink(tmp);
	g_free(tmp);
	if (!ok) {
		g_free(arq);
		return NULL;
	}
	return arq;
}
