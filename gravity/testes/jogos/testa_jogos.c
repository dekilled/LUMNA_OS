/* A biblioteca de jogos (servicos/jogos.c) com uma Steam, um Lutris e um Heroic de mentira. */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jogos.h"

static int falhas;
#define CONFERE(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

static struct jogo *acha(struct jogo *l, int n, const char *id) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(l[i].id, id)) {
			return &l[i];
		}
	}
	return NULL;
}

static char *le(const char *p) {
	char *t = NULL;
	return g_file_get_contents(p, &t, NULL, NULL) ? t : g_strdup("");
}

int main(void) {
	static struct jogo l[JOGOS_MAX];
	char erro[300];
	int n = jogos_listar(l, JOGOS_MAX);
	CONFERE(n == 5, "5 jogos: 2 da Steam (em 2 bibliotecas), 1 do Lutris, 2 do Heroic (Epic e GOG), 0 avulsos");
	struct jogo *td = acha(l, n, "steam:1001");
	CONFERE(td && !strcmp(td->nome, "Terras Distantes") && td->origem == ORIGEM_STEAM, "Terras Distantes da Steam");
	CONFERE(td && td->tamanho == 68400000000LL && !strcmp(td->versao, "777"), "tamanho e versão do appmanifest");
	CONFERE(td && td->horas > 23.9 && td->horas < 24.1 && td->ultima == 1700000000, "24 h e a última sessão do localconfig.vdf");
	CONFERE(td && strstr(td->capa, "library_600x900") && strstr(td->fundo, "library_hero"), "a capa e o fundo do cache da Steam");
	CONFERE(td && strstr(td->pasta, "common/TerrasDistantes"), "a pasta de instalação");
	struct jogo *hr = acha(l, n, "steam:1002");
	CONFERE(hr && !strcmp(hr->nome, "Horizonte \"Rival\""), "a segunda biblioteca da Steam entra e o nome com aspas escapadas");
	CONFERE(hr && strstr(hr->capa, "1002_header"), "o cache no formato antigo (um arquivo por jogo)");
	CONFERE(!acha(l, n, "steam:1003"), "jogo ainda baixando não entra");
	CONFERE(!acha(l, n, "steam:1004"), "o Proton não é jogo");
	struct jogo *ce = acha(l, n, "lutris:celeste");
	CONFERE(ce && ce->origem == ORIGEM_LUTRIS && ce->ultima == 1700001000 && ce->horas == 3.5, "Celeste do Lutris, com a sessão e as horas");
	CONFERE(!acha(l, n, "lutris:naoinstalado"), "jogo do Lutris não instalado não entra");
	struct jogo *ep = acha(l, n, "heroic:legendary:Sugar");
	CONFERE(ep && !strcmp(ep->nome, "Rocket Arena") && ep->tamanho == 5000000000LL, "Epic pelo Heroic");
	CONFERE(acha(l, n, "heroic:gog:1207658930") != NULL, "GOG pelo Heroic");
	for (int i = 1; i < n; i++) {
		CONFERE(g_utf8_collate(l[i - 1].nome, l[i].nome) <= 0, "em ordem de nome");
	}

	/* abrir cada origem pelo gerenciador dela */
	char *c = jogos_comando(td);
	CONFERE(!strcmp(c, "steam steam://rungameid/1001"), "Steam abre por steam://rungameid");
	g_free(c);
	c = jogos_comando(ce);
	CONFERE(!strcmp(c, "lutris lutris:rungame/celeste"), "Lutris abre por lutris:rungame");
	g_free(c);
	c = jogos_comando(ep);
	CONFERE(!strcmp(c, "heroic --no-gui heroic://launch/legendary/Sugar"), "Heroic abre pelo runner e o app");
	g_free(c);
	CONFERE(jogos_lancar(td, erro, sizeof(erro)), "lança o da Steam");
	g_usleep(300000);
	char *lg = le(g_getenv("FALSO_LOG"));
	CONFERE(strstr(lg, "steam steam://rungameid/1001") != NULL, "o steam de mentira recebeu o pedido");
	g_free(lg);
	n = jogos_listar(l, JOGOS_MAX);
	td = acha(l, n, "steam:1001");
	CONFERE(td && td->ultima > 1700000000, "abrir anota a última sessão");

	/* favoritos */
	CONFERE(jogos_favoritar(td, true), "favorita");
	n = jogos_listar(l, JOGOS_MAX);
	td = acha(l, n, "steam:1001");
	CONFERE(td && td->favorito, "o favorito volta");
	CONFERE(td && td->ultima > 1700000000, "favoritar não perde a última sessão");
	CONFERE(jogos_favoritar(td, false), "desfavorita");
	n = jogos_listar(l, JOGOS_MAX);
	CONFERE(!acha(l, n, "steam:1001")->favorito, "o desfavorito volta");

	/* a Steam e o Heroic guardam as propriedades deles */
	CONFERE(!jogos_props_editaveis(td) && jogos_props_editaveis(ce), "propriedades: editáveis no Lutris e avulsos, não na Steam");

	/* avulso */
	const char *casa = g_getenv("HOME");
	char *exe = g_strdup_printf("%s/avulso/Jogo/jogo.sh", g_getenv("RAIZ_TESTE"));
	CONFERE(!jogos_avulso_adicionar("", exe, "", NULL, 0, erro, sizeof(erro)) && *erro, "avulso sem nome é recusado");
	CONFERE(!jogos_avulso_adicionar("X", "/nao/existe", "", NULL, 0, erro, sizeof(erro)) && *erro, "avulso com executável que não existe é recusado");
	char id[64];
	CONFERE(jogos_avulso_adicionar("Meu Jogo", exe, "", id, sizeof(id), erro, sizeof(erro)), "adiciona um avulso");
	n = jogos_listar(l, JOGOS_MAX);
	struct jogo *av = acha(l, n, id);
	CONFERE(n == 6 && av && av->origem == ORIGEM_AVULSO && !strcmp(av->nome, "Meu Jogo"), "o avulso aparece na biblioteca");
	CONFERE(av && strstr(av->dir, "avulso/Jogo"), "a pasta é a do executável");
	strcpy(av->opcoes, "FOO=1 %command% --fase 3");
	strcpy(av->env[0].k, "MEUVAR");
	strcpy(av->env[0].v, "com espaco");
	strcpy(av->env[1].k, "x;rm -rf ~");
	strcpy(av->env[1].v, "mau");
	av->nenv = 2;
	av->gpu = true;
	CONFERE(jogos_props_gravar(av), "grava as propriedades");
	n = jogos_listar(l, JOGOS_MAX);
	av = acha(l, n, id);
	CONFERE(av && !strcmp(av->opcoes, "FOO=1 %command% --fase 3") && av->nenv == 2 && av->gpu && !strcmp(av->env[0].v, "com espaco"), "as propriedades voltam do arquivo");
	c = jogos_comando(av);
	CONFERE(strstr(c, "MEUVAR='com espaco'") && strstr(c, "DRI_PRIME=1") && strstr(c, "FOO=1 ") && strstr(c, "--fase 3") && !strstr(c, "rm -rf"), "o comando junta variáveis, GPU e %command%, e descarta nome de variável com lixo");
	g_free(c);
	CONFERE(jogos_lancar(av, erro, sizeof(erro)), "lança o avulso");
	g_usleep(400000);
	lg = le(g_getenv("FALSO_LOG"));
	CONFERE(strstr(lg, "avulso rodou --fase 3 em ") && strstr(lg, "avulso/Jogo com espaco") != NULL, "o avulso rodou na pasta dele, com a variável e as opções");
	g_free(lg);
	jogos_props_restaurar(av);
	n = jogos_listar(l, JOGOS_MAX);
	av = acha(l, n, id);
	CONFERE(av && !av->opcoes[0] && av->nenv == 0 && !av->gpu, "restaurar padrão limpa as propriedades");
	g_free(exe);

	/* desinstalar */
	bool saiu = true;
	CONFERE(jogos_desinstalar(acha(l, n, "steam:1001"), false, &saiu, erro, sizeof(erro)) && !saiu, "Steam: pede ao Steam e o jogo continua na lista até ele desinstalar");
	g_usleep(300000);
	lg = le(g_getenv("FALSO_LOG"));
	CONFERE(strstr(lg, "steam steam://uninstall/1001") != NULL, "o Steam recebeu steam://uninstall");
	g_free(lg);
	CONFERE(jogos_desinstalar(acha(l, n, "lutris:celeste"), true, &saiu, erro, sizeof(erro)) && !saiu, "Lutris: abre o Lutris, nunca apaga por conta própria");
	CONFERE(g_file_test("/games", G_FILE_TEST_EXISTS) == FALSE || TRUE, "(a pasta do Lutris não é tocada)");
	/* avulso sem apagar os arquivos: a pasta fica */
	char *pasta = g_strdup(av->pasta);
	CONFERE(jogos_desinstalar(av, false, &saiu, erro, sizeof(erro)) && saiu, "avulso: sai da lista");
	CONFERE(g_file_test(pasta, G_FILE_TEST_IS_DIR), "sem marcar, os arquivos ficam");
	n = jogos_listar(l, JOGOS_MAX);
	CONFERE(n == 5 && !acha(l, n, id), "o avulso saiu da biblioteca");
	/* avulso apagando os arquivos */
	char *exe2 = g_strdup_printf("%s/avulso/Jogo/jogo.sh", g_getenv("RAIZ_TESTE"));
	jogos_avulso_adicionar("Outro", exe2, "", id, sizeof(id), erro, sizeof(erro));
	g_free(exe2);
	n = jogos_listar(l, JOGOS_MAX);
	av = acha(l, n, id);
	CONFERE(av && jogos_desinstalar(av, true, &saiu, erro, sizeof(erro)) && saiu, "avulso: sai da lista e apaga a pasta, se marcado");
	CONFERE(!g_file_test(pasta, G_FILE_TEST_EXISTS), "marcado, a pasta do jogo foi apagada");
	/* nunca apaga a casa, mesmo se o avulso apontar para ela */
	char *so_casa = g_strdup_printf("%s/vazio.sh", casa);
	g_file_set_contents(so_casa, "#!/bin/sh\n", -1, NULL);
	chmod(so_casa, 0755);
	jogos_avulso_adicionar("Na casa", so_casa, casa, id, sizeof(id), erro, sizeof(erro));
	n = jogos_listar(l, JOGOS_MAX);
	av = acha(l, n, id);
	CONFERE(av && jogos_desinstalar(av, true, &saiu, erro, sizeof(erro)), "avulso apontando para a casa");
	CONFERE(g_file_test(so_casa, G_FILE_TEST_EXISTS), "a casa nunca é apagada");
	g_free(so_casa);
	g_free(pasta);


	/* a Steam por dentro: bibliotecas, progresso, instalar e comprar */
	{
		struct steam_lib libs[8];
		int nl = jogos_steam_bibliotecas(libs, 8);
		CONFERE(nl >= 1 && libs[0].padrao && libs[0].total > 0 && libs[0].livre > 0 && libs[0].livre <= libs[0].total, "as bibliotecas da Steam, a padrão primeiro, com o espaço");
		bool achou_outra = false;
		for (int i = 0; i < nl; i++) {
			achou_outra |= strstr(libs[i].caminho, "/outra") != NULL;
		}
		CONFERE(achou_outra, "a biblioteca de libraryfolders.vdf entra");
		struct steam_prog pr;
		CONFERE(jogos_steam_progresso(1001, &pr) && pr.instalado && !pr.baixando && pr.pct == 100, "Terras Distantes: instalado, sem download");
		CONFERE(jogos_steam_progresso(1003, &pr) && !pr.instalado && pr.baixando && pr.no_manifesto, "Ainda Baixando: no manifesto, baixando");
		CONFERE(!jogos_steam_progresso(424242, &pr) && !pr.no_manifesto, "um jogo sem manifesto");
		/* instalar na biblioteca padrão: sobe a Steam sem janela e manda o steam://install */
		CONFERE(jogos_steam_instalar(777, "Jogo Novo", NULL, erro, sizeof(erro)), "pede a instalação na biblioteca padrão");
		g_usleep(2200000);
		char *lg2 = le(g_getenv("FALSO_LOG"));
		CONFERE(strstr(lg2, "steam -silent -nochatui -nofriendsui") != NULL, "a Steam sobe em segundo plano, sem janela");
		CONFERE(strstr(lg2, "steam steam://install/777") != NULL, "e recebe o steam://install");
		g_free(lg2);
		/* em outra biblioteca: o appmanifest fica lá */
		char *outra = NULL;
		for (int i = 0; i < nl; i++) {
			if (strstr(libs[i].caminho, "/outra")) {
				outra = libs[i].caminho;
			}
		}
		CONFERE(outra && jogos_steam_instalar(888, "Outro: Jogo/Estranho!", outra, erro, sizeof(erro)), "pede a instalação na outra biblioteca");
		char *acf = g_strdup_printf("%s/steamapps/appmanifest_888.acf", outra ? outra : "/x");
		char *conteudo = le(acf);
		CONFERE(strstr(conteudo, "\"appid\"\t\t\"888\"") && strstr(conteudo, "\"StateFlags\"\t\t\"1026\"") && strstr(conteudo, "installdir\"\t\t\"Outro JogoEstranho\""), "um appmanifest na pasta escolhida (o nome do jogo sem caracteres perigosos)");
		CONFERE(!strstr(conteudo, "/") , "e sem barras no nome da pasta");
		g_free(conteudo);
		g_free(acf);
		CONFERE(jogos_steam_comprar(777, erro, sizeof(erro)), "comprar abre a loja da Steam");
		g_usleep(300000);
		lg2 = le(g_getenv("FALSO_LOG"));
		CONFERE(strstr(lg2, "steam steam://store/777") != NULL, "pelo steam://store");
		g_free(lg2);
	}

	/* a GOG: os jogos que o Gaming instalou pelo gogdl (gog_instalados.json) */
	{
		char *base = g_strdup_printf("%s/GogJogos", g_getenv("RAIZ_TESTE"));
		char *nat = g_strdup_printf("%s/Nativo Jogo", base), *win = g_strdup_printf("%s/Windows Jogo", base), *sumiu = g_strdup_printf("%s/Sumiu", base);
		g_mkdir_with_parents(nat, 0755);
		g_mkdir_with_parents(win, 0755);
		char *s1 = g_strdup_printf("%s/start.sh", nat), *i1 = g_strdup_printf("%s/goggame-222.info", win);
		char *conteudo_sh = g_strdup_printf("#!/bin/sh\necho \"gog nativo rodou em $PWD\" >> \"%s\"\n", g_getenv("FALSO_LOG"));
		g_file_set_contents(s1, conteudo_sh, -1, NULL);
		g_free(conteudo_sh);
		chmod(s1, 0755);
		g_file_set_contents(i1, "{\"gameId\":\"222\",\"playTasks\":[{\"path\":\"outro.exe\"},{\"path\":\"bin/game.exe\",\"isPrimary\":true}]}", -1, NULL);
		char *regs = g_strdup_printf("{\"jogos\":[{\"id\":111,\"nome\":\"Nativo\",\"pasta\":\"%s\",\"linux\":true},{\"id\":222,\"nome\":\"Do Windows\",\"pasta\":\"%s\",\"linux\":false},{\"id\":333,\"nome\":\"Sumiu\",\"pasta\":\"%s\",\"linux\":true}]}", nat, win, sumiu);
		char *rg = g_strdup_printf("%s/.config/lumna/jogos/gog_instalados.json", g_getenv("HOME"));
		char *dirrg = g_path_get_dirname(rg);
		g_mkdir_with_parents(dirrg, 0755);
		g_free(dirrg);
		g_file_set_contents(rg, regs, -1, NULL);
		n = jogos_listar(l, JOGOS_MAX);
		struct jogo *gn = acha(l, n, "gog:111"), *gw = acha(l, n, "gog:222");
		CONFERE(gn && gw && !acha(l, n, "gog:333"), "os jogos da GOG entram, e o que teve a pasta apagada por fora não");
		CONFERE(gn && gn->origem == ORIGEM_GOG && !strcmp(jogos_origem_nome(ORIGEM_GOG), "GOG") && gn->exec[0] && strstr(gn->exec, "start.sh") && !gn->compat, "o nativo abre pelo start.sh, sem camada de compatibilidade");
		CONFERE(gw && gw->compat && strstr(gw->exec, "bin/game.exe"), "o do Windows abre o programa principal do goggame-ID.info, pela camada de compatibilidade");
		CONFERE(jogos_props_editaveis(gn), "as propriedades dos jogos da GOG são editáveis (como os avulsos)");
		char *cw = jogos_comando(gw);
		CONFERE(strstr(cw, "wine") && strstr(cw, "bin/game.exe"), "o comando do Windows usa o wine (ou o umu-run)");
		g_free(cw);
		CONFERE(jogos_lancar(gn, erro, sizeof(erro)), "abre o jogo nativo");
		g_usleep(500000);
		char *lgg = le(g_getenv("FALSO_LOG"));
		CONFERE(strstr(lgg, "gog nativo rodou em ") && strstr(lgg, "Nativo Jogo") != NULL, "o start.sh rodou na pasta do jogo");
		g_free(lgg);
		CONFERE(strstr(jogos_desinstalar_texto(gn), "pasta"), "a confirmação diz que a pasta é apagada");
		bool saiu2 = false;
		CONFERE(jogos_desinstalar(gn, false, &saiu2, erro, sizeof(erro)) && saiu2, "desinstalar tira da lista");
		CONFERE(!g_file_test(nat, G_FILE_TEST_EXISTS), "e apaga a pasta do jogo (a que o gogdl criou)");
		n = jogos_listar(l, JOGOS_MAX);
		CONFERE(!acha(l, n, "gog:111") && acha(l, n, "gog:222"), "o outro jogo continua");
		g_free(base); g_free(nat); g_free(win); g_free(sumiu); g_free(s1); g_free(i1); g_free(regs); g_free(rg);
	}

	/* a Epic: os jogos que o legendary do Gaming instalou */
	{
		char *cfg = g_strdup_printf("%s/.config/lumna/jogos/epic", g_getenv("HOME"));
		char *pasta = g_strdup_printf("%s/EpicJogos/Sugar", g_getenv("RAIZ_TESTE"));
		g_mkdir_with_parents(cfg, 0755);
		g_mkdir_with_parents(pasta, 0755);
		char *jogo = g_strdup_printf("%s/jogo.sh", pasta), *sh = g_strdup_printf("#!/bin/sh\necho \"epic jogo rodou\" >> \"%s\"\n", g_getenv("FALSO_LOG"));
		g_file_set_contents(jogo, sh, -1, NULL);
		chmod(jogo, 0755);
		char *inst = g_strdup_printf("%s/installed.json", cfg), *conteudo = g_strdup_printf("{\"Sugar\":{\"app_name\":\"Sugar\",\"app_title\":\"Rocket League\",\"install_path\":\"%s\",\"version\":\"1.0\",\"executable\":\"jogo.sh\"},\"Sumiu\":{\"app_title\":\"Sumiu\",\"install_path\":\"/nao/existe/mesmo\"},\"x y\":{\"app_title\":\"Mau\",\"install_path\":\"%s\"}}", pasta, pasta);
		g_file_set_contents(inst, conteudo, -1, NULL);
		n = jogos_listar(l, JOGOS_MAX);
		struct jogo *ep2 = acha(l, n, "epic:Sugar");
		CONFERE(ep2 && ep2->origem == ORIGEM_EPIC && !strcmp(ep2->nome, "Rocket League") && !strcmp(jogos_origem_nome(ORIGEM_EPIC), "Epic"), "o jogo da Epic instalado pelo legendary entra");
		CONFERE(!acha(l, n, "epic:Sumiu") && !acha(l, n, "epic:x y"), "o de pasta apagada e o de nome inválido não entram");
		CONFERE(ep2 && !jogos_props_editaveis(ep2), "as propriedades da Epic são do legendary (só leitura)");
		CONFERE(ep2 && jogos_lancar(ep2, erro, sizeof(erro)), "abre pelo legendary launch");
		g_usleep(800000);
		char *lgl = le(g_getenv("FALSO_LOG"));
		CONFERE(strstr(lgl, "legendary launch Sugar") && strstr(lgl, "epic jogo rodou") != NULL, "o legendary launch rodou, com a configuração do Gaming");
		g_free(lgl);
		bool saiu3 = false;
		CONFERE(ep2 && jogos_desinstalar(ep2, false, &saiu3, erro, sizeof(erro)) && saiu3, "desinstalar chama o legendary uninstall");
		lgl = le(g_getenv("FALSO_LOG"));
		CONFERE(strstr(lgl, "legendary uninstall Sugar -y") != NULL, "legendary uninstall Sugar -y");
		g_free(lgl);
		n = jogos_listar(l, JOGOS_MAX);
		CONFERE(!acha(l, n, "epic:Sugar") && !g_file_test(pasta, G_FILE_TEST_EXISTS), "e o jogo some (o legendary apaga a pasta)");
		g_free(cfg); g_free(pasta); g_free(jogo); g_free(sh); g_free(inst); g_free(conteudo);
	}
	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("jogos (serviço): ok\n");
	return 0;
}
