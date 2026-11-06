/*
 * A bateria do computador, lida do /sys/class/power_supply/BAT*: a carga, se carrega, a saúde (o que ela segura hoje
 * comparado com o de fábrica), os ciclos e quanto falta. Não precisa de serviço. Para testar, $LUMNA_RAIZ_SISTEMA
 * aponta para uma árvore falsa (sys/class/power_supply/BAT0/...).
 */
#ifndef LUMNA_BATERIA_H
#define LUMNA_BATERIA_H

#include <stdbool.h>

struct bateria_info {
	bool existe;          /* o computador tem bateria */
	char nome[16];        /* "BAT0" */
	int pct;              /* 0 a 100 */
	char estado[24];      /* "Carregando", "Descarregando", "Cheia", "Parada" */
	bool carregando;
	bool na_tomada;       /* ligado na tomada (a bateria carrega ou está cheia) */
	int saude_pct;        /* a capacidade de hoje sobre a de fábrica; -1 se não sabe */
	int ciclos;           /* -1 se não sabe */
	char tecnologia[24];  /* "Li-ion" */
	int minutos;          /* quanto falta para acabar (descarregando) ou para encher (carregando); -1 se não sabe */
	double watts;         /* o que está saindo (ou entrando); 0 se não sabe */
};

bool bateria_ler(struct bateria_info *b);

#endif
