/*
 * "Enviar para": os destinos que o menu de contexto oferece para um arquivo (o Atlas e a área de trabalho usam o mesmo):
 *   - Área de trabalho (~/Desktop) e Documentos;
 *   - as contas na nuvem (a pasta local de cada uma: o que cai lá sobe na próxima sincronização);
 *   - os compartilhamentos de rede que estão conectados agora (a pasta montada pelo gvfs);
 *   - o E-mail (o cliente do sistema, pelo xdg-email, com os arquivos anexados);
 *   - os celulares conectados pelo LUMNA Connect ("Enviar para dispositivo…").
 * Quem copia é quem chama (cada app tem a sua cópia com progresso); aqui só se diz para onde.
 */
#ifndef LUMNA_ENVIAR_H
#define LUMNA_ENVIAR_H

#include <stdbool.h>
#include <stddef.h>

enum env_tipo { ENV_LOCAL, ENV_NUVEM, ENV_REDE };

typedef struct env_destino {
	enum env_tipo tipo;
	char rotulo[96];
	char caminho[1024]; /* a pasta que recebe a cópia (existe, exceto a Área de trabalho: env_preparar cria) */
} env_destino;

#define ENV_MAX 40

/* os destinos de agora, na ordem do menu (locais, nuvens, redes); devolve quantos */
int env_destinos(env_destino *v, int max);
/* cria a pasta se for a Área de trabalho (ainda vazia, ela pode nem existir) e diz se dá para copiar para lá */
bool env_preparar(const env_destino *d);

/* abre uma mensagem nova no cliente de e-mail do sistema com os arquivos anexados; false se não há cliente (ou nada a anexar) */
bool env_email(const char *const *arquivos, int n);

typedef struct env_celular {
	char id[64];
	char nome[96];
} env_celular;

/* os celulares conectados que aceitam arquivos */
int env_celulares(env_celular *v, int max);
/* oferece os arquivos a um celular; em caso de erro devolve false e escreve o motivo */
bool env_enviar_celular(const char *id, const char *const *arquivos, int n, char *erro, size_t tam_erro);

#endif
