/* O serviço da Galeria (servicos/galeria.c) numa biblioteca de mentira (fixtures.py): ajustes, EXIF, leitura, ordem, filtros, grupos, favoritos, álbuns, miniaturas.
 * ./testa_galeria CASA   (CASA com HOME e XDG_* apontando para dentro dela) */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "galeria.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { falhas++; fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static int acha(const gl_acervo *a, const char *nome) {
	for (int i = 0; i < a->n; i++) {
		if (!strcmp(a->v[i].nome, nome)) {
			return i;
		}
	}
	return -1;
}

static gint pronto_n;
static void pronta(void *d) {
	(void)d;
	g_atomic_int_inc(&pronto_n);
}

static const char *espera_miniatura(const gl_midia *m, int q) {
	for (int i = 0; i < 300; i++) {
		const char *c = gl_miniatura(m, q, pronta, NULL);
		if (c) {
			return c;
		}
		if (gl_miniatura_falhou(m)) {
			return NULL;
		}
		g_usleep(50 * 1000);
	}
	return NULL;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		return 2;
	}
	const char *casa = argv[1];
	gl_cfg c;
	gl_cfg_padrao(&c);
	CONFERE(c.npastas == 3, "as três pastas da biblioteca (%d)", c.npastas);
	CONFERE(strstr(c.pastas[2], "Capturas") != NULL, "a terceira é a das Capturas: %s", c.pastas[2]);
	CONFERE(c.agrupar == GL_AGRUPAR_CAPTURA && c.qualidade == GL_QUAL_EQUILIBRADA && c.buscar_auto && c.subpastas && c.manter_original, "padrões da biblioteca");

	/* ---- ajustes: gravar e ler ---- */
	c.npastas = 2;
	g_snprintf(c.pastas[0], sizeof(c.pastas[0]), "%s/Imagens", casa);
	g_snprintf(c.pastas[1], sizeof(c.pastas[1]), "%s/Vídeos", casa);
	c.tam = 200;
	c.agrupar = GL_AGRUPAR_PASTA;
	c.volume = 33;
	c.formato = GL_FORMATO_PNG;
	CONFERE(gl_cfg_gravar(&c), "gravar os ajustes");
	gl_cfg l;
	gl_cfg_ler(&l);
	CONFERE(l.npastas == 2 && !strcmp(l.pastas[1], c.pastas[1]) && l.tam == 200 && l.agrupar == GL_AGRUPAR_PASTA && l.volume == 33 && l.formato == GL_FORMATO_PNG,
		"os ajustes voltam como foram gravados");
	l.tam = 99999;
	l.npastas = 0;
	gl_cfg_gravar(&l);
	gl_cfg_ler(&l);
	CONFERE(l.tam == 260 && l.npastas == 0, "fora da faixa vira o limite e uma lista vazia de pastas vale (%d, %d)", l.tam, l.npastas);
	/* o player: o padrão é o da Galeria; o ajuste antigo (video_mpv) migra; o novo vale */
	gl_cfg_padrao(&c);
	CONFERE(c.video_com == GL_VIDEO_GALERIA, "o vídeo abre no player da Galeria por padrão (%d)", c.video_com);
	{
		char *ini = g_build_filename(g_get_user_config_dir(), "lumna", "galeria", "config.ini", NULL);
		const char *v[][2] = { { "video_mpv=true", "" }, { "video_mpv=false", "" }, { "video_mpv=true\nvideo_com=0", "" }, { "video_com=2", "" }, { "video_com=9", "" } };
		int esperado[] = { GL_VIDEO_MPV, GL_VIDEO_PADRAO, GL_VIDEO_GALERIA, GL_VIDEO_PADRAO, GL_VIDEO_PADRAO };
		for (int i = 0; i < 5; i++) {
			char *txt = g_strdup_printf("[galeria]\n%s\n", v[i][0]);
			g_file_set_contents(ini, txt, -1, NULL);
			g_free(txt);
			gl_cfg x;
			gl_cfg_ler(&x);
			CONFERE(x.video_com == esperado[i] || (i == 4 && x.video_com == GL_VIDEO_PADRAO), "migração/leitura do ajuste de vídeo %s → %d (esperado %d)", v[i][0], x.video_com, esperado[i]);
		}
		g_free(ini);
	}
	gl_cfg_ler(&c); /* de novo com as pastas vazias */
	gl_cfg_padrao(&c);
	c.npastas = 2;
	g_snprintf(c.pastas[0], sizeof(c.pastas[0]), "%s/Imagens", casa);
	g_snprintf(c.pastas[1], sizeof(c.pastas[1]), "%s/Vídeos", casa);

	/* ---- EXIF ---- */
	char *lago = g_build_filename(casa, "Imagens", "lago.jpg", NULL);
	time_t t = 0;
	CONFERE(gl_data_exif(lago, &t), "lê o EXIF do lago.jpg");
	struct tm tm;
	localtime_r(&t, &tm);
	CONFERE(tm.tm_year == 126 && tm.tm_mon == 9 && tm.tm_mday == 5 && tm.tm_hour == 12 && tm.tm_min == 30, "a data do EXIF: %04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
	char *hoje_jpg = g_build_filename(casa, "Imagens", "hoje.jpg", NULL);
	CONFERE(!gl_data_exif(hoje_jpg, &t), "sem EXIF, falso");
	CONFERE(!gl_data_exif("/nao/existe.jpg", &t), "arquivo que não existe, falso");

	/* ---- leitura ---- */
	gl_acervo a;
	c.agrupar = GL_AGRUPAR_CAPTURA;
	gl_varrer(&c, &a);
	CONFERE(acha(&a, "lago.jpg") >= 0 && acha(&a, "hoje.jpg") >= 0 && acha(&a, "ontem.png") >= 0 && acha(&a, "vidro.png") >= 0 && acha(&a, "praia.jpg") >= 0 && acha(&a, "clipe.mp4") >= 0,
		"as mídias da biblioteca (e as da subpasta)");
	CONFERE(acha(&a, "notas.txt") < 0 && acha(&a, ".oculta.jpg") < 0, "o que não é mídia e o oculto ficam de fora");
	CONFERE(acha(&a, "captura.png") < 0, "a pasta Capturas não estava na lista desta vez");
	int ic = acha(&a, "clipe.mp4");
	CONFERE(ic >= 0 && a.v[ic].tipo == GL_VIDEO && acha(&a, "lago.jpg") >= 0 && a.v[acha(&a, "lago.jpg")].tipo == GL_FOTO, "o tipo de cada um");
	int il = acha(&a, "lago.jpg");
	localtime_r(&a.v[il].data, &tm);
	CONFERE(tm.tm_mon == 9 && tm.tm_mday == 5, "a data da foto com EXIF é a da captura, não a do arquivo");
	int ntodas = a.n;
	gl_acervo_liberar(&a);
	c.subpastas = false;
	gl_varrer(&c, &a);
	CONFERE(acha(&a, "praia.jpg") < 0 && a.n == ntodas - 1, "sem subpastas a da Viagem some (%d de %d)", a.n, ntodas);
	gl_acervo_liberar(&a);
	c.subpastas = true;
	/* uma pasta dentro da outra não duplica */
	c.npastas = 3;
	g_snprintf(c.pastas[2], sizeof(c.pastas[2]), "%s/Imagens/Viagem", casa);
	gl_varrer(&c, &a);
	CONFERE(a.n == ntodas, "a mesma mídia por duas pastas entra uma vez só (%d, esperava %d)", a.n, ntodas);
	gl_acervo_liberar(&a);
	c.npastas = 2;

	/* ---- ordem ---- */
	gl_varrer(&c, &a);
	gl_acervo_ordenar(&a, GL_ORDEM_RECENTES, GL_AGRUPAR_CAPTURA);
	CONFERE(a.v[0].data >= a.v[a.n - 1].data, "mais recentes primeiro");
	gl_acervo_ordenar(&a, GL_ORDEM_ANTIGAS, GL_AGRUPAR_CAPTURA);
	CONFERE(a.v[0].data <= a.v[a.n - 1].data, "mais antigas primeiro");
	gl_acervo_ordenar(&a, GL_ORDEM_NOME, GL_AGRUPAR_CAPTURA);
	CONFERE(g_utf8_collate(a.v[0].nome, a.v[1].nome) <= 0, "por nome");
	gl_acervo_ordenar(&a, GL_ORDEM_TAMANHO, GL_AGRUPAR_CAPTURA);
	CONFERE(a.v[0].tamanho >= a.v[a.n - 1].tamanho, "as maiores primeiro");
	gl_acervo_ordenar(&a, GL_ORDEM_RECENTES, GL_AGRUPAR_PASTA);
	{
		char *d0 = g_path_get_dirname(a.v[0].caminho), *dn = g_path_get_dirname(a.v[a.n - 1].caminho);
		CONFERE(g_utf8_collate(d0, dn) <= 0, "agrupando por pasta, a pasta vem primeiro");
		g_free(d0);
		g_free(dn);
	}
	gl_acervo_ordenar(&a, GL_ORDEM_RECENTES, GL_AGRUPAR_CAPTURA);

	/* ---- filtros ---- */
	int *idx = g_new(int, a.n);
	gl_filtro f = { .tipo = GL_F_TODAS };
	CONFERE(gl_filtrar(&a, &f, idx) == a.n, "todas");
	f.tipo = GL_F_FOTOS;
	int nf = gl_filtrar(&a, &f, idx);
	f.tipo = GL_F_VIDEOS;
	int nv = gl_filtrar(&a, &f, idx);
	CONFERE(nv == 1 && nf + nv == a.n, "fotos %d + vídeos %d = %d", nf, nv, a.n);
	f.tipo = GL_F_TODAS;
	f.busca = "LAGO";
	CONFERE(gl_filtrar(&a, &f, idx) == 1 && !strcmp(a.v[idx[0]].nome, "lago.jpg"), "a busca não distingue maiúsculas");
	f.busca = "ontém";
	CONFERE(gl_filtrar(&a, &f, idx) == 1, "a busca não distingue acentos");
	f.busca = NULL;
	f.tipo = GL_F_RECENTES;
	int nr = gl_filtrar(&a, &f, idx);
	CONFERE(nr >= 2 && nr < a.n, "recentes: os últimos 30 dias (%d de %d); a de outubro e a de setembro de 2026 não, hoje é outra data", nr, a.n);

	/* ---- grupos ---- */
	time_t agora = time(NULL);
	f.tipo = GL_F_TODAS;
	int n = gl_filtrar(&a, &f, idx);
	gl_grupo g[64];
	int ng = gl_agrupar(&a, idx, n, GL_AGRUPAR_CAPTURA, agora, g, 64);
	CONFERE(ng >= 3 && !strcmp(g[0].rotulo, "Hoje"), "o primeiro grupo é Hoje (%s, %d grupos)", g[0].rotulo, ng);
	bool ontem = false, mes = false;
	int soma = 0;
	for (int i = 0; i < ng; i++) {
		ontem |= !strcmp(g[i].rotulo, "Ontem");
		mes |= strstr(g[i].rotulo, " de 2026") != NULL;
		soma += g[i].n;
	}
	CONFERE(ontem && mes && soma == n, "Ontem, um mês de 2026 e todos contados (%d de %d)", soma, n);
	ng = gl_agrupar(&a, idx, n, GL_AGRUPAR_NENHUM, agora, g, 64);
	CONFERE(ng == 1 && g[0].rotulo[0] == '\0' && g[0].n == n, "sem agrupar: um grupo sem rótulo");
	gl_acervo_ordenar(&a, GL_ORDEM_RECENTES, GL_AGRUPAR_PASTA);
	n = gl_filtrar(&a, &f, idx);
	ng = gl_agrupar(&a, idx, n, GL_AGRUPAR_PASTA, agora, g, 64);
	CONFERE(ng == 3, "por pasta: Imagens, Viagem, Vídeos (%d)", ng);
	{
		bool viagem = false;
		for (int i = 0; i < ng; i++) {
			viagem |= !strcmp(g[i].rotulo, "Viagem");
		}
		CONFERE(viagem, "o grupo da pasta Viagem");
	}
	gl_acervo_ordenar(&a, GL_ORDEM_RECENTES, GL_AGRUPAR_CAPTURA);

	/* ---- favoritos ---- */
	CONFERE(!gl_fav_tem(lago), "nada é favorito no começo");
	gl_fav_definir(lago, true);
	CONFERE(gl_fav_tem(lago), "favoritou");
	gl_fav_marcar(&a);
	CONFERE(a.v[acha(&a, "lago.jpg")].favorita && !a.v[acha(&a, "hoje.jpg")].favorita, "marcar as favoritas no acervo");
	f.tipo = GL_F_FAVORITOS;
	CONFERE(gl_filtrar(&a, &f, idx) == 1, "o filtro de favoritos");
	char *arq_fav = g_build_filename(g_get_user_config_dir(), "lumna", "galeria", "favoritos.txt", NULL);
	char *txt = NULL;
	CONFERE(g_file_get_contents(arq_fav, &txt, NULL, NULL) && strstr(txt, "lago.jpg"), "gravou no favoritos.txt");
	g_free(txt);
	gl_fav_definir(lago, false);
	CONFERE(!gl_fav_tem(lago), "desfavoritou");

	/* ---- álbuns ---- */
	CONFERE(gl_album_criar("Viagens"), "criar um álbum");
	CONFERE(!gl_album_criar("Viagens"), "o nome repetido não cria");
	CONFERE(!gl_album_criar("  "), "o nome vazio não cria");
	CONFERE(!gl_album_criar("a[b]"), "colchetes não valem no nome");
	char *praia = g_build_filename(casa, "Imagens", "Viagem", "praia.jpg", NULL);
	char *lista[] = { lago, praia };
	CONFERE(gl_album_adicionar("Viagens", lista, 2) == 2, "pôs duas no álbum");
	CONFERE(gl_album_adicionar("Viagens", lista, 2) == 0, "as que já estão não entram de novo");
	CONFERE(gl_album_adicionar("Nao existe", lista, 2) == 0, "álbum que não existe não recebe");
	gl_albuns al;
	gl_albuns_ler(&al);
	CONFERE(al.n == 1 && !strcmp(al.v[0].nome, "Viagens") && al.v[0].caminhos->len == 2, "lê o álbum com as duas");
	f.tipo = GL_F_ALBUM;
	f.album_caminhos = (char *const *)al.v[0].caminhos->pdata;
	f.album_n = (int)al.v[0].caminhos->len;
	CONFERE(gl_filtrar(&a, &f, idx) == 2, "o filtro de álbum");
	gl_albuns_liberar(&al);
	gl_album_remover("Viagens", lago);
	gl_albuns_ler(&al);
	CONFERE(al.v[0].caminhos->len == 1, "tirou uma do álbum");
	gl_albuns_liberar(&al);
	CONFERE(gl_album_renomear("Viagens", "Férias") && !gl_album_renomear("Viagens", "Outro"), "renomear");
	gl_albuns_ler(&al);
	CONFERE(al.n == 1 && !strcmp(al.v[0].nome, "Férias") && al.v[0].caminhos->len == 1, "o renomeado leva o conteúdo");
	gl_albuns_liberar(&al);
	char *sumiu = g_build_filename(casa, "Imagens", "sumiu.jpg", NULL);
	char *com_sumido[] = { sumiu };
	gl_album_adicionar("Férias", com_sumido, 1);
	gl_fav_definir(sumiu, true);
	gl_limpar_orfaos();
	gl_albuns_ler(&al);
	CONFERE(al.v[0].caminhos->len == 1 && !gl_fav_tem(sumiu), "o que não existe mais sai dos álbuns e dos favoritos");
	gl_albuns_liberar(&al);
	gl_album_apagar("Férias");
	gl_albuns_ler(&al);
	CONFERE(al.n == 0, "apagou o álbum");
	gl_albuns_liberar(&al);

	/* ---- miniaturas ---- */
	gl_cache_limpar();
	CONFERE(gl_cache_tamanho() == 0, "cache vazio");
	const gl_midia *foto = &a.v[acha(&a, "lago.jpg")];
	const char *m1 = gl_miniatura(foto, GL_QUAL_EQUILIBRADA, pronta, NULL);
	CONFERE(m1 == NULL, "a primeira vez ainda não está pronta (não bloqueia)");
	m1 = espera_miniatura(foto, GL_QUAL_EQUILIBRADA);
	CONFERE(m1 && g_file_test(m1, G_FILE_TEST_EXISTS), "a miniatura da foto ficou pronta");
	CONFERE(g_atomic_int_get(&pronto_n) >= 1, "avisou que ficou pronta");
	if (m1) {
		GdkPixbuf *pb = gdk_pixbuf_new_from_file(m1, NULL);
		CONFERE(pb && gdk_pixbuf_get_width(pb) <= 320 && gdk_pixbuf_get_height(pb) <= 320 && gdk_pixbuf_get_width(pb) >= 200, "reduzida para o lado da qualidade");
		if (pb) {
			g_object_unref(pb);
		}
	}
	const gl_midia *png = &a.v[acha(&a, "vidro.png")];
	const char *mp = espera_miniatura(png, GL_QUAL_RAPIDA);
	CONFERE(mp && g_str_has_suffix(mp, ".png"), "a que tem transparência fica PNG (%s)", mp ? mp : "NULL");
	const gl_midia *clipe = &a.v[acha(&a, "clipe.mp4")];
	const char *mv = espera_miniatura(clipe, GL_QUAL_EQUILIBRADA);
	CONFERE(mv && g_file_test(mv, G_FILE_TEST_EXISTS), "a primeira cena do vídeo");
	int dur = gl_duracao_conhecida(clipe->caminho, clipe->mtime);
	CONFERE(dur == 3, "a duração do vídeo (%d)", dur);
	char txtd[16];
	gl_duracao_texto(dur, txtd, sizeof(txtd));
	CONFERE(!strcmp(txtd, "0:03"), "o texto da duração (%s)", txtd);
	gl_duracao_texto(3723, txtd, sizeof(txtd));
	CONFERE(!strcmp(txtd, "1:02:03"), "com horas (%s)", txtd);
	CONFERE(gl_video_duracao(clipe->caminho, clipe->mtime) == 3, "gl_video_duracao responde do cache");
	CONFERE(gl_cache_tamanho() > 0, "o cache pesa alguma coisa");
	/* o que não dá para fazer não fica tentando */
	gl_acervo estragados = { 0 };
	gl_varrer(&c, &estragados);
	const gl_midia *q = &estragados.v[acha(&estragados, "quebrada.jpg")];
	CONFERE(espera_miniatura(q, GL_QUAL_EQUILIBRADA) == NULL && gl_miniatura_falhou(q), "o JPEG estragado: sem miniatura e marcado como falho");
	const gl_midia *enorme = &estragados.v[acha(&estragados, "enorme.png")];
	CONFERE(espera_miniatura(enorme, GL_QUAL_EQUILIBRADA) == NULL && gl_miniatura_falhou(enorme), "o PNG de 81 megapixels: sem miniatura (a memória) e marcado como falho");
	gl_acervo_liberar(&estragados);
	gl_cache_limpar();
	CONFERE(gl_cache_tamanho() == 0, "limpar o cache esvazia");
	CONFERE(gl_miniatura(foto, GL_QUAL_EQUILIBRADA, pronta, NULL) == NULL, "depois de limpar, volta a fazer");
	gl_miniaturas_descartar();
	g_usleep(300 * 1000);

	/* ---- lixeira ---- */
	char *lixo = g_build_filename(g_get_user_data_dir(), "Trash", "files", NULL);
	g_mkdir_with_parents(lixo, 0700);
	char *foto_no_lixo = g_build_filename(lixo, "apagada.jpg", NULL);
	g_file_set_contents(foto_no_lixo, "x", 1, NULL);
	char *texto_no_lixo = g_build_filename(lixo, "apagado.txt", NULL);
	g_file_set_contents(texto_no_lixo, "x", 1, NULL);
	gl_acervo lx;
	gl_varrer_lixeira(&lx);
	CONFERE(lx.n == 1 && !strcmp(lx.v[0].nome, "apagada.jpg"), "a lixeira mostra só fotos e vídeos (%d)", lx.n);
	gl_acervo_liberar(&lx);

	g_free(idx);
	gl_acervo_liberar(&a);
	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("galeria (serviço): ok\n");
	return 0;
}
