/*
 * O player de vídeo do LUMNA (usado pela Galeria), sem interface: o libmpv toca (com som, legendas, todos os formatos) e desenha os quadros em memória
 * (renderização por software, sem GL), que quem usa mostra onde quiser. Tudo o que o mpv conta (tempo, duração, pausa, faixas) vem de propriedades observadas.
 *
 *   vp *p = vp_criar(acorda, dados);      // acorda(dados): de qualquer thread, "tem quadro ou aviso novo": quem usa redesenha
 *   vp_abrir(p, "/caminho/video.mp4", 0, false, 80);
 *   no quadro:  vp_estado e; vp_atualizar(p, &e);  if (vp_quadro(p, w, h, &dados, &stride)) { ...mostra... }
 * LUMNA_GALERIA_AO=null troca a saída de áudio (para os testes).
 */
#ifndef LUMNA_VIDEO_PLAYER_H
#define LUMNA_VIDEO_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vp vp;

typedef struct vp_estado {
	bool aberto;        /* há um vídeo carregado (ou carregando) */
	bool carregando;
	bool tocando;
	bool fim;           /* chegou ao fim (e parou no último quadro) */
	bool erro;          /* não deu para abrir */
	double pos, dur;
	int volume;         /* 0..130 */
	double velocidade;
	int w, h;           /* o tamanho do vídeo */
	int naudio, nlegenda;
	int audio, legenda; /* a faixa escolhida (id do mpv; 0 = nenhuma/desligada) */
	bool repetir;
} vp_estado;

typedef struct vp_faixa {
	int id;
	char titulo[160];   /* "Português (AC3)" ou "Faixa 2" */
	bool escolhida;
} vp_faixa;

vp *vp_criar(void (*acorda)(void *), void *dados);
void vp_liberar(vp *p);
/* Abre o vídeo (substitui o de agora); inicio em segundos. */
bool vp_abrir(vp *p, const char *caminho, double inicio, bool repetir, int volume);
void vp_pausar(vp *p, bool pausar);
void vp_buscar(vp *p, double segundos);       /* vai para esse ponto */
void vp_volume(vp *p, int volume);
void vp_velocidade(vp *p, double v);
void vp_repetir(vp *p, bool sim);
void vp_audio(vp *p, int id);
void vp_legenda(vp *p, int id);               /* 0 desliga */
void vp_parar(vp *p);
/* Lê os avisos do mpv e devolve o estado de agora. */
void vp_atualizar(vp *p, vp_estado *e);
int vp_faixas(vp *p, bool legendas, vp_faixa *v, int max);
/* Desenha o quadro de agora em um buffer BGRA/BGRX w × h (o mpv põe as barras pretas); devolve verdadeiro se mudou desde a última vez
 * (ou se o tamanho mudou). O buffer vale até a próxima chamada. */
bool vp_quadro(vp *p, int w, int h, uint8_t **dados, int *stride);
/* Uma propriedade de texto do mpv (para os testes). */
bool vp_propriedade(vp *p, const char *nome, char *saida, size_t tam);

#endif
