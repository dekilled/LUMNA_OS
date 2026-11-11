/* O catálogo e a conta da GOG (servicos/loja_gog.c) contra um servidor local com respostas reais gravadas e um gogdl de mentira. */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "loja_gog.h"

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

	int n = lg_destaques(l, 24, erro, sizeof(erro));
	CONFERE(n >= 10 && l[0].loja == LS_LOJA_GOG && l[0].appid > 0 && l[0].nome[0] && l[0].capa[0], "os destaques da GOG com nome, id e capa");
	CONFERE(strncmp(l[0].preco, "R$ ", 3) == 0 && !strstr(l[0].preco, "\xC2\xA0"), "o preço em reais, sem o espaço inseparável");
	CONFERE(strstr(l[0].capa, "127.0.0.1") != NULL, "as imagens vêm do servidor de teste (a URL da GOG é trocada)");
	CONFERE(l[0].generos[0] && strstr(l[0].generos, "Action"), "os gêneros da lista");
	bool linux_visto = false, desc = false;
	for (int i = 0; i < n; i++) {
		linux_visto |= l[i].linux_nativo;
	}
	n = lg_ofertas(l, 12, erro, sizeof(erro));
	for (int i = 0; i < n; i++) {
		desc |= l[i].desconto > 0 && l[i].preco_antes[0];
	}
	CONFERE(n >= 5 && desc, "as ofertas trazem o desconto e o preço de antes");
	(void)linux_visto;

	/* busca e filtros */
	struct ls_filtro f = { 0 };
	g_strlcpy(f.termo, "witcher & cia", sizeof(f.termo));
	f.genero = LS_GEN_RPG;
	f.linux_nativo = true;
	f.gratis = true;
	f.cooperativo = true;
	f.ordem = 2;
	int total = 0;
	n = lg_buscar(&f, l, 12, &total, erro, sizeof(erro));
	CONFERE(n >= 1 && total >= 1, "a busca devolve jogos e o total");
	char *lg = le(log);
	CONFERE(strstr(lg, "query=like:witcher%20%26%20cia") && strstr(lg, "genres=in:rpg") && strstr(lg, "systems=in:linux") && strstr(lg, "price=between:0,0") && strstr(lg, "features=in:coop") && strstr(lg, "order=desc:releaseDate"),
		"os filtros viram os parâmetros do catálogo (texto escapado, RPG, Linux, grátis, cooperativo, lançamento)");
	g_free(lg);
	struct ls_filtro indie = { .genero = LS_GEN_INDIE };
	lg_buscar(&indie, l, 12, &total, erro, sizeof(erro));
	lg = le(log);
	CONFERE(strstr(lg, "tags=in:indie") != NULL, "Indie vira uma tag (a GOG não tem esse gênero)");
	g_free(lg);

	/* os gêneros novos e os filtros novos */
	struct ls_filtro novo = { .genero = LS_GEN_TIRO, .multijogador = true, .controle = true, .promocao = true };
	lg_buscar(&novo, l, 12, &total, erro, sizeof(erro));
	lg = le(log);
	CONFERE(strstr(lg, "genres=in:shooter") && strstr(lg, "features=in:multi,controller_support") && strstr(lg, "discounted=eq:true"), "Tiro, multijogador, controle e promoção viram parâmetros do catálogo");
	g_free(lg);
	struct ls_filtro terror = { .genero = LS_GEN_TERROR };
	lg_buscar(&terror, l, 12, &total, erro, sizeof(erro));
	lg = le(log);
	CONFERE(strstr(lg, "tags=in:horror") != NULL, "Terror vira uma etiqueta da GOG");
	g_free(lg);
	CONFERE(!strcmp(ls_gen_nome(LS_GEN_TODOS), "Todas") && LS_GEN_N >= 15, "a lista de categorias tem Todas e as novas");

	/* detalhes */
	static struct ls_detalhe d;
	CONFERE(lg_detalhes(1207658930, &d, erro, sizeof(erro)), "detalhes de The Witcher 2");
	CONFERE(strstr(d.nome, "Witcher 2") && d.resumo[0] && d.sobre[0] && !strchr(d.sobre, '<'), "nome, resumo e sobre sem HTML");
	CONFERE(d.ntelas >= 3 && strstr(d.telas[0], "127.0.0.1") && strstr(d.telas[0], "ggvgm"), "os prints, no tamanho pedido");
	CONFERE(strstr(d.trailer, "wistia") != NULL, "o trailer (uma página: o mpv toca pelo yt-dlp)");
	CONFERE(!strcmp(d.preco, "R$ 59,99") && !d.gratis, "o preço em reais pelo /prices");
	CONFERE(d.linux_nativo && d.windows, "Linux nativo e Windows");
	CONFERE(!lg_detalhes(1, &d, erro, sizeof(erro)) && !d.existe, "um jogo que a GOG não tem");

	/* a conta */
	CONFERE(!lg_logado() && lg_possuidos((int[4]){ 0 }, 4, erro, sizeof(erro)) < 0 && strstr(erro, "Entre"), "sem login: pede para entrar");
	CONFERE(!lg_login("curto", erro, sizeof(erro)) && strstr(erro, "código"), "um código curto é recusado");
	CONFERE(!lg_login("abcdefghij123; rm -rf /", erro, sizeof(erro)), "um código com lixo é recusado (nada vai à linha de comando)");
	CONFERE(!lg_login("ruim12345678", erro, sizeof(erro)) && !lg_logado(), "o gogdl recusa o código: continua sem login");
	CONFERE(lg_login("https://embed.gog.com/on_login_success?origin=client&code=ABCDEFGHIJ1234567&state=x", erro, sizeof(erro)) && lg_logado(), "a URL inteira vale: o código sai dela e o gogdl guarda o login");
	lg = le(log);
	CONFERE(strstr(lg, "gogdl --auth-config-path") && strstr(lg, "auth --code ABCDEFGHIJ1234567") != NULL, "o gogdl recebeu só o código");
	g_free(lg);
	char *a = g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "gog", "auth.json", NULL);
	struct stat st;
	CONFERE(stat(a, &st) == 0 && (st.st_mode & 0777) == 0600, "o login fica num arquivo 600");
	g_free(a);
	int ids[10];
	n = lg_possuidos(ids, 10, erro, sizeof(erro));
	CONFERE(n == 2 && ids[0] == 1207658930, "os jogos da conta (com o token do gogdl)");

	/* o progresso do gogdl */
	CONFERE(lg_progresso_da_linha("Progress: 12.34 5/100") == 12 && lg_progresso_da_linha("downloading 45.6% done") == 45 && lg_progresso_da_linha("sem número") == -1 && lg_progresso_da_linha("Progress: 250") == 100, "ler a linha de progresso");

	/* instalar (o gogdl de mentira baixa em etapas e cria um start.sh) */
	char *pasta = g_build_filename(g_get_home_dir(), "Jogos", NULL);
	CONFERE(!lg_instalar(5, "X", "relativa", true, erro, sizeof(erro)), "pasta relativa é recusada");
	CONFERE(lg_instalar_agora(1207658930, "The Witcher 2: Assassins/Kings", pasta, true) == 0, "o gogdl baixa até o fim");
	struct lg_prog p;
	CONFERE(lg_progresso(1207658930, &p) && p.pronto && p.pct == 100, "o estado vira pronto");
	char *start = g_build_filename(pasta, "The Witcher 2 AssassinsKings", "start.sh", NULL);
	CONFERE(g_file_test(start, G_FILE_TEST_IS_REGULAR), "o jogo foi para a pasta (nome sem caracteres perigosos)");
	g_free(start);
	char *reg = g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "gog_instalados.json", NULL), *rt = le(reg);
	CONFERE(strstr(rt, "1207658930") && strstr(rt, "Witcher") && strstr(rt, "\"linux\":true"), "o jogo foi registrado");
	g_free(rt);
	g_free(reg);
	lg = le(log);
	CONFERE(strstr(lg, "download 1207658930 --platform linux --path ") && strstr(lg, "--with-dlcs"), "gogdl download com plataforma e pasta");
	g_free(lg);
	CONFERE(lg_instalar_agora(999, "Quebra", pasta, false) != 0 && lg_progresso(999, &p) && p.erro && strstr(p.msg, "falhou"), "um gogdl que falha vira estado de erro, sem registrar");
	g_free(pasta);
	lg_sair();
	CONFERE(!lg_logado(), "sair apaga o login");

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("loja gog (catálogo e conta): ok\n");
	return 0;
}
