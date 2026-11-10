/* O serviço da biblioteca do LUMNA Music (servicos/musica.c) com músicas de verdade (fixtures.py). ./testa_musica CASA  (HOME e XDG_* dentro dela) */
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "musica.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { falhas++; fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static int acha(const mu_acervo *a, const char *titulo) {
	for (int i = 0; i < a->n; i++) {
		if (!strcmp(a->v[i].titulo, titulo)) {
			return i;
		}
	}
	return -1;
}

static int ultimo_prog = -1, chamadas;
static void prog(int f, int t, void *d) {
	(void)d;
	chamadas++;
	ultimo_prog = f == t ? t : -2;
}

int main(int argc, char **argv) {
	const char *casa = argc > 1 ? argv[1] : ".";
	mu_cfg c;
	mu_cfg_ler(&c);
	CONFERE(c.npastas == 1 && c.normalizar && c.sem_intervalos && c.transicao == 0 && c.volume == 80, "os padrões");
	char *pasta = g_build_filename(casa, "Músicas", NULL);
	g_strlcpy(c.pastas[0], pasta, sizeof(c.pastas[0]));
	/* as configurações: gravar e ler */
	c.transicao = 5; c.tam_capa = 999; c.volume = 33; c.repetir = 2; c.aleatorio = true; g_strlcpy(c.saida, "pulse/teste", sizeof(c.saida));
	CONFERE(mu_cfg_gravar(&c), "gravar o config");
	mu_cfg c2;
	mu_cfg_ler(&c2);
	CONFERE(c2.transicao == 5 && c2.tam_capa == 240 && c2.volume == 33 && c2.repetir == 2 && c2.aleatorio && !strcmp(c2.saida, "pulse/teste") && !strcmp(c2.pastas[0], pasta), "o config volta, com o limite de 240 na capa (%d)", c2.tam_capa);

	/* a varredura */
	mu_acervo a;
	gint64 t0 = g_get_monotonic_time();
	mu_varrer(&c, &a, prog, NULL);
	double s1 = (double)(g_get_monotonic_time() - t0) / 1e6;
	CONFERE(a.n == 9, "9 músicas (o txt, o oculto e o quebrado ficam fora): %d", a.n);
	CONFERE(ultimo_prog > 0 && chamadas >= 9, "o progresso chega ao fim (%d chamadas)", chamadas);
	int i = acha(&a, "Entre órbitas");
	CONFERE(i >= 0 && !strcmp(a.v[i].artista, "LUMNA Sessions") && !strcmp(a.v[i].album, "Horizonte violeta") && a.v[i].faixa == 1 && a.v[i].ano == 2024 && a.v[i].duracao >= 2 && a.v[i].duracao <= 4 && a.v[i].capa_embutida && !strcmp(a.v[i].genero, "Ambiente"), "as tags do MP3");
	i = acha(&a, "Canção do fim");
	CONFERE(i >= 0 && a.v[i].faixa == 2 && a.v[i].disco == 1 && a.v[i].ano == 2022 && !a.v[i].capa_embutida, "as tags do FLAC");
	i = acha(&a, "Instantes");
	CONFERE(i >= 0 && !strcmp(a.v[i].artista, "Bruno") && a.v[i].ano == 2020, "as tags do Opus (ficam no stream)");
	i = acha(&a, "sem tags");
	CONFERE(i >= 0 && !strcmp(a.v[i].artista, "Artista desconhecido") && !strcmp(a.v[i].album, "Álbum desconhecido") && a.v[i].duracao == 2, "sem tags: o nome do arquivo e os desconhecidos");
	CONFERE(acha(&a, "quebrada") < 0 && acha(&a, "oculta") < 0, "o arquivo quebrado e o oculto ficam de fora");
	/* o cache: a segunda varredura não lê ffprobe de novo */
	mu_acervo b;
	t0 = g_get_monotonic_time();
	mu_varrer(&c, &b, NULL, NULL);
	double s2 = (double)(g_get_monotonic_time() - t0) / 1e6;
	CONFERE(b.n == a.n && s2 < s1 / 3 + 0.05, "o cache acelera (%.2f s -> %.2f s)", s1, s2);
	i = acha(&b, "Canção do fim");
	CONFERE(i >= 0 && b.v[i].disco == 1 && b.v[i].ano == 2022 && b.v[i].duracao >= 2, "e traz as mesmas tags");
	mu_acervo_liberar(&b);
	/* sem subpastas */
	mu_cfg sem = c;
	sem.subpastas = false;
	mu_varrer(&sem, &b, NULL, NULL);
	CONFERE(b.n == 1, "sem subpastas só o wav da raiz (%d)", b.n);
	mu_acervo_liberar(&b);
	/* a mesma pasta duas vezes não duplica */
	mu_cfg dup = c;
	g_strlcpy(dup.pastas[1], pasta, sizeof(dup.pastas[1]));
	dup.npastas = 2;
	mu_varrer(&dup, &b, NULL, NULL);
	CONFERE(b.n == a.n, "pasta repetida não duplica (%d)", b.n);
	mu_acervo_liberar(&b);

	/* álbuns */
	mu_grupos g;
	mu_albuns(&a, MU_ORDEM_TITULO, &g);
	CONFERE(g.n == 5, "5 álbuns (Horizonte, Luz, Instantes, Coletânea, desconhecido): %d", g.n);
	int ih = -1, il = -1, ic = -1;
	for (int k = 0; k < g.n; k++) {
		ih = !strcmp(g.v[k].nome, "Horizonte violeta") ? k : ih;
		il = !strcmp(g.v[k].nome, "Luz distante") ? k : il;
		ic = !strcmp(g.v[k].nome, "Coletânea") ? k : ic;
	}
	CONFERE(ih >= 0 && g.v[ih].n == 2 && g.v[ih].ano == 2024 && !strcmp(g.v[ih].artista, "LUMNA Sessions"), "álbum Horizonte violeta");
	CONFERE(il >= 0 && g.v[il].n == 3 && !strcmp(a.v[g.v[il].idx[0]].titulo, "Aurora lenta") && !strcmp(a.v[g.v[il].idx[1]].titulo, "Canção do fim") && !strcmp(a.v[g.v[il].idx[2]].titulo, "Bis"), "as faixas do álbum: disco 1 antes do 2, por número");
	CONFERE(ic >= 0 && !strcmp(g.v[ic].artista, "Vários artistas"), "álbum com artistas diferentes e sem artista do álbum: Vários artistas");
	for (int k = 1; k < g.n; k++) {
		char *x = mu_normalizar(g.v[k - 1].nome), *y = mu_normalizar(g.v[k].nome);
		CONFERE(strcmp(x, y) <= 0, "álbuns em ordem de título: %s, %s", g.v[k - 1].nome, g.v[k].nome);
		g_free(x); g_free(y);
	}
	mu_grupos_liberar(&g);
	mu_albuns(&a, MU_ORDEM_ANO, &g);
	CONFERE(g.v[0].ano >= g.v[1].ano || g.v[1].ano == 0, "por ano: os mais novos primeiro");
	mu_grupos_liberar(&g);
	mu_artistas(&a, &g);
	int ia = -1;
	for (int k = 0; k < g.n; k++) {
		ia = !strcmp(g.v[k].nome, "Ana Maria") ? k : ia;
	}
	CONFERE(ia >= 0 && g.v[ia].n == 3 && g.n == 6, "artistas: Ana Maria com 3 faixas, 6 no total (%d)", g.n);
	mu_grupos_liberar(&g);
	mu_pastas(&a, &g);
	CONFERE(g.n == 5, "pastas com música: 5 (%d)", g.n);
	mu_grupos_liberar(&g);

	/* busca sem acento e por várias palavras */
	int *idx = g_new(int, a.n);
	CONFERE(mu_buscar(&a, "cancao", idx) == 1 && !strcmp(a.v[idx[0]].titulo, "Canção do fim"), "buscar 'cancao' acha 'Canção'");
	CONFERE(mu_buscar(&a, "ENTRE orbitas", idx) == 1, "maiúsculas e acento não importam");
	CONFERE(mu_buscar(&a, "ana luz", idx) == 3, "duas palavras: artista e álbum");
	CONFERE(mu_buscar(&a, "xyzxyz", idx) == 0, "nada");
	CONFERE(mu_buscar(&a, "", idx) == a.n, "busca vazia traz tudo");
	g_free(idx);
	char *n = mu_normalizar("Ação Ünï");
	CONFERE(!strcmp(n, "acao uni"), "normalizar: %s", n);
	g_free(n);

	/* capas */
	i = acha(&a, "Aurora lenta");
	char *cp = mu_capa_pronta(&a.v[i]);
	CONFERE(cp && g_str_has_suffix(cp, "cover.jpg"), "a capa da pasta (cover.jpg)");
	g_free(cp);
	i = acha(&a, "Entre órbitas");
	cp = mu_capa_pronta(&a.v[i]);
	CONFERE(!cp, "a capa embutida ainda não foi gerada");
	cp = mu_capa_gerar(&a.v[i]);
	CONFERE(cp && g_file_test(cp, G_FILE_TEST_IS_REGULAR), "gerou a capa embutida");
	if (cp) {
		struct stat st;
		CONFERE(stat(cp, &st) == 0 && st.st_size > 100, "a capa tem conteúdo");
	}
	char *cp2 = mu_capa_pronta(&a.v[i]);
	CONFERE(cp2 && cp && !strcmp(cp, cp2), "agora ela está pronta");
	CONFERE(mu_capas_tamanho() > 0, "o cache de capas pesa");
	g_free(cp);
	g_free(cp2);
	i = acha(&a, "Instantes");
	CONFERE(!mu_capa_gerar(&a.v[i]), "faixa sem capa: nada");
	mu_capas_limpar();
	CONFERE(mu_capas_tamanho() == 0, "limpar o cache de capas");

	/* letras */
	mu_letra l;
	i = acha(&a, "Entre órbitas");
	CONFERE(mu_letra_ler(a.v[i].caminho, &l) && l.sincronizada && l.n == 4, "o .lrc ao lado: 4 linhas sincronizadas (%d)", l.n);
	if (l.n == 4) {
		CONFERE(l.v[0].tempo < 0.3 && !strcmp(l.v[0].texto, "Entre órbitas eu te encontro"), "ordenadas por tempo (a primeira, em 0,2 s)");
		CONFERE(!strcmp(l.v[3].texto, "Entre órbitas eu te encontro") && l.v[3].tempo > 2.8, "o carimbo duplo repete a linha");
		CONFERE(mu_letra_linha(&l, 0) == 0 && mu_letra_linha(&l, 1.0) == 1 && mu_letra_linha(&l, 0.1) == 0 && mu_letra_linha(&l, 99) == 3, "a linha da vez em cada instante");
	}
	mu_letra_liberar(&l);
	i = acha(&a, "Maré noturna");
	CONFERE(mu_letra_ler(a.v[i].caminho, &l) && !l.sincronizada && l.n == 4 && !strcmp(l.v[0].texto, "Primeira linha da tag") && l.v[2].texto[0] == 0, "a tag sem tempo: texto inteiro, com a linha vazia (%d linhas)", l.n);
	CONFERE(mu_letra_linha(&l, 1) == -1, "sem tempo, nenhuma linha da vez");
	mu_letra_liberar(&l);
	i = acha(&a, "Aurora lenta");
	CONFERE(mu_letra_ler(a.v[i].caminho, &l) && l.sincronizada && l.n == 2 && l.v[1].tempo == 1.5, "a tag com tempo (FLAC): sincronizada");
	mu_letra_liberar(&l);
	i = acha(&a, "Instantes");
	CONFERE(!mu_letra_ler(a.v[i].caminho, &l) && l.n == 0, "sem letra: falso");
	CONFERE(mu_letra_de_texto("[01:02.5]a\n[1:03]b\n[00:01:02]c", &l) && l.n == 3 && l.v[0].tempo == 62.0 && !strcmp(l.v[0].texto, "c") && l.v[1].tempo == 62.5 && l.v[2].tempo == 63.0, "carimbos de formatos variados (mm:ss.x, m:ss, hh:mm:ss)");
	mu_letra_liberar(&l);
	CONFERE(mu_letra_de_texto("[00:05.00]<00:05.10>pa<00:05.50>lavra", &l) && !strcmp(l.v[0].texto, "palavra"), "marcas de palavra saem do texto");
	mu_letra_liberar(&l);
	CONFERE(mu_letra_de_texto("[offset:+500]\n[00:05.00]x", &l) && l.v[0].tempo > 4.4 && l.v[0].tempo < 4.6, "offset (500 ms adiantam) = 4,5 s (%.2f)", l.n ? l.v[0].tempo : 0);
	mu_letra_liberar(&l);
	CONFERE(!mu_letra_de_texto("", &l) && !mu_letra_de_texto("   \n  ", &l) && !mu_letra_de_texto(NULL, &l), "texto vazio não é letra");

	/* playlists */
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	mu_playlists_listar(nomes);
	CONFERE(nomes->len == 1 && !strcmp(nomes->pdata[0], "Favoritas"), "só a Favoritas no começo");
	CONFERE(mu_playlist_criar("Viagens") && mu_playlist_criar("Foco") && !mu_playlist_criar("Foco") && !mu_playlist_criar("favoritas"), "criar; repetida e 'favoritas' são recusadas");
	CONFERE(!mu_playlist_criar("a/b") && !mu_playlist_criar("") && !mu_playlist_criar(".x") && !mu_playlist_criar("   "), "nomes ruins são recusados");
	g_ptr_array_set_size(nomes, 0);
	mu_playlists_listar(nomes);
	CONFERE(nomes->len == 3 && !strcmp(nomes->pdata[0], "Favoritas") && !strcmp(nomes->pdata[1], "Foco") && !strcmp(nomes->pdata[2], "Viagens"), "Favoritas primeiro, o resto por nome");
	char *f1 = a.v[0].caminho, *f2 = a.v[1].caminho;
	char *dois[] = { f1, f2, f1 };
	CONFERE(mu_playlist_adicionar("Foco", dois, 3), "adicionar");
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	mu_playlist_ler("Foco", v);
	CONFERE(v->len == 2 && !strcmp(v->pdata[0], f1) && !strcmp(v->pdata[1], f2), "sem repetir, na ordem (%u)", v->len);
	CONFERE(mu_playlist_tem("Foco", f2) && !mu_playlist_tem("Viagens", f2), "tem");
	CONFERE(mu_playlist_remover("Foco", f1), "remover");
	g_ptr_array_set_size(v, 0);
	mu_playlist_ler("Foco", v);
	CONFERE(v->len == 1 && !strcmp(v->pdata[0], f2), "ficou uma");
	CONFERE(mu_playlist_renomear("Foco", "Concentração") && !mu_playlist_renomear("Viagens", "Concentração") && !mu_playlist_renomear("Favoritas", "X"), "renomear (e não pisar numa existente nem renomear a Favoritas)");
	g_ptr_array_set_size(v, 0);
	CONFERE(mu_playlist_ler("Concentração", v) && v->len == 1, "a renomeada tem as faixas");
	CONFERE(mu_playlist_apagar("Concentração") && !mu_playlist_apagar("Favoritas") && !mu_playlist_apagar("Concentração"), "apagar (Favoritas não)");
	g_ptr_array_set_size(v, 0);
	CONFERE(mu_playlist_ler("Favoritas", v) && v->len == 0, "a Favoritas vazia lê");
	char *um[] = { f1 };
	mu_playlist_adicionar("Favoritas", um, 1);
	CONFERE(mu_playlist_tem("Favoritas", f1), "favoritar");
	/* nomes com acento e espaço */
	CONFERE(mu_playlist_criar("Rock dos anos 80 ♪") && mu_playlist_apagar("Rock dos anos 80 ♪"), "nome com espaços e símbolos");
	g_ptr_array_free(v, TRUE);
	g_ptr_array_free(nomes, TRUE);

	/* a fila salva */
	mu_fila_salva fs;
	CONFERE(!mu_fila_ler(&fs), "sem fila salva, nada");
	char *tres[] = { f1, f2, a.v[2].caminho };
	mu_fila_gravar(tres, 3, 1, 12.5);
	CONFERE(mu_fila_ler(&fs) && fs.caminhos->len == 3 && fs.atual == 1 && fs.posicao == 12.5 && !strcmp(fs.caminhos->pdata[2], a.v[2].caminho), "a fila volta (3 faixas, a 2ª, 12,5 s)");
	mu_fila_liberar(&fs);
	mu_fila_gravar(tres, 3, 99, 0);
	CONFERE(mu_fila_ler(&fs) && fs.atual == 2, "o índice é limitado ao tamanho");
	mu_fila_liberar(&fs);

	/* um arquivo avulso e os que não são áudio */
	mu_faixa f;
	char *avulso = g_build_filename(pasta, "sem tags.wav", NULL);
	CONFERE(mu_ler_faixa(avulso, &f) && f.duracao == 2, "ler uma faixa avulsa");
	mu_faixa_liberar(&f);
	g_free(avulso);
	char *txt = g_build_filename(pasta, "notas.txt", NULL), *ruim = g_build_filename(pasta, "quebrada.mp3", NULL);
	CONFERE(!mu_ler_faixa(txt, &f) && !mu_ler_faixa(ruim, &f) && !mu_ler_faixa("/nao/existe.mp3", &f), "texto, arquivo quebrado e inexistente não são faixas");
	g_free(txt);
	g_free(ruim);

	/* textos */
	char t[32];
	mu_duracao_texto(248, t, sizeof(t)); CONFERE(!strcmp(t, "4:08"), "4:08 (%s)", t);
	mu_duracao_texto(3730, t, sizeof(t)); CONFERE(!strcmp(t, "1:02:10"), "1:02:10 (%s)", t);
	mu_duracao_longa(1560, t, sizeof(t)); CONFERE(!strcmp(t, "26 min"), "26 min (%s)", t);
	mu_duracao_longa(4320, t, sizeof(t)); CONFERE(!strcmp(t, "1 h 12 min"), "1 h 12 min (%s)", t);
	mu_duracao_longa(20, t, sizeof(t)); CONFERE(!strcmp(t, "1 min"), "menos de um minuto vira 1 min (%s)", t);

	mu_acervo_liberar(&a);
	g_free(pasta);
	if (falhas) {
		fprintf(stderr, "musica: %d falha(s)\n", falhas);
		return 1;
	}
	printf("musica (serviço): ok\n");
	return 0;
}
