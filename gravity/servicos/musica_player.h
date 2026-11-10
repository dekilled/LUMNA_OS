/*
 * O motor do LUMNA Music: o mpv por baixo (sem janela), controlado pelo socket JSON dele. Sem interface; os avisos (a faixa acabou, passou para a próxima)
 * chegam por um callback, de qualquer thread.
 *
 *   - sem intervalos: a próxima faixa entra na lista do mpv e ele emenda sozinho (gapless);
 *   - transição > 0: um segundo mpv começa a próxima antes do fim e as duas se misturam (crossfade);
 *   - nem uma coisa nem outra: acabou, o callback avisa e quem chama toca a seguinte.
 * LUMNA_MUSICA_AO=null troca a saída de áudio (para os testes).
 */
#ifndef LUMNA_MUSICA_PLAYER_H
#define LUMNA_MUSICA_PLAYER_H

#include <glib.h>
#include <stdbool.h>

typedef struct mp_player mp_player;

enum { MP_EV_AVANCOU = 1, MP_EV_FIM, MP_EV_ERRO, MP_EV_MORREU, MP_EV_ESTADO /* pausou ou voltou a tocar (por fora também): é hora de redesenhar */ };

typedef struct mp_estado {
	bool vivo;       /* o mpv está de pé */
	bool carregada;  /* há uma faixa carregada */
	bool tocando;
	double pos, dur; /* segundos (a posição anda sozinha entre as leituras do mpv) */
	int volume;
	char caminho[4096];
} mp_estado;

mp_player *mp_criar(void (*evento)(int tipo, void *dados), void *dados);
void mp_liberar(mp_player *p);   /* mata o que for do mpv */
void mp_ajustes(mp_player *p, const char *saida, bool normalizar, bool sem_intervalos, int transicao);
/* Toca a faixa (substitui a de agora). pausado: carrega e espera. Falso se o mpv não subiu. */
bool mp_tocar(mp_player *p, const char *caminho, double inicio, bool pausado);
/* A que vem depois da de agora (NULL: nenhuma); põe o gapless ou o crossfade no lugar. */
void mp_proxima(mp_player *p, const char *caminho);
void mp_pausar(mp_player *p, bool pausar);
void mp_buscar(mp_player *p, double segundos);   /* vai para esse ponto */
void mp_volume(mp_player *p, int volume);        /* 0..100 */
void mp_parar(mp_player *p);                     /* descarrega a faixa */
void mp_estado_ler(mp_player *p, mp_estado *e);
/* Lê uma propriedade numérica do mpv (para os testes); falso se não deu. */
bool mp_consultar(mp_player *p, const char *propriedade, double *valor);
bool mp_consultar_texto(mp_player *p, const char *propriedade, char *saida, size_t tam);

/* Os dispositivos de áudio que o mpv conhece: "nome\tdescrição" (g_free cada um). O primeiro é "auto". */
void mp_dispositivos(GPtrArray *saida);

#endif
