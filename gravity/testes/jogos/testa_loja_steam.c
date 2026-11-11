/* O catálogo da Steam (servicos/loja_steam.c) contra um servidor local com respostas reais gravadas. */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "loja_steam.h"

static int falhas;
#define CONFERE(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

static char *le(const char *p) {
	char *t = NULL;
	return g_file_get_contents(p, &t, NULL, NULL) ? t : g_strdup("");
}

int main(void) {
	char erro[200] = "";
	static struct ls_item l[LS_MAX];
	const char *log = g_getenv("LOG_FALSO");

	/* destaques: ofertas, mais vendidos, lançamentos */
	int n = ls_ofertas(l, LS_MAX, erro, sizeof(erro));
	CONFERE(n > 0 && l[0].appid > 0 && l[0].nome[0] && l[0].preco[0] && l[0].capa[0], "as ofertas trazem jogos com nome, preço e capa");
	CONFERE(l[0].desconto > 0 && l[0].preco_antes[0] && strncmp(l[0].preco, "R$ ", 3) == 0, "a oferta tem desconto e o preço de antes, em reais");
	CONFERE(strstr(l[0].fundo, "library_hero") != NULL, "o fundo largo vem do library_hero");
	CONFERE(ls_mais_vendidos(l, LS_MAX, erro, sizeof(erro)) > 0 && ls_lancamentos(l, LS_MAX, erro, sizeof(erro)) > 0, "mais vendidos e lançamentos");
	/* o cache: a segunda chamada não vai à rede */
	char *antes = le(log);
	ls_ofertas(l, LS_MAX, erro, sizeof(erro));
	char *depois = le(log);
	CONFERE(!strcmp(antes, depois), "a lista de destaques vem do cache na segunda vez");
	g_free(antes);
	g_free(depois);

	/* busca e filtros */
	struct ls_filtro f = { 0 };
	f.genero = LS_GEN_ACAO;
	f.linux_nativo = true;
	f.gratis = true;
	f.um_jogador = true;
	f.ordem = 1;
	int total = 0;
	n = ls_buscar(&f, l, LS_MAX, &total, erro, sizeof(erro));
	CONFERE(n >= 5 && total > 100, "a busca devolve jogos e o total");
	CONFERE(l[0].appid > 0 && l[0].nome[0] && !strchr(l[0].nome, '<') && l[0].preco[0], "cada resultado tem id, nome sem HTML e preço");
	bool algum_linux = false;
	for (int i = 0; i < n; i++) {
		algum_linux |= l[i].linux_nativo;
	}
	CONFERE(algum_linux, "os resultados marcam o Linux nativo");
	char *lg = le(log);
	CONFERE(strstr(lg, "tags=19") && strstr(lg, "os=linux") && strstr(lg, "maxprice=free") && strstr(lg, "category3=2") && strstr(lg, "filter=topsellers"), "os filtros viram os parâmetros da loja (gênero Ação, Linux, grátis, um jogador, mais vendidos)");
	g_free(lg);
	struct ls_filtro vazio = { 0 };
	g_strlcpy(vazio.termo, "nada-existe", sizeof(vazio.termo));
	CONFERE(ls_buscar(&vazio, l, LS_MAX, &total, erro, sizeof(erro)) == 0 && total == 0, "busca sem resultado: 0");
	struct ls_filtro com_termo = { 0 };
	g_strlcpy(com_termo.termo, "o jogo & cia \"x\"", sizeof(com_termo.termo));
	ls_buscar(&com_termo, l, LS_MAX, &total, erro, sizeof(erro));
	lg = le(log);
	CONFERE(strstr(lg, "term=o%20jogo%20%26%20cia%20%22x%22") != NULL, "o termo da busca é escapado na url");
	g_free(lg);

	/* detalhes */
	static struct ls_detalhe d;
	CONFERE(ls_detalhes(620, &d, erro, sizeof(erro)), "detalhes do Portal 2");
	CONFERE(!strcmp(d.nome, "Portal 2") && d.resumo[0] && d.sobre[0] && !strchr(d.sobre, '<') && !strstr(d.sobre, "&amp;"), "nome, resumo e sobre (sem HTML nem entidades)");
	CONFERE(d.ntelas >= 5 && strncmp(d.telas[0], "http", 4) == 0, "os prints");
	CONFERE(strstr(d.trailer, "http") != NULL && d.trailer_capa[0], "o trailer e a capa dele");
	CONFERE(!strcmp(d.preco, "R$ 33,99") && d.desconto == 0 && !d.gratis, "o preço em reais");
	CONFERE(d.linux_nativo && d.windows && d.um_jogador && d.cooperativo && d.controle, "Linux nativo, um jogador, cooperativo e controle");
	CONFERE(d.requisitos[0] && !strchr(d.requisitos, '<'), "requisitos sem HTML");
	CONFERE(strstr(d.generos, "Ação") != NULL && d.desenvolvedor[0] && d.lancamento[0], "gêneros, desenvolvedora e lançamento");
	CONFERE(!ls_detalhes(999999, &d, erro, sizeof(erro)) && !d.existe && erro[0], "um jogo que a loja não tem");

	/* ProtonDB */
	char nivel[16];
	CONFERE(ls_proton(620, nivel, sizeof(nivel)) && !strcmp(nivel, "platinum"), "o nível do ProtonDB");
	CONFERE(!ls_proton(1, nivel, sizeof(nivel)) && !nivel[0], "jogo sem relatório no ProtonDB");
	CONFERE(strstr(ls_proton_texto("gold"), "bem") && !*ls_proton_texto(""), "texto do nível");

	/* a conta: a chave */
	int ids[10];
	CONFERE(!ls_chave_existe() && ls_possuidos(ids, 10, erro, sizeof(erro)) < 0 && strstr(erro, "chave"), "sem chave: pede a chave");
	CONFERE(!ls_chave_gravar("curto", "1"), "chave curta é recusada");
	CONFERE(!ls_chave_gravar("ABCDEF0123456789ABCDEF0123456789\"; rm -rf", "76561198000000000"), "chave com lixo é recusada (não vai à url nem ao arquivo)");
	CONFERE(!ls_chave_gravar("ABCDEF0123456789ABCDEF0123456789", "7656; x"), "id com lixo é recusado");
	CONFERE(ls_chave_gravar("ABCDEF0123456789ABCDEF0123456789", "76561198000000000"), "grava a chave");
	char *ini = g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "steam.ini", NULL);
	struct stat st;
	CONFERE(stat(ini, &st) == 0 && (st.st_mode & 0777) == 0600, "o arquivo da chave é 0600");
	g_free(ini);
	char id64[24];
	ls_chave_ler(id64, sizeof(id64));
	CONFERE(!strcmp(id64, "76561198000000000"), "o id volta, a chave nunca sai");
	n = ls_possuidos(ids, 10, erro, sizeof(erro));
	CONFERE(n == 2 && ids[0] == 620 && ids[1] == 730, "os jogos da conta");
	ls_chave_gravar("0000000000000000000000000000FFFF", "76561198000000000");
	n = ls_possuidos(ids, 10, erro, sizeof(erro));
	CONFERE(n < 0 || n == 2, "(a resposta do cache vale por um tempo)");
	lg = le(log);
	CONFERE(strstr(lg, "GetOwnedGames") != NULL, "a API da Steam foi chamada");
	g_free(lg);

	/* o id do usuário logado no cliente */
	ls_id_local(id64, sizeof(id64));
	CONFERE(!strcmp(id64, "76561198111111111"), "o id64 do usuário mais recente do loginusers.vdf");

	/* imagens */
	char *img = ls_imagem("http://127.0.0.1:1/x/capa.jpg");
	CONFERE(img == NULL, "imagem que não baixa devolve NULL (sem travar)");
	char url[200];
	g_snprintf(url, sizeof(url), "%s/steam/apps/620/capsule_616x353.jpg", g_getenv("LUMNA_LOJA_STEAM_URL"));
	img = ls_imagem(url);
	CONFERE(img && g_file_test(img, G_FILE_TEST_IS_REGULAR), "a imagem vai para o cache");
	char *img2 = ls_imagem(url);
	CONFERE(img2 && !strcmp(img, img2), "e a segunda vez é o mesmo arquivo");
	g_free(img);
	g_free(img2);

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("loja steam (catálogo): ok\n");
	return 0;
}
