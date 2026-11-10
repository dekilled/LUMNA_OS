/*
 * O editor de vídeos da Galeria, sem interface, em cima do ffmpeg/ffprobe: sondar o vídeo, tirar um quadro (a prévia e a faixa do tempo),
 * tocar a prévia (quadros reduzidos, sem som) e exportar a cópia (recorte, áudio e giro). O original nunca é tocado.
 */
#ifndef LUMNA_EDICAO_VIDEO_H
#define LUMNA_EDICAO_VIDEO_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct ev_info {
	double duracao; /* segundos */
	bool audio;
	bool video;
} ev_info;

typedef struct ev_params {
	double inicio, fim; /* o trecho que fica, em segundos */
	bool mudo;
	int volume;         /* 0..200 (%) */
	int giro;           /* 0..3: quartos de volta no sentido horário */
} ev_params;

void ev_padrao(ev_params *p, double duracao);
bool ev_alterado(const ev_params *p, double duracao);
/* Põe o recorte em ordem: dentro do vídeo e com pelo menos `min` segundos entre o início e o fim. */
void ev_ajustar(ev_params *p, double duracao, double min);

bool ev_sondar(const char *caminho, ev_info *info);
/* Um quadro no instante t (já com a rotação do arquivo e o giro de p), reduzido até max_lado. NULL se não deu. */
GdkPixbuf *ev_quadro(const char *caminho, double t, int giro, int max_lado);

/* A prévia tocando: quadros BGRA w×h (tamanho dado, o mesmo do ev_quadro), a `fps`, a partir de t0 (sem som). */
typedef struct ev_toque ev_toque;
ev_toque *ev_tocar(const char *caminho, double t0, int giro, int w, int h, int fps);
/* O próximo quadro em buf (w*h*4 bytes); falso quando acabou (ou foi parado). Bloqueia até chegar. */
bool ev_toque_proximo(ev_toque *t, uint8_t *buf);
void ev_toque_parar(ev_toque *t);   /* mata o ffmpeg (de qualquer thread; quem lia vê o fim) */
void ev_toque_liberar(ev_toque *t); /* fecha e libera: só depois de parar e de quem lia ter saído */

/* Exporta a cópia ao lado do original ("video-editado.mp4"...). progresso(fracao, dados) vai de 0 a 1; *cancelar != 0 aborta (devolve NULL, sem arquivo).
 * `data` (> 0) vira a data do arquivo novo. Devolve o caminho (g_free) ou NULL (erro em *erro, ou cancelado: erro NULL). */
char *ev_exportar(const char *origem, const ev_params *p, time_t data, void (*progresso)(double, void *), void *dados, volatile int *cancelar, GError **erro);

#endif
