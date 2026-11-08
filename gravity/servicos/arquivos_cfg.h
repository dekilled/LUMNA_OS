/*
 * Os ajustes do explorador (Arquivos > engrenagem): como as pastas aparecem, a página inicial, a barra lateral e o que fazer em conflitos.
 *   ~/.config/lumna/atlas/config.ini    os ajustes
 *   ~/.config/lumna/atlas/barra.ini     a barra lateral (as seções, a ordem, o que aparece e os atalhos da pessoa)
 *   ~/.config/lumna/atlas/visoes.ini    a visualização lembrada de cada pasta (quando "lembrar de cada pasta" está ligado)
 */
#ifndef LUMNA_ARQUIVOS_CFG_H
#define LUMNA_ARQUIVOS_CFG_H

#include <stdbool.h>
#include <stddef.h>

enum { AR_VISTA_GRADE, AR_VISTA_LISTA, AR_VISTA_LISTA_GRANDE };
enum { AR_ABRIR_INICIO, AR_ABRIR_ULTIMA, AR_ABRIR_PERSONALIZADA };

typedef struct ar_cfg {
	/* exibição */
	int vista;               /* AR_VISTA_* */
	int tam_pasta;           /* 64..192 px (o tamanho dos ícones da grade) */
	int texto;               /* 0 padrão, 1 pequeno, 2 grande */
	bool ocultos;            /* mostrar arquivos ocultos */
	bool extensoes;          /* mostrar a extensão no nome */
	bool previas;            /* miniaturas do conteúdo nas pastas */
	bool pastas_primeiro;
	int ordem;               /* enum ar_ordem */
	bool decrescente;
	bool lembrar_por_pasta;  /* cada pasta lembra a sua visualização (senão vale a padrão) */
	/* página inicial */
	int abrir_em;            /* AR_ABRIR_* */
	char pasta_inicial[4096];
	char ultima_pasta[4096]; /* a última pasta aberta (para AR_ABRIR_ULTIMA) */
	bool inicio_pastas;
	bool inicio_unidades;
	/* avançado */
	int conflito;            /* 0 perguntar, 1 substituir, 2 manter os dois, 3 pular */
	bool confirmar_apagar;   /* confirmar a exclusão permanente */
	bool confirmar_esvaziar; /* confirmar ao esvaziar a lixeira */
	bool registrar_erros;
} ar_cfg;

void ar_cfg_padrao(ar_cfg *c);
void ar_cfg_ler(ar_cfg *c);
bool ar_cfg_gravar(const ar_cfg *c);

/* A visualização lembrada de uma pasta (-1: nenhuma lembrada). */
int ar_visao_da_pasta(const char *caminho);
void ar_visao_da_pasta_definir(const char *caminho, int vista);

/* ---- a barra lateral ---- */
enum { AR_SECAO_ESPACOS, AR_SECAO_COLECOES, AR_SECOES_N };
#define AR_BARRA_MAX 64
typedef struct ar_barra_item {
	char id[16];        /* "inicio", "recentes", "favoritos" (os de fábrica que não são pastas); "" numa pasta */
	char rotulo[64];
	char caminho[4096]; /* da pasta ("" nos de fábrica) */
	char icone[24];
	bool visivel;
	bool no_inicio;     /* aparece nas "pastas principais" da página inicial */
	int secao;          /* AR_SECAO_* */
} ar_barra_item;
typedef struct ar_barra {
	ar_barra_item v[AR_BARRA_MAX];
	int n;
} ar_barra;
void ar_barra_padrao(ar_barra *b);   /* Início, Recentes, Favoritos; a casa, Documentos, Imagens... (as que existem), Emma */
void ar_barra_ler(ar_barra *b);      /* o arquivo, ou o padrão se não há */
bool ar_barra_gravar(const ar_barra *b);
/* Os itens de uma seção, na ordem (índices em b->v); devolve quantos. */
int ar_barra_da_secao(const ar_barra *b, int secao, int *indices);
/* Fixar/desafixar uma pasta numa seção, direto no arquivo (o menu de contexto). Devolve o estado de agora. */
bool ar_barra_tem(const char *caminho, int secao);
bool ar_barra_alternar(const char *caminho, int secao);
void ar_barra_mover(ar_barra *b, int indice, int direcao);   /* -1 sobe, +1 desce, dentro da própria seção */
bool ar_barra_remover(ar_barra *b, int indice);
bool ar_barra_adicionar(ar_barra *b, int secao, const char *rotulo, const char *caminho);

#endif
