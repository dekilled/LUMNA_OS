#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "json.h"
#include "loja_gog.h"

#define GOG_CLIENT_ID "46899977096215655"

static const char *env_ou(const char *nome, const char *padrao) {
	const char *e = g_getenv(nome);
	return e && *e ? e : padrao;
}

static const char *base_catalogo(void) { return env_ou("LUMNA_LOJA_GOG_CATALOG_URL", "https://catalog.gog.com"); }
static const char *base_api(void) { return env_ou("LUMNA_LOJA_GOG_API_URL", "https://api.gog.com"); }
static const char *base_embed(void) { return env_ou("LUMNA_LOJA_GOG_EMBED_URL", "https://embed.gog.com"); }

/* as imagens da GOG trazem o endereço inteiro (às vezes sem o https:, e de varios servidores images-N.gog-statics.com); nos testes o servidor local assume o lugar deles */
static void imagem_url(const char *url, char *dest, size_t tam) {
	char *u = g_str_has_prefix(url, "//") ? g_strconcat("https:", url, NULL) : g_strdup(url);
	const char *img = g_getenv("LUMNA_LOJA_GOG_IMG_URL");
	const char *host = g_str_has_prefix(u, "https://") ? u + 8 : NULL;
	const char *fim = host ? strchr(host, '/') : NULL;
	if (img && *img && host && fim && g_str_has_prefix(host, "images") && strstr(host, "gog-statics.com")) {
		g_snprintf(dest, tam, "%s%s", img, fim);
	} else {
		g_strlcpy(dest, u, tam);
	}
	g_free(u);
}

static void formatar(const char *modelo, const char *formato, char *dest, size_t tam) {
	/* "https://.../ID_{formatter}.jpg" → a versão do tamanho pedido */
	char **p = g_strsplit(modelo, "{formatter}", 2);
	char url[500];
	g_snprintf(url, sizeof(url), "%s%s%s", p[0], p[1] ? formato : "", p[1] ? p[1] : "");
	g_strfreev(p);
	imagem_url(url, dest, tam);
}

/* "R$ 26,99" (o espaço que a GOG manda é o inseparável) */
static void limpa_preco(const char *src, char *dest, size_t tam) {
	char *t = g_strdup(src);
	for (char *c = t; *c; c++) {
		if ((unsigned char)*c == 0xC2 && (unsigned char)c[1] == 0xA0) {
			*c = ' ';
			memmove(c + 1, c + 2, strlen(c + 2) + 1);
		}
	}
	g_strlcpy(dest, t, tam);
	g_free(t);
}

static void item_de_produto(const struct json *p, struct ls_item *it) {
	memset(it, 0, sizeof(*it));
	it->loja = LS_LOJA_GOG;
	it->appid = (int)g_ascii_strtoll(json_chave_texto(p, "id"), NULL, 10);
	g_strlcpy(it->nome, json_chave_texto(p, "title"), sizeof(it->nome));
	const char *h = json_chave_texto(p, "coverHorizontal"), *v = json_chave_texto(p, "coverVertical"), *bg = json_chave_texto(p, "galaxyBackgroundImage");
	char tmp[500];
	g_snprintf(tmp, sizeof(tmp), "%s", *h ? h : v);
	imagem_url(tmp, it->capa, sizeof(it->capa));
	imagem_url(*bg ? bg : h, it->fundo, sizeof(it->fundo));
	const struct json *pr = json_chave(p, "price");
	limpa_preco(json_chave_texto(pr, "final"), it->preco, sizeof(it->preco));
	const char *desc = json_chave_texto(pr, "discount");
	it->desconto = desc[0] == '-' ? atoi(desc + 1) : 0;
	if (it->desconto > 0) {
		limpa_preco(json_chave_texto(pr, "base"), it->preco_antes, sizeof(it->preco_antes));
	}
	const char *amt = json_chave_texto(json_chave(pr, "finalMoney"), "amount");
	it->gratis = amt[0] && g_ascii_strtod(amt, NULL) == 0.0;
	if (it->gratis) {
		g_strlcpy(it->preco, "Grátis", sizeof(it->preco));
	}
	const struct json *os = json_chave(p, "operatingSystems");
	for (int i = 0; os && i < os->nfilhos; i++) {
		it->linux_nativo |= json_texto(json_item(os, i)) && !strcmp(json_texto(json_item(os, i)), "linux");
	}
	GString *g = g_string_new(NULL);
	const struct json *gs = json_chave(p, "genres");
	for (int i = 0; gs && i < gs->nfilhos && i < 2; i++) {
		g_string_append_printf(g, "%s%s", i ? " · " : "", json_chave_texto(json_item(gs, i), "name"));
	}
	g_strlcpy(it->generos, g->str, sizeof(it->generos));
	g_string_free(g, TRUE);
}

