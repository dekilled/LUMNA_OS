/*
 * O editor de fotos da Galeria, sem interface: os ajustes (luz, cor, filtro), a geometria (girar, espelhar, recortar) e o salvar uma cópia.
 * Tudo trabalha em GdkPixbuf de 8 bits (RGB ou RGBA). A mesma conta serve para a prévia (uma foto reduzida) e para a cópia final
 * (a foto inteira): o que se vê é o que se salva. O original nunca é tocado.
 */
#ifndef LUMNA_EDICAO_H
#define LUMNA_EDICAO_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdbool.h>
#include <time.h>

enum ed_filtro { ED_F_NENHUM, ED_F_PB, ED_F_SEPIA, ED_F_VIVIDO, ED_F_FRIO, ED_F_QUENTE, ED_F_DESBOTADO, ED_F_DRAMATICO, ED_F_VINTAGE, ED_F_N };
extern const char *const ED_FILTRO_NOME[ED_F_N];

typedef struct ed_params {
	float exposicao;                 /* -3..3 (passos de luz, EV) */
	int contraste, realces, sombras; /* -100..100 */
	int temperatura, saturacao;      /* -100..100 (temperatura: + esquenta) */
	int giro;                        /* 0..3: quartos de volta no sentido horário */
	bool espelho_h, espelho_v;       /* aplicados depois do giro: o que se vê na tela */
	float recorte[4];                /* x, y, largura, altura, de 0 a 1, na foto já girada/espelhada */
	int filtro;                      /* enum ed_filtro */
	int intensidade;                 /* 0..100: quanto do filtro */
} ed_params;

void ed_padrao(ed_params *p);
bool ed_igual(const ed_params *a, const ed_params *b);
bool ed_tem_cor(const ed_params *p);   /* algum ajuste de luz, cor ou filtro */
bool ed_alterado(const ed_params *p);  /* qualquer coisa diferente do original */
void ed_zerar_cor(ed_params *p);       /* "Restaurar ajustes": luz e cor (o filtro e a geometria ficam) */

/* Gira um quarto de volta (dir 1: direita, -1: esquerda) / espelha o que se vê. Ambos desfazem o recorte (ele vale na foto de antes). */
void ed_girar(ed_params *p, int dir);
void ed_espelhar(ed_params *p, bool horizontal);

/* A foto girada/espelhada e, com `com_recorte`, recortada: uma nova (o src não muda). */
GdkPixbuf *ed_geometria(GdkPixbuf *src, const ed_params *p, bool com_recorte);
/* Luz, cor e filtro, no lugar (pb precisa ser gravável). */
void ed_cor(GdkPixbuf *pb, const ed_params *p);
/* Geometria e depois cor: o resultado final, novo. */
GdkPixbuf *ed_processar(GdkPixbuf *src, const ed_params *p, bool com_recorte);

/* ---- o recorte ---- */
enum ed_alca { ED_ALCA_MOVER, ED_ALCA_NO, ED_ALCA_N, ED_ALCA_NE, ED_ALCA_E, ED_ALCA_SE, ED_ALCA_S, ED_ALCA_SO, ED_ALCA_O };
/* O maior retângulo, centralizado, com a proporção prop (largura/altura em pixels; 0: livre = a foto toda) numa foto W × H. */
void ed_recorte_proporcao(float r[4], float prop, int W, int H);
/* Arrasta a alça por (dx, dy), em frações da foto. Com prop > 0 mantém a proporção. Nunca sai da foto nem fica menor que 16 px. */
void ed_recorte_arrastar(float r[4], int alca, float dx, float dy, float prop, int W, int H);
void ed_recorte_pixels(const float r[4], int W, int H, int *x, int *y, int *w, int *h);

/* ---- abrir e salvar ---- */

/* Abre a foto já com a orientação do EXIF aplicada; se max_lado > 0 e ela é maior, reduz até esse lado. */
GdkPixbuf *ed_abrir(const char *caminho, int max_lado, GError **erro);
/* Salva a cópia editada ao lado do original ("foto-editada.jpg", "foto-editada-2.jpg"...). formato: 0 o mesmo do original, 1 JPEG, 2 PNG.
 * `data` (se > 0) vira a data do arquivo novo, para a cópia ficar junto do original na galeria. Devolve o caminho (g_free) ou NULL. */
char *ed_salvar_copia(const char *origem, const ed_params *p, int formato, int qualidade_jpeg, time_t data, GError **erro);

#endif
