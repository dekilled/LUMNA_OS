/*
 * O LUMNA Music (lumna-music) em Gravity: o estado compartilhado entre as telas.
 *   principal.c   a janela, a busca, o motor (tocar, fila, eventos), as capas, as playlists, o MPRIS e a depuração
 *   barra.c       a barra lateral, a barra do player, o painel da fila, os menus e os diálogos
 *   biblioteca.c  as listas: Minha biblioteca, Álbuns, Artistas, Pastas, o detalhe (álbum, artista, pasta, playlist) e os resultados da busca
 *   ouvir.c       "Ouvir agora": a capa, a faixa e a letra sincronizada
 *   ajustes.c     Configurações: Reprodução, Biblioteca, Letras, Aparência e Emma
 * Quem sabe de arquivos, tags, capas, letras e playlists é servicos/musica.c; quem toca é servicos/musica_player.c (mpv); quem fala MPRIS é musica_mpris.c.
 */
#ifndef LUMNA_MUSIC_APP_H
#define LUMNA_MUSIC_APP_H

#include <cairo.h>
#include <glib.h>

#include "gravity.h"
#include "musica.h"
#include "musica_mpris.h"
#include "musica_player.h"

enum vista { V_OUVIR, V_BIBLIOTECA, V_ALBUNS, V_ARTISTAS, V_PASTAS, V_DETALHE, V_BUSCA, V_AJUSTES };
enum detalhe_tipo { D_ALBUM, D_ARTISTA, D_PASTA, D_PLAYLIST };

typedef struct item {   /* uma faixa na fila */
	mu_faixa f;
	int seq;            /* a ordem de antes de embaralhar */
} item;

typedef struct estado {
	gv_app *app;
	mu_cfg cfg;
	mu_acervo ac;                   /* a biblioteca */
	mu_grupos albuns, artistas, pastas;
	bool carregando;
	int prog_feitos, prog_total;
	GHashTable *por_caminho;        /* caminho -> índice no acervo + 1 */
	int vista, vista_antes;
	/* o detalhe aberto */
	int det_tipo, det_grupo;
	char det_playlist[256];
	int *det_idx;
	int det_n;
	/* a busca */
	char busca[256];
	int *busca_idx;
	int busca_n;
	/* playlists e favoritas */
	GPtrArray *playlists;
	GHashTable *favoritas;
	/* a fila */
	item *fila;
	int nfila, capfila, atual;      /* atual: -1 sem faixa */
	int anunciada;                  /* o índice que o motor já sabe que vem depois (-1 nenhum) */
	bool carregada_no_motor;        /* a faixa atual já está no mpv */
	double pos_pendente;            /* a posição da fila restaurada, para quando tocarem */
	int erros_seguidos;
	/* o motor */
	mp_player *mp;
	mp_estado est;
	GPtrArray *dispositivos;        /* "nome\tdescrição" */
	/* a letra */
	mu_letra letra;
	char letra_de[4096];
	bool letra_lendo;
	int letra_linha;
	bool letra_seguir;
	/* o MPRIS */
	mm_mpris *mm;
	double mm_ultimo;
	char mm_id[64];
	int ajustes_aba;
} estado;

extern estado M;

/* ---- principal.c ---- */
void aviso(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ir_para(int vista);
void abrir_detalhe(int tipo, int grupo, const char *playlist);
void recarregar_biblioteca(void);
const mu_faixa *faixa_atual(void);
void tocar_lista(const int *idx, int n, int inicio, bool aleatorio);   /* idx: índices no acervo */
void tocar_indice(int i);                                              /* na fila */
void fila_adicionar(const int *idx, int n, bool depois_da_atual);      /* "tocar a seguir" ou no fim */
void alternar_toque(void);
void proxima(void);
void anterior(void);
void salvar_cfg(void);
void aplicar_cfg(void);
bool eh_favorita(const char *caminho);
void alternar_favorita(const mu_faixa *f);
void menu_da_faixa(int acervo_idx);         /* prepara o menu de uma faixa da biblioteca (abre com gv_menu_pedir_no_ponteiro) */
void listar_playlists(void);
void abrir_dialogo_playlist(int modo, const char *nome);  /* 0 nova, 1 renomear */
void abrir_dialogo_pasta(void);
void tocar_arquivos(char **caminhos);                     /* o que soltaram na janela */
int acervo_indice(const char *caminho);                   /* -1 se não está na biblioteca */
const char *capa_da_faixa(const mu_faixa *f);             /* o arquivo da capa, "" se não tem, NULL se ainda procurando */
void capa_cor(const mu_faixa *f, float *r, float *g, float *b);
const char *aviso_atual(void);                            /* o aviso de agora, ou NULL */
void fila_remover(int i);
void fila_mover(int de, int para);
void fila_limpar(void);

/* ---- barra.c: a barra lateral, a barra do player, o painel da fila, os menus e os diálogos ---- */
void barra_lateral(void);
void barra_do_player(void);
void painel_da_fila(void);
void menus_e_dialogos(void);
void coracao(Clay_ElementId id, bool cheio, float lado, bool *clicou);   /* o coração (cheio quando favorita) */
const char *artista_da_faixa(const mu_faixa *f);
bool dialogo_aberto(void);

/* ---- biblioteca.c ---- */
void tela_biblioteca(void);
void tela_albuns(void);
void tela_artistas(void);
void tela_pastas(void);
void tela_detalhe(void);
void tela_busca(void);
void capa_quadrada(Clay_ElementId id, const mu_faixa *f, float lado, float raio);
void recarregar_listas(void);   /* depois de mudar o acervo ou as playlists */
void abrir_detalhe_de_faixas(const char *titulo, const char *sub, const int *idx, int n);
void barra_de_rolagem_zero(void);

/* ---- ouvir.c ---- */
void tela_ouvir(void);
void ouvir_letra_mudou(void);   /* a faixa mudou: lê a letra */
void ouvir_quadro(void);        /* todo quadro: a linha da vez */
void barras_tocando(Clay_ElementId id, float lado, bool animar);

/* ---- ajustes.c ---- */
void tela_ajustes(void);
bool ajustes_dialogo(void);

#endif
