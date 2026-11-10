/*
 * O que a Galeria (lumna-galeria) sabe fazer com fotos e vídeos, sem desenho nenhum.
 *
 *   ~/.config/lumna/galeria/config.ini      os ajustes (pastas da biblioteca, miniaturas, agrupar, importar, edição, reprodução, privacidade)
 *   ~/.config/lumna/galeria/favoritos.txt   um caminho por linha
 *   ~/.config/lumna/galeria/albuns.ini      cada álbum é um grupo [Nome] com a lista de caminhos
 *   ~/.cache/lumna/galeria/                 as miniaturas (fotos e a primeira cena dos vídeos) e a duração dos vídeos
 *
 * A biblioteca é lida e as miniaturas são feitas em threads: nada aqui roda no meio do desenho (o compositor derruba o app que fica parado).
 * A lixeira é a do freedesktop (a mesma do Atlas): mover para a lixeira e restaurar usam as operações dele (arquivos_op.h).
 */
#ifndef LUMNA_GALERIA_H
#define LUMNA_GALERIA_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define GL_PASTAS_MAX 16

/* ---- ajustes ---- */

enum { GL_AGRUPAR_CAPTURA, GL_AGRUPAR_MODIFICACAO, GL_AGRUPAR_PASTA, GL_AGRUPAR_NENHUM };
enum { GL_ORDEM_RECENTES, GL_ORDEM_ANTIGAS, GL_ORDEM_NOME, GL_ORDEM_TAMANHO };
enum { GL_VISTA_GRADE, GL_VISTA_LISTA };
enum { GL_QUAL_RAPIDA, GL_QUAL_EQUILIBRADA, GL_QUAL_ALTA };
enum { GL_DUP_PERGUNTAR, GL_DUP_SUBSTITUIR, GL_DUP_MANTER_AMBOS, GL_DUP_PULAR };
enum { GL_FORMATO_ORIGINAL, GL_FORMATO_JPEG, GL_FORMATO_PNG };
enum { GL_VIDEO_GALERIA, GL_VIDEO_MPV, GL_VIDEO_PADRAO }; /* o player da Galeria; o mpv numa janela separada; o aplicativo padrão do sistema */

typedef struct gl_cfg {
	/* biblioteca */
	char pastas[GL_PASTAS_MAX][4096];
	int npastas;
	bool buscar_auto;        /* olha as pastas e traz as mídias novas sozinha */
	bool subpastas;          /* inclui as subpastas */
	int agrupar;             /* GL_AGRUPAR_* */
	int qualidade;           /* GL_QUAL_*: o tamanho das miniaturas guardadas */
	int duplicados;          /* GL_DUP_*: ao importar um arquivo que já existe */
	bool manter_original;    /* importar copia (verdadeiro) ou move (falso) */
	/* visualização */
	int ordem;               /* GL_ORDEM_* */
	int vista;               /* GL_VISTA_* */
	int tam;                 /* 96..260 px: o tamanho das miniaturas na grade */
	bool nomes;              /* o nome do arquivo embaixo das miniaturas */
	bool ajustar_tela;       /* abrir a foto ajustada à tela (senão no tamanho real) */
	/* edição */
	int formato;             /* GL_FORMATO_*: o da cópia salva pelo editor */
	int qualidade_jpeg;      /* 50..100 */
	/* reprodução */
	int video_com;           /* GL_VIDEO_*: com que se abre um vídeo */
	int volume;              /* 0..100 */
	bool repetir;
	/* privacidade */
	bool cache;              /* guardar as miniaturas no disco */
	bool lixeira_na_barra;   /* mostrar a Lixeira na barra lateral */
	bool confirmar_apagar;   /* confirmar o excluir para sempre */
} gl_cfg;

void gl_cfg_padrao(gl_cfg *c);
void gl_cfg_ler(gl_cfg *c);
bool gl_cfg_gravar(const gl_cfg *c);

/* ---- a biblioteca ---- */

enum gl_tipo { GL_FOTO, GL_VIDEO };

typedef struct gl_midia {
	char *caminho;
	char *nome;
	enum gl_tipo tipo;
	time_t data;             /* a da captura: o EXIF (DateTimeOriginal) de um JPEG; senão a de modificação */
	time_t mtime;            /* a de modificação */
	int64_t tamanho;
	int duracao;             /* segundos; -1: ainda não se sabe (vídeos) */
	bool favorita;
} gl_midia;

typedef struct gl_acervo {
	gl_midia *v;
	int n, cap;
} gl_acervo;

