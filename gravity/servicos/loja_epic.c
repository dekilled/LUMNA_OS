#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "loja_epic.h"

static const char *base_promocoes(void) {
	const char *e = g_getenv("LUMNA_LOJA_EPIC_URL");
	return e && *e ? e : "https://store-site-backend-static.ak.epicgames.com";
}

char *le_config_dir(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "epic", NULL);
}

bool le_app_name_ok(const char *s) {
	size_t n = s ? strlen(s) : 0;
	for (size_t i = 0; i < n; i++) {
		if (!g_ascii_isalnum(s[i]) && s[i] != '-' && s[i] != '_') {
			return false;
		}
	}
	return n >= 2 && n < 72 && g_ascii_isalnum(s[0]); /* nunca começa com - (viraria uma opção do legendary) */
}

static int hash_id(const char *s) {
	return (int)(g_str_hash(s) & 0x7fffffff);
}

static void imagem_epic(const char *url, char *dest, size_t tam) {
	const char *img = g_getenv("LUMNA_LOJA_EPIC_IMG_URL");
	const char *p = strstr(url, "://");
	const char *fim = p ? strchr(p + 3, '/') : NULL;
	if (img && *img && fim && strstr(url, "epicgames.com")) {
		g_snprintf(dest, tam, "%s%s", img, fim);
	} else {
		g_strlcpy(dest, url, tam);
	}
}

static const char *img_do_tipo(const struct json *imgs, const char *tipo) {
	for (int i = 0; imgs && i < imgs->nfilhos; i++) {
		if (!strcmp(json_chave_texto(json_item(imgs, i), "type"), tipo)) {
			return json_chave_texto(json_item(imgs, i), "url");
		}
	}
	return "";
}

static void preco(int centavos, char *dest, size_t tam) {
	g_snprintf(dest, tam, "R$ %d,%02d", centavos / 100, centavos % 100);
}

/* ---------------- promoções ---------------- */

static char *promocoes_json(char *erro, size_t tam) {
	char *url = g_strdup_printf("%s/freeGamesPromotions?locale=pt-BR&country=BR&allowCountries=BR", base_promocoes());
	char *c = ls_http(url, 3600, erro, tam);
	g_free(url);
	return c;
}

static const char *slug_de(const struct json *e) {
	const struct json *m = json_chave(e, "offerMappings");
	if (m && m->nfilhos && *json_chave_texto(json_item(m, 0), "pageSlug")) {
		return json_chave_texto(json_item(m, 0), "pageSlug");
	}
	if (*json_chave_texto(e, "productSlug")) {
		return json_chave_texto(e, "productSlug");
	}
	return json_chave_texto(e, "urlSlug");
}

static bool gratis_agora(const struct json *e) {
	const struct json *p = json_chave(e, "promotions");
	const struct json *c = json_chave(p, "promotionalOffers");
	return c && c->nfilhos > 0;
}

static bool gratis_em_breve(const struct json *e) {
	const struct json *p = json_chave(e, "promotions");
	const struct json *c = json_chave(p, "upcomingPromotionalOffers");
	return c && c->nfilhos > 0;
}

