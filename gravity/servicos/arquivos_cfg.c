#include "arquivos_cfg.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static char *arq(const char *nome) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "atlas", nome, NULL);
}

static bool gravar_kf(GKeyFile *kf, const char *nome) {
	char *a = arq(nome);
	char *pasta = g_path_get_dirname(a);
	g_mkdir_with_parents(pasta, 0700);
	bool ok = g_key_file_save_to_file(kf, a, NULL);
	g_free(pasta);
	g_free(a);
	return ok;
}

static GKeyFile *ler_kf(const char *nome) {
	GKeyFile *kf = g_key_file_new();
	char *a = arq(nome);
	g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL);
	g_free(a);
	return kf;
}

/* ---- ajustes ---- */

void ar_cfg_padrao(ar_cfg *c) {
	memset(c, 0, sizeof(*c));
	c->vista = AR_VISTA_GRADE;
	c->tam_pasta = 128;
	c->texto = 0;
	c->ocultos = false;
	c->extensoes = true;
	c->previas = true;
	c->pastas_primeiro = true;
	c->ordem = 0;
	c->decrescente = false;
	c->lembrar_por_pasta = false;
	c->abrir_em = AR_ABRIR_INICIO;
	c->inicio_pastas = true;
	c->inicio_unidades = true;
	c->conflito = 0;
	c->confirmar_apagar = true;
	c->confirmar_esvaziar = true;
	c->registrar_erros = true;
}

