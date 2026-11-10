/*
 * A Loja LUMNA, sem interface: o catálogo dos repositórios do Arch (core, extra, multilib, lido do `pacman -Si`), o AUR (pela RPC do aur.archlinux.org, com o
 * paru ou o yay que estiver instalado), a busca com filtros, o que está instalado e o que tem atualização, e as ações (instalar, remover, atualizar tudo) com a
 * saída chegando linha por linha. Quem tem senha (pacman) passa pelo pkexec, que pede a senha numa janela do LUMNA; o AUR roda como a pessoa (o paru/yay
 * compilam como usuário e chamam o pkexec só para instalar).
 *
 * O Arch não tem categorias nem tipo: aqui eles são deduzidos (uma tabela de apps conhecidos e o jeito dos nomes: lib*, *-utils, linux*, nvidia*...).
 * As avaliações da comunidade LUMNA ainda não têm servidor: nota 0 e 0 avaliações ("Sem avaliações").
 *
 * Para testar: um `pacman` falso no PATH ($LUMNA_SEM_PKEXEC roda sem pkexec), $LUMNA_AUR_URL (o servidor do AUR de mentira, no lugar de https://aur.archlinux.org)
 * e um paru/yay falsos.
 */
#ifndef LUMNA_LOJA_H
#define LUMNA_LOJA_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

enum lj_tipo { LJ_APP, LJ_UTILITARIO, LJ_BIBLIOTECA, LJ_DRIVER, LJ_KERNEL };
enum lj_cat { LJ_TODAS, LJ_DESENVOLVIMENTO, LJ_GRAFICOS, LJ_INTERNET, LJ_MULTIMIDIA, LJ_JOGOS, LJ_SISTEMA, LJ_NCATS };

typedef struct lj_pacote {
	char nome[96];           /* o nome do pacote */
	char exibicao[64];       /* "Firefox" para os conhecidos; senão, o próprio nome */
	char versao[48];
	char descricao[256];
	char repo[24];           /* core, extra, multilib ou aur */
	char url[200];
	char licenca[96];
	char grupos[96];
	char mantenedor[64];     /* AUR */
	int64_t baixar, tamanho;  /* bytes */
	enum lj_tipo tipo;
	enum lj_cat cat;
	bool destaque;           /* está na tabela de apps conhecidos */
	bool instalado, atualizavel, fora_de_data;
	bool explicito;          /* foi pedido pela pessoa (pacman -Qe), não veio como dependência */
	bool externo;            /* instalado do AUR e trazido só pela lista de atualizações: sobrevive a lj_aur_limpar */
	char nova_versao[48];
	char instalada[48];      /* a versão instalada (a de versao é a do repositório) */
	int votos;               /* AUR */
	double popularidade;     /* AUR */
	double nota;             /* avaliações da comunidade LUMNA: 0 se não há */
	int avaliacoes;
} lj_pacote;

typedef struct lj_catalogo lj_catalogo;

/* lê os repositórios e o que está instalado (leva uns segundos com o catálogo inteiro: chame numa thread). NULL se o pacman não responde */
lj_catalogo *lj_catalogo_ler(void);
void lj_catalogo_liberar(lj_catalogo *c);
int lj_n(const lj_catalogo *c);
const lj_pacote *lj_pacote_n(const lj_catalogo *c, int i);
const lj_pacote *lj_achar(const lj_catalogo *c, const char *nome);   /* repositórios e o que o AUR já trouxe */

int lj_contar_atualizaveis(const lj_catalogo *c);

/* a versão instalada de um pacote pelo nome (NULL se não está instalado) */
const char *lj_versao_instalada(const lj_catalogo *c, const char *nome);

/* depois de atualizar: tira a marca de atualização de um pacote (nome) ou de todos (NULL) */
void lj_marcar_atualizado(lj_catalogo *c, const char *nome);

/* o que o `pacman -Qu` (checkupdates) diz: marca atualizavel/nova_versao. Devolve quantos. Bloqueia. */
int lj_marcar_atualizacoes(lj_catalogo *c, const char *helper);

enum lj_buscar_por { LJ_POR_NOME, LJ_POR_DESCRICAO, LJ_POR_AMBOS };
enum lj_ordem { LJ_RELEVANCIA, LJ_NOME, LJ_TAMANHO, LJ_NOTA };
typedef struct lj_filtro {
	const char *texto;        /* NULL ou vazio: tudo */
	enum lj_buscar_por por;
	int tipo;                 /* -1: todos; senão um enum lj_tipo */
	int cat;                  /* LJ_TODAS ou uma categoria */
	bool core, extra, multilib, aur;  /* os repositórios que entram */
	double nota_min;          /* 0: sem mínimo */
	bool sem_avaliacao;       /* inclui os sem avaliação */
	bool so_instalados, so_atualizaveis;
	enum lj_ordem ordem;
} lj_filtro;
void lj_filtro_padrao(lj_filtro *f);   /* tudo ligado, sem texto, relevância */

/* a lista filtrada e ordenada (índices do catálogo, ou -(1+i) para os do AUR em `aur`); devolve o total. Os AUR vêm do último lj_aur_buscar. */
int lj_buscar(const lj_catalogo *c, const lj_filtro *f, int *saida, int max);
/* o i-ésimo item de um resultado de lj_buscar */
const lj_pacote *lj_resultado(const lj_catalogo *c, int codigo);

/* os apps conhecidos de uma categoria (Explorar): índices do catálogo */
int lj_destaques(const lj_catalogo *c, enum lj_cat cat, enum lj_ordem ordem, int *saida, int max);
const char *lj_nome_cat(enum lj_cat cat);
const char *lj_nome_tipo(enum lj_tipo t);

/* o AUR */
const char *lj_aur_helper(const char *preferido);   /* "paru", "yay" ou NULL (nenhum instalado); preferido: "paru"|"yay"|"" */
/* busca no AUR e guarda no catálogo (para lj_resultado e lj_achar). Bloqueia (rede). Devolve quantos, ou -1 com o erro. */
int lj_aur_buscar(lj_catalogo *c, const char *termo, char *erro, size_t tam);
/* o mesmo em duas metades, para a rede ficar numa thread: consultar só lê o catálogo e devolve um GArray de lj_pacote (NULL com o erro); usar, na linha principal, o troca. */
GArray *lj_aur_consultar(const lj_catalogo *c, const char *termo, char *erro, size_t tam);
void lj_aur_usar(lj_catalogo *c, GArray *achados);   /* toma posse de achados */
/* depois de instalar ou remover: ler de novo o que está instalado (thread) e aplicar (linha principal) */
typedef struct lj_instalados lj_instalados;
lj_instalados *lj_instalados_ler(void);
void lj_instalados_usar(lj_catalogo *c, lj_instalados *i);   /* toma posse */
void lj_aur_limpar(lj_catalogo *c);

/* ações: bloqueiam, a saída vai linha por linha para `linha` (NULL ok), de qualquer thread. 0 se deu certo; o fim do erro em `erro`. */
enum lj_acao { LJ_INSTALAR, LJ_REMOVER, LJ_ATUALIZAR_TUDO };
typedef void (*lj_linha_fn)(void *dados, const char *linha);
int lj_executar(enum lj_acao acao, const lj_pacote *p, const char *helper, lj_linha_fn linha, void *dados, char *erro, size_t tam);
/* vários pacotes dos repositórios oficiais numa transação só (um kernel e os headers dele): LJ_INSTALAR ou LJ_REMOVER */
int lj_executar_nomes(enum lj_acao acao, const char *const *nomes, int n, lj_linha_fn linha, void *dados, char *erro, size_t tam);

#endif