/* Lê as pastas da biblioteca (e as subpastas, se a opção estiver ligada). Bloqueia: chame numa thread. Marca as favoritas. */
void gl_varrer(const gl_cfg *c, gl_acervo *saida);
/* O que há na lixeira (só fotos e vídeos). */
void gl_varrer_lixeira(gl_acervo *saida);
void gl_acervo_liberar(gl_acervo *a);
/* Ordena. Agrupando por pasta, a pasta vem primeiro (os grupos são faixas seguidas) e a ordem vale dentro dela. */
void gl_acervo_ordenar(gl_acervo *a, int ordem, int agrupar);
bool gl_eh_foto(const char *nome);
bool gl_eh_video(const char *nome);
/* A data de captura de um JPEG (DateTimeOriginal, ou DateTime); falso se não tem EXIF com data. */
bool gl_data_exif(const char *caminho, time_t *data);

enum gl_filtro_tipo { GL_F_TODAS, GL_F_FOTOS, GL_F_VIDEOS, GL_F_FAVORITOS, GL_F_RECENTES, GL_F_ALBUM };

typedef struct gl_filtro {
	enum gl_filtro_tipo tipo;
	const char *busca;               /* parte do nome, sem distinguir maiúsculas nem acentos; NULL ou "" : tudo */
	char *const *album_caminhos;     /* GL_F_ALBUM: os caminhos do álbum */
	int album_n;
} gl_filtro;

/* Os índices (em a->v) do que passa no filtro, na ordem em que estão; devolve quantos. indices tem a->n lugares. */
int gl_filtrar(const gl_acervo *a, const gl_filtro *f, int *indices);

/* ---- grupos por data ---- */

typedef struct gl_grupo {
	char rotulo[96];         /* "Hoje", "Ontem", "Outubro de 2026", o nome da pasta; "" sem agrupar */
	int inicio, n;           /* a faixa em indices[] */
} gl_grupo;

/* Divide indices[] (já na ordem) em grupos seguidos. agora: o "hoje". Devolve quantos grupos (no máximo max). */
int gl_agrupar(const gl_acervo *a, const int *indices, int n, int agrupar, time_t agora, gl_grupo *grupos, int max);

/* ---- favoritos ---- */

bool gl_fav_tem(const char *caminho);
void gl_fav_definir(const char *caminho, bool sim);
void gl_fav_marcar(gl_acervo *a);

/* ---- álbuns ---- */

typedef struct gl_album {
	char nome[96];
	GPtrArray *caminhos;     /* char * */
} gl_album;

typedef struct gl_albuns {
	gl_album *v;
	int n;
} gl_albuns;

void gl_albuns_ler(gl_albuns *a);
void gl_albuns_liberar(gl_albuns *a);
/* Cria (falso se o nome é vazio ou já existe). */
bool gl_album_criar(const char *nome);
bool gl_album_renomear(const char *nome, const char *novo);
void gl_album_apagar(const char *nome);
/* Põe no álbum o que ainda não está nele; devolve quantos entraram. */
int gl_album_adicionar(const char *nome, char *const *caminhos, int n);
void gl_album_remover(const char *nome, const char *caminho);
/* Um caminho que mudou (renomeado, restaurado) ou sumiu: tira dos álbuns e dos favoritos o que não existe mais. */
void gl_limpar_orfaos(void);

/* ---- miniaturas e vídeos ---- */

/* O arquivo da miniatura (já feita) desta mídia, ou NULL se ainda não. Faz a miniatura numa thread (uma foto reduzida, ou a primeira cena do vídeo) e
 * chama pronta(dados) DA THREAD quando fica pronta (o app a leva para a linha principal). O ponteiro vale até a próxima limpeza do cache. */
const char *gl_miniatura(const gl_midia *m, int qualidade, void (*pronta)(void *), void *dados);
/* A miniatura não saiu (arquivo estragado, imagem enorme demais, ffmpeg falhou): não adianta pedir de novo. */
bool gl_miniatura_falhou(const gl_midia *m);
/* Guarda as miniaturas no disco (senão numa pasta temporária, que some ao sair). */
void gl_cache_usar_disco(bool sim);
/* A duração já conhecida de um vídeo (do cache), ou -1. Não bloqueia. */
int gl_duracao_conhecida(const char *caminho, time_t mtime);
/* Desiste do que ainda está na fila (outra pasta, outra tela). */
void gl_miniaturas_descartar(void);
/* A duração de um vídeo em segundos (ffprobe; guarda no cache); -1 se não deu. Bloqueia: numa thread. */
int gl_video_duracao(const char *caminho, time_t mtime);
int64_t gl_cache_tamanho(void);
void gl_cache_limpar(void);
/* "0:42", "1:18", "1:02:03" */
void gl_duracao_texto(int segundos, char *saida, size_t tam);

#endif
