/*
 * A Epic Games Store no Loja do Gaming. A Epic **não tem catálogo público pesquisável** (a busca da loja fica atrás de um desafio do Cloudflare), então aqui
 * a Epic mostra duas coisas: o que a própria Epic publica (/freeGamesPromotions: os jogos grátis da semana, os que vêm aí e as ofertas) e a **sua biblioteca**
 * (o que você já tem na conta), pelo legendary, o motor de linha de comando que o Heroic usa. Instalar, abrir e desinstalar também são do legendary; Resgatar
 * e Comprar abrem a loja da Epic no navegador.
 *
 * O legendary guarda tudo em ~/.config/lumna/jogos/epic ($LEGENDARY_CONFIG_PATH): o login (user.json) e os jogos instalados (installed.json).
 * Para testar: $LUMNA_LOJA_EPIC_URL (a lista de promoções), $LUMNA_LOJA_EPIC_IMG_URL e um legendary de mentira no PATH.
 */
#ifndef LUMNA_LOJA_EPIC_H
#define LUMNA_LOJA_EPIC_H

#include "loja_steam.h"

/* o que a Epic publica: os grátis agora primeiro, depois os que vêm aí e as ofertas; erro recebe o motivo de uma falha de rede */
int le_promocoes(struct ls_item *l, int max, char *erro, size_t tam);
/* o detalhe de um item das promoções (pelo id da oferta) */
bool le_detalhes_promocao(const char *gid, struct ls_detalhe *d, char *erro, size_t tam);

/* ---- a conta ---- */
bool le_logado(void);
const char *le_url_login(void);
/* o código (ou o JSON/URL que a Epic mostra depois do login, com authorizationCode) vira o login pelo legendary */
bool le_login(const char *entrada, char *erro, size_t tam);
void le_sair(void);

/* ---- a biblioteca (precisa do login) ---- */
/* os jogos da conta: nome, app_name e as imagens; devolve quantos (-1 com erro) */
int le_biblioteca(struct ls_item *l, int max, char *erro, size_t tam);
/* o detalhe de um jogo da biblioteca (pelo app_name), do que o legendary guardou */
bool le_detalhes_biblioteca(const char *app_name, struct ls_detalhe *d, char *erro, size_t tam);

/* ---- instalar ---- */
struct le_prog {
	bool existe, baixando, pronto, erro;
	int pct;
	char msg[160];
};
bool le_progresso(const char *app_name, struct le_prog *p);
/* começa a instalar num processo à parte (`lumna-jogos epic-instalar`) */
bool le_instalar(const char *app_name, const char *titulo, const char *pasta, char *erro, size_t tam);
/* o que o processo à parte faz: legendary install, o progresso num arquivo de estado; devolve o código de saída */
int le_instalar_agora(const char *app_name, const char *titulo, const char *pasta);
/* "= Progress: 12.34% (3/65), Running for..." → 12; -1 se a linha não é de progresso */
int le_progresso_da_linha(const char *linha);

/* o diretório de configuração do legendary desta sessão ("" é o padrão do legendary) e se o app_name é seguro para a linha de comando */
char *le_config_dir(void);
bool le_app_name_ok(const char *s);

#endif
