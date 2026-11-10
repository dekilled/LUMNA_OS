/*
 * A Galeria (lumna-galeria) em Gravity: o estado compartilhado entre as telas.
 *   principal.c     a janela, a barra lateral, o cabeçalho, a barra de seleção, o rodapé, os menus, os diálogos e as operações
 *   grade.c         as mídias por data (grade e lista), com seleção
 *   visualizador.c  uma foto ou um vídeo em tela cheia (zoom, deslizar, detalhes)
 *   ajustes.c       a engrenagem: Biblioteca, Visualização, Edição, Reprodução e Privacidade
 *   player.c        o player de vídeo (libmpv dentro da janela, com os controles do LUMNA)
 *   editor_video.c  o editor de vídeos: recorte na linha do tempo, áudio e giro (a conta é de servicos/edicao_video.c)
 *   editor.c        o editor de fotos: Ajustar, Recortar, Girar e Filtros (a conta é de servicos/edicao.c)
 * O que sabe de arquivos, EXIF, álbuns e miniaturas fica em servicos/galeria.c.
 */
#ifndef LUMNA_GALERIA_APP_H
#define LUMNA_GALERIA_APP_H

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include "galeria.h"
#include "gravity.h"

enum vista { V_TODAS, V_FOTOS, V_VIDEOS, V_FAVORITOS, V_RECENTES, V_LIXEIRA, V_ALBUM };
enum tela { T_BIBLIOTECA, T_VISUALIZADOR, T_AJUSTES, T_EDITOR, T_EDITOR_VIDEO, T_PLAYER };

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
void abrir_no_player_externo(const gl_midia *m);
void gravar_cfg(void);
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

/* editor.c */
void editor_abrir(const gl_midia *m, int voltar_para); /* voltar_para: a tela de onde veio (T_VISUALIZADOR ou T_BIBLIOTECA) */
void editor_desenhar(void);
void editor_estado(GString *s); /* para os testes */
cairo_surface_t *superficie_de(GdkPixbuf *pb); /* ARGB32 do pixbuf (para o cairo) */

/* player.c */
void player_abrir(int k);              /* o vídeo k da vista (posição em vis) */
void player_desenhar(void);
void player_estado(GString *s);        /* para os testes */
bool player_tela_cheia(void);          /* a janela está em tela cheia por causa do player (o título some) */
void player_fechar(void);
void player_encerrar(void);            /* ao sair do app */
bool visualizador_reposicionar_video(void); /* a lista mudou com o player aberto: ele segue o vídeo (verdadeiro) ou já não existe */

/* editor_video.c */
void editor_video_abrir(const gl_midia *m);
void editor_video_desenhar(void);
void editor_video_estado(GString *s); /* para os testes */

/* ajustes.c */
void ajustes_abrir(int pagina);
void ajustes_desenhar(void);
bool ajustes_dialogo(void); /* o diálogo de adicionar pasta, por cima de tudo; verdadeiro se está aberto */

#endif
