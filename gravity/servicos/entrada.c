#include "entrada.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "modos.h"

void entrada_cfg_padrao(struct entrada_cfg *c) {
	memset(c, 0, sizeof(*c));
	g_strlcpy(c->layout, "br", sizeof(c->layout));
	g_strlcpy(c->variante, "abnt2", sizeof(c->variante));
	c->repeticao_taxa = 25;
	c->repeticao_atraso = 600;
	c->touchpad.tocar = true;
}

static char *caminho(void) {
	const char *xdg = g_getenv("XDG_CONFIG_HOME");
	return xdg && *xdg ? g_build_filename(xdg, "lumna", "entrada.ini", NULL) : g_build_filename(g_get_home_dir(), ".config", "lumna", "entrada.ini", NULL);
}

static double limitar(double v) {
	return v < -1 ? -1 : v > 1 ? 1 : v;
}

bool entrada_cfg_ler(struct entrada_cfg *c) {
	entrada_cfg_padrao(c);
	/* o ambiente vale quando não há arquivo (como o Orbit) */
	const char *e = g_getenv("XKB_DEFAULT_LAYOUT");
	if (e && *e) {
		g_strlcpy(c->layout, e, sizeof(c->layout));
	}
	char *p = caminho(), *texto = NULL;
	bool ok = g_file_get_contents(p, &texto, NULL, NULL);
	if (ok) {
		char **linhas = g_strsplit(texto, "\n", -1);
		int sec = 0; /* 1 teclado, 2 mouse, 3 touchpad */
		for (int i = 0; linhas[i]; i++) {
			char *l = g_strstrip(linhas[i]);
			if (*l == '[') {
				sec = g_str_has_prefix(l, "[teclado]") ? 1 : g_str_has_prefix(l, "[mouse]") ? 2 : g_str_has_prefix(l, "[touchpad]") ? 3 : 0;
				continue;
			}
			char *eq = strchr(l, '=');
			if (!eq || *l == '#') {
				continue;
			}
			*eq = '\0';
			const char *k = g_strstrip(l), *v = g_strstrip(eq + 1);
			bool b = !strcmp(v, "1") || !g_ascii_strcasecmp(v, "sim") || !g_ascii_strcasecmp(v, "true");
			if (sec == 1) {
				if (!strcmp(k, "layout")) {
					g_strlcpy(c->layout, v, sizeof(c->layout));
				} else if (!strcmp(k, "variant")) {
					g_strlcpy(c->variante, v, sizeof(c->variante));
				} else if (!strcmp(k, "options")) {
					g_strlcpy(c->opcoes, v, sizeof(c->opcoes));
				} else if (!strcmp(k, "repeticao_taxa")) {
					c->repeticao_taxa = CLAMP(atoi(v), 1, 100);
				} else if (!strcmp(k, "repeticao_atraso")) {
					c->repeticao_atraso = CLAMP(atoi(v), 100, 2000);
				}
			} else if (sec == 2) {
				if (!strcmp(k, "botao_primario")) {
					c->mouse.primario_direito = !g_ascii_strcasecmp(v, "direito");
				} else if (!strcmp(k, "velocidade")) {
					c->mouse.velocidade = limitar(atof(v));
				} else if (!strcmp(k, "perfil")) {
					c->mouse.plano = !g_ascii_strcasecmp(v, "plano");
				} else if (!strcmp(k, "rolagem_natural")) {
					c->mouse.natural = b;
				}
			} else if (sec == 3) {
				if (!strcmp(k, "tocar_para_clicar")) {
					c->touchpad.tocar = b;
				} else if (!strcmp(k, "rolagem_natural")) {
					c->touchpad.natural = b;
				} else if (!strcmp(k, "botao_primario")) {
					c->touchpad.primario_direito = !g_ascii_strcasecmp(v, "direito");
				} else if (!strcmp(k, "velocidade")) {
					c->touchpad.velocidade = limitar(atof(v));
				} else if (!strcmp(k, "perfil")) {
					c->touchpad.plano = !g_ascii_strcasecmp(v, "plano");
				} else if (!strcmp(k, "digitar_desativa")) {
					c->touchpad.digitar_desativa = b;
				}
			}
		}
		g_strfreev(linhas);
	}
	g_free(texto);
	g_free(p);
	return ok;
}

