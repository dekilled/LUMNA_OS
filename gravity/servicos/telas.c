#include "telas.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "modos.h"

bool telas_ler(struct telas_estado *e) {
	memset(e, 0, sizeof(*e));
	char *r = g_malloc(96 * 1024);
	bool ok = orbit_pedido("saidas", r, 96 * 1024);
	struct json *j = ok ? json_ler(r) : NULL;
	g_free(r);
	if (!j || !json_bool(json_chave(j, "ok"), false)) {
		json_liberar(j);
		return false;
	}
	e->orbit = true;
	const struct json *lista = json_chave(j, "saidas");
	for (int i = 0; lista && i < lista->nfilhos && e->n < TELAS_MAX; i++) {
		const struct json *o = json_item(lista, i);
		struct tela *t = &e->v[e->n];
		memset(t, 0, sizeof(*t));
		g_strlcpy(t->nome, json_chave_texto(o, "nome"), sizeof(t->nome));
		g_strlcpy(t->fabricante, json_chave_texto(o, "fabricante"), sizeof(t->fabricante));
		g_strlcpy(t->modelo, json_chave_texto(o, "modelo"), sizeof(t->modelo));
		t->largura = (int)json_numero(json_chave(o, "largura"), 0);
		t->altura = (int)json_numero(json_chave(o, "altura"), 0);
		t->hz = (int)json_numero(json_chave(o, "hz"), 0);
		t->escala = json_numero(json_chave(o, "escala"), 1);
		t->rotacao = (int)json_numero(json_chave(o, "rotacao"), 0);
		t->x = (int)json_numero(json_chave(o, "x"), 0);
		t->y = (int)json_numero(json_chave(o, "y"), 0);
		t->ligada = json_bool(json_chave(o, "ligada"), true);
		t->desligavel = json_bool(json_chave(o, "desligavel"), true);
		t->telemovel = json_bool(json_chave(o, "telemovel"), false);
		t->ponteiro = json_bool(json_chave(o, "ponteiro"), false);
		const struct json *mm = json_chave(o, "mm");
		t->mm_l = (int)json_numero(json_item(mm, 0), 0);
		t->mm_a = (int)json_numero(json_item(mm, 1), 0);
		const struct json *ms = json_chave(o, "modos");
		for (int k = 0; ms && k < ms->nfilhos && t->nmodos < TELAS_MAX_MODOS; k++) {
			const struct json *m = json_item(ms, k);
			t->modos[t->nmodos++] = (struct tela_modo){ (int)json_numero(json_chave(m, "l"), 0), (int)json_numero(json_chave(m, "a"), 0),
				(int)json_numero(json_chave(m, "hz"), 0) };
		}
		e->n++;
	}
	json_liberar(j);
	return true;
}

void tela_tamanho_logico(const struct tela *t, int *w, int *h) {
	int pw = t->largura, ph = t->altura;
	if (t->rotacao == 90 || t->rotacao == 270) {
		int tmp = pw;
		pw = ph;
		ph = tmp;
	}
	double s = t->escala > 0 ? t->escala : 1;
	*w = (int)lround(pw / s);
	*h = (int)lround(ph / s);
}

const char *tela_polegadas(const struct tela *t) {
	static char buf[8][16];
	static int i;
	char *s = buf[i++ % 8];
	if (t->mm_l <= 0 || t->mm_a <= 0) {
		s[0] = '\0';
		return s;
	}
	double d = sqrt((double)t->mm_l * t->mm_l + (double)t->mm_a * t->mm_a) / 25.4;
	snprintf(s, 16, "%.0f\"", d);
	return s;
}

/* ---- telas.ini ---- */

static char *caminho(void) {
	const char *xdg = g_getenv("XDG_CONFIG_HOME");
	return xdg && *xdg ? g_build_filename(xdg, "lumna", "telas.ini", NULL) : g_build_filename(g_get_home_dir(), ".config", "lumna", "telas.ini", NULL);
}

