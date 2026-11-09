/* O serviço do explorador (arquivos.c): listar, ordenar, filtrar, tipos, tamanhos e datas, histórico, caminhos, favoritos e recentes, numa árvore de teste. */
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "arquivos.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); falhas++; } } while (0)

static void escreve(const char *pasta, const char *nome, const char *conteudo) {
	char *p = g_build_filename(pasta, nome, NULL);
	g_file_set_contents(p, conteudo, -1, NULL);
	g_free(p);
}

static int acha(const ar_lista *l, const char *nome) {
	for (int i = 0; i < l->n; i++) {
		if (!strcmp(l->v[i].nome, nome)) {
			return i;
		}
	}
	return -1;
}

int main(void) {
	char *raiz = g_dir_make_tmp("arquivos-XXXXXX", NULL);
	char *sub = g_build_filename(raiz, "Projetos", NULL);
	char *sub2 = g_build_filename(raiz, "Estudos", NULL);
	g_mkdir(sub, 0755);
	g_mkdir(sub2, 0755);
	escreve(sub, "a.txt", "um");
	escreve(sub, ".escondido", "x");
	escreve(sub, "b.md", "# oi");
	escreve(raiz, "planejamento.pdf", "%PDF");
	escreve(raiz, "notas.MD", "# notas");
	escreve(raiz, "orçamento.ods", "tabela");
	escreve(raiz, "Ação.txt", "acento");
	escreve(raiz, "arquivo2.txt", "2");
	escreve(raiz, "arquivo10.txt", "10");
	escreve(raiz, "foto.PNG", "png");
	escreve(raiz, ".oculto", "x");
	char *grande = g_build_filename(raiz, "grande.bin", NULL);
	FILE *f = fopen(grande, "wb");
	fseek(f, 1500000, SEEK_SET);
	fputc(0, f);
	fclose(f);
	char *antigo = g_build_filename(raiz, "arquivo2.txt", NULL);
	struct utimbuf tb = { 1000000000, 1000000000 };
	utime(antigo, &tb);
	symlink(raiz, g_build_filename(raiz, "atalho", NULL));

	/* tipos */
	CONFERE(ar_tipo_do_nome("a.PDF") == AR_PDF && ar_tipo_do_nome("x.jpeg") == AR_IMAGEM && ar_tipo_do_nome("x.ods") == AR_PLANILHA && ar_tipo_do_nome("x.odp") == AR_APRESENTACAO &&
		ar_tipo_do_nome("Makefile") == AR_OUTRO && ar_tipo_do_nome(".bashrc") == AR_OUTRO && ar_tipo_do_nome("x.tar") == AR_COMPACTADO && ar_tipo_do_nome("x.rs") == AR_CODIGO, "tipos pela extensão");

	/* listar: sem ocultos / com ocultos */
	ar_lista l;
	char *erro = NULL;
	CONFERE(ar_listar(raiz, false, &l, &erro), "listar");
	CONFERE(acha(&l, ".oculto") < 0 && acha(&l, "Projetos") >= 0, "sem ocultos");
	int n_sem = l.n;
	ar_liberar(&l);
	ar_listar(raiz, true, &l, &erro);
	CONFERE(l.n == n_sem + 1 && acha(&l, ".oculto") >= 0 && l.v[acha(&l, ".oculto")].oculto, "com ocultos entra o .oculto");
	ar_liberar(&l);
	CONFERE(!ar_listar("/nao/existe/mesmo", false, &l, &erro) && erro, "pasta que não existe dá erro");
	g_free(erro);

	/* ordenar */
	ar_listar(raiz, false, &l, NULL);
	ar_ordenar(&l, AR_O_NOME, false);
	CONFERE(l.v[0].pasta && l.v[1].pasta, "as pastas primeiro");
	CONFERE(!strcmp(l.v[0].nome, "atalho") || !strcmp(l.v[0].nome, "Estudos") || !strcmp(l.v[1].nome, "Estudos"), "ordem de pastas");
	CONFERE(acha(&l, "arquivo2.txt") < acha(&l, "arquivo10.txt"), "arquivo2 vem antes de arquivo10");
	CONFERE(acha(&l, "Ação.txt") < acha(&l, "arquivo2.txt"), "Ação (com acento e maiúscula) ordena como 'acao'");
	CONFERE(acha(&l, "foto.PNG") > acha(&l, "arquivo10.txt") && acha(&l, "planejamento.pdf") > acha(&l, "orçamento.ods"), "ordem pelo nome sem caixa nem acento");
	ar_ordenar(&l, AR_O_NOME, true);
	CONFERE(l.v[0].pasta && acha(&l, "planejamento.pdf") < acha(&l, "Ação.txt"), "do Z ao A, as pastas continuam primeiro");
	ar_ordenar(&l, AR_O_TAMANHO, true);
	CONFERE(!strcmp(l.v[acha(&l, "grande.bin")].nome, "grande.bin") && acha(&l, "grande.bin") < acha(&l, "notas.MD"), "o maior primeiro (depois das pastas)");
	ar_ordenar(&l, AR_O_DATA, false);
	CONFERE(acha(&l, "arquivo2.txt") < acha(&l, "notas.MD"), "o mais antigo primeiro");

	/* filtros e busca */
	int idx[64];
	int n = ar_filtrar(&l, "", AR_F_PASTAS, idx);
	CONFERE(n == 3, "3 pastas (Projetos, Estudos e o atalho): %d", n);
	n = ar_filtrar(&l, "", AR_F_DOCUMENTOS, idx);
	CONFERE(n >= 5, "documentos: pdf, md, ods, txts (%d)", n);
	n = ar_filtrar(&l, "", AR_F_IMAGENS, idx);
	CONFERE(n == 1 && !strcmp(l.v[idx[0]].nome, "foto.PNG"), "uma imagem");
	n = ar_filtrar(&l, "acao", AR_F_TUDO, idx);
	CONFERE(n == 1 && !strcmp(l.v[idx[0]].nome, "Ação.txt"), "a busca ignora acento e caixa (%d)", n);
	n = ar_filtrar(&l, "ORCAMENTO", AR_F_TUDO, idx);
	CONFERE(n == 1, "orçamento por ORCAMENTO");
	CONFERE(ar_busca_combina("Relatório Final.pdf", "relatorio fin") && !ar_busca_combina("abc", "abd") && ar_busca_combina("qualquer", ""), "busca por parte do nome");
	n = ar_filtrar(&l, "", AR_F_OUTROS, idx);
	CONFERE(n >= 1, "outros: grande.bin");
	ar_liberar(&l);

	/* contar e tamanho */
	CONFERE(ar_contar_itens(sub, false) == 2 && ar_contar_itens(sub, true) == 3, "contar itens: 2 sem ocultos, 3 com");
	CONFERE(ar_contar_itens("/nao/existe", false) == -1, "contar numa pasta que não abre");
	int64_t bytes;
	int itens;
	CONFERE(ar_tamanho_pasta(sub, 1000, &bytes, &itens) && itens == 3 && bytes == 2 + 1 + 4, "tamanho da pasta (%d itens, %lld bytes)", itens, (long long)bytes);
	CONFERE(!ar_tamanho_pasta(sub, 1, &bytes, &itens), "o teto corta a conta");

	/* tamanhos e datas */
	char s[64];
	ar_tamanho_humano(0, s, sizeof(s));
	CONFERE(!strcmp(s, "0 B"), "0 B (%s)", s);
	ar_tamanho_humano(999, s, sizeof(s));
	CONFERE(!strcmp(s, "999 B"), "999 B (%s)", s);
	ar_tamanho_humano(1800000000LL, s, sizeof(s));
	CONFERE(!strcmp(s, "1,8 GB"), "1,8 GB (%s)", s);
	ar_tamanho_humano(2000000, s, sizeof(s));
	CONFERE(!strcmp(s, "2 MB"), "2 MB (%s)", s);
	ar_tamanho_humano(320000, s, sizeof(s));
	CONFERE(!strcmp(s, "320 KB"), "320 KB (%s)", s);
	setenv("TZ", "UTC", 1);
	tzset();
	time_t agora = 1760000000; /* 2025-10-09 08:53 UTC */
	ar_data_humana(agora - 3600, agora, s, sizeof(s));
	CONFERE(!strcmp(s, "Hoje, 07:53"), "hoje (%s)", s);
	ar_data_humana(agora - 86400, agora, s, sizeof(s));
	CONFERE(!strncmp(s, "Ontem, ", 7), "ontem (%s)", s);
	ar_data_humana(agora - 5 * 86400, agora, s, sizeof(s));
	CONFERE(!strcmp(s, "4 out, 08:53"), "neste ano (%s)", s);
	ar_data_humana(agora - 400 * 86400LL, agora, s, sizeof(s));
	CONFERE(strstr(s, "2024") != NULL && !strchr(s, ':'), "outro ano (%s)", s);
	ar_permissoes_texto(0755, s, sizeof(s));
	CONFERE(!strcmp(s, "rwxr-xr-x"), "permissões (%s)", s);
	ar_permissoes_texto(0600, s, sizeof(s));
	CONFERE(!strcmp(s, "rw-------"), "permissões 600 (%s)", s);

	/* histórico */
	ar_hist h = { 0 };
	ar_hist_ir(&h, "/a");
	ar_hist_ir(&h, "/a/b");
	ar_hist_ir(&h, "/a/b/c");
	CONFERE(!strcmp(ar_hist_atual(&h), "/a/b/c") && ar_hist_pode_voltar(&h) && !ar_hist_pode_avancar(&h), "histórico: três lugares");
	CONFERE(!strcmp(ar_hist_voltar(&h), "/a/b") && !strcmp(ar_hist_voltar(&h), "/a") && ar_hist_voltar(&h) == NULL, "voltar duas vezes e parar");
	CONFERE(!strcmp(ar_hist_avancar(&h), "/a/b") && ar_hist_pode_avancar(&h), "avançar");
	ar_hist_ir(&h, "/x");
	CONFERE(!ar_hist_pode_avancar(&h) && !strcmp(ar_hist_atual(&h), "/x"), "ir a um lugar novo corta o que havia à frente");
	ar_hist_ir(&h, "/x");
	CONFERE(h.n == 3, "ir ao mesmo lugar não repete (%d)", h.n);
	ar_hist_liberar(&h);

	/* caminhos */
	const char *casa = g_get_home_dir();
	char *docs = g_build_filename(casa, "Documentos", "Projetos", NULL);
	ar_parte p[16];
	int np = ar_partes(docs, p, 16);
	CONFERE(np == 3 && !strcmp(p[0].rotulo, "Home") && !strcmp(p[0].caminho, casa) && !strcmp(p[2].rotulo, "Projetos") && !strcmp(p[2].caminho, docs), "partes na casa (%d: %s/%s)", np, p[0].rotulo,
		np > 1 ? p[1].rotulo : "");
	ar_partes_liberar(p, np);
	np = ar_partes("/usr/share", p, 16);
	CONFERE(np == 3 && !strcmp(p[0].rotulo, "Computador") && !strcmp(p[1].rotulo, "usr") && !strcmp(p[2].caminho, "/usr/share"), "partes fora da casa (%d)", np);
	ar_partes_liberar(p, np);
	np = ar_partes(casa, p, 16);
	CONFERE(np == 1 && !strcmp(p[0].rotulo, "Home"), "a própria casa é uma parte só");
	ar_partes_liberar(p, np);
	char *pai = ar_pai("/usr/share");
	CONFERE(!strcmp(pai, "/usr") && ar_pai("/") == NULL, "o pai");
	g_free(pai);
	g_free(docs);

	/* favoritos e recentes */
	ar_lugar v[16];
	CONFERE(ar_favoritos(v, 16) == 0 && !ar_eh_favorito(sub), "sem favoritos");
	CONFERE(ar_favorito_alternar(sub) && ar_eh_favorito(sub), "favoritar");
	ar_favorito_alternar(raiz);
	int nf = ar_favoritos(v, 16);
	CONFERE(nf == 2 && !strcmp(v[0].rotulo, "Projetos") && !strcmp(v[0].icone, "folder"), "dois favoritos, na ordem em que entraram (%d)", nf);
	ar_lugares_liberar(v, nf);
	CONFERE(!ar_favorito_alternar(sub) && !ar_eh_favorito(sub) && ar_eh_favorito(raiz), "desfavoritar");
	ar_recente_registrar(sub);
	ar_recente_registrar(raiz);
	ar_recente_registrar(sub);
	int nr = ar_recentes(v, 16);
	CONFERE(nr == 2 && !strcmp(v[0].caminho, sub) && !strcmp(v[1].caminho, raiz), "recentes: o último primeiro, sem repetir (%d)", nr);
	ar_lugares_liberar(v, nr);
	int nc = ar_colecoes(v, 16);
	CONFERE(nc >= 1 && !strcmp(v[0].rotulo, "Home") && !strcmp(v[0].caminho, casa), "coleções começam pela casa");
	ar_lugares_liberar(v, nc);

	/* prévia do conteúdo da pasta */
	enum ar_tipo pt[4];
	int npv = ar_previa_pasta(raiz, pt, 4);
	CONFERE(npv == 4 && pt[0] != AR_PASTA, "a prévia traz até 4 itens, arquivos antes (%d)", npv);
	int np2 = ar_previa_pasta(sub, pt, 4);
	CONFERE(np2 == 2, "uma pasta com 2 itens (a.txt e b.md; o oculto fica de fora): %d", np2);
	CONFERE(ar_previa_pasta("/nao/existe", pt, 4) == 0, "uma pasta que não abre não tem prévia");
	/* a ordem da prévia é a do nome (antes comparava ponteiros e mudava a cada leitura), arquivos antes das pastas, e é a mesma sempre */
	char *ord = g_dir_make_tmp("previa-XXXXXX", NULL);
	escreve(ord, "c.png", "x");
	escreve(ord, "a.mp3", "x");
	escreve(ord, "b.pdf", "x");
	char *ord_pasta = g_build_filename(ord, "0pasta", NULL);
	g_mkdir(ord_pasta, 0755);
	enum ar_tipo po[4], po2[4];
	int nor = ar_previa_pasta(ord, po, 4);
	CONFERE(nor == 4 && po[0] == AR_AUDIO && po[1] == AR_PDF && po[2] == AR_IMAGEM && po[3] == AR_PASTA, "a prévia segue a ordem dos nomes: arquivos (a, b, c) e depois a pasta (%d)", nor);
	for (int rodada = 0; rodada < 50; rodada++) {
		for (int junk = 0; junk < 7; junk++) { /* mexe no heap: a ordem por ponteiro mudaria */
			g_free(g_malloc(37 + rodada * junk));
		}
		ar_previa_pasta(ord, po2, 4);
		CONFERE(!memcmp(po, po2, sizeof(po)), "a prévia é a mesma a cada leitura (rodada %d)", rodada);
	}
	g_free(ord_pasta);
	g_free(ord);

	/* unidades */
	ar_unidade un[16];
	int nu = ar_unidades(un, 16);
	CONFERE(nu >= 1 && !strcmp(un[0].ponto, "/") && un[0].total > 0 && un[0].usado > 0 && un[0].usado <= un[0].total, "a raiz está entre as unidades (%d)", nu);
	/* pastas depois dos arquivos, se a pessoa quiser */
	ar_listar(raiz, false, &l, NULL);
	ar_ordenar_pastas_primeiro(false);
	ar_ordenar(&l, AR_O_NOME, false);
	CONFERE(!l.v[0].pasta || l.v[l.n - 1].pasta || acha(&l, "Projetos") > acha(&l, "Ação.txt"), "pastas misturadas aos arquivos pelo nome");
	ar_ordenar_pastas_primeiro(true);
	ar_ordenar(&l, AR_O_NOME, false);
	CONFERE(l.v[0].pasta, "de volta: pastas primeiro");
	ar_liberar(&l);

	/* cor e ícone de uma pasta */
	uint32_t rgb;
	char ic[32];
	ar_pasta_estilo(sub, &rgb, ic, sizeof(ic));
	CONFERE(rgb == 0 && !ic[0], "uma pasta sem estilo");
	ar_pasta_estilo_definir(sub, 0x30A46C, "music");
	ar_pasta_estilo(sub, &rgb, ic, sizeof(ic));
	CONFERE(rgb == 0x30A46C && !strcmp(ic, "music"), "cor e emblema gravados (%06X %s)", rgb, ic);
	ar_pasta_estilo_definir(sub, 0xE5484D, "");
	ar_pasta_estilo(sub, &rgb, ic, sizeof(ic));
	CONFERE(rgb == 0xE5484D && !ic[0], "só a cor");
	ar_pasta_estilo_definir(sub, 0, "");
	ar_pasta_estilo(sub, &rgb, ic, sizeof(ic));
	CONFERE(rgb == 0 && !ic[0], "voltar ao padrão apaga o estilo");
	ar_pasta_estilo_definir(raiz, 0, "inexistente");
	ar_pasta_estilo(raiz, &rgb, ic, sizeof(ic));
	CONFERE(!ic[0], "um emblema que não existe não é desenhado");

	/* o caminho digitado */
	char *proj = g_build_filename(raiz, "Projetos", NULL);
	g_mkdir(g_build_filename(raiz, "Propostas", NULL), 0755);
	g_mkdir(g_build_filename(raiz, "Protocolos", NULL), 0755);
	ar_sugestao sg[64];
	char *entrada = g_strdup_printf("%s/Pro", raiz);
	int ns = ar_sugestoes(entrada, sg, 12);
	CONFERE(ns >= 3 && !sg[0].recente && !strcmp(sg[0].nome, "Projetos") && !strcmp(sg[1].nome, "Propostas") && !strcmp(sg[2].nome, "Protocolos"), "sugestões: as pastas que começam com Pro, em ordem (%d: %s)", ns, ns ? sg[0].nome : "");
	ar_sugestoes_liberar(sg, ns);
	g_free(entrada);
	entrada = g_strdup_printf("%s/pro", raiz);
	ns = ar_sugestoes(entrada, sg, 12);
	CONFERE(ns >= 3, "sem distinguir caixa (%d)", ns);
	ar_sugestoes_liberar(sg, ns);
	g_free(entrada);
	entrada = g_strdup_printf("%s/", raiz);
	ns = ar_sugestoes(entrada, sg, 12);
	CONFERE(ns >= 3, "uma barra no fim lista as subpastas (%d)", ns);
	ar_sugestoes_liberar(sg, ns);
	g_free(entrada);
	entrada = g_strdup_printf("%s/Pro/", raiz);
	CONFERE(ar_sugestoes(entrada, sg, 12) == 0, "um caminho que não existe não sugere nada");
	g_free(entrada);
	CONFERE(ar_sugestoes("", sg, 12) == 0, "vazio não sugere");
	/* uma pasta com muita coisa dentro não passa do que cabe em quem chamou (12) nem do teto de 6 pastas + recentes */
	for (int k = 0; k < 100; k++) {
		char *nome = g_strdup_printf("%s/Muitas%03d", raiz, k);
		g_mkdir(nome, 0755);
		g_free(nome);
	}
	entrada = g_strdup_printf("%s/Muitas", raiz);
	ns = ar_sugestoes(entrada, sg, 12);
	CONFERE(ns == 6 && !strcmp(sg[0].nome, "Muitas000"), "muitas pastas que combinam: só as 6 primeiras, em ordem (%d)", ns);
	ar_sugestoes_liberar(sg, ns);
	g_free(entrada);
	ar_recente_registrar(proj);
	entrada = g_strdup_printf("%s/Pro", raiz);
	ns = ar_sugestoes(entrada, sg, 12);
	int recentes_n = 0;
	for (int i = 0; i < ns; i++) {
		recentes_n += sg[i].recente;
	}
	CONFERE(recentes_n == 0, "o recente que já apareceu como pasta não repete (%d)", recentes_n);
	ar_sugestoes_liberar(sg, ns);
	g_free(entrada);
	char *dig = ar_caminho_digitado(proj, raiz);
	CONFERE(dig && !strcmp(dig, proj), "caminho digitado que existe");
	g_free(dig);
	dig = ar_caminho_digitado("Projetos", raiz);
	CONFERE(dig && !strcmp(dig, proj), "um nome relativo vale a partir da pasta de agora");
	g_free(dig);
	dig = ar_caminho_digitado("~", raiz);
	CONFERE(dig && !strcmp(dig, g_get_home_dir()), "~ é a casa");
	g_free(dig);
	CONFERE(ar_caminho_digitado("/nao/existe", raiz) == NULL && ar_caminho_digitado("   ", raiz) == NULL, "o que não existe não vale");
	dig = ar_caminho_digitado("Projetos/../Propostas/", raiz);
	CONFERE(dig && g_str_has_suffix(dig, "/Propostas"), "ponto-ponto é resolvido (%s)", dig);
	g_free(dig);

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	puts("explorador (serviço de arquivos): ok");
	return 0;
}
