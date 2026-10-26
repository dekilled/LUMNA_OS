#include "midia.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "json.h"

static const struct {
	const char *id, *nome, *url, *cor, *login;
} padroes[] = {
	{ "youtube", "YouTube", "https://www.youtube.com/tv", "#FF0033",
		"https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fwww.youtube.com%2Ftv" },
	{ "netflix", "Netflix", "https://www.netflix.com/browse", "#E50914", NULL },
	{ "prime", "Prime Video", "https://www.primevideo.com/", "#1A98FF", NULL },
	{ "disney", "Disney+", "https://www.disneyplus.com/", "#113CCF", NULL },
	{ "max", "HBO Max", "https://play.max.com/", "#7B2FF7", NULL },
	{ "paramount", "Paramount+", "https://www.paramountplus.com/", "#0064FF", NULL },
};
#define N_PADROES ((int)(sizeof(padroes) / sizeof(padroes[0])))

static char *caminho_ini(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "midia.ini", NULL);
}

char *midia_pasta_dados(void) {
	char *p = g_build_filename(g_get_user_data_dir(), "lumna", "midia", NULL);
	g_mkdir_with_parents(p, 0700);
	return p;
}

char *midia_icone(const char *id) {
	char *dados = midia_pasta_dados();
	const char *ext[] = { "svg", "png" };
	for (int i = 0; i < 2; i++) {
		char *f = g_strdup_printf("%s/icones/%s.%s", dados, id, ext[i]);
		if (g_file_test(f, G_FILE_TEST_IS_REGULAR)) {
			g_free(dados);
			return f;
		}
		g_free(f);
	}
	g_free(dados);
	return NULL;
}

/* ---- catálogo ---- */

struct entrada {
	char id[32];
	bool tem_nome, tem_url, ativo;
	char nome[48], url[512];
};

static bool endereco_serve(const char *url) {
	return (g_str_has_prefix(url, "https://") || g_str_has_prefix(url, "http://")) && strlen(url) < 500 && !strpbrk(url, " \t\r\n\"'\\<>");
}

static void slug(const char *nome, char *saida, size_t tam) {
	size_t n = 0;
	char *baixo = g_ascii_strdown(nome, -1);
	for (const char *c = baixo; *c && n + 1 < tam; c++) {
		if (isalnum((unsigned char)*c)) {
			saida[n++] = *c;
		} else if (n && saida[n - 1] != '-') {
			saida[n++] = '-';
		}
	}
	while (n && saida[n - 1] == '-') {
		n--;
	}
	saida[n] = '\0';
	g_free(baixo);
	if (!n) {
		snprintf(saida, tam, "servico");
	}
}

