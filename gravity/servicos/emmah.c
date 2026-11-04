#include "emmah.h"

#include <ctype.h>
#include <string.h>
#include <time.h>
#include <glib/gstdio.h>

#include "emmaq.h"

#define LIMITE_BIBLIA (24 * 1024)
#define LIMITE_FINAL 3000

static const char *MODELO_BIBLIA =
	"# %s\n\n"
	"## Premissa\n\n%s\n\n"
	"## Personagens\n\n(nome, idade, jeito de falar, o que quer, o que esconde)\n\n"
	"## Mundo\n\n(lugar, época, regras, o que existe e o que não)\n\n"
	"## Tom e estilo\n\n(narrador, ritmo, o que evitar, referências)\n\n"
	"## Linha do tempo\n\n(o que já aconteceu, em ordem)\n\n"
	"## Onde paramos\n\n(ainda nenhum capítulo)\n";

bool emmah_e_dela(const char *n) {
	return g_str_has_prefix(n, "historia.");
}

static void falha(struct emmaa_res *r, const char *msg) {
	r->ok = false;
	g_free(r->resumo);
	r->resumo = g_strdup(msg);
}

static void sucesso(struct emmaa_res *r, char *resumo, char *conteudo) {
	r->ok = true;
	g_free(r->resumo);
	g_free(r->conteudo);
	r->resumo = resumo;
	r->conteudo = conteudo;
}

static char *pasta_historias(void) {
	char *dela = emmaq_pasta_dela();
	char *p = g_build_filename(dela, "Histórias", NULL);
	g_free(dela);
	return p;
}

/* um nome de história que cabe num nome de pasta: sem barras, sem ponto no começo, não vazio (g_free; NULL se não sobra nada) */
static char *nome_limpo(const char *nome) {
	char *n = g_strstrip(g_strdup(nome ? nome : ""));
	for (char *p = n; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t') {
			*p = '-';
		}
	}
	char *ini = n;
	while (*ini == '.' || *ini == ' ') {
		ini++;
	}
	char *r = *ini ? g_utf8_substring(ini, 0, 60) : NULL; /* no máximo 60 letras */
	g_free(n);
	return r;
}

/* a pasta de uma história que já existe (g_free), achando o nome sem se importar com maiúsculas; NULL se não existe */
static char *achar_historia(const char *nome) {
	char *limpo = nome_limpo(nome);
	if (!limpo) {
		return NULL;
	}
	char *base = pasta_historias(), *dir = g_build_filename(base, limpo, NULL), *achada = NULL;
	if (g_file_test(dir, G_FILE_TEST_IS_DIR)) {
		achada = g_strdup(dir);
	} else {
		char *alvo = g_utf8_casefold(limpo, -1);
		GDir *d = g_dir_open(base, 0, NULL);
		const char *f;
		while (d && (f = g_dir_read_name(d))) {
			char *c = g_utf8_casefold(f, -1);
			char *cand = g_build_filename(base, f, NULL);
			if (!strcmp(c, alvo) && g_file_test(cand, G_FILE_TEST_IS_DIR)) {
				achada = cand;
				g_free(c);
				break;
			}
			g_free(c);
			g_free(cand);
		}
		if (d) {
			g_dir_close(d);
		}
		g_free(alvo);
	}
	g_free(dir);
	g_free(base);
	g_free(limpo);
	return achada;
}

static char *nomes_existentes(void) {
	char *base = pasta_historias();
	GString *g = g_string_new(NULL);
	GDir *d = g_dir_open(base, 0, NULL);
	const char *f;
	while (d && (f = g_dir_read_name(d))) {
		char *c = g_build_filename(base, f, NULL);
		if (f[0] != '.' && g_file_test(c, G_FILE_TEST_IS_DIR)) {
			g_string_append_printf(g, "%s“%s”", g->len ? ", " : "", f);
		}
		g_free(c);
	}
	if (d) {
		g_dir_close(d);
	}
	g_free(base);
	return g_string_free(g, FALSE);
}

static void nao_achou(const char *nome, struct emmaa_res *r) {
	char *ex = nomes_existentes(), *m;
	m = *ex ? g_strdup_printf("Não achei a história “%s”. Existem: %s.", nome, ex) : g_strdup_printf("Não achei a história “%s” (ainda não há nenhuma: use historia_criar).", nome);
	falha(r, m);
	g_free(m);
	g_free(ex);
}

static gint ordem_texto(gconstpointer a, gconstpointer b) {
	return g_strcmp0(*(const char *const *)a, *(const char *const *)b);
}

