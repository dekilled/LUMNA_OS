/* O núcleo do Signals (servicos/sgbuffer.c) contra um modelo simples (uma string) em sequências aleatórias, e os casos de arquivo, busca e desfazer. */
#define _GNU_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sgbuffer.h"

static int falhas, total;
#define CONFERE(c, m) do { total++; if (!(c)) { fprintf(stderr, "FALHOU: %s (linha %d)\n", m, __LINE__); falhas++; } } while (0)

static bool igual(sg_buffer *b, const char *esperado) {
	char *t = sg_texto_todo(b);
	bool ok = !strcmp(t, esperado);
	if (!ok) {
		fprintf(stderr, "   tinha [%s] queria [%s]\n", t, esperado);
	}
	g_free(t);
	return ok;
}

static struct sg_pos aqui(sg_buffer *b, int off, const char *modelo) {
	/* o deslocamento (bytes) no modelo vira (linha, coluna) */
	int linha = 0, col = 0;
	for (int i = 0; i < off; i++) {
		if (modelo[i] == '\n') {
			linha++;
			col = 0;
		} else {
			col++;
		}
	}
	(void)b;
	return (struct sg_pos){ linha, col };
}

/* os deslocamentos que caem na fronteira de um caractere */
static int fronteira(const char *s, GRand *r) {
	size_t n = strlen(s);
	if (n == 0) {
		return 0;
	}
	for (int t = 0; t < 40; t++) {
		int off = (int)g_rand_int_range(r, 0, (gint32)n + 1);
		if ((size_t)off == n || ((unsigned char)s[off] & 0xC0) != 0x80) {
			return off;
		}
	}
	return 0;
}

static const char *PEDACOS[] = { "a", "b", "ç", "ã", "日本", "\n", "ab\ncd", " ", "palavra outra", "\n\n", "x\n", "é\nñ\n", "😀", "" };

static void aleatorio(unsigned semente, int passos) {
	GRand *r = g_rand_new_with_seed(semente);
	sg_buffer *b = sg_novo();
	GString *m = g_string_new(NULL);
	GPtrArray *historico = g_ptr_array_new_with_free_func(g_free); /* o modelo depois de cada edição, para conferir o desfazer */
	g_ptr_array_add(historico, g_strdup(""));
	int altura = 0;
	for (int i = 0; i < passos; i++) {
		int op = (int)g_rand_int_range(r, 0, 10);
		if (op < 5) { /* inserir */
			const char *t = PEDACOS[g_rand_int_range(r, 0, G_N_ELEMENTS(PEDACOS))];
			if (!*t) {
				continue;
			}
			int off = fronteira(m->str, r);
			bool junta = g_rand_boolean(r);
			sg_inserir(b, aqui(b, off, m->str), t, strlen(t), junta);
			g_string_insert(m, off, t);
		} else if (op < 8) { /* apagar */
			int a = fronteira(m->str, r), z = fronteira(m->str, r);
			if (a > z) {
				int t = a;
				a = z;
				z = t;
			}
			if (a == z) {
				continue;
			}
			sg_apagar(b, aqui(b, a, m->str), aqui(b, z, m->str), g_rand_boolean(r));
			g_string_erase(m, a, z - a);
		} else if (op == 8 && sg_pode_desfazer(b)) { /* desfazer e refazer: o texto volta ao que era, e vai de novo */
			char *antes = sg_texto_todo(b);
			struct sg_pos c;
			CONFERE(sg_desfazer(b, &c), "desfazer devia desfazer");
			CONFERE(sg_refazer(b, &c), "refazer devia refazer");
			CONFERE(igual(b, antes), "desfazer e refazer voltam ao mesmo texto");
			g_free(antes);
			continue;
		} else {
			sg_fechar_grupo(b);
			continue;
		}
		if (!igual(b, m->str)) {
			fprintf(stderr, "   semente %u passo %d\n", semente, i);
			falhas++;
			break;
		}
		altura++;
	}
	/* desfaz tudo: tem de chegar ao vazio, e refaz tudo: tem de voltar ao final */
	char *final = g_strdup(m->str);
	int guarda = 0;
	while (sg_desfazer(b, NULL) && guarda++ < 100000) {
	}
	CONFERE(igual(b, ""), "desfazer tudo volta ao documento vazio");
	CONFERE(!sg_modificado(b), "desfeito até o começo, o documento não está modificado");
	guarda = 0;
	while (sg_refazer(b, NULL) && guarda++ < 100000) {
	}
	CONFERE(igual(b, final), "refazer tudo volta ao final");
	(void)altura;
	g_free(final);
	g_ptr_array_free(historico, TRUE);
	g_string_free(m, TRUE);
	sg_liberar(b);
	g_rand_free(r);
}

