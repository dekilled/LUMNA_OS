/*
 * Como o relógio aparece (24 horas ou 12 horas, com AM/PM): ~/.config/lumna/hora.ini, que a seção
 * Data e hora do Painel grava, e a barra, a tela de bloqueio e o calendário leem a cada quadro
 * (a leitura é guardada por uns segundos: custa nada).
 *
 *   [geral]
 *   formato = 24      ; ou 12
 */
#ifndef LUMNA_HORA_H
#define LUMNA_HORA_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

bool hora_24h(void);
/* "14:05" ou "2:05 PM" (com segundos: "14:05:09" / "2:05:09 PM") */
void hora_texto(const struct tm *t, bool segundos, char *buf, size_t tam);

#endif
