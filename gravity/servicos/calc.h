/*
 * O motor da calculadora (lumna-calculadora), sem tela: lê uma conta escrita em português e devolve o resultado.
 *
 *   128 × 3            85 / 4            2^10            (3 + 4) × 5       raiz(81)    √81    pi
 *   15% de 89,90       200 + 10%         200 - 10%       200 × 10%
 *   89,90 + 39,90      1.234,56 × 2      (vírgula decimal; ponto como separador de milhar quando vem em grupos de 3)
 *   5 km em milhas     3h20 × 4          3 horas e 20 min       100 °C em °F       2 GB em MB       90 km/h em m/s
 *
 * Porcentagem: "N% de X" é N/100 de X; "X + N%" e "X - N%" são relativos a X; "X × N%" é N/100 de X; "N%" sozinho continua N%.
 * Unidades: comprimento, massa, tempo, volume, temperatura, velocidade e dados (GB = 1000 MB; GiB = 1024 MiB). Soma e subtração pedem a
 * mesma grandeza; "em"/"para" converte. Temperatura só converte (não soma). Moeda não entra: sem cotação offline.
 */
#ifndef LUMNA_CALC_H
#define LUMNA_CALC_H

#include <stdbool.h>
#include <stddef.h>

enum calc_estado {
	CALC_OK,         /* texto tem o resultado */
	CALC_VAZIO,      /* nada escrito */
	CALC_INCOMPLETO, /* falta o resto da conta ("128 ×", "(3 + 4"): ainda não é erro */
	CALC_ERRO,       /* erro tem o motivo */
};

struct calc_res {
	char texto[160];   /* o resultado como se mostra: "1.234,56", "10%", "3,106856 mi", "13 h 20 min" */
	char numero[64];   /* só o número, no mesmo formato (para copiar e reusar numa conta) */
	double valor;      /* o número (na unidade mostrada) */
	char erro[160];
};

enum calc_estado calc_avaliar(const char *entrada, struct calc_res *saida);

/* "128*3" → "128 × 3", "85/4" → "85 ÷ 4" (o "km/h" fica como está) */
void calc_embelezar(const char *entrada, char *saida, size_t tam);

#endif