static char *escrever(const char *dir, const char *nome, const void *dados, size_t n) {
	char *c = g_build_filename(dir, nome, NULL);
	g_file_set_contents(c, dados, (gssize)n, NULL);
	return c;
}

static char *ler(const char *c, gsize *n) {
	char *d = NULL;
	g_file_get_contents(c, &d, n, NULL);
	return d;
}

static void arquivos(const char *dir) {
	char erro[200];
	/* ida e volta exata: cada arquivo salvo sem mexer sai idêntico */
	struct { const char *nome; const char *conteudo; size_t n; } casos[] = {
		{ "lf.txt", "um\ndois\ntres\n", 0 }, { "sem-nl.txt", "um\ndois", 0 }, { "crlf.txt", "um\r\ndois\r\n", 0 }, { "vazio.txt", "", 0 },
		{ "bom.txt", "\xEF\xBB\xBF" "ol\xC3\xA1\n", 0 }, { "latin1.txt", "ol\xE1 caf\xE9\n", 0 }, { "linhas-vazias.txt", "\n\n\n", 0 },
		{ "utf8.txt", "日本語 ação 😀\nfim", 0 }, { "so-cr-no-meio.txt", "a\rb\nc\n", 0 },
	};
	for (size_t i = 0; i < G_N_ELEMENTS(casos); i++) {
		size_t n = strlen(casos[i].conteudo);
		char *c = escrever(dir, casos[i].nome, casos[i].conteudo, n);
		sg_buffer *b = sg_novo();
		CONFERE(sg_abrir(b, c, erro, sizeof(erro)) == 0, casos[i].nome);
		CONFERE(!sg_modificado(b), "recém-aberto não está modificado");
		CONFERE(sg_salvar(b, NULL, erro, sizeof(erro)) == 0, "salvar sem mexer");
		gsize m;
		char *d = ler(c, &m);
		CONFERE(m == n && !memcmp(d, casos[i].conteudo, n), casos[i].nome);
		if (m != n || memcmp(d, casos[i].conteudo, n)) {
			fprintf(stderr, "   [%s] voltou diferente\n", casos[i].nome);
		}
		g_free(d);
		sg_liberar(b);
		g_free(c);
	}
	/* o que o arquivo é */
	char *c = escrever(dir, "crlf2.txt", "a\r\nb\r\nc", 7);
	sg_buffer *b = sg_novo();
	sg_abrir(b, c, erro, sizeof(erro));
	CONFERE(sg_fim_de_linha(b) == SG_CRLF && !sg_fim_misto(b) && sg_linhas(b) == 3 && igual(b, "a\nb\nc"), "CRLF: lido como linhas sem o \\r e lembra o fim de linha");
	sg_definir_fim_de_linha(b, SG_LF);
	CONFERE(sg_modificado(b), "mudar o fim de linha conta como modificação");
	sg_salvar(b, NULL, erro, sizeof(erro));
	gsize m;
	char *d = ler(c, &m);
	CONFERE(m == 5 && !memcmp(d, "a\nb\nc", 5), "salvo com LF depois de trocar");
	g_free(d);
	sg_liberar(b);
	g_free(c);
	/* recarregar: reabrir com o próprio caminho do buffer (o app faz isso quando o arquivo muda no disco) */
	c = escrever(dir, "recarga.txt", "um\ndois\n", 8);
	b = sg_novo();
	sg_abrir(b, c, erro, sizeof(erro));
	sg_inserir(b, (struct sg_pos){ 0, 0 }, "X", 1, false);
	CONFERE(sg_abrir(b, sg_caminho(b), erro, sizeof(erro)) == 0 && igual(b, "um\ndois\n") && !sg_modificado(b) && !strcmp(sg_caminho(b), c), "reabrir com o próprio caminho (recarregar) descarta o que mudou e mantém o caminho");
	sg_liberar(b);
	g_free(c);
	c = escrever(dir, "misto.txt", "a\r\nb\nc\r\nd\r\n", 11);
	b = sg_novo();
	sg_abrir(b, c, erro, sizeof(erro));
	CONFERE(sg_fim_de_linha(b) == SG_CRLF && sg_fim_misto(b), "misto: o predominante vence e avisa");
	sg_liberar(b);
	g_free(c);
	c = escrever(dir, "l1.txt", "caf\xE9\n", 5);
	b = sg_novo();
	sg_abrir(b, c, erro, sizeof(erro));
	CONFERE(sg_codificacao(b) == SG_LATIN1 && igual(b, "café\n"), "Latin-1: reconhecido e convertido para UTF-8");
	sg_inserir(b, (struct sg_pos){ 0, 0 }, "日", 3, false);
	CONFERE(sg_salvar(b, NULL, erro, sizeof(erro)) == -1 && strstr(erro, "Latin-1"), "um caractere que não cabe em Latin-1 recusa salvar, com o motivo");
	sg_desfazer(b, NULL);
	sg_definir_codificacao(b, SG_UTF8);
	CONFERE(sg_salvar(b, NULL, erro, sizeof(erro)) == 0, "trocando para UTF-8, salva");
	d = ler(c, &m);
	CONFERE(m == 6 && !memcmp(d, "caf\xC3\xA9\n", 6), "salvo em UTF-8 depois de trocar a codificação");
	g_free(d);
	sg_liberar(b);
	g_free(c);
	/* recusas */
	c = escrever(dir, "bin.dat", "ab\0cd", 5);
	b = sg_novo();
	CONFERE(sg_abrir(b, c, erro, sizeof(erro)) == -1 && strstr(erro, "binário"), "binário recusado");
	g_free(c);
	c = escrever(dir, "u16.txt", "\xFF\xFE" "a\0", 4);
	CONFERE(sg_abrir(b, c, erro, sizeof(erro)) == -1 && strstr(erro, "UTF-16"), "UTF-16 recusado com o motivo");
	g_free(c);
	CONFERE(sg_abrir(b, dir, erro, sizeof(erro)) == -1 && strstr(erro, "pasta"), "pasta recusada");
	char *nao = g_build_filename(dir, "nao-existe.txt", NULL);
	CONFERE(sg_abrir(b, nao, erro, sizeof(erro)) == -1 && erro[0], "arquivo que não existe recusado, com o motivo");
	g_free(nao);
	sg_liberar(b);
	/* permissões, link simbólico, arquivo novo e sem sobras do temporário */
	c = escrever(dir, "perm.sh", "#!/bin/sh\n", 10);
	chmod(c, 0750);
	b = sg_novo();
	sg_abrir(b, c, erro, sizeof(erro));
	sg_inserir(b, sg_pos_fim(b), "echo oi\n", 8, false);
	sg_salvar(b, NULL, erro, sizeof(erro));
	struct stat st;
	stat(c, &st);
	CONFERE((st.st_mode & 07777) == 0750, "salvar mantém as permissões (um script continua executável)");
	char *link = g_build_filename(dir, "elo.txt", NULL);
	CONFERE(symlink(c, link) == 0, "criar o link");
	sg_inserir(b, sg_pos_fim(b), "mais\n", 5, false);
	sg_salvar(b, link, erro, sizeof(erro));
	struct stat ls;
	lstat(link, &ls);
	d = ler(c, &m);
	CONFERE(S_ISLNK(ls.st_mode) && strstr(d, "mais"), "salvar por um link grava no arquivo de destino e o link continua link");
	g_free(d);
	g_free(link);
	char *novo = g_build_filename(dir, "novo.txt", NULL);
	sg_liberar(b);
	b = sg_novo();
	sg_inserir(b, (struct sg_pos){ 0, 0 }, "olá", 4, false);
	CONFERE(sg_modificado(b), "um documento novo com texto está modificado");
	CONFERE(sg_salvar(b, NULL, erro, sizeof(erro)) == -1 && strstr(erro, "nome"), "sem nome, salvar pede um");
	CONFERE(sg_salvar(b, novo, erro, sizeof(erro)) == 0 && !sg_modificado(b) && !strcmp(sg_caminho(b), novo), "salvar como: grava, guarda o caminho e não está mais modificado");
	CONFERE(g_file_test(novo, G_FILE_TEST_EXISTS), "o arquivo novo existe");
	g_free(novo);
	GDir *gd = g_dir_open(dir, 0, NULL);
	const char *nome;
	int sobras = 0;
	while (gd && (nome = g_dir_read_name(gd))) {
		if (strstr(nome, ".signals-")) {
			sobras++;
		}
	}
	if (gd) {
		g_dir_close(gd);
	}
	CONFERE(sobras == 0, "salvar não deixa arquivo temporário para trás");
	/* mudou no disco */
	g_free(c);
	c = escrever(dir, "ext.txt", "um", 2);
	sg_liberar(b);
	b = sg_novo();
	sg_abrir(b, c, erro, sizeof(erro));
	CONFERE(!sg_mudou_no_disco(b), "recém-aberto não mudou no disco");
	g_file_set_contents(c, "outro programa escreveu isto", -1, NULL);
	CONFERE(sg_mudou_no_disco(b), "outro programa gravou: mudou no disco");
	sg_salvar(b, NULL, erro, sizeof(erro));
	CONFERE(!sg_mudou_no_disco(b), "depois de salvar, a nossa versão é a do disco");
	g_unlink(c);
	CONFERE(sg_mudou_no_disco(b), "o arquivo sumiu: conta como mudou");
	sg_liberar(b);
	g_free(c);
}

