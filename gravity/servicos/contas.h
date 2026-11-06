/*
 * As contas do computador (Painel → Usuários): quem existe (do /etc/passwd e /etc/group), o nome e a foto de quem está
 * logado, e as mudanças que pedem root (criar, tirar, administrador, nome e senha de outra conta), que o
 * session/lumna-usuarios faz pelo pkexec. Para testar, $LUMNA_ETC aponta para um /etc falso, $LUMNA_USUARIOS_AJUDANTE
 * para o ajudante e $LUMNA_SEM_PKEXEC liga direto (sem a janela de senha).
 */
#ifndef LUMNA_CONTAS_H
#define LUMNA_CONTAS_H

#include <stdbool.h>
#include <stddef.h>

#define CONTAS_MAX 32

struct conta {
	char login[64];
	char nome[128];   /* o nome completo (vazio se não tem) */
	int uid;
	bool admin;       /* do grupo wheel ou sudo */
	bool atual;       /* quem está logado */
};

/* as contas de gente (uid de 1000 a 59999, com um shell de verdade); a de quem está logado primeiro */
int contas_listar(struct conta *lista, int max);

bool contas_adicionar(const char *login, const char *nome, const char *senha, bool admin, char *erro, size_t tam);
bool contas_remover(const char *login, bool apagar_pasta, char *erro, size_t tam);
bool contas_admin(const char *login, bool admin, char *erro, size_t tam);
bool contas_nome(const char *login, const char *nome, char *erro, size_t tam);
bool contas_senha(const char *login, const char *senha, char *erro, size_t tam);

/* o nome de usuário de quem está logado */
const char *contas_eu(void);

/* a foto de quem está logado (~/.face): o caminho se existe, senão NULL; definir a partir de uma imagem (recorta ao quadrado e vira 256 px) */
const char *contas_foto(void);
bool contas_foto_definir(const char *imagem, char *erro, size_t tam);
bool contas_foto_tirar(void);

#endif
