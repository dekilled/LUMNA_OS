/*
 * Os processos do sistema (o gerenciador de processos): ler o /proc, encerrar sem corrida (pidfd) e mudar a prioridade. Só libc/Linux, sem tela.
 *
 * Encerrar: SIGTERM (pede) ou SIGKILL (o kernel tira o processo na hora, sem o processo poder recusar). Funciona em qualquer processo que a pessoa possa
 * sinalizar; só não tira quem está em espera de disco/driver (estado D: o sinal fica pendente até o kernel o liberar), zumbis (quem os remove é o pai) e as threads
 * do kernel. O processo é identificado por PID + o instante em que começou (um PID reaproveitado não é o mesmo processo): o sinal vai por um pidfd aberto
 * depois de conferir o instante, então não há corrida.
 *
 * Prioridade: cinco níveis (Baixa, Média, Alta, Máxima, Tempo real) = nice + classe de E/S + política do escalonador. Abaixar vale sempre; subir pede privilégio
 * (CAP_SYS_NICE): sem ele devolve PR_PRECISA_PRIV e quem chamou usa o ajudante (pkexec, política os.lumna.processos).
 */
#ifndef LUMNA_PROCESSOS_H
#define LUMNA_PROCESSOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum pr_nivel { PR_BAIXA, PR_MEDIA, PR_ALTA, PR_MAXIMA, PR_TEMPO_REAL, PR_NIVEIS };

enum pr_resultado {
	PR_OK = 0,
	PR_ERRO = 1,        /* não deu (o texto diz por quê) */
	PR_PRECISA_PRIV = 2, /* falta permissão: pedir ao ajudante com privilégio */
	PR_PROTEGIDO = 3,    /* o PID 1, o próprio gerenciador, threads do kernel */
	PR_MUDOU = 4,        /* o processo acabou ou já é outro (o PID foi reaproveitado) */
};

struct pr_proc {
	int pid, ppid, sessao;
	uid_t uid;
	char usuario[32];
	char nome[64];       /* o nome (comm, ou o do programa quando o comm cortou) */
	char linha[512];     /* a linha de comando */
	char estado;         /* R S D T Z I ... (o do /proc) */
	int nice;
	int politica;        /* SCHED_OTHER, SCHED_FIFO, SCHED_RR, SCHED_BATCH, SCHED_IDLE */
	int prio_rt;         /* 1..99 nas políticas de tempo real */
	int io_classe, io_nivel; /* 1 tempo real, 2 normal, 3 ocioso */
	int threads;
	int64_t memoria_kb;  /* residente */
	double cpu;          /* % de um núcleo desde a leitura anterior */
	uint64_t inicio;     /* o instante em que começou (starttime do /proc, em ticks): com o PID, identifica o processo */
	bool kernel;         /* thread do kernel */
	enum pr_nivel nivel; /* o nível que esta prioridade corresponde (o mais próximo) */
};

struct pr_lista {
	struct pr_proc *v;
	int n;
};

/* lê todos os processos; o % de CPU é o da diferença para a leitura anterior (a primeira devolve 0). */
void pr_listar(struct pr_lista *l);
void pr_liberar(struct pr_lista *l);
const struct pr_proc *pr_achar(const struct pr_lista *l, int pid);

/* encerra: forcar = SIGKILL (senão SIGTERM); arvore = e tudo o que ele abriu (os filhos, de baixo para cima). inicio 0 = não conferir.
 * *quantos recebe quantos processos foram sinalizados. */
enum pr_resultado pr_encerrar(int pid, uint64_t inicio, bool arvore, bool forcar, int *quantos, char *erro, size_t tam);

/* muda a prioridade para um dos cinco níveis */
enum pr_resultado pr_prioridade(int pid, uint64_t inicio, enum pr_nivel nivel, char *erro, size_t tam);

const char *pr_nivel_nome(enum pr_nivel n);
const char *pr_estado_texto(char estado); /* "Executando", "Dormindo", "Esperando disco (D)"... */

/* o gerenciador em si: prioridade de tempo real baixa (para responder mesmo com um jogo usando toda a CPU) e fora do alcance do OOM killer.
 * Só o que o privilégio deixa; devolve o que conseguiu num texto de uma linha. */
void pr_proteger_este_processo(char *resumo, size_t tam);

#endif