static int dentro(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

void ar_cfg_ler(ar_cfg *c) {
	ar_cfg_padrao(c);
	GKeyFile *kf = ler_kf("config.ini");
#define INT(chave, campo, min, max) if (g_key_file_has_key(kf, "explorador", chave, NULL)) c->campo = dentro(g_key_file_get_integer(kf, "explorador", chave, NULL), min, max)
#define BOOL(chave, campo) if (g_key_file_has_key(kf, "explorador", chave, NULL)) c->campo = g_key_file_get_boolean(kf, "explorador", chave, NULL)
	INT("vista", vista, 0, 2);
	INT("tam_pasta", tam_pasta, 64, 192);
	INT("texto", texto, 0, 2);
	BOOL("ocultos", ocultos);
	BOOL("extensoes", extensoes);
	BOOL("previas", previas);
	BOOL("pastas_primeiro", pastas_primeiro);
	INT("ordem", ordem, 0, 3);
	BOOL("decrescente", decrescente);
	BOOL("lembrar_por_pasta", lembrar_por_pasta);
	INT("abrir_em", abrir_em, 0, 2);
	BOOL("inicio_pastas", inicio_pastas);
	BOOL("inicio_unidades", inicio_unidades);
	INT("conflito", conflito, 0, 3);
	BOOL("confirmar_apagar", confirmar_apagar);
	BOOL("confirmar_esvaziar", confirmar_esvaziar);
	BOOL("registrar_erros", registrar_erros);
#undef INT
#undef BOOL
	char *p = g_key_file_get_string(kf, "explorador", "pasta_inicial", NULL);
	if (p) {
		g_strlcpy(c->pasta_inicial, p, sizeof(c->pasta_inicial));
		g_free(p);
	}
	p = g_key_file_get_string(kf, "explorador", "ultima_pasta", NULL);
	if (p) {
		g_strlcpy(c->ultima_pasta, p, sizeof(c->ultima_pasta));
		g_free(p);
	}
	g_key_file_free(kf);
}

bool ar_cfg_gravar(const ar_cfg *c) {
	GKeyFile *kf = g_key_file_new();
#define I(chave, campo) g_key_file_set_integer(kf, "explorador", chave, c->campo)
#define B(chave, campo) g_key_file_set_boolean(kf, "explorador", chave, c->campo)
	I("vista", vista);
	I("tam_pasta", tam_pasta);
	I("texto", texto);
	B("ocultos", ocultos);
	B("extensoes", extensoes);
	B("previas", previas);
	B("pastas_primeiro", pastas_primeiro);
	I("ordem", ordem);
	B("decrescente", decrescente);
	B("lembrar_por_pasta", lembrar_por_pasta);
	I("abrir_em", abrir_em);
	B("inicio_pastas", inicio_pastas);
	B("inicio_unidades", inicio_unidades);
	I("conflito", conflito);
	B("confirmar_apagar", confirmar_apagar);
	B("confirmar_esvaziar", confirmar_esvaziar);
	B("registrar_erros", registrar_erros);
#undef I
#undef B
	g_key_file_set_string(kf, "explorador", "pasta_inicial", c->pasta_inicial);
	g_key_file_set_string(kf, "explorador", "ultima_pasta", c->ultima_pasta);
	bool ok = gravar_kf(kf, "config.ini");
	g_key_file_free(kf);
	return ok;
}

/* ---- a visualização de cada pasta ---- */

int ar_visao_da_pasta(const char *caminho) {
	GKeyFile *kf = ler_kf("visoes.ini");
	int v = g_key_file_has_key(kf, "pastas", caminho, NULL) ? dentro(g_key_file_get_integer(kf, "pastas", caminho, NULL), 0, 2) : -1;
	g_key_file_free(kf);
	return v;
}

void ar_visao_da_pasta_definir(const char *caminho, int vista) {
	GKeyFile *kf = ler_kf("visoes.ini");
	if (vista < 0) {
		g_key_file_remove_key(kf, "pastas", caminho, NULL);
	} else {
		g_key_file_set_integer(kf, "pastas", caminho, vista);
	}
	gravar_kf(kf, "visoes.ini");
	g_key_file_free(kf);
}

/* ---- a barra lateral ---- */

static void item(ar_barra *b, int secao, const char *id, const char *rotulo, const char *caminho, const char *icone) {
	if (b->n >= AR_BARRA_MAX) {
		return;
	}
	ar_barra_item *i = &b->v[b->n++];
	memset(i, 0, sizeof(*i));
	i->secao = secao;
	i->visivel = true;
	i->no_inicio = true;
	g_strlcpy(i->id, id, sizeof(i->id));
	g_strlcpy(i->rotulo, rotulo, sizeof(i->rotulo));
	g_strlcpy(i->caminho, caminho, sizeof(i->caminho));
	g_strlcpy(i->icone, icone, sizeof(i->icone));
}

void ar_barra_padrao(ar_barra *b) {
	b->n = 0;
	item(b, AR_SECAO_ESPACOS, "inicio", "Início", "", "home");
	item(b, AR_SECAO_ESPACOS, "recentes", "Recentes", "", "clock");
	item(b, AR_SECAO_ESPACOS, "favoritos", "Favoritos", "", "star");
	item(b, AR_SECAO_COLECOES, "", "Home", g_get_home_dir(), "folder");
	static const struct { GUserDirectory d; const char *rotulo, *icone; } U[] = {
		{ G_USER_DIRECTORY_DOCUMENTS, "Documentos", "document" }, { G_USER_DIRECTORY_PICTURES, "Imagens", "image" }, { G_USER_DIRECTORY_MUSIC, "Músicas", "music" },
		{ G_USER_DIRECTORY_DOWNLOAD, "Downloads", "download" }, { G_USER_DIRECTORY_VIDEOS, "Vídeos", "video" }, { G_USER_DIRECTORY_TEMPLATES, "Templates", "file" },
	};
	for (size_t k = 0; k < sizeof(U) / sizeof(U[0]); k++) {
		const char *d = g_get_user_special_dir(U[k].d);
		if (d && strcmp(d, g_get_home_dir()) && g_file_test(d, G_FILE_TEST_IS_DIR)) {
			item(b, AR_SECAO_COLECOES, "", U[k].rotulo, d, U[k].icone);
		}
	}
	char *emma = g_build_filename(g_get_home_dir(), "Emma", NULL);
	if (g_file_test(emma, G_FILE_TEST_IS_DIR)) {
		item(b, AR_SECAO_COLECOES, "", "Emma", emma, "user");
	}
	g_free(emma);
}

void ar_barra_ler(ar_barra *b) {
	GKeyFile *kf = ler_kf("barra.ini");
	gsize n = 0;
	char **grupos = g_key_file_get_groups(kf, &n);
	if (!n) {
		g_strfreev(grupos);
		g_key_file_free(kf);
		ar_barra_padrao(b);
		return;
	}
	b->n = 0;
	for (gsize i = 0; i < n && b->n < AR_BARRA_MAX; i++) {
		char *id = g_key_file_get_string(kf, grupos[i], "id", NULL);
		char *rotulo = g_key_file_get_string(kf, grupos[i], "rotulo", NULL);
		char *caminho = g_key_file_get_string(kf, grupos[i], "caminho", NULL);
		char *icone = g_key_file_get_string(kf, grupos[i], "icone", NULL);
		int secao = g_key_file_get_integer(kf, grupos[i], "secao", NULL);
		bool vis = g_key_file_has_key(kf, grupos[i], "visivel", NULL) ? g_key_file_get_boolean(kf, grupos[i], "visivel", NULL) : true;
		if (rotulo && *rotulo && (secao == AR_SECAO_ESPACOS || secao == AR_SECAO_COLECOES)) {
			item(b, secao, id ? id : "", rotulo, caminho ? caminho : "", icone && *icone ? icone : "folder");
			b->v[b->n - 1].visivel = vis;
			b->v[b->n - 1].no_inicio = g_key_file_has_key(kf, grupos[i], "inicio", NULL) ? g_key_file_get_boolean(kf, grupos[i], "inicio", NULL) : true;
		}
		g_free(id);
		g_free(rotulo);
		g_free(caminho);
		g_free(icone);
	}
	g_strfreev(grupos);
	g_key_file_free(kf);
	if (!b->n) {
		ar_barra_padrao(b);
	}
}

bool ar_barra_gravar(const ar_barra *b) {
	GKeyFile *kf = g_key_file_new();
	for (int i = 0; i < b->n; i++) {
		char g[24];
		snprintf(g, sizeof(g), "item%03d", i);
		g_key_file_set_string(kf, g, "id", b->v[i].id);
		g_key_file_set_string(kf, g, "rotulo", b->v[i].rotulo);
		g_key_file_set_string(kf, g, "caminho", b->v[i].caminho);
		g_key_file_set_string(kf, g, "icone", b->v[i].icone);
		g_key_file_set_integer(kf, g, "secao", b->v[i].secao);
		g_key_file_set_boolean(kf, g, "visivel", b->v[i].visivel);
		g_key_file_set_boolean(kf, g, "inicio", b->v[i].no_inicio);
	}
	bool ok = gravar_kf(kf, "barra.ini");
	g_key_file_free(kf);
	return ok;
}

int ar_barra_da_secao(const ar_barra *b, int secao, int *indices) {
	int n = 0;
	for (int i = 0; i < b->n; i++) {
		if (b->v[i].secao == secao) {
			indices[n++] = i;
		}
	}
	return n;
}

bool ar_barra_tem(const char *caminho, int secao) {
	ar_barra b;
	ar_barra_ler(&b);
	for (int i = 0; i < b.n; i++) {
		if (b.v[i].secao == secao && b.v[i].caminho[0] && !strcmp(b.v[i].caminho, caminho)) {
			return true;
		}
	}
	return false;
}

bool ar_barra_alternar(const char *caminho, int secao) {
	ar_barra b;
	ar_barra_ler(&b);
	for (int i = 0; i < b.n; i++) {
		if (b.v[i].secao == secao && b.v[i].caminho[0] && !strcmp(b.v[i].caminho, caminho)) {
			ar_barra_remover(&b, i);
			ar_barra_gravar(&b);
			return false;
		}
	}
	char *base = g_path_get_basename(caminho);
	bool ok = ar_barra_adicionar(&b, secao, base, caminho);
	g_free(base);
	if (ok) {
		ar_barra_gravar(&b);
	}
	return ok;
}

bool ar_barra_remover(ar_barra *b, int indice) {
	if (indice < 0 || indice >= b->n) {
		return false;
	}
	memmove(&b->v[indice], &b->v[indice + 1], (size_t)(b->n - indice - 1) * sizeof(b->v[0]));
	b->n--;
	return true;
}

bool ar_barra_adicionar(ar_barra *b, int secao, const char *rotulo, const char *caminho) {
	if (b->n >= AR_BARRA_MAX || !rotulo || !*rotulo) {
		return false;
	}
	item(b, secao, "", rotulo, caminho ? caminho : "", "folder");
	return true;
}

int ar_barra_mover_para(ar_barra *b, int indice, int secao, int antes) {
	if (indice < 0 || indice >= b->n || secao < 0 || secao >= AR_SECOES_N || antes >= b->n || antes == indice) {
		return indice;
	}
	ar_barra_item t = b->v[indice];
	memmove(&b->v[indice], &b->v[indice + 1], sizeof(ar_barra_item) * (size_t)(b->n - indice - 1));
	if (antes > indice) {
		antes--;
	}
	int pos;
	if (antes >= 0) {
		pos = antes;
	} else { /* o fim da seção: logo depois do último que ela tem (sem nenhum, o fim da lista) */
		pos = -1;
		for (int i = 0; i < b->n - 1; i++) {
			if (b->v[i].secao == secao) {
				pos = i + 1;
			}
		}
		if (pos < 0) {
			pos = b->n - 1;
		}
	}
	memmove(&b->v[pos + 1], &b->v[pos], sizeof(ar_barra_item) * (size_t)(b->n - 1 - pos));
	t.secao = secao;
	b->v[pos] = t;
	return pos;
}

void ar_barra_mover(ar_barra *b, int indice, int direcao) {
	if (indice < 0 || indice >= b->n) {
		return;
	}
	int secao = b->v[indice].secao;
	for (int j = indice + direcao; j >= 0 && j < b->n; j += direcao) {
		if (b->v[j].secao == secao) {
			ar_barra_item t = b->v[indice];
			b->v[indice] = b->v[j];
			b->v[j] = t;
			return;
		}
	}
}