/* ---------------- catálogo ---------------- */

static const char *const generos_slug[LS_GEN_N] = { "", "action", "adventure", "rpg", "racing", "strategy", "" };

static int consulta(const char *extra, struct ls_item *l, int max, int *total, char *erro, size_t tam) {
	char *url = g_strdup_printf("%s/v1/catalog?countryCode=BR&currencyCode=BRL&productType=in:game,pack&page=1&limit=%d%s", base_catalogo(), max > 48 ? 48 : max, extra);
	char *corpo = ls_http(url, 1800, erro, tam);
	g_free(url);
	struct json *j = corpo ? json_ler(corpo) : NULL;
	if (total) {
		*total = (int)json_numero(json_chave(j, "productCount"), 0);
	}
	const struct json *ps = json_chave(j, "products");
	int n = 0;
	for (int i = 0; ps && i < ps->nfilhos && n < max; i++) {
		item_de_produto(json_item(ps, i), &l[n]);
		if (l[n].appid > 0) {
			n++;
		}
	}
	json_liberar(j);
	g_free(corpo);
	return n;
}

int lg_destaques(struct ls_item *l, int max, char *erro, size_t tam) {
	return consulta("&order=desc:trending", l, max, NULL, erro, tam);
}

int lg_ofertas(struct ls_item *l, int max, char *erro, size_t tam) {
	return consulta("&order=desc:trending&discounted=eq:true", l, max, NULL, erro, tam);
}

int lg_buscar(const struct ls_filtro *f, struct ls_item *l, int max, int *total, char *erro, size_t tam) {
	GString *q = g_string_new(NULL);
	if (f->termo[0]) {
		char *t = g_uri_escape_string(f->termo, NULL, FALSE);
		g_string_append_printf(q, "&query=like:%s", t);
		g_free(t);
	}
	if (f->genero == LS_GEN_INDIE) {
		g_string_append(q, "&tags=in:indie");
	} else if (f->genero > 0 && generos_slug[f->genero][0]) {
		g_string_append_printf(q, "&genres=in:%s", generos_slug[f->genero]);
	}
	if (f->gratis) {
		g_string_append(q, "&price=between:0,0");
	}
	if (f->linux_nativo) {
		g_string_append(q, "&systems=in:linux");
	}
	if (f->um_jogador && !f->cooperativo) {
		g_string_append(q, "&features=in:single");
	} else if (f->cooperativo && !f->um_jogador) {
		g_string_append(q, "&features=in:coop");
	}
	static const char *const ordens[] = { "&order=desc:score", "&order=desc:trending", "&order=desc:releaseDate", "&order=asc:price" };
	g_string_append(q, ordens[f->ordem >= 0 && f->ordem < 4 ? f->ordem : 0]);
	int n = consulta(q->str, l, max, total, erro, tam);
	g_string_free(q, TRUE);
	return n;
}

/* ---------------- detalhes ---------------- */