/* o ini inteiro para uma lista de entradas (padrões alterados e adicionados, na ordem do arquivo) */
static int ini_ler(struct entrada *e, int max) {
	char *caminho = caminho_ini(), *texto = NULL;
	int n = 0;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		struct entrada *atual = NULL;
		for (char **l = linhas; *l; l++) {
			char *s = g_strstrip(*l);
			if (*s == '[') {
				char *f = strchr(s, ']');
				atual = NULL;
				if (f && n < max) {
					*f = '\0';
					const char *sec = s + 1;
					atual = &e[n++];
					memset(atual, 0, sizeof(*atual));
					atual->ativo = true;
					g_strlcpy(atual->id, g_str_has_prefix(sec, "servico ") ? sec + 8 : sec, sizeof(atual->id));
				}
				continue;
			}
			char *igual = strchr(s, '=');
			if (!atual || !igual || *s == '#') {
				continue;
			}
			*igual = '\0';
			char *k = g_strstrip(s), *v = g_strstrip(igual + 1);
			if (!strcmp(k, "nome")) {
				atual->tem_nome = true;
				g_strlcpy(atual->nome, v, sizeof(atual->nome));
			} else if (!strcmp(k, "url") && endereco_serve(v)) {
				atual->tem_url = true;
				g_strlcpy(atual->url, v, sizeof(atual->url));
			} else if (!strcmp(k, "ativo")) {
				atual->ativo = strcmp(v, "nao") != 0;
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(caminho);
	return n;
}

static bool ini_gravar(const struct entrada *e, int n) {
	GString *s = g_string_new("# Os serviços da Mídia (a tela de Mídia grava este arquivo)\n");
	for (int i = 0; i < n; i++) {
		bool padrao = false;
		for (int p = 0; p < N_PADROES; p++) {
			padrao = padrao || !strcmp(padroes[p].id, e[i].id);
		}
		if (padrao) {
			g_string_append_printf(s, "\n[%s]\nativo = %s\n", e[i].id, e[i].ativo ? "sim" : "nao");
		} else {
			g_string_append_printf(s, "\n[servico %s]\nnome = %s\nurl = %s\nativo = %s\n", e[i].id, e[i].nome, e[i].url, e[i].ativo ? "sim" : "nao");
		}
	}
	char *caminho = caminho_ini(), *dir = g_path_get_dirname(caminho);
	g_mkdir_with_parents(dir, 0700);
	GError *erro = NULL;
	bool ok = g_file_set_contents(caminho, s->str, (gssize)s->len, &erro);
	g_clear_error(&erro);
	g_free(dir);
	g_free(caminho);
	g_string_free(s, TRUE);
	return ok;
}

int midia_servicos_ler(struct midia_servico *saida, int max) {
	struct entrada e[MIDIA_MAX_SERVICOS * 2];
	int ne = ini_ler(e, MIDIA_MAX_SERVICOS * 2), n = 0;
	for (int p = 0; p < N_PADROES && n < max; p++) {
		struct midia_servico *s = &saida[n++];
		memset(s, 0, sizeof(*s));
		g_strlcpy(s->id, padroes[p].id, sizeof(s->id));
		g_strlcpy(s->nome, padroes[p].nome, sizeof(s->nome));
		g_strlcpy(s->url, padroes[p].url, sizeof(s->url));
		g_strlcpy(s->cor, padroes[p].cor, sizeof(s->cor));
		g_strlcpy(s->login, padroes[p].login ? padroes[p].login : padroes[p].url, sizeof(s->login));
		s->padrao = true;
		s->ativo = true;
		for (int i = 0; i < ne; i++) {
			if (!strcmp(e[i].id, s->id)) {
				s->ativo = e[i].ativo;
			}
		}
	}
	for (int i = 0; i < ne && n < max; i++) {
		if (!e[i].tem_nome || !e[i].tem_url) {
			continue; /* uma seção de padrão (já contada) ou incompleta */
		}
		struct midia_servico *s = &saida[n++];
		memset(s, 0, sizeof(*s));
		g_strlcpy(s->id, e[i].id, sizeof(s->id));
		g_strlcpy(s->nome, e[i].nome, sizeof(s->nome));
		g_strlcpy(s->url, e[i].url, sizeof(s->url));
		g_strlcpy(s->cor, "#8B7CF6", sizeof(s->cor));
		g_strlcpy(s->login, e[i].url, sizeof(s->login));
		s->ativo = e[i].ativo;
	}
	return n;
}

const struct midia_servico *midia_servico_achar(const struct midia_servico *lista, int n, const char *id) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(lista[i].id, id)) {
			return &lista[i];
		}
	}
	return NULL;
}

bool midia_servico_adicionar(const char *nome, const char *url, char *id_saida, size_t tam) {
	if (!nome || !*nome || !endereco_serve(url ? url : "")) {
		return false;
	}
	struct midia_servico todos[MIDIA_MAX_SERVICOS];
	int nt = midia_servicos_ler(todos, MIDIA_MAX_SERVICOS);
	if (nt >= MIDIA_MAX_SERVICOS) {
		return false;
	}
	char base[24], id[32];
	slug(nome, base, sizeof(base));
	snprintf(id, sizeof(id), "%s", base);
	for (int k = 2; midia_servico_achar(todos, nt, id) && k < 100; k++) {
		snprintf(id, sizeof(id), "%s-%d", base, k);
	}
	struct entrada e[MIDIA_MAX_SERVICOS * 2];
	int ne = ini_ler(e, MIDIA_MAX_SERVICOS * 2 - 1);
	memset(&e[ne], 0, sizeof(e[ne]));
	g_strlcpy(e[ne].id, id, sizeof(e[ne].id));
	g_strlcpy(e[ne].nome, nome, sizeof(e[ne].nome));
	g_strlcpy(e[ne].url, url, sizeof(e[ne].url));
	e[ne].ativo = e[ne].tem_nome = e[ne].tem_url = true;
	if (!ini_gravar(e, ne + 1)) {
		return false;
	}
	if (id_saida) {
		g_strlcpy(id_saida, id, tam);
	}
	return true;
}

bool midia_servico_remover(const char *id) {
	for (int p = 0; p < N_PADROES; p++) {
		if (!strcmp(padroes[p].id, id)) {
			return false;
		}
	}
	struct entrada e[MIDIA_MAX_SERVICOS * 2];
	int ne = ini_ler(e, MIDIA_MAX_SERVICOS * 2), k = 0;
	bool achou = false;
	for (int i = 0; i < ne; i++) {
		if (!strcmp(e[i].id, id)) {
			achou = true;
		} else {
			e[k++] = e[i];
		}
	}
	if (achou) {
		midia_recente_apagar(id);
	}
	return achou && ini_gravar(e, k);
}

