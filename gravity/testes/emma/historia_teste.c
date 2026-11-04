/* As histórias (servicos/emmah.c), num HOME de mentira: criar, salvar capítulos, "onde paramos", a bíblia, abrir e listar. */
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emmah.h"
#include "emmaq.h"

static int falhas;
static void confere(bool c, const char *m) {
	if (!c) {
		falhas++;
		printf("FALHOU: %s\n", m);
	}
}

/* o que o emmaa.c faria: minúsculo e sem acento (aqui só o que o teste usa) */
char *emmaa_normalizar(const char *t) {
	GString *g = g_string_new(NULL);
	for (const char *p = t; *p;) {
		gunichar c = g_utf8_get_char(p);
		p = g_utf8_next_char(p);
		if (c == 0xe7) c = 'c'; else if (c == 0xe1 || c == 0xe3 || c == 0xe2) c = 'a'; else if (c == 0xe9 || c == 0xea) c = 'e'; else if (c == 0xed) c = 'i'; else if (c == 0xf3 || c == 0xf4) c = 'o';
		if (g_unichar_isalnum(c)) {
			g_string_append_c(g, (char)g_unichar_tolower(c));
		} else if (g->len && g->str[g->len - 1] != ' ') {
			g_string_append_c(g, ' ');
		}
	}
	while (g->len && g->str[g->len - 1] == ' ') g_string_truncate(g, g->len - 1);
	return g_string_free(g, FALSE);
}

static struct json *args(const char *j) {
	return json_ler(j);
}

static char *rodar(const char *acao, const char *j, bool *ok) {
	struct json *a = args(j);
	struct emmaa_res r = { 0 };
	emmah_executar(acao, a, &r);
	*ok = r.ok;
	char *t = g_strdup_printf("%s\n%s", r.resumo ? r.resumo : "", r.conteudo ? r.conteudo : "");
	emmaa_res_limpar(&r);
	json_liberar(a);
	return t;
}

void emmaa_res_limpar(struct emmaa_res *r) {
	g_free(r->resumo);
	g_free(r->conteudo);
	r->resumo = r->conteudo = NULL;
}

int main(void) {
	char *casa = g_dir_make_tmp("emma-hist-XXXXXX", NULL);
	g_setenv("HOME", casa, TRUE);
	g_setenv("XDG_CONFIG_HOME", casa, TRUE);
	emmaq_preparar();
	bool ok;
	char *t;

	/* a seção: troca, acrescenta e cria */
	char *b = emmah_secao_definir("# X\n\n## Personagens\n\nAna\n\n## Mundo\n\nUma ilha\n", "Personagens", "Ana e Bia", false);
	confere(strstr(b, "Ana e Bia") && !strstr(b, "\nAna\n") && strstr(b, "## Mundo\n\nUma ilha"), "trocar a seção não pode mexer nas outras");
	char *b2 = emmah_secao_definir(b, "personagens", "Caio", true);
	confere(strstr(b2, "Ana e Bia\nCaio"), "acrescentar junta ao que já havia (e o nome da seção não depende de maiúsculas)");
	char *b3 = emmah_secao_definir(b2, "Segredos", "O farol", false);
	confere(strstr(b3, "## Segredos\n\nO farol") && strstr(b3, "## Mundo"), "uma seção nova vai ao fim");
	g_free(b); g_free(b2); g_free(b3);

	t = rodar("historia.listar", "{}", &ok);
	confere(ok && strstr(t, "Ainda não há"), "sem histórias");
	g_free(t);
	t = rodar("historia.criar", "{\"nome\":\"A Ilha dos Ventos\",\"premissa\":\"Duas irmãs num farol.\"}", &ok);
	confere(ok, "criar a história");
	g_free(t);
	t = rodar("historia.criar", "{\"nome\":\"a ilha dos ventos\"}", &ok);
	confere(!ok && strstr(t, "já existe"), "não cria duas (nem com outras maiúsculas)");
	g_free(t);
	t = rodar("historia.criar", "{\"nome\":\"../../etc\"}", &ok);
	confere(ok, "um nome com barras vira um nome seguro");
	g_free(t);
	char *fora = g_build_filename(casa, "etc", NULL);
	confere(!g_file_test(fora, G_FILE_TEST_EXISTS), "o nome com barras escapou da pasta das histórias");
	g_free(fora);

	t = rodar("historia.salvar_capitulo", "{\"nome\":\"A ILHA DOS VENTOS\",\"titulo\":\"A chegada ao farol\",\"conteudo\":\"As irmãs chegaram ao amanhecer.\"}", &ok);
	confere(ok && strstr(t, "capitulo 1") == NULL && strstr(t, "01-a-chegada-ao-farol.md"), "o primeiro capítulo é o 01");
	g_free(t);
	t = rodar("historia.salvar_capitulo", "{\"nome\":\"A Ilha dos Ventos\",\"titulo\":\"A tempestade\",\"conteudo\":\"O vento arrancou o telhado.\\nFim do capítulo.\"}", &ok);
	confere(ok && strstr(t, "02-a-tempestade.md"), "o segundo é o 02");
	g_free(t);
	t = rodar("historia.atualizar_biblia", "{\"nome\":\"A Ilha dos Ventos\",\"secao\":\"Personagens\",\"texto\":\"Lia (a mais velha) e Nina.\"}", &ok);
	confere(ok, "atualizar a bíblia");
	g_free(t);

	t = rodar("historia.abrir", "{\"nome\":\"a ilha dos ventos\"}", &ok);
	confere(ok && strstr(t, "Duas irmãs num farol") && strstr(t, "Lia (a mais velha)") && strstr(t, "Capítulo 2 — A tempestade") && strstr(t, "01-a-chegada-ao-farol.md") && strstr(t, "Fim do capítulo."),
		"abrir devolve a bíblia, a lista, onde paramos e o final do último");
	if (!ok || !strstr(t, "Capítulo 2 — A tempestade")) printf("%s\n", t);
	g_free(t);
	t = rodar("historia.abrir", "{\"nome\":\"Outra\"}", &ok);
	confere(!ok && strstr(t, "Existem:") && strstr(t, "A Ilha dos Ventos"), "história que não existe: diz quais existem");
	g_free(t);
	t = rodar("historia.listar", "{}", &ok);
	confere(ok && strstr(t, "A Ilha dos Ventos: 2 capítulo(s)"), "listar mostra os capítulos");
	g_free(t);

	char *cap = g_build_filename(casa, "Emma", "Histórias", "A Ilha dos Ventos", "capitulos", "02-a-tempestade.md", NULL), *txt = NULL;
	confere(g_file_get_contents(cap, &txt, NULL, NULL) && g_str_has_prefix(txt, "# A tempestade\n\nO vento"), "o arquivo do capítulo tem o título");
	g_free(txt); g_free(cap);
	printf(falhas ? "historia: %d falha(s)\n" : "historia ok\n", falhas);
	return falhas ? 1 : 0;
}