bool lg_detalhes(int id, struct ls_detalhe *d, char *erro, size_t tam) {
	memset(d, 0, sizeof(*d));
	char *url = g_strdup_printf("%s/products/%d?expand=description,screenshots,videos", base_api(), id);
	char *corpo = ls_http(url, 6 * 3600, erro, tam);
	g_free(url);
	struct json *j = corpo ? json_ler(corpo) : NULL;
	if (!j || !*json_chave_texto(j, "title")) {
		json_liberar(j);
		g_free(corpo);
		if (erro && corpo) {
			g_strlcpy(erro, "A GOG não tem esse jogo.", tam);
		}
		return false;
	}
	d->loja = LS_LOJA_GOG;
	d->appid = id;
	d->existe = true;
	g_strlcpy(d->nome, json_chave_texto(j, "title"), sizeof(d->nome));
	const struct json *desc = json_chave(j, "description");
	ls_sem_html(json_chave_texto(desc, "lead"), d->resumo, sizeof(d->resumo));
	ls_sem_html(json_chave_texto(desc, "full"), d->sobre, sizeof(d->sobre));
	g_strlcpy(d->lancamento, json_chave_texto(j, "release_date"), sizeof(d->lancamento));
	if (strlen(d->lancamento) > 10) {
		d->lancamento[10] = '\0';
	}
	const struct json *lk = json_chave(j, "links");
	g_strlcpy(d->loja_url, *json_chave_texto(lk, "product_card") ? json_chave_texto(lk, "product_card") : json_chave_texto(lk, "purchase_link"), sizeof(d->loja_url));
	const struct json *comp = json_chave(j, "content_system_compatibility");
	d->linux_nativo = json_bool(json_chave(comp, "linux"), false);
	d->windows = json_bool(json_chave(comp, "windows"), false);
	const char *bg = json_chave_texto(json_chave(j, "images"), "background");
	if (*bg) {
		imagem_url(bg, d->fundo, sizeof(d->fundo));
	}
	const struct json *sc = json_chave(j, "screenshots");
	for (int i = 0; sc && i < sc->nfilhos && d->ntelas < LS_TELAS; i++) {
		formatar(json_chave_texto(json_item(sc, i), "formatter_template_url"), "ggvgm", d->telas[d->ntelas], sizeof(d->telas[0]));
		if (d->telas[d->ntelas][0]) {
			d->ntelas++;
		}
	}
	if (!d->fundo[0] && d->ntelas) {
		g_strlcpy(d->fundo, d->telas[0], sizeof(d->fundo));
	}
	g_strlcpy(d->capa, d->fundo, sizeof(d->capa));
	const struct json *vs = json_chave(j, "videos");
	if (vs && vs->nfilhos) {
		/* os vídeos da GOG são páginas (Wistia/YouTube): o mpv toca pelo yt-dlp */
		g_strlcpy(d->trailer, json_chave_texto(json_item(vs, 0), "video_url"), sizeof(d->trailer));
		imagem_url(json_chave_texto(json_item(vs, 0), "thumbnail_url"), d->trailer_capa, sizeof(d->trailer_capa));
	}
	json_liberar(j);
	g_free(corpo);
	/* o preço, em reais */
	char *purl = g_strdup_printf("%s/products/%d/prices?countryCode=BR", base_api(), id), e2[80] = "";
	char *pc = ls_http(purl, 1800, e2, sizeof(e2));
	g_free(purl);
	struct json *pj = pc ? json_ler(pc) : NULL;
	const struct json *lista = json_chave(json_chave(pj, "_embedded"), "prices");
	for (int i = 0; lista && i < lista->nfilhos; i++) {
		const struct json *p = json_item(lista, i);
		if (!strcmp(json_chave_texto(json_chave(p, "currency"), "code"), "BRL")) {
			int fin = atoi(json_chave_texto(p, "finalPrice")), base = atoi(json_chave_texto(p, "basePrice"));
			d->gratis = fin == 0;
			if (d->gratis) {
				g_strlcpy(d->preco, "Grátis", sizeof(d->preco));
			} else {
				g_snprintf(d->preco, sizeof(d->preco), "R$ %d,%02d", fin / 100, fin % 100);
			}
			if (base > fin) {
				g_snprintf(d->preco_antes, sizeof(d->preco_antes), "R$ %d,%02d", base / 100, base % 100);
				d->desconto = (base - fin) * 100 / base;
			}
		}
	}
	json_liberar(pj);
	g_free(pc);
	return true;
}

/* ---------------- a conta ---------------- */

static char *dir_gog(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "gog", NULL);
}

static char *arq_auth(void) {
	char *d = dir_gog(), *a = g_build_filename(d, "auth.json", NULL);
	g_free(d);
	return a;
}

bool lg_logado(void) {
	char *a = arq_auth();
	bool ok = g_file_test(a, G_FILE_TEST_IS_REGULAR);
	g_free(a);
	return ok;
}

const char *lg_url_login(void) {
	return "https://auth.gog.com/auth?client_id=" GOG_CLIENT_ID "&redirect_uri=https%3A%2F%2Fembed.gog.com%2Fon_login_success%3Forigin%3Dclient&response_type=code&layout=galaxy";
}

