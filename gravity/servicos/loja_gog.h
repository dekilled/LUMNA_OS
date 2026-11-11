/*
 * O catálogo da GOG para a Loja do modo Gaming (mesma forma do loja_steam.h: os mesmos ls_item e ls_detalhe, com loja = LS_LOJA_GOG) e a conta/instalação
 * pelo gogdl (o motor do Heroic, de linha de comando).
 *
 *   catálogo  catalog.gog.com/v1/catalog (destaques, ofertas, busca com filtros), preços em reais
 *   detalhes  api.gog.com/products/ID (descrição, prints, vídeos) e /prices (preço)
 *   conta     login uma vez: a GOG devolve um código numa URL; o gogdl troca por um token (~/.config/lumna/jogos/gog/auth.json, 0600)
 *   instalar  `lumna-jogos gog-instalar ID` (um processo à parte, que sobrevive à janela): gogdl download, com o progresso em um arquivo de estado
 *
 * Para testar: $LUMNA_LOJA_GOG_CATALOG_URL, $LUMNA_LOJA_GOG_API_URL, $LUMNA_LOJA_GOG_EMBED_URL, $LUMNA_LOJA_GOG_IMG_URL e um gogdl de mentira no PATH.
 */
#ifndef LUMNA_LOJA_GOG_H
#define LUMNA_LOJA_GOG_H

#include "loja_steam.h"

int lg_destaques(struct ls_item *l, int max, char *erro, size_t tam);
int lg_ofertas(struct ls_item *l, int max, char *erro, size_t tam);
int lg_buscar(const struct ls_filtro *f, struct ls_item *l, int max, int *total, char *erro, size_t tam);
bool lg_detalhes(int id, struct ls_detalhe *d, char *erro, size_t tam);

/* ---- a conta ---- */
bool lg_logado(void);
/* a página de login da GOG (abre no navegador) */
const char *lg_url_login(void);
/* o código (ou a URL inteira para onde a GOG mandou depois do login) vira um token pelo gogdl; erro recebe o motivo */
bool lg_login(const char *codigo_ou_url, char *erro, size_t tam);
void lg_sair(void);
/* os ids dos jogos da conta (precisa do login); -1 com erro preenchido */
int lg_possuidos(int *ids, int max, char *erro, size_t tam);

/* ---- instalar ---- */
struct lg_prog {
	bool existe;      /* há estado deste jogo (instalando, pronto ou erro) */
	bool baixando;
	bool pronto;
	bool erro;
	int pct;
	char msg[160];
};
bool lg_progresso(int id, struct lg_prog *p);
/* começa a instalar num processo à parte (`lumna-jogos gog-instalar`); pasta: onde o jogo vai (a pasta do jogo é criada dentro) */
bool lg_instalar(int id, const char *titulo, const char *pasta, bool linux_nativo, char *erro, size_t tam);
/* o que o processo à parte faz (o gog-instalar do lumna-jogos): baixa, anota o progresso e, no fim, registra o jogo; devolve o código de saída */
int lg_instalar_agora(int id, const char *titulo, const char *pasta, bool linux_nativo);
/* o analisa uma linha de saída do gogdl e devolve o percentual (-1 se não é de progresso) */
int lg_progresso_da_linha(const char *linha);

#endif
