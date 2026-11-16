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
	bool lumna; /* vem do repositório [lumna] (o "só o LUMNA" atualiza só estes) */
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
/* A lista "Escolher pacotes": cada atualização pendente analisada pelo lumna-pkg-analisar (sem root; leva uns segundos por pacote: chame numa tarefa) */
enum sistema_pacote_situacao { SISTEMA_PACOTE_OK, SISTEMA_PACOTE_ARRASTA, SISTEMA_PACOTE_QUEBRA, SISTEMA_PACOTE_BASE };
struct sistema_pacote {
	char nome[64], antiga[48], nova[48], repo[24];
	enum sistema_pacote_situacao situacao; /* OK: pode atualizar sozinho; ARRASTA: puxaria outros já instalados; QUEBRA: o pacman recusa; BASE: só junto com tudo */
	char motivo[200];
	bool marcado;
};
/* devolve quantos (0: nada a atualizar), ou -1 se não deu para analisar. LUMNA_PKG_ANALISAR troca o programa (testes) */
int sistema_analisar_pacotes(struct sistema_pacote *v, int max);

/* A atualização no horário marcado: o que o Painel gravou (/etc/lumna/atualizar.conf, pelo lumna-pkg agendar) e o que o último agendado fez (/var/lib/lumna/atualizar.estado).
 * LUMNA_ETC e LUMNA_VARLIB trocam as pastas (testes). Sem arquivos: desligado, "tudo", 03:00, todos os dias. */
struct sistema_agenda {
	bool ligado;
	char escopo[8];     /* "tudo" ou "lumna" */
	char hora[8];       /* "03:00" */
	char dias[40];      /* "todos" ou "seg,qua,sex" */
	int64_t ultimo;     /* quando o agendado rodou por último (unix; 0: nunca) */
	char resultado[24]; /* ok, nada, precisa-completa, falhou ("" se nunca) */
	char ultimo_escopo[8];
	char pacotes[200];  /* os pacotes do LUMNA que o último agendado atualizou */
	int quantos;        /* quantos pacotes o último agendado atualizou (escopo "tudo") */
};
void sistema_agenda_ler(struct sistema_agenda *a);

/* os últimos eventos do pacman.log, do mais novo para o mais velho */
int sistema_historico(struct sistema_evento *v, int max);

#endif
