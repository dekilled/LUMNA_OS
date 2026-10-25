/*
 * A tela de login fala com o greetd ($GREETD_SOCK: mensagens JSON com o tamanho na frente, nos
 * bytes da máquina). Quem pode entrar vem do /etc/passwd (uid de 1000 a 59999, com um shell de
 * verdade); as sessões, de /usr/share/wayland-sessions (a do LUMNA primeiro).
 *
 * greetd_entrar faz o login inteiro (criar a sessão, responder a senha, pedir a sessão) e demora
 * o que o PAM demorar: chame-o fora da linha da interface (greetd_entrar_em_fundo).
 */
#ifndef LUMNA_GREETD_H
#define LUMNA_GREETD_H

#include <stdbool.h>

struct greetd_usuario {
	char login[64];
	char nome[128]; /* o nome completo (GECOS); vazio se não houver */
};

struct greetd_sessao {
	char id[64];
	char nome[128];
	char exec[512]; /* o comando (separado por espaços, sem shell) */
};

int greetd_usuarios(struct greetd_usuario *lista, int max);
int greetd_sessoes(struct greetd_sessao *lista, int max);

/* NULL: entrou (o greetd abre a sessão quando esta tela sair); senão, o erro em português */
const char *greetd_entrar(const char *login, const char *senha, const char *exec);

/* o mesmo numa linha à parte: greetd_terminou() diz se acabou, e com que erro (NULL: entrou) */
void greetd_entrar_em_fundo(const char *login, const char *senha, const char *exec);
bool greetd_terminou(const char **erro);

#endif