static void modificado_e_desfazer(void) {
	sg_buffer *b = sg_novo();
	CONFERE(!sg_modificado(b) && !sg_pode_desfazer(b), "documento novo vazio: nada a desfazer");
	struct sg_pos p = { 0, 0 };
	/* digitar uma palavra desfaz de uma vez; o Enter separa */
	p = sg_inserir(b, p, "a", 1, true);
	p = sg_inserir(b, p, "b", 1, true);
	p = sg_inserir(b, p, "c", 1, true);
	CONFERE(igual(b, "abc"), "digitou abc");
	sg_desfazer(b, &p);
	CONFERE(igual(b, "") && p.linha == 0 && p.col == 0, "digitação seguida desfaz de uma vez, com o cursor no começo");
	sg_refazer(b, &p);
	CONFERE(igual(b, "abc") && p.col == 3, "refazer leva o cursor para o fim do que foi digitado");
	p = sg_inserir(b, p, "\n", 1, true);
	p = sg_inserir(b, p, "d", 1, true);
	sg_desfazer(b, NULL);
	CONFERE(igual(b, "abc\n"), "depois de uma quebra de linha começa outro grupo");
	sg_desfazer(b, NULL);
	CONFERE(igual(b, "abc"), "e a quebra de linha desfaz sozinha");
	/* apagar com Backspace repetido */
	sg_fechar_grupo(b);
	p = (struct sg_pos){ 0, 3 };
	p = sg_apagar(b, (struct sg_pos){ 0, 2 }, (struct sg_pos){ 0, 3 }, true);
	p = sg_apagar(b, (struct sg_pos){ 0, 1 }, (struct sg_pos){ 0, 2 }, true);
	CONFERE(igual(b, "a"), "apagou duas");
	sg_desfazer(b, &p);
	CONFERE(igual(b, "abc") && p.col == 3, "Backspace repetido desfaz de uma vez");
	/* modificado volta a falso ao desfazer até o salvamento, e fica verdadeiro se editar depois de passar dele */
	sg_marcar_salvo(b);
	CONFERE(!sg_modificado(b), "marcado como salvo");
	sg_fechar_grupo(b);
	sg_inserir(b, (struct sg_pos){ 0, 3 }, "!", 1, false);
	CONFERE(sg_modificado(b), "editar depois de salvar modifica");
	sg_desfazer(b, NULL);
	CONFERE(!sg_modificado(b), "desfazer até o ponto salvo desmodifica");
	sg_desfazer(b, NULL);
	CONFERE(sg_modificado(b), "desfazer para antes do ponto salvo modifica");
	sg_inserir(b, (struct sg_pos){ 0, 0 }, "z", 1, false);
	sg_marcar_salvo(b);
	sg_fechar_grupo(b);
	sg_inserir(b, (struct sg_pos){ 0, 0 }, "y", 1, false);
	sg_desfazer(b, NULL);
	sg_desfazer(b, NULL);
	sg_inserir(b, (struct sg_pos){ 0, 0 }, "w", 1, false); /* corta o refazer que passava pelo ponto salvo */
	CONFERE(sg_modificado(b), "depois de editar por cima do ponto salvo, nunca mais volta a 'não modificado' por desfazer");
	/* trocar é uma operação só */
	sg_liberar(b);
	b = sg_novo();
	sg_inserir(b, (struct sg_pos){ 0, 0 }, "um dois tres", 12, false);
	sg_fechar_grupo(b);
	sg_trocar(b, (struct sg_pos){ 0, 3 }, (struct sg_pos){ 0, 7 }, "2\n2", 3);
	CONFERE(igual(b, "um 2\n2 tres"), "trocar um trecho por outro com quebra de linha");
	sg_desfazer(b, NULL);
	CONFERE(igual(b, "um dois tres"), "trocar desfaz de uma vez");
	/* o \r de um texto colado some */
	sg_inserir(b, sg_pos_fim(b), "\r\nx\r\n", 5, false);
	CONFERE(igual(b, "um dois tres\nx\n"), "o \\r de um texto colado é descartado");
	sg_liberar(b);
}