static void item_de_promocao(const struct json *e, struct ls_item *it) {
	memset(it, 0, sizeof(*it));
	it->loja = LS_LOJA_EPIC;
	g_strlcpy(it->gid, json_chave_texto(e, "id"), sizeof(it->gid));
	it->appid = hash_id(it->gid);
	g_strlcpy(it->nome, json_chave_texto(e, "title"), sizeof(it->nome));
	g_strlcpy(it->slug, slug_de(e), sizeof(it->slug));
	const struct json *imgs = json_chave(e, "keyImages");
	const char *w = img_do_tipo(imgs, "OfferImageWide"), *t = img_do_tipo(imgs, "Thumbnail"), *tl = img_do_tipo(imgs, "OfferImageTall");
	imagem_epic(*w ? w : *t ? t : tl, it->capa, sizeof(it->capa));
	imagem_epic(*w ? w : tl, it->fundo, sizeof(it->fundo));
	const struct json *tp = json_chave(json_chave(e, "price"), "totalPrice");
	int final_ = (int)json_numero(json_chave(tp, "discountPrice"), 0), orig = (int)json_numero(json_chave(tp, "originalPrice"), 0);
	it->gratis = final_ == 0;
	if (gratis_agora(e)) {
		g_strlcpy(it->preco, "Grátis agora", sizeof(it->preco));
		it->gratis = true;
		if (orig > 0) {
			preco(orig, it->preco_antes, sizeof(it->preco_antes));
			it->desconto = 100;
		}
	} else if (gratis_em_breve(e)) {
		preco(orig, it->preco, sizeof(it->preco));
		g_strlcpy(it->generos, "Grátis em breve", sizeof(it->generos));
		it->gratis = false;
	} else if (final_ == 0) {
		g_strlcpy(it->preco, "Grátis", sizeof(it->preco));
	} else {
		preco(final_, it->preco, sizeof(it->preco));
		if (orig > final_) {
			preco(orig, it->preco_antes, sizeof(it->preco_antes));
			it->desconto = (orig - final_) * 100 / orig;
		}
	}
}

int le_promocoes(struct ls_item *l, int max, char *erro, size_t tam) {
	char *corpo = promocoes_json(erro, tam);
	struct json *j = corpo ? json_ler(corpo) : NULL;
	const struct json *els = json_chave(json_chave(json_chave(json_chave(j, "data"), "Catalog"), "searchStore"), "elements");
	int n = 0;
	/* só jogos (não complementos nem pacotes), os grátis agora primeiro */
	for (int fase = 0; fase < 3; fase++) {
		for (int i = 0; els && i < els->nfilhos && n < max; i++) {
			const struct json *e = json_item(els, i);
			const char *tipo = json_chave_texto(e, "offerType");
			bool agora = gratis_agora(e), breve = gratis_em_breve(e);
			if (strcmp(tipo, "BASE_GAME") != 0 && !(fase == 0 && agora)) {
				continue;
			}
			if ((fase == 0 && !agora) || (fase == 1 && (agora || !breve)) || (fase == 2 && (agora || breve))) {
				continue;
			}
			item_de_promocao(e, &l[n]);
			if (l[n].gid[0]) {
				n++;
			}
		}
	}
	json_liberar(j);
	g_free(corpo);
	return n;
}

bool le_detalhes_promocao(const char *gid, struct ls_detalhe *d, char *erro, size_t tam) {
	memset(d, 0, sizeof(*d));
	char *corpo = promocoes_json(erro, tam);
	struct json *j = corpo ? json_ler(corpo) : NULL;
	const struct json *els = json_chave(json_chave(json_chave(json_chave(j, "data"), "Catalog"), "searchStore"), "elements");
	bool ok = false;
	for (int i = 0; els && i < els->nfilhos && !ok; i++) {
		const struct json *e = json_item(els, i);
		if (strcmp(json_chave_texto(e, "id"), gid) != 0) {
			continue;
		}
		struct ls_item it;
		item_de_promocao(e, &it);
		d->loja = LS_LOJA_EPIC;
		d->existe = true;
		g_strlcpy(d->gid, gid, sizeof(d->gid));
		d->appid = it.appid;
		g_strlcpy(d->nome, it.nome, sizeof(d->nome));
		ls_sem_html(json_chave_texto(e, "description"), d->resumo, sizeof(d->resumo));
		g_strlcpy(d->sobre, d->resumo, sizeof(d->sobre));
		g_strlcpy(d->capa, it.capa, sizeof(d->capa));
		g_strlcpy(d->fundo, it.fundo, sizeof(d->fundo));
		g_strlcpy(d->preco, it.preco, sizeof(d->preco));
		g_strlcpy(d->preco_antes, it.preco_antes, sizeof(d->preco_antes));
		d->desconto = it.desconto;
		d->gratis = it.gratis;
		g_strlcpy(d->generos, it.generos, sizeof(d->generos));
		g_strlcpy(d->desenvolvedor, json_chave_texto(json_chave(e, "seller"), "name"), sizeof(d->desenvolvedor));
		g_strlcpy(d->lancamento, json_chave_texto(e, "effectiveDate"), sizeof(d->lancamento));
		if (strlen(d->lancamento) > 10) {
			d->lancamento[10] = '\0';
		}
		g_snprintf(d->loja_url, sizeof(d->loja_url), "https://store.epicgames.com/pt-BR/p/%s", it.slug);
		d->windows = true; /* a Epic só publica Windows (e macOS): no Linux roda por Wine/Proton */
		ok = true;
	}
	json_liberar(j);
	g_free(corpo);
	if (!ok && erro && corpo) {
		g_strlcpy(erro, "A Epic não lista mais esse jogo.", tam);
	}
	return ok;
}

