/* servicos/idioma.c com um /etc falso, um localectl e um locale falsos (testes/painel/bin) e o session/lumna-idioma-gerar. ./testa_idioma PASTA AJUDANTE */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "idioma.h"

int main(int argc, char **argv) {
	const char *raiz = argv[1];
	char *etc = g_build_filename(raiz, "etc", NULL), *sup = g_build_filename(raiz, "SUPPORTED", NULL);
	g_mkdir_with_parents(etc, 0755);
	setenv("LUMNA_ETC", etc, 1);
	setenv("LUMNA_SUPPORTED", sup, 1);
	setenv("LUMNA_LOCALE_GEN", "true", 1);
	setenv("LUMNA_IDIOMA_GERAR", argv[2], 1);
	setenv("LUMNA_SEM_PKEXEC", "1", 1);
	char *conf = g_build_filename(etc, "locale.conf", NULL);
	g_file_set_contents(conf, "LANG=pt_BR.UTF-8\nLC_TIME=\"en_US.UTF-8\"\n# comentario\n", -1, NULL);
	g_file_set_contents(sup, "pt_BR.UTF-8 UTF-8\npt_BR ISO-8859-1\nja_JP.UTF-8 UTF-8\nfr_FR.UTF-8 UTF-8\nde_DE.UTF-8 UTF-8\nsr_RS@latin UTF-8\nen_US.UTF-8 UTF-8\n", -1, NULL);

	struct idioma_cfg c;
	idioma_ler(&c);
	assert(!strcmp(c.lang, "pt_BR.UTF-8") && !strcmp(c.tempo, "en_US.UTF-8") && !c.numeros[0]);
	char nome[96];
	idioma_nome("pt_BR.UTF-8", nome, sizeof(nome));
	assert(!strcmp(nome, "Português (Brasil)"));
	idioma_nome("ja_JP.UTF-8", nome, sizeof(nome));
	assert(!strcmp(nome, "Japonês (Japão)"));
	idioma_nome("xx_YY.UTF-8", nome, sizeof(nome));
	assert(!strcmp(nome, "xx_YY.UTF-8")); /* não sabe: o código */
	puts("ler e nomes ok");

	char (*lista)[48] = g_malloc((size_t)IDIOMA_MAX * 48);
	int n = idioma_geradas(lista, IDIOMA_MAX);
	assert(n == 4); /* sem C, POSIX, C.utf8 e o que não é UTF-8 */
	assert(!strcmp(lista[0], "de_DE.UTF-8") && !strcmp(lista[3], "pt_PT.UTF-8")); /* por nome: Alemão ... Português (Portugal) */
	n = idioma_suportados(lista, IDIOMA_MAX);
	assert(n == 2); /* japonês e francês: os outros já estão gerados, o ISO-8859-1 e o @latin não entram */
	puts("geradas e suportadas ok");

	char erro[200] = "";
	struct idioma_cfg novo = c;
	g_strlcpy(novo.lang, "de_DE.UTF-8", 48);
	g_strlcpy(novo.moeda, "pt_BR.UTF-8", 48);
	novo.tempo[0] = 0; /* vazio: o mesmo do idioma (o localectl tira o LC_TIME) */
	assert(idioma_definir(&novo, erro, sizeof(erro)));
	idioma_ler(&c);
	assert(!strcmp(c.lang, "de_DE.UTF-8") && !strcmp(c.moeda, "pt_BR.UTF-8") && !c.tempo[0]);
	strcpy(novo.lang, "de;rm -rf");
	assert(!idioma_definir(&novo, erro, sizeof(erro)));
	puts("definir ok");

	assert(!idioma_gerar("pt_BR", erro, sizeof(erro)) || 1);
	assert(idioma_gerar("ja_JP.UTF-8", erro, sizeof(erro)));
	char *gen = g_build_filename(etc, "locale.gen", NULL), *t = NULL;
	assert(g_file_get_contents(gen, &t, NULL, NULL) && strstr(t, "ja_JP.UTF-8 UTF-8"));
	assert(!idioma_gerar("zz_ZZ.UTF-8", erro, sizeof(erro)) && strstr(erro, "suportado"));
	g_file_set_contents(gen, "# fr_FR.UTF-8 UTF-8\n#de_DE.UTF-8 UTF-8\n", -1, NULL);
	assert(idioma_gerar("fr_FR.UTF-8", erro, sizeof(erro)));
	assert(g_file_get_contents(gen, &t, NULL, NULL) && strstr(t, "\nfr_FR.UTF-8 UTF-8") == NULL && g_str_has_prefix(t, "fr_FR.UTF-8 UTF-8") && strstr(t, "#de_DE.UTF-8 UTF-8"));
	puts("gerar ok");

	char d[64], num[64], moeda[64];
	idioma_amostra("C.UTF-8", d, num, moeda, sizeof(d)); /* um idioma que há em qualquer lugar: só confere que não quebra */
	idioma_amostra("zz_ZZ.UTF-8", d, num, moeda, sizeof(d));
	assert(!d[0] && !num[0]); /* sem o idioma, sem amostra */
	puts("amostra ok");
	puts("idioma ok");
	return 0;
}
