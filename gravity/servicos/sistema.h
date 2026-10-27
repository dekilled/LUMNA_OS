/*
 * O que o Painel mostra em Sobre: o computador (modelo, processador, memória, vídeo, sistema,
 * versão do LUMNA), as atualizações do sistema (checkupdates: sem senha, sem mexer no banco do pacman)
 * e o histórico do pacman.log. Para testar, $LUMNA_RAIZ_SISTEMA aponta para uma árvore falsa
 * (proc/, sys/, etc/, var/log/) e $PATH para um checkupdates falso.
 */
#ifndef LUMNA_SISTEMA_H
#define LUMNA_SISTEMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct sistema_info {
	char host[64], distro[96], kernel[96], cpu[128], gpu[128], modelo[128], arquitetura[24], lumna[32];
	int nucleos;
	int64_t memoria_total, memoria_livre; /* bytes */
	int64_t disco_total, disco_livre;     /* a raiz */
	int64_t ligado_ha_s;
};

#define SISTEMA_MAX_ATUALIZACOES 400
struct sistema_atualizacao {
	char nome[64], antiga[48], nova[48];
};

struct sistema_evento {
	int64_t quando;       /* unix */
	char acao[16];        /* instalado, atualizado, removido, rebaixado */
	char nome[64], de[48], para[48];
};

void sistema_ler(struct sistema_info *s);
/* o texto de "Copiar informações" */
void sistema_resumo(const struct sistema_info *s, char *buf, size_t tam);

/* procura atualizações (leva alguns segundos: chame numa tarefa); devolve quantas, ou -1 se não deu para saber
 * (sem checkupdates nem pacman) */
int sistema_atualizacoes(struct sistema_atualizacao *v, int max);
/* os últimos eventos do pacman.log, do mais novo para o mais velho */
int sistema_historico(struct sistema_evento *v, int max);

#endif