/* o código: só letras, números, - e _ (o que a GOG manda); da URL de volta pega o que vem depois de code= */
static bool extrair_codigo(const char *entrada, char *codigo, size_t tam) {
	const char *c = strstr(entrada, "code=");
	char *t = g_strdup(c ? c + 5 : entrada);
	g_strstrip(t);
	char *fim = strpbrk(t, "&# \t\n");
	if (fim) {
		*fim = '\0';
	}
	bool ok = strlen(t) >= 10 && strlen(t) < tam;
	for (const char *p = t; ok && *p; p++) {
		ok = g_ascii_isalnum(*p) || *p == '-' || *p == '_';
	}
	if (ok) {
		g_strlcpy(codigo, t, tam);
	}
	g_free(t);
	return ok;
}

static bool gogdl(const char **args, char **saida, char *erro, size_t tam) {
	GError *ge = NULL;
	GSubprocess *p = g_subprocess_newv(args, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, &ge);
	if (!p) {
		g_strlcpy(erro, "O gogdl não está instalado (AUR: gogdl).", tam);
		g_clear_error(&ge);
		return false;
	}
	char *out = NULL, *err = NULL;
	bool ok = g_subprocess_communicate_utf8(p, NULL, NULL, &out, &err, &ge) && g_subprocess_get_if_exited(p) && g_subprocess_get_exit_status(p) == 0;
	if (!ok) {
		g_strlcpy(erro, err && *g_strstrip(err) ? err : "O gogdl falhou.", tam);
	}
	if (saida) {
		*saida = out;
	} else {
		g_free(out);
	}
	g_free(err);
	g_object_unref(p);
	g_clear_error(&ge);
	return ok;
}

bool lg_login(const char *entrada, char *erro, size_t tam) {
	char codigo[160];
	if (!extrair_codigo(entrada ? entrada : "", codigo, sizeof(codigo))) {
		g_strlcpy(erro, "Esse código não parece o da GOG (cole a URL inteira para onde ela mandou depois do login).", tam);
		return false;
	}
	char *dir = dir_gog(), *a = arq_auth();
	g_mkdir_with_parents(dir, 0700);
	const char *args[] = { "gogdl", "--auth-config-path", a, "auth", "--code", codigo, NULL };
	bool ok = gogdl(args, NULL, erro, tam);
	if (ok && !g_file_test(a, G_FILE_TEST_IS_REGULAR)) {
		g_strlcpy(erro, "O gogdl não guardou o login.", tam);
		ok = false;
	}
	if (ok) {
		g_chmod(a, 0600);
	}
	g_free(a);
	g_free(dir);
	return ok;
}

void lg_sair(void) {
	char *a = arq_auth();
	g_unlink(a);
	g_free(a);
}

/* o token de acesso do auth.json do gogdl: o primeiro objeto com access_token */
static char *token(void) {
	char *a = arq_auth(), *t = NULL, *r = NULL;
	if (g_file_get_contents(a, &t, NULL, NULL)) {
		struct json *j = json_ler(t);
		if (j && *json_chave_texto(j, "access_token")) {
			r = g_strdup(json_chave_texto(j, "access_token"));
		}
		for (int i = 0; j && !r && i < j->nfilhos; i++) {
			if (*json_chave_texto(j->filhos[i], "access_token")) {
				r = g_strdup(json_chave_texto(j->filhos[i], "access_token"));
			}
		}
		json_liberar(j);
		g_free(t);
	}
	g_free(a);
	return r;
}