int telas_cfg_ler(struct tela_cfg *v, int max) {
	char *p = caminho(), *texto = NULL;
	int n = 0;
	if (g_file_get_contents(p, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		struct tela_cfg *c = NULL;
		for (int i = 0; linhas[i]; i++) {
			char *l = g_strstrip(linhas[i]);
			if (*l == '[' && n < max) {
				char *fim = strchr(l, ']');
				if (fim) {
					*fim = '\0';
					c = &v[n++];
					memset(c, 0, sizeof(*c));
					c->ligada = -1;
					g_strlcpy(c->nome, l + 1, sizeof(c->nome));
				}
				continue;
			}
			char *eq = strchr(l, '=');
			if (!c || !eq) {
				continue;
			}
			*eq = '\0';
			const char *k = g_strstrip(l), *val = g_strstrip(eq + 1);
			if (!strcmp(k, "ligada")) {
				c->ligada = atoi(val) ? 1 : 0;
			} else if (!strcmp(k, "modo")) {
				sscanf(val, "%dx%d@%d", &c->l, &c->a, &c->hz);
			} else if (!strcmp(k, "escala")) {
				c->escala = atof(val);
			} else if (!strcmp(k, "rotacao")) {
				c->rotacao = atoi(val);
			} else if (!strcmp(k, "x")) {
				c->x = atoi(val);
				c->pos = true;
			} else if (!strcmp(k, "y")) {
				c->y = atoi(val);
				c->pos = true;
			}
		}
		g_strfreev(linhas);
	}
	g_free(texto);
	g_free(p);
	return n;
}

static void escrever(GString *s, const struct tela_cfg *c) {
	g_string_append_printf(s, "[%s]\n", c->nome);
	if (c->ligada >= 0) {
		g_string_append_printf(s, "ligada=%d\n", c->ligada);
	}
	if (c->l > 0 && c->a > 0) {
		g_string_append_printf(s, "modo=%dx%d@%d\n", c->l, c->a, c->hz);
	}
	if (c->escala > 0) {
		g_string_append_printf(s, "escala=%.2f\n", c->escala);
	}
	g_string_append_printf(s, "rotacao=%d\n", c->rotacao);
	if (c->pos) {
		g_string_append_printf(s, "x=%d\ny=%d\n", c->x, c->y);
	}
	g_string_append_c(s, '\n');
}

bool telas_cfg_gravar(const struct tela_cfg *v, int n) {
	struct tela_cfg antigas[32];
	int na = telas_cfg_ler(antigas, 32);
	GString *s = g_string_new("# Gravado pelo Painel de controle (Telas e modos > Monitores)\n\n");
	for (int i = 0; i < n; i++) {
		escrever(s, &v[i]);
	}
	for (int i = 0; i < na; i++) {
		bool nova = false;
		for (int k = 0; k < n; k++) {
			nova |= !strcmp(v[k].nome, antigas[i].nome);
		}
		if (!nova) {
			escrever(s, &antigas[i]);
		}
	}
	char *p = caminho(), *dir = g_path_get_dirname(p);
	g_mkdir_with_parents(dir, 0755);
	char *tmp = g_strconcat(p, ".tmp", NULL);
	bool ok = g_file_set_contents(tmp, s->str, -1, NULL) && g_rename(tmp, p) == 0;
	g_free(tmp);
	g_free(dir);
	g_free(p);
	g_string_free(s, TRUE);
	return ok;
}

void telas_cfg_de_estado(const struct tela *t, struct tela_cfg *c) {
	memset(c, 0, sizeof(*c));
	g_strlcpy(c->nome, t->nome, sizeof(c->nome));
	c->ligada = t->ligada ? 1 : 0;
	c->l = t->largura;
	c->a = t->altura;
	c->hz = t->hz;
	c->escala = t->escala;
	c->rotacao = t->rotacao;
	c->pos = true;
	c->x = t->x;
	c->y = t->y;
}
