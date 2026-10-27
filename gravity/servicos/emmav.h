/*
 * A voz da Emma no serviço: ouvir o microfone do sistema (parec ou arecord, 16 kHz mono), achar onde a fala
 * começa e termina pelo volume, e falar (o texto vira áudio no backend e toca por pw-play ou paplay, uma frase
 * por vez, buscando a próxima enquanto toca a atual).
 *
 * Tudo bloqueia: ouvir roda numa linha à parte. Para testar: $LUMNA_GRAVADOR (um comando que escreve o PCM
 * s16le 16 kHz mono na saída, no lugar do parec) e $LUMNA_TOCADOR (no lugar do pw-play).
 */
#ifndef LUMNA_EMMAV_H
#define LUMNA_EMMAV_H

#include <glib.h>
#include <stdbool.h>

/* grava até o fim da fala; devolve o WAV (apagar depois) ou NULL se ninguém falou (erro: o microfone não abriu).
 * nivel() sai a cada 100 ms com o volume de 0 a 1 e se já começou a falar */
char *emmav_ouvir(void (*nivel)(double nivel, bool fala, void *dados), void *dados, char **erro);
/* termina a gravação agora: cancelar descarta, senão fica o que já veio */
void emmav_ouvir_parar(bool cancelar);
bool emmav_ouvindo(void);

/* a fila de fala: estado("falando") quando começa, estado("calada") quando acaba */
void emmav_falar_iniciar(void (*estado)(const char *estado, void *dados), void *dados);
void emmav_falar(const char *texto);
void emmav_calar(void);                 /* corta o que toca e esvazia a fila */
bool emmav_falando(void);

/* o PCM s16le mono 16 kHz vira um arquivo WAV (para o /api/stt); a detecção está aqui para o teste */
bool emmav_wav_gravar(const char *caminho, const guint8 *pcm, gsize n);

/* o detector de fala: empurre o volume de cada pedaço de 50 ms */
struct emmav_detector {
	double piso;
	int altos;
	bool comecou;
	int silencio_ms, passado_ms;
};
enum { EMMAV_ESPERA, EMMAV_FALANDO, EMMAV_FIM, EMMAV_NADA };
int emmav_detector_empurrar(struct emmav_detector *d, double volume);

#endif
