/*
 * Os monitores, para o Painel (Telas e modos → Monitores): o que o Orbit vê agora (orbit-msg saidas) e o que
 * se grava em ~/.config/lumna/telas.ini, que o Orbit vigia e aplica na hora:
 *
 *   [HDMI-A-1]
 *   ligada=1
 *   modo=1920x1080@60
 *   escala=1.25
 *   rotacao=0|90|180|270
 *   x=0
 *   y=0
 *
 * O que não está no arquivo o Orbit deixa como está. Para testar: $XDG_CONFIG_HOME e um Orbit de teste.
 */
#ifndef LUMNA_TELAS_H
#define LUMNA_TELAS_H

#include <stdbool.h>
#include <stddef.h>

#define TELAS_MAX 8
#define TELAS_MAX_MODOS 128

struct tela_modo {
	int l, a, hz;
};

struct tela {
	char nome[32], fabricante[48], modelo[64];
	int largura, altura, hz;     /* o modo de agora */
	double escala;
	int rotacao;                 /* 0, 90, 180, 270 */
	int x, y;                    /* posição no layout, em pixels lógicos */
	bool ligada, desligavel, telemovel, ponteiro;
	int mm_l, mm_a;              /* o tamanho físico, 0 se não se sabe */
	struct tela_modo modos[TELAS_MAX_MODOS];
	int nmodos;
};

struct telas_estado {
	bool orbit;                  /* o Orbit respondeu */
	struct tela v[TELAS_MAX];
	int n;
};

bool telas_ler(struct telas_estado *e);

/* o tamanho em pixels lógicos de uma tela, já com a rotação e a escala */
void tela_tamanho_logico(const struct tela *t, int *w, int *h);
/* "27\"" (da diagonal em mm), ou "" */
const char *tela_polegadas(const struct tela *t);

/* uma seção do telas.ini */
struct tela_cfg {
	char nome[32];
	int ligada;                  /* 0 ou 1 */
	int l, a, hz;
	double escala;
	int rotacao;
	bool pos;
	int x, y;
};

/* o arquivo inteiro (as seções de monitores que não estão ligados agora ficam como estão) */
int telas_cfg_ler(struct tela_cfg *v, int max);
/* troca as seções de v (as que já existiam com o mesmo nome) e guarda as outras; true se gravou */
bool telas_cfg_gravar(const struct tela_cfg *v, int n);
/* a cfg que deixaria a tela exatamente como está agora (para desfazer) */
void telas_cfg_de_estado(const struct tela *t, struct tela_cfg *c);

#endif
