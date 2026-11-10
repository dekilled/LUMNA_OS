/*
 * A biblioteca do LUMNA Music, sem interface: ajustes, varredura das pastas (tags pelo ffprobe, com cache), capas, letras (.lrc ao lado ou a tag),
 * álbuns, artistas, pastas, busca, playlists e a fila salva. Quem toca é servicos/musica_player.c.
 */
#ifndef LUMNA_MUSICA_H
#define LUMNA_MUSICA_H

#include <glib.h>
#include <stdbool.h>
#include <time.h>

#define MU_PASTAS_MAX 8

typedef struct mu_cfg {
	/* reprodução */
	char saida[256];           /* o nome do dispositivo do mpv ("" = o padrão do sistema) */
	bool normalizar;
	bool sem_intervalos;
	int transicao;             /* 0..12 s entre as faixas */
	bool restaurar_fila;
	bool emma_no_app;
	bool segundo_plano;        /* fechar a janela tocando: continua tocando, sem janela */
	/* biblioteca */
	char pastas[MU_PASTAS_MAX][4096];
	int npastas;
	bool subpastas;
	bool buscar_auto;
	/* letras */
	bool letras_mostrar;
	bool letras_seguir;
	int letras_tamanho;        /* 0 pequena, 1 média, 2 grande */
	/* aparência */
	bool capas;                /* mostrar as capas */
	bool fundo_capa;           /* o brilho da capa atrás da letra */
	int tam_capa;              /* 120..240 px: o tamanho das capas na grade */
	/* o que o app lembra */
	int volume;                /* 0..100 */
	int repetir;               /* 0 não, 1 a fila, 2 a faixa */
	bool aleatorio;
	int vista_albuns;          /* 0 grade, 1 lista */
	int ordem_albuns;          /* 0 título, 1 artista, 2 ano */
	bool fila_aberta;          /* o painel da fila à direita */
} mu_cfg;

void mu_cfg_padrao(mu_cfg *c);
void mu_cfg_ler(mu_cfg *c);
bool mu_cfg_gravar(const mu_cfg *c);
char *mu_pasta_config(void);   /* ~/.config/lumna/musica (g_free) */

/* ---- a biblioteca ---- */

typedef struct mu_faixa {
	char *caminho, *pasta, *titulo, *artista, *album, *album_artista, *genero;
	int faixa, disco, ano, duracao;   /* duração em segundos */
	time_t mtime;
	gint64 tamanho;
	bool capa_embutida, letra_tag;
} mu_faixa;

typedef struct mu_acervo {
	mu_faixa *v;
	int n;
} mu_acervo;

void mu_acervo_liberar(mu_acervo *a);
/* Varre as pastas e lê as tags (ffprobe, em 4 threads; o que já está no cache não é lido de novo). progresso(feitos, total, dados) pode vir de qualquer thread. */
void mu_varrer(const mu_cfg *c, mu_acervo *a, void (*progresso)(int, int, void *), void *dados);
/* Lê as tags de UM arquivo (para tocar o que não está na biblioteca). Falso se não é áudio. */
bool mu_ler_faixa(const char *caminho, mu_faixa *f);
void mu_faixa_copiar(mu_faixa *para, const mu_faixa *de);
void mu_faixa_liberar(mu_faixa *f);
bool mu_extensao_de_audio(const char *caminho);

/* ordem do álbum e da biblioteca: disco, faixa, depois o título */
void mu_ordenar_faixas(mu_acervo *a);
void mu_duracao_texto(int segundos, char *saida, size_t tam);   /* 4:08, 1:02:10 */
/* "26 min", "1 h 12 min" */
void mu_duracao_longa(int segundos, char *saida, size_t tam);
/* Texto sem acento e sem maiúsculas, para buscar. */
char *mu_normalizar(const char *texto);

typedef struct mu_grupo {   /* um álbum, um artista ou uma pasta */
	char *nome, *artista;
	int ano, ntotal, duracao;
	int primeira;           /* o índice (no acervo) da faixa que dá a capa */
	int *idx;               /* os índices das faixas, na ordem de tocar */
	int n;
} mu_grupo;

typedef struct mu_grupos {
	mu_grupo *v;
	int n;
} mu_grupos;

enum { MU_ORDEM_TITULO, MU_ORDEM_ARTISTA, MU_ORDEM_ANO };
void mu_albuns(const mu_acervo *a, int ordem, mu_grupos *g);
void mu_artistas(const mu_acervo *a, mu_grupos *g);
void mu_pastas(const mu_acervo *a, mu_grupos *g);
void mu_grupos_liberar(mu_grupos *g);
/* os índices das faixas que combinam com todas as palavras do texto (título, artista, álbum, pasta); devolve quantos (idx tem a->n lugares) */
int mu_buscar(const mu_acervo *a, const char *texto, int *idx);

/* ---- capas ---- */
/* O arquivo da capa (g_free) se já existe: a imagem da pasta (cover/folder/front/album .jpg/.png) ou a do cache; NULL se ainda não. */
char *mu_capa_pronta(const mu_faixa *f);
/* Gera a capa embutida no cache (bloqueia: chame de uma thread). Devolve o arquivo (g_free) ou NULL se a faixa não tem capa. */
char *mu_capa_gerar(const mu_faixa *f);
void mu_capas_limpar(void);
gint64 mu_capas_tamanho(void);

/* ---- letras ---- */
typedef struct mu_linha {
	double tempo;   /* segundos; -1 se a letra não é sincronizada */
	char *texto;
} mu_linha;

typedef struct mu_letra {
	mu_linha *v;
	int n;
	bool sincronizada;
} mu_letra;

void mu_letra_liberar(mu_letra *l);
/* Lê a letra: o .lrc de mesmo nome ao lado do arquivo, ou a tag (bloqueia: de uma thread). Falso se não tem. */
bool mu_letra_ler(const char *caminho, mu_letra *l);
bool mu_letra_de_texto(const char *texto, mu_letra *l);   /* o texto de um .lrc ou de uma tag */
/* A linha da vez em t (a última com tempo <= t + 0,3 s), -1 antes da primeira. */
int mu_letra_linha(const mu_letra *l, double t);

/* ---- playlists (m3u8 em ~/.config/lumna/musica/playlists; "Favoritas" sempre existe) ---- */
#define MU_FAVORITAS "Favoritas"
void mu_playlists_listar(GPtrArray *nomes);   /* GPtrArray de char* (g_free); Favoritas primeiro, o resto por nome */
bool mu_playlist_ler(const char *nome, GPtrArray *caminhos);
bool mu_playlist_gravar(const char *nome, char *const *caminhos, int n);
bool mu_playlist_criar(const char *nome);
bool mu_playlist_renomear(const char *nome, const char *novo);
bool mu_playlist_apagar(const char *nome);
bool mu_playlist_nome_valido(const char *nome);
bool mu_playlist_tem(const char *nome, const char *caminho);
bool mu_playlist_adicionar(const char *nome, char *const *caminhos, int n);   /* sem repetir */
bool mu_playlist_remover(const char *nome, const char *caminho);

/* ---- a fila salva ---- */
typedef struct mu_fila_salva {
	GPtrArray *caminhos;
	int atual;
	double posicao;
} mu_fila_salva;
void mu_fila_gravar(char *const *caminhos, int n, int atual, double posicao);
bool mu_fila_ler(mu_fila_salva *f);
void mu_fila_liberar(mu_fila_salva *f);

#endif