static void posicoes_e_palavras(void) {
	sg_buffer *b = sg_novo();
	const char *texto = "olá mundo, ação!\nsegunda linha";
	sg_inserir(b, (struct sg_pos){ 0, 0 }, texto, strlen(texto), false);
	struct sg_pos p = sg_pos_valida(b, (struct sg_pos){ 0, 3 });
	CONFERE(p.col == 2, "uma posição no meio do 'á' (2 bytes) recua para a fronteira do caractere");
	p = sg_pos_proximo(b, (struct sg_pos){ 0, 2 });
	CONFERE(p.col == 4, "do 'á' (2 bytes) o próximo caractere está 2 bytes à frente");
	struct sg_pos fim_l0 = { 0, (int)strlen("olá mundo, ação!") };
	p = sg_pos_proximo(b, fim_l0);
	CONFERE(p.linha == 1 && p.col == 0, "no fim da linha, o próximo passa para a seguinte");
	p = sg_pos_anterior(b, (struct sg_pos){ 1, 0 });
	CONFERE(p.linha == 0 && p.col == fim_l0.col, "no começo da linha, o anterior volta para o fim da de cima");
	p = sg_pos_palavra_proxima(b, (struct sg_pos){ 0, 0 });
	CONFERE(p.col == (int)strlen("olá "), "Ctrl+Direita: pula a palavra e o espaço");
	p = sg_pos_palavra_proxima(b, p);
	CONFERE(p.col == (int)strlen("olá mundo") + 0 || p.col == (int)strlen("olá mundo, "), "Ctrl+Direita: depois de 'mundo' vem a pontuação");
	p = sg_pos_palavra_anterior(b, (struct sg_pos){ 0, (int)strlen("olá mundo") });
	CONFERE(p.col == (int)strlen("olá "), "Ctrl+Esquerda: volta ao começo da palavra");
	struct sg_pos a, z;
	sg_palavra_em(b, (struct sg_pos){ 0, 6 }, &a, &z);
	char *t = sg_texto(b, a, z);
	CONFERE(!strcmp(t, "mundo"), "duplo clique: a palavra sob o ponto");
	g_free(t);
	sg_palavra_em(b, (struct sg_pos){ 0, 1 }, &a, &z);
	t = sg_texto(b, a, z);
	CONFERE(!strcmp(t, "olá"), "duplo clique numa palavra com acento pega ela inteira");
	g_free(t);
	CONFERE(sg_pos_cmp((struct sg_pos){ 0, 5 }, (struct sg_pos){ 1, 0 }) < 0 && sg_pos_cmp((struct sg_pos){ 2, 0 }, (struct sg_pos){ 2, 0 }) == 0, "comparar posições");
	struct sg_pos fora = sg_pos_valida(b, (struct sg_pos){ 99, 99 });
	CONFERE(fora.linha == 1 && fora.col == (int)strlen("segunda linha"), "uma posição fora do documento vai para o fim");
	t = sg_texto(b, (struct sg_pos){ 1, 3 }, (struct sg_pos){ 0, 4 });
	CONFERE(!strcmp(t, " mundo, ação!\nseg"), "pegar o texto com os extremos trocados, atravessando linhas");
	g_free(t);
	sg_liberar(b);
}

