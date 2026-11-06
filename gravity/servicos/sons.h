/*
 * Os sons do sistema (Som → Sons do sistema): o som dos avisos. Os sons são os do tema freedesktop
 * (/usr/share/sounds/freedesktop/stereo, pacote sound-theme-freedesktop) e tocam pelo pw-play ou paplay.
 * O ajuste fica em ~/.config/lumna/sons.ini:
 *   avisos = sim|nao   toca um som quando chega um aviso (os de prioridade baixa e os que pedem silêncio não tocam)
 *   volume = 0 a 100
 *   aviso = nome       o som dos avisos normais
 *   critico = nome     o som dos avisos críticos
 * Para testar, $LUMNA_SONS_DIR aponta para uma pasta falsa e o PATH para um pw-play falso.
 */
#ifndef LUMNA_SONS_H
#define LUMNA_SONS_H

#include <stdbool.h>
#include <stddef.h>

#define SONS_MAX 64

struct sons_cfg {
	bool avisos;
	int volume;
	char aviso[40], critico[40];
};

void sons_ler(struct sons_cfg *c);
bool sons_gravar(const struct sons_cfg *c);
/* os sons que há (sem a extensão), por nome; devolve quantos */
int sons_listar(char nomes[][40], int max);
/* toca um som (pelo nome) no volume dado, sem esperar; false se não achou o som ou o tocador */
bool sons_tocar(const char *nome, int volume);
/* "message-new-instant" → "Mensagem nova" (o nome que a pessoa lê; o próprio nome se não souber) */
const char *sons_rotulo(const char *nome);

#endif