bool entrada_cfg_gravar(const struct entrada_cfg *c) {
	GString *s = g_string_new("# Gravado pelo Painel de controle (Teclado e mouse)\n\n");
	g_string_append_printf(s, "[teclado]\nlayout=%s\nvariant=%s\noptions=%s\nrepeticao_taxa=%d\nrepeticao_atraso=%d\n\n", c->layout, c->variante, c->opcoes,
		c->repeticao_taxa, c->repeticao_atraso);
	g_string_append_printf(s, "[mouse]\nbotao_primario=%s\nvelocidade=%.2f\nperfil=%s\nrolagem_natural=%d\n\n", c->mouse.primario_direito ? "direito" : "esquerdo",
		c->mouse.velocidade, c->mouse.plano ? "plano" : "adaptativo", c->mouse.natural);
	g_string_append_printf(s, "[touchpad]\ntocar_para_clicar=%d\nrolagem_natural=%d\nbotao_primario=%s\nvelocidade=%.2f\nperfil=%s\ndigitar_desativa=%d\n",
		c->touchpad.tocar, c->touchpad.natural, c->touchpad.primario_direito ? "direito" : "esquerdo", c->touchpad.velocidade,
		c->touchpad.plano ? "plano" : "adaptativo", c->touchpad.digitar_desativa);
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

/* ---- o que o XKB oferece ---- */

static char *ler_lst(void) {
	const char *env = g_getenv("LUMNA_XKB_LST");
	char *texto = NULL;
	if (g_file_get_contents(env && *env ? env : "/usr/share/X11/xkb/rules/evdev.lst", &texto, NULL, NULL)) {
		return texto;
	}
	return NULL;
}

static int comparar(const void *a, const void *b) {
	return g_utf8_collate(((const struct entrada_item *)a)->nome, ((const struct entrada_item *)b)->nome);
}

int entrada_layouts(struct entrada_item *v, int max) {
	char *texto = ler_lst();
	int n = 0;
	if (!texto) {
		return 0;
	}
	char **linhas = g_strsplit(texto, "\n", -1);
	bool dentro = false;
	for (int i = 0; linhas[i] && n < max; i++) {
		if (linhas[i][0] == '!') {
			dentro = g_str_has_prefix(linhas[i], "! layout");
			continue;
		}
		if (!dentro || linhas[i][0] != ' ') {
			continue;
		}
		char *l = g_strstrip(linhas[i]);
		char *sp = strchr(l, ' ');
		if (!sp) {
			continue;
		}
		*sp = '\0';
		g_strlcpy(v[n].id, l, sizeof(v[n].id));
		g_strlcpy(v[n].nome, g_strstrip(sp + 1), sizeof(v[n].nome));
		n++;
	}
	g_strfreev(linhas);
	g_free(texto);
	qsort(v, (size_t)n, sizeof(v[0]), comparar);
	return n;
}

int entrada_variantes(const char *layout, struct entrada_item *v, int max) {
	char *texto = ler_lst();
	int n = 0;
	if (!texto) {
		return 0;
	}
	char **linhas = g_strsplit(texto, "\n", -1);
	bool dentro = false;
	char *prefixo = g_strdup_printf("%s:", layout);
	for (int i = 0; linhas[i] && n < max; i++) {
		if (linhas[i][0] == '!') {
			dentro = g_str_has_prefix(linhas[i], "! variant");
			continue;
		}
		if (!dentro || linhas[i][0] != ' ') {
			continue;
		}
		char *l = g_strstrip(linhas[i]);
		char *sp = strchr(l, ' ');
		if (!sp) {
			continue;
		}
		*sp = '\0';
		char *resto = g_strstrip(sp + 1); /* "br: Portuguese (Brazil, no dead keys)" */
		if (!g_str_has_prefix(resto, prefixo)) {
			continue;
		}
		g_strlcpy(v[n].id, l, sizeof(v[n].id));
		g_strlcpy(v[n].nome, g_strstrip(resto + strlen(prefixo)), sizeof(v[n].nome));
		n++;
	}
	g_free(prefixo);
	g_strfreev(linhas);
	g_free(texto);
	qsort(v, (size_t)n, sizeof(v[0]), comparar);
	return n;
}

bool entrada_dispositivos(struct entrada_dispositivos *d) {
	memset(d, 0, sizeof(*d));
	char *r = g_malloc(16 * 1024);
	bool ok = orbit_pedido("entrada", r, 16 * 1024);
	struct json *j = ok ? json_ler(r) : NULL;
	g_free(r);
	if (!j || !json_bool(json_chave(j, "ok"), false)) {
		json_liberar(j);
		return false;
	}
	d->orbit = true;
	g_strlcpy(d->mapa, json_chave_texto(j, "nome_do_mapa"), sizeof(d->mapa));
	const struct json *lista = json_chave(j, "dispositivos");
	for (int i = 0; lista && i < lista->nfilhos; i++) {
		const struct json *o = json_item(lista, i);
		const char *tipo = json_chave_texto(o, "tipo");
		bool cfg = json_bool(json_chave(o, "configuravel"), false);
		if (!strcmp(tipo, "teclado")) {
			d->teclado = true;
		} else if (!strcmp(tipo, "mouse")) {
			d->mouse = true;
			d->ponteiro_ajustavel |= cfg;
		} else if (!strcmp(tipo, "touchpad")) {
			d->touchpad = true;
			d->ponteiro_ajustavel |= cfg;
		}
	}
	json_liberar(j);
	return true;
}
