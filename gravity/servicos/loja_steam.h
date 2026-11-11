/*
 * O catálogo da Steam para a Loja do modo Gaming: a API pública da loja (sem chave nem login) e, com uma chave de API, o que a conta já tem.
 * Tudo roda por `curl` (como a Loja de apps), com cache em disco (~/.cache/lumna/jogos/loja) e sem bloquear quem desenha: chame de uma linha à parte.
 *
 *   destaques  /api/featuredcategories (ofertas, mais vendidos, lançamentos)
 *   busca      /search/results (por texto, gênero, preço, Linux nativo, jogadores)
 *   detalhes   /api/appdetails (descrição, prints, trailers, requisitos, preço, plataformas)
 *   proton     protondb.com/api/v1/reports/summaries (o "Via Proton")
 *   possuidos  IPlayerService/GetOwnedGames (precisa da chave em ~/.config/lumna/jogos/steam.ini, 0600)
 *
 * Para testar sem a internet: $LUMNA_LOJA_STEAM_URL, $LUMNA_LOJA_PROTONDB_URL, $LUMNA_LOJA_IMG_URL e $LUMNA_LOJA_API_URL apontam para um servidor local com respostas gravadas.
 */
#ifndef LUMNA_LOJA_STEAM_H
#define LUMNA_LOJA_STEAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LS_MAX 60
#define LS_TELAS 16

enum { LS_LOJA_STEAM, LS_LOJA_GOG };

struct ls_item {
	int loja;             /* LS_LOJA_* */
	int appid;            /* o id na loja (Steam: appid; GOG: o id numérico do produto) */
	char nome[128];
	char capa[400];       /* a imagem de cápsula (URL) */
	char fundo[400];      /* o fundo largo (URL) */
	char preco[32];       /* "R$ 89,90", "Grátis" */
	int desconto;         /* % */
	char preco_antes[32]; /* o preço sem desconto, se há desconto */
	bool gratis;
	bool linux_nativo;
	char generos[96];     /* na GOG vem da lista ("Ação · RPG") */
	bool possui;          /* a conta tem (só com a chave); ou está instalado */
};

struct ls_detalhe {
	int loja;
	int appid;
	char nome[128];
	char resumo[512];
	char sobre[2048];     /* "sobre o jogo", sem HTML */
	char generos[160];    /* "RPG de ação · Mundo aberto" */
	char desenvolvedor[96];
	char lancamento[32];
	char capa[400], fundo[400];
	char telas[LS_TELAS][400];
	int ntelas;
	char trailer[400];    /* o primeiro vídeo (lista HLS/DASH ou mp4/webm: o mpv toca), "" se não há */
	char trailer_capa[400];
	char preco[32], preco_antes[32];
	int desconto;
	bool gratis, linux_nativo, windows, um_jogador, cooperativo, multijogador, controle;
	char requisitos[1536]; /* mínimos para o Linux (ou Windows), sem HTML */
	int metacritic;       /* 0: sem nota */
	char loja_url[300];   /* a página do jogo na loja (Comprar, na GOG) */
	bool existe;          /* o app existe e é um jogo */
};

enum ls_gen { LS_GEN_TODOS, LS_GEN_ACAO, LS_GEN_AVENTURA, LS_GEN_RPG, LS_GEN_CORRIDA, LS_GEN_ESTRATEGIA, LS_GEN_INDIE, LS_GEN_N };

struct ls_filtro {
	char termo[96];
	int genero;          /* enum ls_gen */
	bool gratis;
	bool linux_nativo;
	bool um_jogador, cooperativo;
	int ordem;           /* 0 relevância, 1 mais vendidos, 2 lançamento, 3 preço crescente */
};

/* as listas de destaque: cada uma devolve quantos itens; erro (se não NULL) recebe o motivo de uma falha de rede */
int ls_ofertas(struct ls_item *l, int max, char *erro, size_t tam);
int ls_mais_vendidos(struct ls_item *l, int max, char *erro, size_t tam);
int ls_lancamentos(struct ls_item *l, int max, char *erro, size_t tam);
int ls_buscar(const struct ls_filtro *f, struct ls_item *l, int max, int *total, char *erro, size_t tam);
bool ls_detalhes(int appid, struct ls_detalhe *d, char *erro, size_t tam);
/* o nível do ProtonDB ("platinum", "gold", "silver", "bronze", "borked", "pending", "") */
bool ls_proton(int appid, char *nivel, size_t tam);
const char *ls_proton_texto(const char *nivel);   /* "Roda bem via Proton" etc.; "" se não há */

/* o que a conta tem: precisa da chave (steam.ini [api] chave, id64) */
bool ls_chave_existe(void);
bool ls_chave_gravar(const char *chave, const char *id64);
void ls_chave_ler(char *id64, size_t tam);    /* só o id; a chave nunca sai daqui */
/* o id64 do usuário que está logado no cliente da Steam (loginusers.vdf), "" se não achou */
void ls_id_local(char *id64, size_t tam);
int ls_possuidos(int *appids, int max, char *erro, size_t tam);

/* para o catálogo da GOG (loja_gog.h): a mesma rede com cache, o mesmo limpador de HTML */
char *ls_http(const char *url, int ttl, char *erro, size_t tam);   /* g_free */
void ls_sem_html(const char *src, char *dest, size_t tam);

/* baixa uma imagem para o cache e devolve o caminho local (g_free); NULL se falhar. Bloqueia: linha à parte. */
char *ls_imagem(const char *url);
/* o nome de um gênero para mostrar */
const char *ls_gen_nome(int g);

#endif
