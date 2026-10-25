/*
 * O que está tocando, por MPRIS (org.mpris.MediaPlayer2.*: Spotify, navegadores, mpv, o LUMNA
 * Music...). Uma linha à parte fala com o D-Bus da sessão; a app vigia o descritor e lê o estado.
 *
 *   int fd = mpris_iniciar();
 *   gv_app_vigiar(app, fd, mudou, NULL);   // em mudou(): mpris_ler(&estado)
 *   mpris_tocar_pausar();
 *
 * O player escolhido é o que está a tocar; senão, o último que tocou; senão, o primeiro.
 */
#ifndef LUMNA_MPRIS_H
#define LUMNA_MPRIS_H

#include <stdbool.h>
#include <stdint.h>

struct mpris_estado {
	bool tem;           /* há algum player */
	bool tocando;
	char titulo[256], artista[256];
	char capa[1024];    /* um arquivo local (file://), ou vazio */
	int64_t duracao_us;
	int64_t posicao_us;    /* a posição quando se leu (ver mpris_posicao) */
	int64_t lida_us;       /* quando se leu (g_get_monotonic_time) */
	bool pode_anterior, pode_proxima;
};

int mpris_iniciar(void);
void mpris_ler(struct mpris_estado *e);
/* a posição agora (anda sozinha enquanto toca, sem perguntar ao player a cada quadro) */
int64_t mpris_posicao(const struct mpris_estado *e);
void mpris_tocar_pausar(void);
void mpris_proxima(void);
void mpris_anterior(void);

#endif