/* os capítulos de uma história: nomes de arquivo ordenados; *maior recebe o maior número */
static GPtrArray *capitulos_de(const char *dir, int *maior) {
	GPtrArray *l = g_ptr_array_new_with_free_func(g_free);
	char *cap = g_build_filename(dir, "capitulos", NULL);
	GDir *d = g_dir_open(cap, 0, NULL);
	const char *f;
	*maior = 0;
	while (d && (f = g_dir_read_name(d))) {
		if (isdigit((unsigned char)f[0]) && g_str_has_suffix(f, ".md")) {
			g_ptr_array_add(l, g_strdup(f));
			int n = atoi(f);
			if (n > *maior) {
				*maior = n;
			}
		}
	}
	if (d) {
		g_dir_close(d);
	}
	g_free(cap);
	g_ptr_array_sort(l, ordem_texto);
	return l;
}

static char *ler_arquivo(const char *caminho) {
	char *t = NULL;
	if (!g_file_get_contents(caminho, &t, NULL, NULL)) {
		return NULL;
	}
	if (!g_utf8_validate(t, -1, NULL)) {
		char *v = g_utf8_make_valid(t, -1);
		g_free(t);
		t = v;
	}
	return t;
}

char *emmah_secao_definir(const char *biblia, const char *secao, const char *corpo, bool acrescentar) {
	char **ls = g_strsplit(biblia, "\n", -1);
	GString *g = g_string_new(NULL);
	char *alvo = g_utf8_casefold(secao, -1);
	bool achou = false;
	for (int i = 0; ls[i]; i++) {
		bool cabeca = g_str_has_prefix(ls[i], "## ");
		char *t = cabeca ? g_utf8_casefold(g_strstrip(g_strdup(ls[i] + 3)), -1) : NULL;
		if (cabeca && !achou && t && !strcmp(t, alvo)) {
			achou = true;
			/* o corpo antigo vai até a próxima seção */
			int j = i + 1;
			GString *velho = g_string_new(NULL);
			for (; ls[j] && !g_str_has_prefix(ls[j], "## "); j++) {
				g_string_append_printf(velho, "%s\n", ls[j]);
			}
			g_string_append_printf(g, "%s\n\n", ls[i]);
			if (acrescentar) {
				char *v = g_strstrip(g_strdup(velho->str));
				g_string_append_printf(g, "%s%s%s\n\n", v, *v ? "\n" : "", corpo);
				g_free(v);
			} else {
				g_string_append_printf(g, "%s\n\n", corpo);
			}
			g_string_free(velho, TRUE);
			i = j - 1;
			g_free(t);
			continue;
		}
		g_free(t);
		g_string_append_printf(g, "%s\n", ls[i]);
	}
	if (!achou) {
		while (g->len && g->str[g->len - 1] == '\n') {
			g_string_truncate(g, g->len - 1);
		}
		g_string_append_printf(g, "\n\n## %s\n\n%s\n", secao, corpo);
	}
	g_free(alvo);
	g_strfreev(ls);
	/* tira as linhas em branco a mais no fim */
	while (g->len > 1 && g->str[g->len - 1] == '\n' && g->str[g->len - 2] == '\n') {
		g_string_truncate(g, g->len - 1);
	}
	return g_string_free(g, FALSE);
}

static char *hoje(void) {
	GDateTime *t = g_date_time_new_now_local();
	char *s = g_date_time_format(t, "%Y-%m-%d");
	g_date_time_unref(t);
	return s;
}

static void listar(struct emmaa_res *r) {
	char *base = pasta_historias(), *ex = nomes_existentes();
	(void)ex;
	GString *g = g_string_new(NULL);
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	GDir *d = g_dir_open(base, 0, NULL);
	const char *f;
	while (d && (f = g_dir_read_name(d))) {
		char *c = g_build_filename(base, f, NULL);
		if (f[0] != '.' && g_file_test(c, G_FILE_TEST_IS_DIR)) {
			g_ptr_array_add(nomes, g_strdup(f));
		}
		g_free(c);
	}
	if (d) {
		g_dir_close(d);
	}
	g_ptr_array_sort(nomes, ordem_texto);
	for (guint i = 0; i < nomes->len; i++) {
		char *dir = g_build_filename(base, nomes->pdata[i], NULL);
		int maior;
		GPtrArray *caps = capitulos_de(dir, &maior);
		GStatBuf st;
		char *ult = caps->len ? g_strdup(caps->pdata[caps->len - 1]) : g_strdup("nenhum capítulo ainda");
		char quando[32] = "";
		char *ultimo = caps->len ? g_build_filename(dir, "capitulos", caps->pdata[caps->len - 1], NULL) : NULL;
		if (ultimo && g_stat(ultimo, &st) == 0) {
			struct tm tm;
			time_t t = st.st_mtime;
			localtime_r(&t, &tm);
			strftime(quando, sizeof(quando), "%Y-%m-%d", &tm);
		}
		g_string_append_printf(g, "- %s: %u capítulo(s); último: %s%s%s\n", (char *)nomes->pdata[i], caps->len, ult, quando[0] ? ", em " : "", quando);
		g_free(ult);
		g_free(ultimo);
		g_ptr_array_free(caps, TRUE);
		g_free(dir);
	}
	if (!nomes->len) {
		g_string_append(g, "Ainda não há nenhuma história em ~/Emma/Histórias. Crie uma com historia_criar.");
	}
	sucesso(r, g_strdup_printf("%u história(s).", nomes->len), g_string_free(g, FALSE));
	g_ptr_array_free(nomes, TRUE);
	g_free(base);
	g_free(ex);
}