/* ---------------- o legendary ---------------- */

static bool legendary(const char **args, char **saida, char *erro, size_t tam) {
	char *cfg = le_config_dir();
	g_mkdir_with_parents(cfg, 0700);
	char **env = g_get_environ();
	env = g_environ_setenv(env, "LEGENDARY_CONFIG_PATH", cfg, TRUE);
	g_free(cfg);
	GError *ge = NULL;
	GSubprocessLauncher *lc = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_set_environ(lc, env);
	GSubprocess *p = g_subprocess_launcher_spawnv(lc, args, &ge);
	g_strfreev(env);
	g_object_unref(lc);
	if (!p) {
		g_strlcpy(erro, "O legendary não está instalado (pacman -S legendary, ou AUR).", tam);
		g_clear_error(&ge);
		return false;
	}
	char *out = NULL, *err = NULL;
	bool ok = g_subprocess_communicate_utf8(p, NULL, NULL, &out, &err, &ge) && g_subprocess_get_if_exited(p) && g_subprocess_get_exit_status(p) == 0;
	if (!ok) {
		g_strlcpy(erro, err && *g_strstrip(err) ? err : "O legendary falhou.", tam);
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

bool le_logado(void) {
	char *cfg = le_config_dir(), *a = g_build_filename(cfg, "user.json", NULL);
	bool ok = g_file_test(a, G_FILE_TEST_IS_REGULAR);
	g_free(a);
	g_free(cfg);
	return ok;
}

const char *le_url_login(void) {
	return "https://legendary.gl/epiclogin";
}

static bool extrair_codigo(const char *entrada, char *codigo, size_t tam) {
	/* a Epic mostra {"authorizationCode":"..."} depois do login: vale o JSON inteiro, a URL com code= ou só o código */
	const char *c = strstr(entrada, "authorizationCode");
	char *t = NULL;
	if (c) {
		c = strchr(c, ':');
		const char *a = c ? strchr(c, '"') : NULL;
		t = a ? g_strdup(a + 1) : g_strdup("");
		char *fim = strchr(t, '"');
		if (fim) {
			*fim = '\0';
		}
	} else if ((c = strstr(entrada, "code="))) {
		t = g_strdup(c + 5);
		char *fim = strpbrk(t, "&# \n");
		if (fim) {
			*fim = '\0';
		}
	} else {
		t = g_strdup(entrada);
	}
	g_strstrip(t);
	bool ok = strlen(t) >= 20 && strlen(t) < tam;
	for (const char *p = t; ok && *p; p++) {
		ok = g_ascii_isalnum(*p);
	}
	if (ok) {
		g_strlcpy(codigo, t, tam);
	}
	g_free(t);
	return ok;
}

bool le_login(const char *entrada, char *erro, size_t tam) {
	char codigo[96];
	if (!extrair_codigo(entrada ? entrada : "", codigo, sizeof(codigo))) {
		g_strlcpy(erro, "Esse código não parece o da Epic (cole o texto inteiro que a página mostra depois do login).", tam);
		return false;
	}
	const char *args[] = { "legendary", "auth", "--code", codigo, NULL };
	bool ok = legendary(args, NULL, erro, tam);
	if (ok && !le_logado()) {
		g_strlcpy(erro, "O legendary não guardou o login.", tam);
		ok = false;
	}
	return ok;
}

void le_sair(void) {
	const char *args[] = { "legendary", "auth", "--delete", NULL };
	char e[80];
	legendary(args, NULL, e, sizeof(e));
	char *cfg = le_config_dir(), *a = g_build_filename(cfg, "user.json", NULL);
	g_unlink(a);
	g_free(a);
	g_free(cfg);
}

/* ---------------- biblioteca ---------------- */

static void item_de_jogo(const struct json *g, struct ls_item *it) {
	memset(it, 0, sizeof(*it));
	it->loja = LS_LOJA_EPIC;
	g_strlcpy(it->gid, json_chave_texto(g, "app_name"), sizeof(it->gid));
	it->appid = hash_id(it->gid);
	const struct json *md = json_chave(g, "metadata");
	g_strlcpy(it->nome, *json_chave_texto(g, "app_title") ? json_chave_texto(g, "app_title") : json_chave_texto(md, "title"), sizeof(it->nome));
	const struct json *imgs = json_chave(md, "keyImages");
	const char *w = img_do_tipo(imgs, "DieselGameBox"), *tl = img_do_tipo(imgs, "DieselGameBoxTall"), *th = img_do_tipo(imgs, "Thumbnail");
	imagem_epic(*w ? w : *th ? th : tl, it->capa, sizeof(it->capa));
	imagem_epic(*w ? w : tl, it->fundo, sizeof(it->fundo));
	g_strlcpy(it->preco, "Na biblioteca", sizeof(it->preco));
	it->possui = true;
}

int le_biblioteca(struct ls_item *l, int max, char *erro, size_t tam) {
	if (!le_logado()) {
		g_strlcpy(erro, "Entre na Epic (Configurações > Epic) para ver a sua biblioteca.", tam);
		return -1;
	}
	const char *args[] = { "legendary", "list", "--json", NULL };
	char *out = NULL;
	if (!legendary(args, &out, erro, tam)) {
		g_free(out);
		return -1;
	}
	struct json *j = out ? json_ler(out) : NULL;
	int n = 0;
	for (int i = 0; j && i < j->nfilhos && n < max; i++) {
		item_de_jogo(json_item(j, i), &l[n]);
		if (le_app_name_ok(l[n].gid)) {
			n++;
		}
	}
	if (!j) {
		g_strlcpy(erro, "O legendary não devolveu a lista de jogos.", tam);
		n = -1;
	}
	json_liberar(j);
	g_free(out);
	return n;
}

bool le_detalhes_biblioteca(const char *app_name, struct ls_detalhe *d, char *erro, size_t tam) {
	memset(d, 0, sizeof(*d));
	const char *args[] = { "legendary", "list", "--json", NULL };
	char *out = NULL;
	if (!legendary(args, &out, erro, tam)) {
		g_free(out);
		return false;
	}
	struct json *j = out ? json_ler(out) : NULL;
	bool ok = false;
	for (int i = 0; j && i < j->nfilhos && !ok; i++) {
		const struct json *g = json_item(j, i);
		if (strcmp(json_chave_texto(g, "app_name"), app_name) != 0) {
			continue;
		}
		struct ls_item it;
		item_de_jogo(g, &it);
		const struct json *md = json_chave(g, "metadata");
		d->loja = LS_LOJA_EPIC;
		d->existe = true;
		d->appid = it.appid;
		g_strlcpy(d->gid, it.gid, sizeof(d->gid));
		g_strlcpy(d->nome, it.nome, sizeof(d->nome));
		ls_sem_html(json_chave_texto(md, "description"), d->resumo, sizeof(d->resumo));
		ls_sem_html(json_chave_texto(md, "longDescription"), d->sobre, sizeof(d->sobre));
		if (!d->sobre[0]) {
			g_strlcpy(d->sobre, d->resumo, sizeof(d->sobre));
		}
		g_strlcpy(d->capa, it.capa, sizeof(d->capa));
		g_strlcpy(d->fundo, it.fundo, sizeof(d->fundo));
		g_strlcpy(d->desenvolvedor, json_chave_texto(md, "developer"), sizeof(d->desenvolvedor));
		g_strlcpy(d->preco, "Na biblioteca", sizeof(d->preco));
		d->windows = true;
		ok = true;
	}
	json_liberar(j);
	g_free(out);
	if (!ok && erro) {
		g_strlcpy(erro, "A Epic não tem esse jogo na sua conta.", tam);
	}
	return ok;
}

/* ---------------- instalar ---------------- */

static char *arq_estado(const char *app) {
	char *d = g_build_filename(g_get_user_cache_dir(), "lumna", "jogos", NULL), nome[120], *a;
	g_mkdir_with_parents(d, 0700);
	g_snprintf(nome, sizeof(nome), "epic_%s.estado", app);
	a = g_build_filename(d, nome, NULL);
	g_free(d);
	return a;
}

static void estado_gravar(const char *app, const char *estado, int pct, const char *msg) {
	char *a = arq_estado(app), *t = g_strdup_printf("%s\n%d\n%s\n", estado, pct, msg ? msg : "");
	g_file_set_contents(a, t, -1, NULL);
	g_free(t);
	g_free(a);
}

bool le_progresso(const char *app, struct le_prog *p) {
	memset(p, 0, sizeof(*p));
	if (!le_app_name_ok(app)) {
		return false;
	}
	char *a = arq_estado(app), *t = NULL;
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

int le_progresso_da_linha(const char *linha) {
	const char *p = strstr(linha, "Progress:");
	if (!p) {
		return -1;
	}
	p += 9;
	while (*p == ' ') {
		p++;
	}
	if (!g_ascii_isdigit(*p)) {
		return -1;
	}
	double v = g_ascii_strtod(p, NULL);
	return v < 0 ? 0 : v > 100 ? 100 : (int)v;
}

int le_instalar_agora(const char *app, const char *titulo, const char *pasta) {
	(void)titulo;
	if (!le_app_name_ok(app)) {
		return 2;
	}
	g_mkdir_with_parents(pasta, 0755);
	estado_gravar(app, "baixando", 0, "Preparando…");
	char *cfg = le_config_dir();
	g_mkdir_with_parents(cfg, 0700);
	char **env = g_get_environ();
	env = g_environ_setenv(env, "LEGENDARY_CONFIG_PATH", cfg, TRUE);
	g_free(cfg);
	const char *argv[] = { "legendary", "install", app, "--base-path", pasta, "-y", NULL };
	GError *ge = NULL;
	GSubprocessLauncher *lc = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
	g_subprocess_launcher_set_environ(lc, env);
	GSubprocess *p = g_subprocess_launcher_spawnv(lc, argv, &ge);
	g_strfreev(env);
	g_object_unref(lc);
	if (!p) {
		estado_gravar(app, "erro", 0, "O legendary não está instalado.");
		g_clear_error(&ge);
		return 1;
	}
	GDataInputStream *in = g_data_input_stream_new(g_subprocess_get_stdout_pipe(p));
	char *linha, msg[160] = "";
	int ultimo = 0;
	while ((linha = g_data_input_stream_read_line(in, NULL, NULL, NULL))) {
		int pct = le_progresso_da_linha(linha);
		if (pct >= 0 && pct != ultimo) {
			ultimo = pct;
			estado_gravar(app, "baixando", pct, "Baixando…");
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
	estado_gravar(app, ok ? "pronto" : "erro", ok ? 100 : ultimo, ok ? "" : msg[0] ? msg : "O legendary falhou.");
	return ok ? 0 : 1;
}

bool le_instalar(const char *app, const char *titulo, const char *pasta, char *erro, size_t tam) {
	if (!le_logado()) {
		g_strlcpy(erro, "Entre na Epic primeiro (Configurações > Epic).", tam);
		return false;
	}
	if (!le_app_name_ok(app) || !pasta || !g_path_is_absolute(pasta)) {
		g_strlcpy(erro, "Escolha uma pasta de instalação.", tam);
		return false;
	}
	char *exe = g_file_read_link("/proc/self/exe", NULL);
	const char *argv[] = { exe ? exe : "lumna-jogos", "epic-instalar", app, titulo, pasta, NULL };
	GError *ge = NULL;
	bool ok = g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &ge);
	if (!ok) {
		g_strlcpy(erro, ge ? ge->message : "Não consegui começar a instalação.", tam);
		g_clear_error(&ge);
	} else {
		estado_gravar(app, "baixando", 0, "Preparando…");
	}
	g_free(exe);
	return ok;
}
