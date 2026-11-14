/*
 * A linha do tempo da animação de abertura (a que toca depois do login). Só conta: "no instante e (segundos desde que começou), em que ponto do
 * arquivo abertura.mp4 estar?". Sem tela e sem libmpv, para valer igual em todas as telas (cada tela é um processo) e para ser testada.
 *
 * O arquivo (iso/branding/abertura/gera.sh) tem quatro trechos:  [ start ][ F ][ R ][ end ]
 *   start: as letras surgem, o curto-circuito e o sussurro da Emma (toca sempre inteiro, a não ser que se pule);
 *   F e R: o mesmo vídeo lento (zoom out) para a frente e para trás: a "respiração". Repete enquanto o desktop não está pronto;
 *   end: a distorção e o escurecer.
 * Quando o desktop fica pronto (pronto >= 0, em segundos desde o começo), a animação sai do laço sem salto: se está em F, termina F; se está
 * em R, pula para o ponto igual de F (o mesmo quadro, só que o zoom segue para fora) e termina F. Em F fim, vai para o end. Pular (pulo >= 0) vai direto
 * para o end.
 */
#ifndef LUMNA_ABERTURA_TEMPO_H
#define LUMNA_ABERTURA_TEMPO_H

#include <stdbool.h>

typedef struct ab_tempos {
	double frente;   /* onde começa F (= a duração do start) */
	double trecho;   /* a duração de F (e de R) */
	double fim;      /* onde começa o end (frente + 2 * trecho) */
	double dur_fim;  /* a duração do end */
} ab_tempos;

typedef struct ab_eventos {
	double pronto;   /* o desktop ficou pronto neste instante; < 0: ainda não */
	double pulo;     /* alguém pulou neste instante; < 0: não */
} ab_eventos;

/* O ponto do arquivo (segundos) para o instante e. *acabou fica verdadeiro quando o end terminou (o resto é só o esmaecer). */
double ab_posicao(const ab_tempos *t, const ab_eventos *ev, double e, bool *acabou);

/* Quanto tempo falta, no mínimo, se ficasse pronto agora (para os testes e para o aviso do log): o instante em que o end começa. */
double ab_inicio_do_fim(const ab_tempos *t, const ab_eventos *ev);

/* Lê abertura.ini (o que gera.sh escreve). Devolve falso se faltar. */
bool ab_ler_tempos(const char *caminho, ab_tempos *t);

#endif
