/* O destaque de sintaxe (servicos/destaque.c): não troca nenhuma letra do código e marca o que deve marcar. */
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <pango/pango.h>

#include "destaque.h"

static int falhas;
static void confere(gboolean c, const char *m) {
	if (!c) {
		falhas++;
		printf("FALHOU: %s\n", m);
	}
}

static const struct destaque_cores cores = { "#a00001", "#00a002", "#888883", "#0000a4", "#a0a005" };

/* o texto de um markup (sem as tags e com as entidades desfeitas) */
static char *so_texto(const char *markup) {
	GString *g = g_string_new(NULL);
	for (const char *p = markup; *p;) {
		if (*p == '<') {
			p = strchr(p, '>') + 1;
		} else if (!strncmp(p, "&lt;", 4)) {
			g_string_append_c(g, '<');
			p += 4;
		} else if (!strncmp(p, "&gt;", 4)) {
			g_string_append_c(g, '>');
			p += 4;
		} else if (!strncmp(p, "&amp;", 5)) {
			g_string_append_c(g, '&');
			p += 5;
		} else if (!strncmp(p, "&quot;", 6)) {
			g_string_append_c(g, '"');
			p += 6;
		} else if (!strncmp(p, "&apos;", 6)) {
			g_string_append_c(g, '\'');
			p += 6;
		} else {
			g_string_append_c(g, *p++);
		}
	}
	return g_string_free(g, FALSE);
}

static void caso(const char *lingua, const char *codigo, const char *deve[]) {
	char *m = destaque_markup(lingua, codigo, &cores);
	char *t = so_texto(m);
	if (strcmp(t, codigo)) {
		falhas++;
		printf("FALHOU: o destaque de %s mudou o texto:\n--- era:\n%s\n--- ficou:\n%s\n", lingua, codigo, t);
	}
	for (int i = 0; deve[i]; i++) {
		if (!strstr(m, deve[i])) {
			falhas++;
			printf("FALHOU: %s: faltou %s em %s\n", lingua, deve[i], m);
		}
	}
	GError *e = NULL;
	if (!pango_parse_markup(m, -1, 0, NULL, NULL, NULL, &e)) {
		falhas++;
		printf("FALHOU: %s: marcação inválida do pango: %s\n", lingua, e ? e->message : "");
		g_clear_error(&e);
	}
	g_free(m);
	g_free(t);
}

int main(void) {
	const char *c1[] = { "<span foreground=\"#a00001\">if</span>", "<span foreground=\"#a0a005\">int</span>", "<span foreground=\"#00a002\">&quot;oi &lt;3&quot;</span>", "<span foreground=\"#0000a4\">42</span>", "foreground=\"#888883\" style=\"italic\">// nota", "<span foreground=\"#a00001\">#include</span>", NULL };
	caso("c", "#include <stdio.h>\nint main(void) {\n\tif (x < 42 && y) { printf(\"oi <3\"); } // nota\n\treturn 0;\n}\n", c1);
	const char *c2[] = { "<span foreground=\"#a00001\">def</span>", "<span foreground=\"#888883\" style=\"italic\"># comentário", "<span foreground=\"#00a002\">&apos;&apos;&apos;doc\nmultilinha&apos;&apos;&apos;</span>", "<span foreground=\"#a0a005\">None</span>", NULL };
	caso("python", "def f(x):\n    '''doc\nmultilinha'''\n    # comentário\n    return None\n", c2);
	const char *c3[] = { "<span foreground=\"#a00001\">if</span>", "<span foreground=\"#a00001\">fi</span>", "<span foreground=\"#00a002\">&quot;$HOME&quot;</span>", NULL };
	caso("bash", "if [ -d \"$HOME\" ]; then echo ${#a}; fi # fim\n", c3);
	const char *c4[] = { "<span foreground=\"#00a002\">&quot;a&quot;</span>", "<span foreground=\"#0000a4\">3.14</span>", "<span foreground=\"#a0a005\">true</span>", NULL };
	caso("json", "{\"a\": 3.14, \"b\": true}", c4);
	const char *c5[] = { "<span foreground=\"#a00001\">fn</span>", "<span foreground=\"#a00001\">let</span>", NULL };
	caso("rust", "fn f<'a>(x: &'a str) -> i32 { let y = 1; y }", c5);
	const char *c6[] = { "foreground=\"#888883\" style=\"italic\">&lt;!-- x --&gt;", NULL };
	caso("html", "<!-- x --><p class=\"a\">&amp;</p>", c6);
	const char *c7[] = { "&lt;", "&amp;", NULL }, *c8[] = { "&lt;", NULL };
	caso("", "a < b && c", c7);                 /* sem linguagem: só escapa */
	caso("linguagem-que-nao-existe", "x = 1 < 2", c8);
	caso("c", "", (const char *[]){ NULL });    /* vazio */
	caso("python", "s = 'não fechada\nx = 1", (const char *[]){ NULL });  /* texto sem fechar não derruba */
	caso("c", "/* bloco sem fechar", (const char *[]){ "style=\"italic\"", NULL });

	confere(!strcmp(destaque_extensao("Python"), "py") && !strcmp(destaque_extensao("bash"), "sh") && !strcmp(destaque_extensao("c++"), "cpp") && !strcmp(destaque_extensao("xyz"), "txt"), "extensões");
	confere(destaque_parece_codigo("int main() {\n    int x = 1;\n    return x;\n}\n"), "um C devia parecer código");
	confere(destaque_parece_codigo("def f(x):\n    y = x + 1\n    return y\n\nprint(f(2))\n"), "um Python devia parecer código");
	confere(!destaque_parece_codigo("Oi Emma, tudo bem?\nQueria uma ideia de conto.\nAlgo romântico e engraçado.\nPode ser?"), "prosa não é código");
	confere(!destaque_parece_codigo("```c\nint x;\n```\nolha isso"), "texto com bloco já pronto não precisa ser embrulhado");
	confere(!destaque_parece_codigo("uma linha só;"), "uma linha só não é código");
	confere(!strcmp(destaque_adivinhar("#!/bin/bash\necho oi"), "bash") && !strcmp(destaque_adivinhar("#include <x.h>\nint main(){}"), "c") && !strcmp(destaque_adivinhar("def f():\n  pass"), "python"), "adivinhar a linguagem");
	printf(falhas ? "destaque: %d falha(s)\n" : "destaque ok\n", falhas);
	return falhas ? 1 : 0;
}