bool midia_servico_ativar(const char *id, bool ativo) {
	struct midia_servico todos[MIDIA_MAX_SERVICOS];
	int nt = midia_servicos_ler(todos, MIDIA_MAX_SERVICOS);
	if (!midia_servico_achar(todos, nt, id)) {
		return false;
	}
	struct entrada e[MIDIA_MAX_SERVICOS * 2];
	int ne = ini_ler(e, MIDIA_MAX_SERVICOS * 2 - 1);
	for (int i = 0; i < ne; i++) {
		if (!strcmp(e[i].id, id)) {
			e[i].ativo = ativo;
			return ini_gravar(e, ne);
		}
	}
	memset(&e[ne], 0, sizeof(e[ne])); /* um padrão que ainda não tinha seção */
	g_strlcpy(e[ne].id, id, sizeof(e[ne].id));
	e[ne].ativo = ativo;
	return ini_gravar(e, ne + 1);
}

/* ---- recentes ---- */

static char *caminho_recentes(void) {
	char *dir = midia_pasta_dados();
	char *c = g_build_filename(dir, "recentes.json", NULL);
	g_free(dir);
	return c;
}

int midia_recentes_ler(struct midia_recente *saida, int max) {
	char *c = caminho_recentes(), *texto = NULL;
	int n = 0;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		struct json *lista = json_ler(texto);
		for (int i = 0; lista && i < lista->nfilhos && n < max; i++) {
			const struct json *j = lista->filhos[i];
			struct midia_recente *r = &saida[n];
			memset(r, 0, sizeof(*r));
			g_strlcpy(r->servico, json_chave_texto(j, "servico"), sizeof(r->servico));
			g_strlcpy(r->url, json_chave_texto(j, "url"), sizeof(r->url));
			g_strlcpy(r->titulo, json_chave_texto(j, "titulo"), sizeof(r->titulo));
			g_strlcpy(r->miniatura, json_chave_texto(j, "miniatura"), sizeof(r->miniatura));
			r->quando = (gint64)json_numero(json_chave(j, "quando"), 0);
			if (r->servico[0] && endereco_serve(r->url)) {
				n++;
			}
		}
		json_liberar(lista);
		g_free(texto);
	}
	g_free(c);
	return n;
}

static void recentes_gravar(const struct midia_recente *r, int n) {
	GString *s = g_string_new("[");
	for (int i = 0; i < n; i++) {
		g_string_append(s, i ? ",\n " : "\n ");
		g_string_append(s, "{\"servico\":");
		json_escrever_texto(s, r[i].servico);
		g_string_append(s, ",\"url\":");
		json_escrever_texto(s, r[i].url);
		g_string_append(s, ",\"titulo\":");
		json_escrever_texto(s, r[i].titulo);
		g_string_append(s, ",\"miniatura\":");
		json_escrever_texto(s, r[i].miniatura);
		g_string_append_printf(s, ",\"quando\":%" G_GINT64_FORMAT "}", r[i].quando);
	}
	g_string_append(s, n ? "\n]\n" : "]\n");
	char *c = caminho_recentes();
	GError *erro = NULL;
	g_file_set_contents(c, s->str, (gssize)s->len, &erro);
	g_clear_error(&erro);
	g_free(c);
	g_string_free(s, TRUE);
}

void midia_recente_gravar(const struct midia_recente *novo) {
	struct midia_recente lista[MIDIA_MAX_RECENTES + 1];
	int n = midia_recentes_ler(lista + 1, MIDIA_MAX_RECENTES), k = 1;
	lista[0] = *novo;
	if (!lista[0].quando) {
		lista[0].quando = g_get_real_time() / G_USEC_PER_SEC;
	}
	for (int i = 1; i <= n; i++) {
		if (strcmp(lista[i].servico, novo->servico)) {
			lista[k++] = lista[i];
		}
	}
	recentes_gravar(lista, k > MIDIA_MAX_RECENTES ? MIDIA_MAX_RECENTES : k);
}

void midia_recente_apagar(const char *servico) {
	struct midia_recente lista[MIDIA_MAX_RECENTES];
	int n = midia_recentes_ler(lista, MIDIA_MAX_RECENTES), k = 0;
	for (int i = 0; i < n; i++) {
		if (strcmp(lista[i].servico, servico)) {
			lista[k++] = lista[i];
		}
	}
	recentes_gravar(lista, k);
}