int lg_possuidos(int *ids, int max, char *erro, size_t tam) {
	if (!lg_logado()) {
		g_strlcpy(erro, "Entre na GOG (Configurações > GOG) para ver os jogos da conta.", tam);
		return -1;
	}
	char *tk = token();
	if (!tk) {
		g_strlcpy(erro, "O login da GOG expirou: entre de novo.", tam);
		return -1;
	}
	/* o cabeçalho com o token vai pela entrada do curl, não pela linha de comando */
	GError *ge = NULL;
	const char *argv[] = { "curl", "-sS", "-L", "-m", "25", "--fail", "-K", "-", NULL };
	GSubprocess *p = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &ge);
	if (!p) {
		g_free(tk);
		g_strlcpy(erro, "O curl não está instalado.", tam);
		g_clear_error(&ge);
		return -1;
	}
	char *conf = g_strdup_printf("url = \"%s/user/data/games\"\nheader = \"Authorization: Bearer %s\"\n", base_embed(), tk), *out = NULL;
	GBytes *in = g_bytes_new_take(conf, strlen(conf)), *saida = NULL;
	bool ok = g_subprocess_communicate(p, in, NULL, &saida, NULL, &ge) && g_subprocess_get_if_exited(p) && g_subprocess_get_exit_status(p) == 0;
	g_bytes_unref(in);
	if (saida) {
		gsize n;
		const char *dd = g_bytes_get_data(saida, &n);
		out = g_strndup(dd, n);
		g_bytes_unref(saida);
	}
	g_object_unref(p);
	g_clear_error(&ge);
	g_free(tk);
	int n = 0;
	if (!ok || !out) {
		g_strlcpy(erro, "A GOG não respondeu (o login pode ter expirado).", tam);
		g_free(out);
		return -1;
	}
	struct json *j = json_ler(out);
	const struct json *o = json_chave(j, "owned");
	for (int i = 0; o && i < o->nfilhos && n < max; i++) {
		ids[n++] = (int)json_numero(json_item(o, i), 0);
	}
	if (!o) {
		g_strlcpy(erro, "A GOG não devolveu a lista de jogos.", tam);
		n = -1;
	}
	json_liberar(j);
	g_free(out);
	return n;
}

/* ---------------- instalar ---------------- */

static char *arq_estado(int id) {
	char *d = g_build_filename(g_get_user_cache_dir(), "lumna", "jogos", NULL), name[48], *a;
	g_mkdir_with_parents(d, 0700);
	g_snprintf(name, sizeof(name), "gog_%d.estado", id);
	a = g_build_filename(d, name, NULL);
	g_free(d);
	return a;
}

static void estado_gravar(int id, const char *estado, int pct, const char *msg) {
	char *a = arq_estado(id), *t = g_strdup_printf("%s\n%d\n%s\n", estado, pct, msg ? msg : "");
	g_file_set_contents(a, t, -1, NULL);
	g_free(t);
	g_free(a);
}

bool lg_progresso(int id, struct lg_prog *p) {
	memset(p, 0, sizeof(*p));
	char *a = arq_estado(id), *t = NULL;
	bool ok = g_file_get_contents(a, &t, NULL, NULL);
	if (ok) {
		char **l = g_strsplit(t, "\n", 4);
		p->existe = true;
		p->baixando = l[0] && !strcmp(l[0], "baixando");
		p->pronto = l[0] && !strcmp(l[0], "pronto");
		p->erro = l[0] && !strcmp(l[0], "erro");
		p->pct = l[0] && l[1] ? atoi(l[1]) : 0;
		g_strlcpy(p->msg, l[0] && l[1] && l[2] ? l[2] : "", sizeof(p->msg));
		g_strfreev(l);
		g_free(t);
	}
	g_free(a);
	return ok;
}

int lg_progresso_da_linha(const char *linha) {
	/* o gogdl escreve "Progress: 12.34 ..." ou "... 12.3% ..." */
	const char *p = strstr(linha, "Progress:");
	if (p) {
		p += 9;
		while (*p == ' ') {
			p++;
		}
		if (g_ascii_isdigit(*p)) {
			double v = g_ascii_strtod(p, NULL);
			return v < 0 ? 0 : v > 100 ? 100 : (int)v;
		}
	}
	const char *pc = strchr(linha, '%');
	if (pc && pc > linha) {
		const char *q = pc - 1;
		while (q > linha && (g_ascii_isdigit(*q) || *q == '.')) {
			q--;
		}
		if (!g_ascii_isdigit(*q) && *q != '.') {
			q++;
		}
		if (g_ascii_isdigit(*q)) {
			double v = g_ascii_strtod(q, NULL);
			return v < 0 ? 0 : v > 100 ? 100 : (int)v;
		}
	}
	return -1;
}

static char *arq_registro(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "gog_instalados.json", NULL);
}

static void registrar(int id, const char *titulo, const char *pasta, bool linux_nativo) {
	char *a = arq_registro(), *t = NULL;
	struct json *j = g_file_get_contents(a, &t, NULL, NULL) ? json_ler(t) : NULL;
	const struct json *l = json_chave(j, "jogos");
	GString *s = g_string_new("{\"jogos\":[");
	for (int i = 0; l && i < l->nfilhos; i++) {
		if ((int)json_numero(json_chave(json_item(l, i), "id"), 0) != id) {
			json_escrever(s, json_item(l, i));
			g_string_append_c(s, ',');
		}
	}
	g_string_append_printf(s, "{\"id\":%d,\"nome\":", id);
	json_escrever_texto(s, titulo);
	g_string_append(s, ",\"pasta\":");
	json_escrever_texto(s, pasta);
	g_string_append_printf(s, ",\"linux\":%s}]}", linux_nativo ? "true" : "false");
	char *dir = g_path_get_dirname(a);
	g_mkdir_with_parents(dir, 0700);
	g_file_set_contents(a, s->str, -1, NULL);
	g_free(dir);
	g_string_free(s, TRUE);
	json_liberar(j);
	g_free(t);
	g_free(a);
}

