/*
 * Arquivos (lumna-arquivos): o que o explorador sabe do disco, sem desenho nenhum: listar uma pasta, ordenar, filtrar e buscar, o tipo de cada
 * arquivo, tamanhos e datas em português, o histórico de navegação (voltar/avançar), os lugares da barra lateral, favoritos e recentes.
 */
#ifndef LUMNA_ARQUIVOS_H
#define LUMNA_ARQUIVOS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

enum ar_tipo {
	AR_PASTA, AR_PDF, AR_IMAGEM, AR_AUDIO, AR_VIDEO, AR_TEXTO, AR_MARKDOWN, AR_PLANILHA, AR_APRESENTACAO, AR_DOCUMENTO, AR_COMPACTADO, AR_CODIGO, AR_OUTRO,
};
/* os grupos dos filtros ("Tudo", "Pastas", "Documentos"...) */
enum ar_filtro { AR_F_TUDO, AR_F_PASTAS, AR_F_DOCUMENTOS, AR_F_IMAGENS, AR_F_MIDIA, AR_F_OUTROS };
enum ar_ordem { AR_O_NOME, AR_O_DATA, AR_O_TAMANHO, AR_O_TIPO };

typedef struct ar_entrada {
	char nome[256];
	bool pasta, oculto, link;
	enum ar_tipo tipo;
	int64_t tamanho;
	time_t mtime;
	mode_t modo;
	uid_t uid;
	int itens;        /* de uma pasta: quantos itens tem (-1: ainda não contou, ar_contar_itens) */
} ar_entrada;

typedef struct ar_lista {
	char *pasta;      /* o caminho listado */
	ar_entrada *v;
	int n, cap;
} ar_lista;

/* Lê a pasta (sem as ocultas, a menos que pedido). Falso se não deu (sem permissão, não existe); o erro vai em *erro (g_free). */
bool ar_listar(const char *pasta, bool ocultos, ar_lista *saida, char **erro);
void ar_liberar(ar_lista *l);
/* Uma entrada só, de um caminho completo (os favoritos e os recentes são listas de caminhos soltos). */
bool ar_entrada_de(const char *caminho, ar_entrada *e);
/* Pastas primeiro, depois pela ordem pedida (o nome não distingue maiúsculas e acentos, e "arquivo2" vem antes de "arquivo10"). */
void ar_ordenar(ar_lista *l, enum ar_ordem ordem, bool decrescente);
/* Os índices (em l->v) do que passa no filtro e na busca (parte do nome, sem distinguir maiúsculas e acentos); devolve quantos. indices tem l->n lugares. */
int ar_filtrar(const ar_lista *l, const char *busca, enum ar_filtro filtro, int *indices);
bool ar_no_filtro(const ar_entrada *e, enum ar_filtro filtro);
bool ar_busca_combina(const char *nome, const char *termo);

enum ar_tipo ar_tipo_do_nome(const char *nome);
const char *ar_tipo_nome(const ar_entrada *e);          /* "Pasta", "PDF", "Imagem PNG"... */
void ar_selo(const ar_entrada *e, char *saida, size_t tam); /* o rótulo curto do ícone ("PDF", "MD", "ODS") */
int ar_contar_itens(const char *pasta, bool ocultos);    /* quantos itens há dentro (-1 se não abre) */
/* O tamanho de uma pasta e dos itens dentro dela, sem passar de `teto` entradas (devolve falso se parou antes de acabar). */
bool ar_tamanho_pasta(const char *pasta, int teto, int64_t *bytes, int *itens);

void ar_tamanho_humano(int64_t bytes, char *saida, size_t tam);           /* "1,8 GB", "320 KB" */
void ar_data_humana(time_t t, time_t agora, char *saida, size_t tam);     /* "Hoje, 15:24", "Ontem, 09:10", "12 mar, 09:10", "3 jun 2024" */
void ar_permissoes_texto(mode_t modo, char *saida, size_t tam);           /* "rwxr-xr-x" */

/* ---- navegação ---- */
typedef struct ar_hist {
	char **v;
	int n, atual;
} ar_hist;
void ar_hist_ir(ar_hist *h, const char *caminho);        /* abre um lugar novo: corta o que havia à frente */
const char *ar_hist_atual(const ar_hist *h);
const char *ar_hist_voltar(ar_hist *h);                  /* NULL se não há para onde */
const char *ar_hist_avancar(ar_hist *h);
bool ar_hist_pode_voltar(const ar_hist *h);
bool ar_hist_pode_avancar(const ar_hist *h);
void ar_hist_liberar(ar_hist *h);

/* O caminho em pedaços clicáveis: "Home" / "Documentos" / "Projetos" (fora da casa: "Computador" / "usr" / ...). */
typedef struct ar_parte { char rotulo[128]; char *caminho; } ar_parte;
int ar_partes(const char *caminho, ar_parte *v, int max);   /* caminho de cada um em g_malloc; ar_partes_liberar */
void ar_partes_liberar(ar_parte *v, int n);
char *ar_pai(const char *caminho);                          /* g_free; NULL na raiz */
char *ar_unir(const char *pasta, const char *nome);         /* g_free */

/* ---- barra lateral ---- */
typedef struct ar_lugar {
	char rotulo[64];
	char *caminho;     /* g_malloc */
	const char *icone;
} ar_lugar;
/* As coleções: a casa, Documentos, Imagens, Músicas, Downloads, Vídeos, Modelos e a pasta da Emma (só as que existem). */
int ar_colecoes(ar_lugar *v, int max);
void ar_lugares_liberar(ar_lugar *v, int n);
/* Favoritos (~/.config/lumna/arquivos/favoritos) e recentes (~/.local/share/lumna/arquivos/recentes, os últimos 50). */
int ar_favoritos(ar_lugar *v, int max);
bool ar_eh_favorito(const char *caminho);
bool ar_favorito_alternar(const char *caminho);   /* devolve o estado de agora */
int ar_recentes(ar_lugar *v, int max);
void ar_recente_registrar(const char *caminho);

#endif