static void criar(const struct json *args, struct emmaa_res *r) {
	char *nome = nome_limpo(json_chave_texto(args, "nome"));
	if (!nome) {
		falha(r, "Falta o nome da história.");
		return;
	}
	char *existe = achar_historia(nome);
	if (existe) {
		falha(r, "Essa história já existe: use historia_abrir.");
		g_free(existe);
		g_free(nome);
		return;
	}
	char *base = pasta_historias(), *dir = g_build_filename(base, nome, NULL), *cap = g_build_filename(dir, "capitulos", NULL);
	char *erro = NULL, *ok = emmaq_criar_pasta(cap, &erro);
	if (!ok) {
		falha(r, erro ? erro : "Não consegui criar a pasta.");
	} else {
		const char *premissa = json_chave_texto(args, "premissa");
		char *bib = g_strdup_printf(MODELO_BIBLIA, nome, *premissa ? premissa : "(a ideia central, em poucas linhas)");
		char *bpath = g_build_filename(dir, "biblia.md", NULL), *e2 = NULL, *w = emmaq_escrever(bpath, bib, false, &e2);
		if (!w) {
			falha(r, e2 ? e2 : "Não consegui criar a bíblia.");
		} else {
			sucesso(r, g_strdup_printf("Criei a história “%s” (pasta e bíblia).", nome), g_strdup_printf("Criada %s com biblia.md e capitulos/. Preencha a bíblia com o que a pessoa contar (historia_atualizar_biblia).", dir));
		}
		g_free(w);
		g_free(e2);
		g_free(bpath);
		g_free(bib);
	}
	g_free(ok);
	g_free(erro);
	g_free(cap);
	g_free(dir);
	g_free(base);
	g_free(nome);
}

static void abrir(const struct json *args, struct emmaa_res *r) {
	const char *nome = json_chave_texto(args, "nome");
	char *dir = achar_historia(nome);
	if (!dir) {
		nao_achou(nome, r);
		return;
	}
	char *bpath = g_build_filename(dir, "biblia.md", NULL), *bib = ler_arquivo(bpath);
	int maior;
	GPtrArray *caps = capitulos_de(dir, &maior);
	g_ptr_array_sort(caps, ordem_texto);
	GString *g = g_string_new(NULL);
	if (bib) {
		if (strlen(bib) > LIMITE_BIBLIA) {
			bib[LIMITE_BIBLIA] = '\0';
			char *v = g_utf8_make_valid(bib, -1);
			g_free(bib);
			bib = v;
		}
		g_string_append_printf(g, "=== BÍBLIA (biblia.md) ===\n%s\n", bib);
	} else {
		g_string_append(g, "=== BÍBLIA ===\n(esta história ainda não tem biblia.md: crie com historia_atualizar_biblia)\n");
	}
	g_string_append(g, "\n=== CAPÍTULOS ===\n");
	for (guint i = 0; i < caps->len; i++) {
		g_string_append_printf(g, "%s\n", (char *)caps->pdata[i]);
	}
	if (!caps->len) {
		g_string_append(g, "(nenhum ainda)\n");
	} else {
		char *ult = g_build_filename(dir, "capitulos", caps->pdata[caps->len - 1], NULL), *txt = ler_arquivo(ult);
		if (txt) {
			size_t n = strlen(txt);
			const char *ini = n > LIMITE_FINAL ? txt + n - LIMITE_FINAL : txt;
			while (*ini && ((unsigned char)*ini & 0xC0) == 0x80) {
				ini++; /* não corta no meio de uma letra */
			}
			g_string_append_printf(g, "\n=== FINAL DO ÚLTIMO CAPÍTULO (%s) ===\n%s%s\n", (char *)caps->pdata[caps->len - 1], ini > txt ? "…" : "", ini);
		}
		g_free(txt);
		g_free(ult);
	}
	char *base = g_path_get_basename(dir);
	sucesso(r, g_strdup_printf("Abri a história “%s”: %u capítulo(s).", base, caps->len), g_string_free(g, FALSE));
	g_free(base);
	g_ptr_array_free(caps, TRUE);
	g_free(bib);
	g_free(bpath);
	g_free(dir);
}

