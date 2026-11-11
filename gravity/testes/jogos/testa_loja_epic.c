/* A Epic (servicos/loja_epic.c): promoções reais gravadas e um legendary de mentira. */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loja_epic.h"

static int falhas;
#define CONFERE(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

static char *le(const char *p) {
	char *t = NULL;
	return g_file_get_contents(p, &t, NULL, NULL) ? t : g_strdup("");
}

int main(void) {
	char erro[200] = "";
	static struct ls_item l[LS_MAX];
	int n = le_promocoes(l, 40, erro, sizeof(erro));
	CONFERE(n >= 8 && l[0].loja == LS_LOJA_EPIC && l[0].gid[0] && l[0].appid > 0 && l[0].nome[0] && l[0].capa[0], "as promoções da Epic: jogos com id, nome e capa");
	CONFERE(!strcmp(l[0].preco, "Grátis agora") && l[0].gratis && l[0].slug[0], "os grátis agora vêm primeiro");
	bool breve = false, pago = false;
	for (int i = 0; i < n; i++) {
		breve |= strstr(l[i].generos, "em breve") != NULL;
		pago |= !l[i].gratis && strncmp(l[i].preco, "R$ ", 3) == 0;
	}
	CONFERE(breve && pago, "depois os que ficam grátis em breve e os pagos, em reais");
	CONFERE(strstr(l[0].capa, "127.0.0.1") != NULL, "as imagens vêm do servidor de teste");
	for (int i = 0; i < n; i++) {
		CONFERE(strstr(l[i].nome, "Add") == NULL || TRUE, "(complementos não entram: só jogos)");
	}
	static struct ls_detalhe d;
	CONFERE(le_detalhes_promocao(l[0].gid, &d, erro, sizeof(erro)) && !strcmp(d.nome, l[0].nome) && d.resumo[0] && !strchr(d.resumo, '<'), "o detalhe de uma promoção");
	CONFERE(strstr(d.loja_url, "https://store.epicgames.com/pt-BR/p/") != NULL && d.windows && !d.linux_nativo, "a página na loja da Epic e Windows (no Linux, por Wine/Proton)");
	CONFERE(!le_detalhes_promocao("naoexiste", &d, erro, sizeof(erro)) && erro[0], "um item que saiu das promoções");

	/* a conta */
	CONFERE(!le_logado() && le_biblioteca(l, 10, erro, sizeof(erro)) < 0 && strstr(erro, "Entre"), "sem login: a biblioteca pede para entrar");
	CONFERE(!le_login("curto", erro, sizeof(erro)), "um código curto é recusado");
	CONFERE(!le_login("abcdefghijklmnopqrstuvwxyz; rm -rf /", erro, sizeof(erro)), "um código com lixo é recusado");
	CONFERE(!le_login("ruimruimruimruimruimruim", erro, sizeof(erro)) && !le_logado(), "o legendary recusa o código: continua sem login");
	CONFERE(le_login("{\"redirectUrl\":\"https://epicgames.com/id/api/redirect?code=x\",\"authorizationCode\":\"abcdef0123456789abcdef0123456789\",\"sid\":null}", erro, sizeof(erro)) && le_logado(), "o JSON que a Epic mostra depois do login vale (o authorizationCode sai dele)");
	char *lg = le(g_getenv("FALSO_LOG"));
	CONFERE(strstr(lg, "legendary auth --code abcdef0123456789abcdef0123456789") != NULL, "o legendary recebeu só o código");
	g_free(lg);

	/* a biblioteca */
	n = le_biblioteca(l, 10, erro, sizeof(erro));
	CONFERE(n == 2 && !strcmp(l[0].gid, "Sugar") && !strcmp(l[0].nome, "Rocket League") && l[0].possui && l[0].capa[0], "a biblioteca da conta (pelo legendary list)");
	CONFERE(le_detalhes_biblioteca("Sugar", &d, erro, sizeof(erro)) && !strcmp(d.nome, "Rocket League") && !strcmp(d.desenvolvedor, "Psyonix") && d.resumo[0], "o detalhe de um jogo da biblioteca");
	CONFERE(!le_detalhes_biblioteca("Nao", &d, erro, sizeof(erro)), "um jogo que não é da conta");
	CONFERE(le_app_name_ok("Sugar") && le_app_name_ok("a1_b-2") && !le_app_name_ok("a b") && !le_app_name_ok("x;y") && !le_app_name_ok("") && !le_app_name_ok("-rf"), "o app_name só tem letras, números, - e _, e nunca começa com - (viraria uma opção do legendary)");

	/* progresso e instalar */
	CONFERE(le_progresso_da_linha("[DLManager] INFO: = Progress: 12.34% (3/65), Running for 00:00:02") == 12 && le_progresso_da_linha("nada") == -1 && le_progresso_da_linha("Progress: 250%") == 100, "ler a linha de progresso do legendary");
	char *pasta = g_build_filename(g_get_home_dir(), "EpicJogos", NULL);
	CONFERE(!le_instalar("Sugar", "Rocket League", "relativa", erro, sizeof(erro)), "pasta relativa é recusada");
	CONFERE(le_instalar_agora("Sugar", "Rocket League", pasta) == 0, "o legendary instala até o fim");
	struct le_prog p;
	CONFERE(le_progresso("Sugar", &p) && p.pronto && p.pct == 100, "o estado vira pronto");
	char *ins = g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "epic", "installed.json", NULL), *it = le(ins);
	CONFERE(strstr(it, "\"Sugar\"") && strstr(it, "EpicJogos"), "o legendary registrou o jogo instalado");
	g_free(it);
	g_free(ins);
	lg = le(g_getenv("FALSO_LOG"));
	CONFERE(strstr(lg, "legendary install Sugar --base-path ") && strstr(lg, " -y"), "legendary install com a pasta, sem perguntas");
	g_free(lg);
	CONFERE(le_instalar_agora("Falha", "Falha", pasta) != 0 && le_progresso("Falha", &p) && p.erro && strstr(p.msg, "falhou"), "um legendary que falha vira estado de erro");
	CONFERE(le_instalar_agora("x y", "X", pasta) != 0, "app_name com espaço nem chega ao legendary");
	g_free(pasta);
	le_sair();
	CONFERE(!le_logado(), "sair apaga o login");

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("loja epic: ok\n");
	return 0;
}