int lg_instalar_agora(int id, const char *titulo, const char *pasta, bool linux_nativo) {
	char *auth = arq_auth(), sid[24], msg[160] = "";
	g_snprintf(sid, sizeof(sid), "%d", id);
	/* a pasta do jogo: o título sem caracteres perigosos, dentro da pasta escolhida */
	char dirnome[96] = "";
	for (const char *c = titulo; *c && strlen(dirnome) < sizeof(dirnome) - 1; c++) {
		if (g_ascii_isalnum(*c) || *c == ' ' || *c == '-' || *c == '_') {
			dirnome[strlen(dirnome)] = *c;
		}
	}
	if (!dirnome[0]) {
		g_snprintf(dirnome, sizeof(dirnome), "gog_%d", id);
	}
	char *destino = g_build_filename(pasta, dirnome, NULL);
	g_mkdir_with_parents(destino, 0755);
	estado_gravar(id, "baixando", 0, "Preparando…");
	const char *argv[] = { "gogdl", "--auth-config-path", auth, "download", sid, "--platform", linux_nativo ? "linux" : "windows", "--path", destino, "--with-dlcs", NULL };
	GError *ge = NULL;
	GSubprocess *p = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE, &ge);
	if (!p) {
		estado_gravar(id, "erro", 0, "O gogdl não está instalado (AUR: gogdl).");
		g_clear_error(&ge);
		g_free(auth);
		g_free(destino);
		return 1;
	}
	GDataInputStream *in = g_data_input_stream_new(g_subprocess_get_stdout_pipe(p));
	char *linha;
	int ultimo = 0;
	while ((linha = g_data_input_stream_read_line(in, NULL, NULL, NULL))) {
		int pct = lg_progresso_da_linha(linha);
		if (pct >= 0 && pct != ultimo) {
			ultimo = pct;
			estado_gravar(id, "baixando", pct, "Baixando…");
		}
		if (*linha) {
			g_strlcpy(msg, linha, sizeof(msg));
		}
		g_free(linha);
	}
	g_object_unref(in);
	g_subprocess_wait(p, NULL, NULL);
	bool ok = g_subprocess_get_if_exited(p) && g_subprocess_get_exit_status(p) == 0;
	g_object_unref(p);
	if (ok) {
		registrar(id, titulo, destino, linux_nativo);
		estado_gravar(id, "pronto", 100, "");
	} else {
		estado_gravar(id, "erro", ultimo, msg[0] ? msg : "O gogdl falhou.");
	}
	g_free(auth);
	g_free(destino);
	return ok ? 0 : 1;
}

bool lg_instalar(int id, const char *titulo, const char *pasta, bool linux_nativo, char *erro, size_t tam) {
	if (!lg_logado()) {
		g_strlcpy(erro, "Entre na GOG primeiro (Configurações > GOG).", tam);
		return false;
	}
	if (!pasta || !g_path_is_absolute(pasta)) {
		g_strlcpy(erro, "Escolha uma pasta de instalação.", tam);
		return false;
	}
	char sid[24];
	g_snprintf(sid, sizeof(sid), "%d", id);
	char *exe = g_file_read_link("/proc/self/exe", NULL);
	const char *argv[] = { exe ? exe : "lumna-jogos", "gog-instalar", sid, titulo, pasta, linux_nativo ? "linux" : "windows", NULL };
	/* o instalador é o próprio programa, num processo à parte: sobrevive a quem o pediu */
	GError *ge = NULL;
	bool ok = g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &ge);
	if (!ok) {
		g_strlcpy(erro, ge ? ge->message : "Não consegui começar a instalação.", tam);
		g_clear_error(&ge);
	} else {
		estado_gravar(id, "baixando", 0, "Preparando…");
	}
	g_free(exe);
	return ok;
}
