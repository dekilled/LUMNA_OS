/*
 * A luz noturna (Telas e modos → Luz noturna): cores mais quentes à noite, tirando o azul da tela. O Painel grava
 * ~/.config/lumna/luz.ini e o lumna-luz (exemplos/luz.c) aplica pelo wlr-gamma-control do Orbit:
 *   modo = desligada | sempre | horario
 *   temperatura = 2500 a 6000    quanto mais baixo, mais quente (6500 é a cor normal da tela)
 *   inicio = HH:MM   fim = HH:MM  o horário (vale à meia-noite também); a luz muda aos poucos nos 30 minutos de cada ponta
 */
#ifndef LUMNA_LUZ_H
#define LUMNA_LUZ_H

#include <stdbool.h>
#include <stdint.h>

#define LUZ_NORMAL 6500

enum luz_modo { LUZ_DESLIGADA, LUZ_SEMPRE, LUZ_HORARIO };

struct luz_cfg {
	int modo;
	int temperatura;      /* a do auge da noite */
	int inicio, fim;      /* minutos desde a meia-noite */
};

void luz_ler(struct luz_cfg *c);
bool luz_gravar(const struct luz_cfg *c);

/* a temperatura (em K) que vale neste minuto do dia (0 a 1439); LUZ_NORMAL quando a luz não age */
int luz_kelvin_agora(const struct luz_cfg *c, int minuto);
/* quanto cada cor passa (0 a 1) nessa temperatura */
void luz_rgb(int kelvin, double *r, double *g, double *b);
/* as rampas de gama (tamanho entradas cada) para essa temperatura */
void luz_rampa(int kelvin, uint16_t *r, uint16_t *g, uint16_t *b, uint32_t tamanho);

#endif