static void atualizar_biblia(const struct json *args, struct emmaa_res *r) {
	const char *nome = json_chave_texto(args, "nome"), *secao = json_chave_texto(args, "secao"), *texto = json_chave_texto(args, "texto");
	char *dir = achar_historia(nome);
	if (!dir) {
		nao_achou(nome, r);
		return;
	}
	if (!*secao || !*texto) {
		falha(r, "Faltou a seção ou o texto.");
		g_free(dir);
		return;
	}
	char *bpath = g_build_filename(dir, "biblia.md", NULL), *bib = ler_arquivo(bpath);
	char *novo = emmah_secao_definir(bib ? bib : "", secao, texto, json_bool(json_chave(args, "acrescentar"), false));
	char *erro = NULL, *w = emmaq_escrever(bpath, novo, false, &erro);
	if (!w) {
		falha(r, erro ? erro : "Não consegui salvar a bíblia.");
	} else {
		sucesso(r, g_strdup_printf("Atualizei a seção “%s” da bíblia.", secao), g_strdup_printf("Bíblia atualizada (seção %s).", secao));
	}
	g_free(w);
	g_free(erro);
	g_free(novo);
	g_free(bib);
	g_free(bpath);
	g_free(dir);
}

static void salvar_capitulo(const struct json *args, struct emmaa_res *r) {
	const char *nome = json_chave_texto(args, "nome"), *titulo = json_chave_texto(args, "titulo"), *texto = json_chave_texto(args, "conteudo");
	char *dir = achar_historia(nome);
	if (!dir) {
		nao_achou(nome, r);
		return;
	}
	if (!*texto) {
		falha(r, "O capítulo está vazio.");
		g_free(dir);
		return;
	}
	int maior;
	GPtrArray *caps = capitulos_de(dir, &maior);
	g_ptr_array_free(caps, TRUE);
	int numero = json_chave(args, "numero") ? (int)json_numero(json_chave(args, "numero"), 0) : 0;
	if (numero < 1) {
		numero = maior + 1;
	}
	char *norm = emmaa_normalizar(*titulo ? titulo : "sem titulo");
	for (char *p = norm; *p; p++) {
		if (*p == ' ') {
			*p = '-';
		}
	}
	if (strlen(norm) > 40) {
		norm[40] = '\0';
	}
	char *arq = g_strdup_printf("%02d-%s.md", numero, *norm ? norm : "capitulo");
	char *caminho = g_build_filename(dir, "capitulos", arq, NULL);
	GString *doc = g_string_new(NULL);
	g_string_append_printf(doc, "# %s\n\n%s%s", *titulo ? titulo : "Capítulo", texto, g_str_has_suffix(texto, "\n") ? "" : "\n");
	char *erro = NULL, *w = emmaq_escrever(caminho, doc->str, false, &erro);
	if (!w) {
		falha(r, erro ? erro : "Não consegui salvar o capítulo.");
	} else {
		/* "Onde paramos" na bíblia: o servidor o lê no bom dia para retomar; não depende de o modelo lembrar */
		char *bpath = g_build_filename(dir, "biblia.md", NULL), *bib = ler_arquivo(bpath), *data = hoje();
		char *onde = g_strdup_printf("Capítulo %d — %s (%s)", numero, *titulo ? titulo : "sem título", data);
		char *novo = emmah_secao_definir(bib ? bib : "", "Onde paramos", onde, false);
		char *e2 = NULL, *w2 = emmaq_escrever(bpath, novo, false, &e2);
		g_free(w2);
		g_free(e2);
		sucesso(r, g_strdup_printf("Salvei o capítulo %d (%s).", numero, arq),
			g_strdup_printf("Capítulo salvo em %s. A bíblia já marca onde paramos; se algo mudou na história (quem morreu, o que se revelou), chame historia_atualizar_biblia.", caminho));
		g_free(onde);
		g_free(novo);
		g_free(data);
		g_free(bib);
		g_free(bpath);
	}
	g_free(w);
	g_free(erro);
	g_string_free(doc, TRUE);
	g_free(caminho);
	g_free(arq);
	g_free(norm);
	g_free(dir);
}

void emmah_executar(const char *n, const struct json *args, struct emmaa_res *r) {
	if (!strcmp(n, "historia.listar")) {
		listar(r);
	} else if (!strcmp(n, "historia.criar")) {
		criar(args, r);
	} else if (!strcmp(n, "historia.abrir")) {
		abrir(args, r);
	} else if (!strcmp(n, "historia.salvar_capitulo")) {
		salvar_capitulo(args, r);
	} else if (!strcmp(n, "historia.atualizar_biblia")) {
		atualizar_biblia(args, r);
	} else {
		falha(r, "Essa ação de história não existe.");
	}
}
