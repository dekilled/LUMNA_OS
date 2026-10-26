/*
 * O som do LUMNA para as apps Gravity: as saídas (alto-falante, fones, HDMI), o volume e o mudo
 * de cada uma e de cada app que está tocando, e a saída padrão. Fala com o PipeWire pela
 * interface do PulseAudio (pipewire-pulse) e assina as mudanças: quando outra coisa mexe no
 * volume (as teclas, a topbar, outra app), o descritor de aviso fica legível.
 *
 *   int aviso = som_iniciar("Mixer");
 *   gv_app_vigiar(app, aviso, mudou, NULL);    // em mudou(): som_ler(&estado)
 *   som_volume_saida(estado.saidas[i].indice, 0.5f);
 *
 * Volumes de 0 a 1,5 (150%), na escala do wpctl e do pactl. Os pedidos voltam pelo aviso.
 */
#ifndef LUMNA_SOM_H
#define LUMNA_SOM_H

#include <stdbool.h>
#include <stdint.h>

#define SOM_MAX_SAIDAS 16
#define SOM_MAX_APPS 32

struct som_no {
	uint32_t indice;
	char nome[128];      /* o nome interno (da saída) */
	char descricao[128]; /* o que a pessoa lê: "Alto-falante", o nome da app */
	char icone[64];      /* o ícone que a app declara (application.icon_name), pode vir vazio */
	char binario[64];    /* application.process.binary */
	int canais;
	float volume;        /* 0..1,5 */
	bool mudo;
	bool padrao;         /* a saída (ou a entrada) padrão */
};

struct som_estado {
	bool ligado; /* falou com o servidor de som */
	struct som_no saidas[SOM_MAX_SAIDAS];
	int nsaidas;
	struct som_no apps[SOM_MAX_APPS];
	int napps;
	struct som_no entradas[SOM_MAX_SAIDAS]; /* os microfones (sem os "monitores" das saídas) */
	int nentradas;
};

/* Liga ao servidor de som numa linha à parte. Devolve o descritor de aviso (ou -1). */
int som_iniciar(const char *nome_app);
/* Copia o estado mais recente e limpa o aviso. */
void som_ler(struct som_estado *estado);
void som_parar(void);

void som_volume_saida(uint32_t indice, float volume);
void som_mudo_saida(uint32_t indice, bool mudo);
void som_saida_padrao(const char *nome);
void som_volume_entrada(uint32_t indice, float volume);
void som_mudo_entrada(uint32_t indice, bool mudo);
void som_entrada_padrao(const char *nome);
void som_volume_app(uint32_t indice, float volume);
void som_mudo_app(uint32_t indice, bool mudo);

/* A saída padrão do estado (ou NULL). */
const struct som_no *som_saida_atual(const struct som_estado *estado);
/* O microfone padrão do estado (ou NULL). */
const struct som_no *som_entrada_atual(const struct som_estado *estado);

#endif