static void busca(void) {
	sg_buffer *b = sg_novo();
	const char *doc = "Casa casa CASA\ncasamento a casa\nÉvora évora\n";
	sg_inserir(b, (struct sg_pos){ 0, 0 }, doc, strlen(doc), false);
	struct sg_busca q = { "casa", false, false };
	CONFERE(sg_contar(b, &q) == 5, "sem diferenciar maiúsculas: 'casa' aparece 5 vezes (3 na primeira linha, 'casamento' e 'a casa')");
	q.maiusculas = true;
	CONFERE(sg_contar(b, &q) == 3, "diferenciando: só as 3 em minúsculas");
	q.maiusculas = false;
	q.palavra = true;
	CONFERE(sg_contar(b, &q) == 4, "palavras inteiras: 'casamento' não conta");
	struct sg_busca e = { "évora", false, false };
	CONFERE(sg_contar(b, &e) == 2, "maiúsculas e minúsculas acentuadas ('É' e 'é') também se casam");
	struct sg_pos ini, fim;
	bool voltou;
	q.palavra = false;
	CONFERE(sg_buscar(b, &q, (struct sg_pos){ 0, 0 }, true, &ini, &fim, &voltou) && ini.linha == 0 && ini.col == 0 && fim.col == 4 && !voltou, "o primeiro achado é o do começo");
	CONFERE(sg_buscar(b, &q, fim, true, &ini, &fim, &voltou) && ini.col == 5, "o seguinte a partir do fim do anterior");
	CONFERE(sg_buscar(b, &q, (struct sg_pos){ 2, 0 }, true, &ini, &fim, &voltou) && voltou && ini.linha == 0, "passou do último: volta ao começo e avisa");
	CONFERE(sg_buscar(b, &q, (struct sg_pos){ 1, 5 }, false, &ini, &fim, &voltou) && ini.linha == 1 && ini.col == 0 && !voltou, "para trás: o anterior ao ponto");
	CONFERE(sg_buscar(b, &q, (struct sg_pos){ 0, 0 }, false, &ini, &fim, &voltou) && voltou && ini.linha == 1 && ini.col == 12, "para trás do começo: volta ao último do documento e avisa");
	struct sg_busca nada = { "zebra", false, false };
	CONFERE(!sg_buscar(b, &nada, (struct sg_pos){ 0, 0 }, true, &ini, &fim, &voltou) && !sg_buscar(b, &nada, (struct sg_pos){ 0, 0 }, false, &ini, &fim, &voltou), "sem achado: falso nos dois sentidos");
	struct sg_busca vazia = { "", false, false }, quebra = { "a\nb", false, false };
	CONFERE(sg_contar(b, &vazia) == 0 && !sg_buscar(b, &vazia, (struct sg_pos){ 0, 0 }, true, &ini, &fim, &voltou), "agulha vazia não acha nada");
	CONFERE(sg_contar(b, &quebra) == 0, "agulha com quebra de linha não é suportada: não acha, sem travar");
	/* substituir tudo, desfeito de uma vez, incluindo troca por texto com quebra de linha */
	q.maiusculas = false;
	int n = sg_substituir_tudo(b, &q, "X");
	CONFERE(n == 5, "substituiu as 5");
	CONFERE(igual(b, "X X X\nXmento a X\nÉvora évora\n"), "o texto depois de substituir");
	sg_desfazer(b, NULL);
	CONFERE(igual(b, doc), "substituir tudo desfaz de uma vez");
	sg_substituir_tudo(b, &e, "<\n>");
	CONFERE(igual(b, "Casa casa CASA\ncasamento a casa\n<\n> <\n>\n"), "trocar por texto com quebra de linha");
	sg_desfazer(b, NULL);
	CONFERE(igual(b, doc), "e desfaz de uma vez");
	struct sg_busca ss = { "aa", false, false };
	sg_liberar(b);
	b = sg_novo();
	sg_inserir(b, (struct sg_pos){ 0, 0 }, "aaaa", 4, false);
	CONFERE(sg_contar(b, &ss) == 2 && sg_substituir_tudo(b, &ss, "b") == 2 && igual(b, "bb"), "achados não se sobrepõem: 'aa' em 'aaaa' são 2");
	sg_liberar(b);
}

int main(void) {
	char *dir = g_dir_make_tmp("signals-XXXXXX", NULL);
	for (unsigned s = 1; s <= 60; s++) {
		aleatorio(s, 400);
	}
	arquivos(dir);
	modificado_e_desfazer();
	posicoes_e_palavras();
	busca();
	char *cmd = g_strdup_printf("rm -rf '%s'", dir);
	if (system(cmd) != 0) {
		fprintf(stderr, "(não limpei %s)\n", dir);
	}
	g_free(cmd);
	g_free(dir);
	if (falhas) {
		fprintf(stderr, "%d de %d verificações falharam\n", falhas, total);
		return 1;
	}
	printf("signals (núcleo): ok (%d verificações)\n", total);
	return 0;
}
