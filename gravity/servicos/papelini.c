/* O papel.ini do LUMNA: ler, gravar e decidir o que vale em cada tela (papelini.h). */
#define _DEFAULT_SOURCE /* realpath */
#include "papelini.h"

#include <ctype.h>
#include <glib.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static void pasta_config(char *buf, size_t tam) {
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *casa = getenv("HOME");
	if (config && *config) {
		snprintf(buf, tam, "%s/lumna", config);
	} else {
		snprintf(buf, tam, "%s/.config/lumna", casa ? casa : "");
	}
}

void papel_caminho_ini(char *buf, size_t tam) {
	char pasta[512];
	pasta_config(pasta, sizeof(pasta));
	snprintf(buf, tam, "%s/papel.ini", pasta);
}

static char *aparar(char *s) {
	while (isspace((unsigned char)*s)) {
		s++;
	}
	char *fim = s + strlen(s);
	while (fim > s && isspace((unsigned char)fim[-1])) {
		*--fim = '\0';
	}
	return s;
}

static bool sim(const char *v) {
	return !strcasecmp(v, "sim") || !strcmp(v, "1") || !strcasecmp(v, "true");
}

static int ler_ajuste(const char *v) {
	return !strcasecmp(v, "ajustar") ? PAPEL_AJUSTAR : !strcasecmp(v, "esticar") ? PAPEL_ESTICAR : PAPEL_PREENCHER;
}

static const char *nome_ajuste(int a) {
	return a == PAPEL_AJUSTAR ? "ajustar" : a == PAPEL_ESTICAR ? "esticar" : "preencher";
}

struct papel_item *papel_editar(struct papel_cfg *c, const char *modo, const char *saida) {
	for (int i = 0; i < c->n; i++) {
		if (!strcmp(c->itens[i].modo, modo) && !strcmp(c->itens[i].saida, saida)) {
			return &c->itens[i];
		}
	}
	if (c->n == PAPEL_MAX) {
		return &c->itens[PAPEL_MAX - 1];
	}
	struct papel_item *it = &c->itens[c->n++];
	memset(it, 0, sizeof(*it));
	snprintf(it->modo, sizeof(it->modo), "%s", modo);
	snprintf(it->saida, sizeof(it->saida), "%s", saida);
	return it;
}

void papel_ler(struct papel_cfg *c) {
	memset(c, 0, sizeof(*c));
	c->intervalo = 30;
	char caminho[600];
	papel_caminho_ini(caminho, sizeof(caminho));
	FILE *f = fopen(caminho, "r");
	if (!f) {
		/* o antigo: só um caminho */
		char pasta[512], antigo[600];
		pasta_config(pasta, sizeof(pasta));
		snprintf(antigo, sizeof(antigo), "%s/papel", pasta);
		FILE *g = fopen(antigo, "r");
		char linha[512];
		if (g && fgets(linha, sizeof(linha), g)) {
			linha[strcspn(linha, "\r\n")] = '\0';
			if (linha[0]) {
				snprintf(papel_editar(c, "todos", "")->imagem, 512, "%s", linha);
			}
		}
		if (g) {
			fclose(g);
		}
		return;
	}
	char linha[700];
	struct papel_item *atual = NULL;
	bool geral = false;
	while (fgets(linha, sizeof(linha), f)) {
		char *s = aparar(linha);
		if (!*s || *s == '#' || *s == ';') {
			continue;
		}
		if (*s == '[') {
			char *fim = strchr(s, ']');
			if (fim) {
				*fim = '\0';
			}
			char modo[16] = "", saida[64] = "";
			sscanf(s + 1, "%15s %63s", modo, saida);
			geral = !strcmp(modo, "geral");
			atual = geral ? NULL : papel_editar(c, modo, saida);
			continue;
		}
		char *igual = strchr(s, '=');
		if (!igual) {
			continue;
		}
		*igual = '\0';
		char *chave = aparar(s), *valor = aparar(igual + 1);
		char *comentario = strstr(valor, " ;");
		if (comentario) {
			*comentario = '\0';
			valor = aparar(valor);
		}
		if (geral) {
			if (!strcmp(chave, "por_tela")) {
				c->por_tela = sim(valor);
			} else if (!strcmp(chave, "trocar")) {
				c->trocar = sim(valor);
			} else if (!strcmp(chave, "intervalo")) {
				c->intervalo = atoi(valor) > 0 ? atoi(valor) : 30;
			}
		} else if (atual) {
			if (!strcmp(chave, "imagem")) {
				snprintf(atual->imagem, sizeof(atual->imagem), "%s", valor);
			} else if (!strcmp(chave, "ajuste")) {
				atual->ajuste = ler_ajuste(valor);
			}
		}
	}
	fclose(f);
}

char *papel_texto(const struct papel_cfg *c) {
	GString *s = g_string_new("# O papel de parede do LUMNA (o Painel de controle grava este arquivo)\n");
	g_string_append_printf(s, "[geral]\npor_tela = %s\ntrocar = %s\nintervalo = %d\n", c->por_tela ? "sim" : "nao",
		c->trocar ? "sim" : "nao", c->intervalo);
	for (int i = 0; i < c->n; i++) {
		const struct papel_item *it = &c->itens[i];
		if (!it->imagem[0]) {
			continue;
		}
		g_string_append_printf(s, "\n[%s%s%s]\nimagem = %s\najuste = %s\n", it->modo, it->saida[0] ? " " : "", it->saida,
			it->imagem, nome_ajuste(it->ajuste));
	}
	return g_string_free(s, FALSE);
}

