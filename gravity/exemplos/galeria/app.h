/*
 * A Galeria (lumna-galeria) em Gravity: o estado compartilhado entre as telas.
 *   principal.c     a janela, a barra lateral, o cabeçalho, a barra de seleção, o rodapé, os menus, os diálogos e as operações
 *   grade.c         as mídias por data (grade e lista), com seleção
 *   visualizador.c  uma foto ou um vídeo em tela cheia (zoom, deslizar, detalhes)
 *   ajustes.c       a engrenagem: Biblioteca, Visualização, Edição, Reprodução e Privacidade
 * O que sabe de arquivos, EXIF, álbuns e miniaturas fica em servicos/galeria.c.
 */
#ifndef LUMNA_GALERIA_APP_H
#define LUMNA_GALERIA_APP_H

#include <glib.h>

#include "galeria.h"
#include "gravity.h"

enum vista { V_TODAS, V_FOTOS, V_VIDEOS, V_FAVORITOS, V_RECENTES, V_LIXEIRA, V_ALBUM };
enum tela { T_BIBLIOTECA, T_VISUALIZADOR, T_AJUSTES };

typedef struct estado {
	gv_app *app;
	int tela;
	gl_cfg cfg;            /* o que vale agora */
	gl_cfg edit;           /* o que se mexe nos ajustes (vale ao Aplicar) */
	gl_acervo acervo;      /* a biblioteca */
	gl_acervo lixo;        /* a lixeira (lida ao abrir a vista) */
	bool carregando;
	gl_albuns albuns;
	int vista;
	int album;             /* V_ALBUM: o índice em albuns */
	char busca[256];
	int *vis;              /* os índices (no acervo de agora) do que a vista mostra, na ordem */
	int nvis;
	gl_grupo grupos[512];
	int ngrupos;
	GHashTable *sel;       /* os caminhos selecionados */
	int ancora;            /* a posição (em vis) do último clicado, para o Shift */
	int aberta;            /* V visualizador: a posição (em vis) da mídia aberta */
	int pagina_ajustes;
} estado;

extern estado G;

/* o acervo que a vista de agora mostra (a biblioteca, ou a lixeira) */
static inline const gl_acervo *acervo_atual(void) {
	return G.vista == V_LIXEIRA ? &G.lixo : &G.acervo;
}
static inline const gl_midia *midia_vis(int k) {
	return &acervo_atual()->v[G.vis[k]];
}

/* principal.c */
void aviso(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void refiltrar(void);
void recarregar_biblioteca(void);
void mudar_vista(int vista, int album);
void ao_miniatura_pronta(void *dados);
void sel_alternar(const char *caminho);
void sel_limpar(void);
int sel_n(void);
void sel_intervalo(int de, int ate);
void alternar_favorita(const gl_midia *m);
void mover_para_lixeira(char *const *caminhos, int n);
void abrir_midia(int k);
void abrir_no_player(const gl_midia *m);
void aplicar_ajustes(const gl_cfg *novos);
void menu_da_midia(int k);

/* grade.c */
void grade_desenhar(Clay_ElementId rolagem);
void miniatura(const gl_midia *m, float lado, Clay_ElementId id, bool arredondar);

/* visualizador.c */
void visualizador_abrir(int k);
void visualizador_desenhar(void);
void visualizador_fechar(void);
bool visualizador_reposicionar(void); /* a lista mudou com uma foto aberta: ela segue onde estiver (verdadeiro) ou já não existe */

/* ajustes.c */
void ajustes_abrir(int pagina);
void ajustes_desenhar(void);
bool ajustes_dialogo(void); /* o diálogo de adicionar pasta, por cima de tudo; verdadeiro se está aberto */

#endif
