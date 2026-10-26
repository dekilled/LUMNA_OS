/*
 * O Firefox como o player web do LUMNA: um só, em kiosk (sem barra, sem abas à vista), com perfil
 * próprio em ~/.local/share/lumna/midia/perfil. Cada serviço da Mídia (Netflix, YouTube...) é uma
 * aba dele, e a aba é o "serviço vivo": voltar a ele é só ativar a aba, no ponto exato do vídeo.
 * O Orbit vê uma janela só (app_id lumna-midia-web).
 *
 * O controle é por WebDriver BiDi (--remote-debugging-port, só em 127.0.0.1): abrir e ativar abas,
 * saber quando a página carregou (para o loading sair com fade), a URL e o título de cada uma e a
 * miniatura. Uma linha à parte faz isso; a app vigia o descritor e lê o estado:
 *
 *   int fd = ff_iniciar();
 *   gv_app_vigiar(app, fd, mudou, NULL);   // em mudou(): ff_ler(&estado)
 *   ff_abrir("netflix", "https://www.netflix.com/browse");
 *
 * O programa é o que $LUMNA_FIREFOX disser, ou "firefox".
 */
#ifndef LUMNA_FIREFOX_H
#define LUMNA_FIREFOX_H

#include <stdbool.h>

#define FF_MAX_ABAS 8
#define FF_APP_ID "lumna-midia-web"

struct ff_aba {
	char servico[32];
	char contexto[64];  /* o id da aba no BiDi */
	char url[1024];
	char titulo[160];
	bool carregando;
	bool ativa;
};

struct ff_estado {
	bool lancado;   /* o processo existe */
	bool ligado;    /* o controle respondeu: dá para mandar abas */
	struct ff_aba abas[FF_MAX_ABAS];
	int n;
	char ativo[32]; /* o serviço da aba à frente, ou "" */
};

int ff_iniciar(void);               /* o descritor que acorda a cada mudança; -1 se falhou */
void ff_ler(struct ff_estado *e);
/* abre o serviço: se já tem aba, só ativa (nada recarrega); senão abre a aba e vai ao endereço */
void ff_abrir(const char *servico, const char *url);
void ff_ativar(const char *servico);
/* fecha a aba do serviço (a memória volta; o endereço fica nos recentes) */
void ff_fechar(const char *servico);
/*
 * Guarda o serviço nos recentes (endereço, título e, se pedido, a miniatura). Quem sai de um
 * serviço chama com miniatura = true; o resto é só o ponto de retomada, que também se grava
 * sozinho a cada 20 segundos enquanto o serviço está à frente.
 */
void ff_salvar(const char *servico, bool miniatura);
void ff_encerrar(void);             /* fecha o Firefox */

#endif
