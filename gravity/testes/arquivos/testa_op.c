/* O motor de operações do explorador (arquivos_op.c): copiar, mover, lixeira com desfazer, apagar, compactar, extrair, renomear, criar, os conflitos e a área de transferência. */
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "arquivos_op.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); falhas++; } } while (0)

static char *raiz;
static char *P(const char *a, const char *b) { return g_build_filename(raiz, a, b, NULL); }
static void escreve(const char *a, const char *b, const char *conteudo) {
	char *p = P(a, b);
	char *pasta = g_path_get_dirname(p);
	g_mkdir_with_parents(pasta, 0755);
	g_file_set_contents(p, conteudo, -1, NULL);
	g_free(pasta);
	g_free(p);
}
static char *le(const char *a, const char *b) {
	char *p = P(a, b), *c = NULL;
	g_file_get_contents(p, &c, NULL, NULL);
	g_free(p);
	return c;
}
static bool existe(const char *a, const char *b) {
	char *p = P(a, b);
	bool e = g_file_test(p, G_FILE_TEST_EXISTS) || g_file_test(p, G_FILE_TEST_IS_SYMLINK);
	g_free(p);
	return e;
}

/* roda uma operação até o fim; responde às perguntas com `resposta` (AR_C_PERGUNTAR: devolve na primeira pergunta sem responder, para o teste olhar) */
static ar_op_estado roda(enum ar_op_tipo tipo, char **origens, int n, const char *destino, enum ar_conflito resposta, bool todos, int *perguntas) {
	ar_op *op = ar_op_iniciar(tipo, origens, n, destino, NULL, NULL);
	ar_op_estado st;
	for (int i = 0; i < 3000; i++) {
		ar_op_estado_ler(op, &st);
		if (st.pergunta) {
			if (perguntas) {
				(*perguntas)++;
			}
			ar_op_responder(op, resposta, todos);
		}
		if (st.terminou) {
			break;
		}
		g_usleep(2000);
	}
	ar_op_estado_ler(op, &st);
	ar_op_liberar(op);
	return st;
}

