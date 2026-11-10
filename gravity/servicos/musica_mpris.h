/*
 * O LUMNA Music como player MPRIS (org.mpris.MediaPlayer2.lumna_music): a tela de bloqueio, o controle do celular, a Mídia e as teclas de mídia o
 * enxergam e o controlam. Fala com o D-Bus da sessão numa thread própria; as ações chegam por callbacks, DESSA thread (quem usa passa para a sua).
 */
#ifndef LUMNA_MUSICA_MPRIS_H
#define LUMNA_MUSICA_MPRIS_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct mm_estado {
	bool tem_faixa, tocando;
	char titulo[256], artista[256], album[256], capa[1024];   /* capa: caminho de arquivo ou vazio */
	char id[64];                                                /* identifica a faixa (muda quando a faixa muda) */
	int64_t duracao_us, pos_us;
	double volume;       /* 0..1 */
	bool pode_proxima, pode_anterior;
	bool aleatorio;
	int repetir;         /* 0 nenhum, 1 a fila, 2 a faixa */
} mm_estado;

typedef struct mm_acoes {
	void (*tocar)(void *);
	void (*pausar)(void *);
	void (*alternar)(void *);
	void (*parar)(void *);
	void (*proxima)(void *);
	void (*anterior)(void *);
	void (*buscar)(void *, int64_t delta_us);
	void (*posicionar)(void *, int64_t pos_us);
	void (*volume)(void *, double v);
	void (*aleatorio)(void *, bool ligado);
	void (*repetir)(void *, int modo);
	void (*abrir_janela)(void *);
	void (*abrir_uri)(void *, const char *uri);
	void *dados;
} mm_acoes;

typedef struct mm_mpris mm_mpris;

/* Sobe o serviço; o nome "org.mpris.MediaPlayer2.<sufixo>" (sufixo vazio: lumna_music). NULL se não há D-Bus de sessão. */
mm_mpris *mm_iniciar(const mm_acoes *acoes, const char *sufixo);
/* O estado de agora; manda PropertiesChanged do que mudou. De qualquer thread. */
void mm_atualizar(mm_mpris *m, const mm_estado *e);
/* A posição pulou (busca do usuário): manda o sinal Seeked. */
void mm_pulou(mm_mpris *m, int64_t pos_us);
void mm_parar(mm_mpris *m);

/* Instância única: se já há um player com esse nome no D-Bus da sessão, manda para ele os arquivos (OpenUri, um por um) e o Raise, e devolve verdadeiro:
 * quem chamou deve sair sem abrir janela. Falso se não há outro (ou não há D-Bus): siga em frente. caminhos: caminhos ou URIs. */
bool mm_encaminhar(const char *sufixo, char *const *caminhos, int n);

#endif