bool papel_gravar(const struct papel_cfg *c) {
	char *texto = papel_texto(c);
	char caminho[600], temp[620], pasta[512];
	pasta_config(pasta, sizeof(pasta));
	g_mkdir_with_parents(pasta, 0755);
	papel_caminho_ini(caminho, sizeof(caminho));
	snprintf(temp, sizeof(temp), "%s.novo", caminho);
	bool ok = g_file_set_contents(temp, texto, -1, NULL) && rename(temp, caminho) == 0;
	g_free(texto);
	return ok;
}

static const struct papel_item *achar(const struct papel_cfg *c, const char *modo, const char *saida) {
	for (int i = 0; i < c->n; i++) {
		if (!strcmp(c->itens[i].modo, modo) && !strcmp(c->itens[i].saida, saida) && c->itens[i].imagem[0]) {
			return &c->itens[i];
		}
	}
	return NULL;
}

const struct papel_item *papel_para(const struct papel_cfg *c, const char *modo, const char *saida) {
	const struct papel_item *it = NULL;
	if (c->por_tela && saida && *saida) {
		it = achar(c, modo, saida);
		if (!it) {
			it = achar(c, "todos", saida);
		}
	}
	if (!it) {
		it = achar(c, modo, "");
	}
	if (!it) {
		it = achar(c, "todos", "");
	}
	return it;
}

/* a pasta session/ do repositório, ao lado do programa (gravity/../session) */
static bool pasta_repositorio(char *buf, size_t tam) {
	char exe[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n <= 0) {
		return false;
	}
	exe[n] = '\0';
	char junto[PATH_MAX + 16];
	snprintf(junto, sizeof(junto), "%s/../session", dirname(exe));
	char limpo[PATH_MAX];
	if (!realpath(junto, limpo)) {
		return false;
	}
	snprintf(buf, tam, "%s", limpo);
	return access(buf, R_OK) == 0;
}

const char *papel_padrao(void) {
	static char caminho[PATH_MAX + 32];
	if (access("/usr/share/lumna/wallpaper.png", R_OK) == 0) {
		return "/usr/share/lumna/wallpaper.png";
	}
	char repo[PATH_MAX];
	if (pasta_repositorio(repo, sizeof(repo))) {
		snprintf(caminho, sizeof(caminho), "%s/wallpaper.png", repo);
		return caminho;
	}
	return "";
}

static int comparar(const void *a, const void *b) {
	return strcmp((const char *)a, (const char *)b);
}

static int juntar(const char *pasta, char caminhos[][512], int n, int max) {
	GDir *d = g_dir_open(pasta, 0, NULL);
	if (!d) {
		return n;
	}
	const char *nome;
	while (n < max && (nome = g_dir_read_name(d))) {
		const char *ponto = strrchr(nome, '.');
		if (ponto && (!strcasecmp(ponto, ".jpg") || !strcasecmp(ponto, ".jpeg") || !strcasecmp(ponto, ".png") ||
			!strcasecmp(ponto, ".webp"))) {
			snprintf(caminhos[n++], 512, "%.255s/%.255s", pasta, nome);
		}
	}
	g_dir_close(d);
	return n;
}

int papel_colecao(char caminhos[][512], int max) {
	int n = 0;
	const char *padrao = papel_padrao();
	if (*padrao && n < max) {
		snprintf(caminhos[n++], 512, "%.511s", padrao);
	}
	int antes = n;
	n = juntar("/usr/share/lumna/wallpapers", caminhos, n, max);
	if (n == antes) {
		char repo[PATH_MAX], pasta[PATH_MAX + 16];
		if (pasta_repositorio(repo, sizeof(repo))) {
			snprintf(pasta, sizeof(pasta), "%s/wallpapers", repo);
			n = juntar(pasta, caminhos, n, max);
		}
	}
	qsort(caminhos[antes], (size_t)(n - antes), 512, comparar);
	return n;
}

void papel_nome(const char *caminho, char *nome, size_t tam) {
	const char *base = strrchr(caminho, '/');
	base = base ? base + 1 : caminho;
	if (!strcmp(base, "wallpaper.png")) {
		snprintf(nome, tam, "LUMNA");
		return;
	}
	snprintf(nome, tam, "%s", base);
	char *ponto = strrchr(nome, '.');
	if (ponto) {
		*ponto = '\0';
	}
	for (char *p = nome; *p; p++) {
		if (*p == '-' || *p == '_') {
			*p = ' ';
		}
	}
	nome[0] = (char)toupper((unsigned char)nome[0]);
}

const char *papel_modo_atual(void) {
	static char modo[16];
	char pasta[512], caminho[600];
	pasta_config(pasta, sizeof(pasta));
	snprintf(caminho, sizeof(caminho), "%s/modo", pasta);
	FILE *f = fopen(caminho, "r");
	modo[0] = '\0';
	if (f) {
		if (fscanf(f, "%15s", modo) != 1) {
			modo[0] = '\0';
		}
		fclose(f);
	}
	if (!modo[0]) {
		snprintf(modo, sizeof(modo), "desktop");
	}
	return modo;
}