int main(void) {
	raiz = g_dir_make_tmp("op-XXXXXX", NULL);
	char *dados = g_build_filename(raiz, "dados", NULL);
	g_setenv("XDG_DATA_HOME", dados, TRUE);
	g_setenv("XDG_RUNTIME_DIR", raiz, TRUE);

	/* uma árvore: Projetos/{a.txt, sub/b.txt}, solto.txt */
	escreve("Projetos", "a.txt", "AAA");
	escreve("Projetos/sub", "b.txt", "BBBBB");
	escreve("", "solto.txt", "solto");
	g_mkdir(P("destino", ""), 0755);
	g_mkdir(P("outro", ""), 0755);

	/* nomes */
	char *m = NULL;
	CONFERE(!ar_nome_valido("", &m) && m, "vazio não vale");
	g_free(m);
	CONFERE(!ar_nome_valido("a/b", &m) && !ar_nome_valido("..", &m), "barra e .. não valem");
	CONFERE(ar_nome_valido("foto (1).png", &m), "nome comum vale");
	char *livre = ar_nome_livre(raiz, "solto.txt");
	CONFERE(!strcmp(livre, "solto (2).txt"), "nome livre: %s", livre);
	g_free(livre);
	livre = ar_nome_livre(raiz, "novo.txt");
	CONFERE(!strcmp(livre, "novo.txt"), "nome que já está livre fica");
	g_free(livre);
	escreve("", "pacote.tar.gz", "x");
	livre = ar_nome_livre(raiz, "pacote.tar.gz");
	CONFERE(!strcmp(livre, "pacote (2).tar.gz"), "a extensão .tar.gz é uma só: %s", livre);
	g_free(livre);

	/* copiar uma pasta inteira */
	char *o1[] = { P("Projetos", ""), P("solto.txt", "") };
	char *dest = P("destino", "");
	int perguntas = 0;
	ar_op_estado st = roda(AR_OP_COPIAR, o1, 2, dest, AR_C_PERGUNTAR, false, &perguntas);
	CONFERE(st.terminou && st.erros == 0 && perguntas == 0, "copiar sem conflito (erros %d: %s)", st.erros, st.erro);
	char *c = le("destino/Projetos/sub", "b.txt");
	CONFERE(c && !strcmp(c, "BBBBB") && existe("destino", "solto.txt") && existe("Projetos", "a.txt"), "a cópia tem tudo e o original continua");
	g_free(c);
	CONFERE(st.itens_total >= 5 && st.itens_feitos == st.itens_total && st.feito == st.total && st.total == 3 + 5 + 5, "progresso: %d/%d itens, %lld/%lld bytes", st.itens_feitos, st.itens_total,
		(long long)st.feito, (long long)st.total);

	/* conflito: manter ambos */
	escreve("Projetos", "a.txt", "NOVO");
	perguntas = 0;
	char *so_solto[] = { P("solto.txt", "") };
	escreve("", "solto.txt", "versao2");
	st = roda(AR_OP_COPIAR, so_solto, 1, dest, AR_C_MANTER_AMBOS, false, &perguntas);
	c = le("destino", "solto.txt");
	char *c2 = le("destino", "solto (2).txt");
	CONFERE(perguntas == 1 && c && !strcmp(c, "solto") && c2 && !strcmp(c2, "versao2"), "manter ambos: o velho fica e o novo vira (2) (%s/%s)", c, c2);
	g_free(c);
	g_free(c2);
	/* conflito: pular */
	perguntas = 0;
	st = roda(AR_OP_COPIAR, so_solto, 1, dest, AR_C_PULAR, false, &perguntas);
	c = le("destino", "solto.txt");
	CONFERE(perguntas == 1 && !strcmp(c, "solto") && !existe("destino", "solto (3).txt"), "pular não mexe em nada");
	g_free(c);
	/* conflito: substituir (o velho vai para a lixeira) */
	st = roda(AR_OP_COPIAR, so_solto, 1, dest, AR_C_SUBSTITUIR, false, NULL);
	c = le("destino", "solto.txt");
	CONFERE(!strcmp(c, "versao2"), "substituir troca o conteúdo (%s)", c);
	g_free(c);
	char *lix = ar_lixeira_pasta();
	CONFERE(g_file_test(lix, G_FILE_TEST_IS_DIR), "o substituído foi para a lixeira");
	/* o aplicar a todos pergunta uma vez só */
	escreve("Projetos", "c.txt", "CCC");
	perguntas = 0;
	char *so_pasta[] = { P("Projetos", "") };
	st = roda(AR_OP_COPIAR, so_pasta, 1, dest, AR_C_SUBSTITUIR, true, &perguntas);
	CONFERE(perguntas == 1 && st.erros == 0, "pasta com pasta junta, e os arquivos que existem perguntam uma vez com 'para todos' (%d perguntas)", perguntas);

	/* copiar na própria pasta: uma cópia (2), sem perguntar */
	char *proj[] = { P("Projetos", "") };
	char *raiz_dir = g_strdup(raiz);
	perguntas = 0;
	st = roda(AR_OP_COPIAR, proj, 1, raiz_dir, AR_C_PERGUNTAR, false, &perguntas);
	CONFERE(perguntas == 0 && existe("Projetos (2)", "a.txt"), "colar no mesmo lugar faz uma cópia (2)");

	/* pasta dentro dela mesma */
	char *sub = P("Projetos/sub", "");
	st = roda(AR_OP_COPIAR, proj, 1, sub, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 1 && !existe("Projetos/sub/Projetos", ""), "uma pasta não entra nela mesma (%s)", st.erro);

	/* mover, e desfazer */
	ar_desfazer_limpar();
	escreve("mov", "m1.txt", "m1");
	escreve("mov", "m2.txt", "m2");
	char *mv[] = { P("mov/m1.txt", ""), P("mov/m2.txt", "") };
	char *dout = P("outro", "");
	st = roda(AR_OP_MOVER, mv, 2, dout, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 0 && existe("outro", "m1.txt") && !existe("mov", "m1.txt"), "mover");
	char desc[96];
	CONFERE(ar_desfazer_ha(desc, sizeof(desc)) && strstr(desc, "Mover"), "há o que desfazer (%s)", desc);
	char *erro = NULL;
	CONFERE(ar_desfazer(&erro) && existe("mov", "m1.txt") && existe("mov", "m2.txt") && !existe("outro", "m1.txt"), "desfazer o mover devolve");
	CONFERE(!ar_desfazer_ha(NULL, 0), "e não sobra nada");
	/* mover para a própria pasta não faz nada */
	char *mvm[] = { P("mov/m1.txt", "") };
	char *dmov = P("mov", "");
	st = roda(AR_OP_MOVER, mvm, 1, dmov, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 0 && existe("mov", "m1.txt") && !ar_desfazer_ha(NULL, 0), "mover para onde já está não faz nada");
	/* mover com conflito */
	escreve("outro", "m1.txt", "do outro");
	perguntas = 0;
	st = roda(AR_OP_MOVER, mvm, 1, dout, AR_C_MANTER_AMBOS, false, &perguntas);
	c = le("outro", "m1.txt");
	c2 = le("outro", "m1 (2).txt");
	CONFERE(perguntas == 1 && !strcmp(c, "do outro") && !strcmp(c2, "m1") && !existe("mov", "m1.txt"), "mover com conflito, manter ambos");
	g_free(c);
	g_free(c2);

	/* lixeira, restaurar pelo desfazer, apagar de vez */
	ar_desfazer_limpar();
	escreve("lix", "tchau.txt", "tchau");
	char *lx[] = { P("lix/tchau.txt", "") };
	st = roda(AR_OP_LIXEIRA, lx, 1, NULL, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 0 && !existe("lix", "tchau.txt"), "foi para a lixeira (%s)", st.erro);
	char *tfile = g_build_filename(lix, "tchau.txt", NULL);
	char *tinfo = g_build_filename(dados, "Trash", "info", "tchau.txt.trashinfo", NULL);
	char *info = NULL;
	CONFERE(g_file_test(tfile, G_FILE_TEST_EXISTS) && g_file_get_contents(tinfo, &info, NULL, NULL) && strstr(info, "[Trash Info]") && strstr(info, "Path=") && strstr(info, "tchau.txt") &&
		strstr(info, "DeletionDate="), "a lixeira segue o padrão (files + info com Path e DeletionDate)");
	g_free(info);
	char *orig = ar_lixeira_origem("tchau.txt");
	char *esperado = P("lix/tchau.txt", "");
	CONFERE(orig && !strcmp(orig, esperado), "a lixeira sabe de onde veio (%s)", orig);
	g_free(orig);
	g_free(esperado);
	CONFERE(ar_desfazer_ha(desc, sizeof(desc)) && strstr(desc, "lixeira") && ar_desfazer(&erro) && existe("lix", "tchau.txt") && !g_file_test(tfile, G_FILE_TEST_EXISTS) && !g_file_test(tinfo, G_FILE_TEST_EXISTS),
		"desfazer a lixeira restaura e limpa a info");
	/* dois com o mesmo nome na lixeira não se atropelam */
	escreve("lix2", "tchau.txt", "outro tchau");
	char *lx2[] = { P("lix/tchau.txt", ""), P("lix2/tchau.txt", "") };
	st = roda(AR_OP_LIXEIRA, lx2, 2, NULL, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 0 && g_file_test(tfile, G_FILE_TEST_EXISTS), "dois itens de mesmo nome na lixeira");
	GDir *d = g_dir_open(lix, 0, NULL);
	int na_lixeira = 0;
	while (g_dir_read_name(d)) {
		na_lixeira++;
	}
	g_dir_close(d);
	CONFERE(na_lixeira >= 3, "os dois ficaram (%d na lixeira)", na_lixeira);
	/* restaurar quando o lugar já está ocupado: volta com (2) */
	escreve("lix", "tchau.txt", "ocupado");
	char *nomes_t[] = { g_strdup("tchau.txt") };
	st = roda(AR_OP_RESTAURAR, nomes_t, 1, NULL, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 0 && existe("lix", "tchau (2).txt"), "restaurar sem apagar o que ocupa o lugar (%s)", st.erro);
	g_free(nomes_t[0]);
	/* apagar de vez */
	ar_desfazer_limpar();
	char *ap[] = { P("lix2", "") };
	st = roda(AR_OP_APAGAR, ap, 1, NULL, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 0 && !existe("lix2", "") && !ar_desfazer_ha(NULL, 0), "apagar de vez não deixa o que desfazer");
	/* esvaziar a lixeira */
	CONFERE(ar_lixeira_esvaziar(&erro), "esvaziar");
	d = g_dir_open(lix, 0, NULL);
	CONFERE(g_dir_read_name(d) == NULL, "a lixeira ficou vazia");
	g_dir_close(d);

	/* renomear, criar e desfazer */
	ar_desfazer_limpar();
	char *novo = NULL;
	char *p_solto = P("solto.txt", "");
	CONFERE(ar_renomear(p_solto, "renomeado.txt", &novo, &erro) && existe("", "renomeado.txt") && !existe("", "solto.txt"), "renomear");
	g_free(novo);
	char *p_ren = P("renomeado.txt", "");
	CONFERE(!ar_renomear(p_ren, "pacote.tar.gz", &novo, &erro) && erro && strstr(erro, "Já existe"), "renomear para um nome que existe é recusado");
	g_free(erro);
	erro = NULL;
	CONFERE(!ar_renomear(p_ren, "a/b", &novo, &erro), "renomear com barra é recusado");
	g_free(erro);
	erro = NULL;
	CONFERE(ar_desfazer(&erro) && existe("", "solto.txt"), "desfazer o renomear");
	char *criado = NULL;
	CONFERE(ar_criar(raiz, "Nova pasta", true, &criado, &erro) && g_file_test(criado, G_FILE_TEST_IS_DIR), "criar pasta");
	char *criado2 = NULL;
	CONFERE(ar_criar(raiz, "Nova pasta", true, &criado2, &erro) && strstr(criado2, "Nova pasta (2)"), "criar de novo vira (2)");
	CONFERE(ar_criar(raiz, "n.txt", false, &criado, &erro) && g_file_test(criado, G_FILE_TEST_IS_REGULAR), "criar arquivo");
	CONFERE(ar_desfazer(&erro) && !g_file_test(criado, G_FILE_TEST_EXISTS), "desfazer criar manda para a lixeira");

	/* compactar e extrair */
	char *cp[] = { P("Projetos", "") };
	st = roda(AR_OP_COMPACTAR, cp, 1, raiz, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 0 && existe("", "Projetos.zip"), "compactar uma pasta (%s)", st.erro);
	char *zp[] = { P("Projetos.zip", "") };
	char *dx = P("extraido", "");
	g_mkdir(dx, 0755);
	st = roda(AR_OP_EXTRAIR, zp, 1, dx, AR_C_PERGUNTAR, false, NULL);
	c = le("extraido/Projetos/sub", "b.txt");
	CONFERE(st.erros == 0 && c && !strcmp(c, "BBBBB"), "extrair devolve a árvore (%s)", st.erro);
	g_free(c);
	char *dois[] = { P("renomeado.txt", ""), P("solto.txt", "") };
	g_unlink(dois[0]);
	escreve("", "x1.txt", "1");
	escreve("", "x2.txt", "2");
	char *vs[] = { P("x1.txt", ""), P("x2.txt", "") };
	st = roda(AR_OP_COMPACTAR, vs, 2, raiz, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 0 && existe("", "Arquivos.zip"), "vários arquivos viram Arquivos.zip");
	char *ex2[] = { P("Arquivos.zip", "") };
	st = roda(AR_OP_EXTRAIR, ex2, 1, dx, AR_C_PERGUNTAR, false, NULL);
	CONFERE(st.erros == 0 && existe("extraido/Arquivos", "x2.txt"), "extrair numa pasta com o nome do arquivo");

	/* cancelar */
	escreve("cancela", "grande.bin", "x");
	{
		char *gp = P("cancela/grande.bin", "");
		FILE *f = fopen(gp, "wb");
		fseek(f, 400 * 1000 * 1000, SEEK_SET);
		fputc(0, f);
		fclose(f);
		char *go[] = { gp };
		ar_op *op = ar_op_iniciar(AR_OP_COPIAR, go, 1, raiz_dir, NULL, NULL);
		ar_op_cancelar(op);
		ar_op_estado ce;
		for (int i = 0; i < 3000; i++) {
			ar_op_estado_ler(op, &ce);
			if (ce.terminou) {
				break;
			}
			g_usleep(2000);
		}
		ar_op_liberar(op);
		CONFERE(ce.terminou && ce.cancelado, "cancelar termina a operação");
		g_unlink(gp);
	}

	/* a área de transferência */
	char *caminhos[] = { "/etc/hostname", "/etc/passwd" };
	char *texto = ar_area_gravar(true, caminhos, 2);
	CONFERE(!strcmp(texto, "/etc/hostname\n/etc/passwd"), "o texto da área leva os caminhos");
	bool recortar = false;
	char **lidos = ar_area_ler(texto, &recortar);
	CONFERE(lidos && recortar && !strcmp(lidos[0], "/etc/hostname") && !strcmp(lidos[1], "/etc/passwd") && !lidos[2], "ler de volta (recortar)");
	g_strfreev(lidos);
	lidos = ar_area_ler("outra coisa qualquer", &recortar);
	CONFERE(!lidos, "se a área de transferência mudou, não vale");
	lidos = ar_area_ler("/etc/passwd", &recortar);
	CONFERE(lidos && !recortar && !strcmp(lidos[0], "/etc/passwd"), "um caminho solto que existe vale como copiar");
	g_strfreev(lidos);
	lidos = ar_area_ler("/nao/existe/mesmo", &recortar);
	CONFERE(!lidos, "um caminho que não existe não vale");
	g_free(texto);

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	puts("operações com arquivos: ok");
	return 0;
}
