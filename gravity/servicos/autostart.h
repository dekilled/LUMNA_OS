/*
 * Os apps que abrem sozinhos ao entrar (o autostart do freedesktop): os .desktop de ~/.config/autostart e
 * de /etc/xdg/autostart (ou de $XDG_CONFIG_DIRS). O do usuário cobre o do sistema de mesmo nome, e é assim que
 * se desliga um app do sistema: uma cópia no usuário com Hidden=true. Quem roda é o lumna-inicio.
 * Para testar, $XDG_CONFIG_HOME e $XDG_CONFIG_DIRS apontam para pastas falsas.
 */
#ifndef LUMNA_AUTOSTART_H
#define LUMNA_AUTOSTART_H

#include <stdbool.h>
#include <stddef.h>

#define AUTOSTART_MAX 96

struct autostart_item {
	char arquivo[200];   /* o nome do .desktop ("syncthing.desktop") */
	char nome[128];
	char comando[512];   /* o Exec= */
	char icone[96];
	bool ligado;         /* roda ao entrar (não está escondido, serve para este desktop) */
	bool do_sistema;     /* vem de /etc/xdg/autostart (e pode ter sido desligado por uma cópia no usuário) */
	bool do_usuario;     /* há um .desktop dele em ~/.config/autostart */
};

/* a lista, em ordem de nome; devolve quantos */
int autostart_listar(struct autostart_item *lista, int max);
/* liga ou desliga (uma cópia no usuário com Hidden=true/false) */
bool autostart_definir(const char *arquivo, bool ligado);
/* um app novo; arquivo (se não NULL) recebe o nome do .desktop criado */
bool autostart_adicionar(const char *nome, const char *comando, const char *icone, char *arquivo, size_t tam);
/* o do usuário sai; o do sistema só se desliga */
bool autostart_remover(const char *arquivo);
/* roda os que estão ligados; devolve quantos abriu */
int autostart_rodar(void);

#endif
