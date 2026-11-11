/* O serviço de processos (servicos/processos.c) contra processos de verdade: ler o /proc, CPU, prioridade, encerrar (pedir e forçar), a árvore e as proteções. */
#define _GNU_SOURCE
#include "processos.h"

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int falhas;
#define CONFERE(c, msg) do { if (!(c)) { fprintf(stderr, "FALHOU: %s (linha %d)\n", msg, __LINE__); falhas++; } } while (0)

static pid_t gerar(const char *script) {
	pid_t p = fork();
	if (p == 0) {
		setsid();
		execl("/bin/sh", "sh", "-c", script, (char *)NULL);
		_exit(127);
	}
	return p;
}

static void dorme(double s) {
	struct timespec ts = { (time_t)s, (long)((s - (time_t)s) * 1e9) };
	nanosleep(&ts, NULL);
}

static bool vivo(pid_t p) {
	return kill(p, 0) == 0;
}

/* acabou de verdade (e foi recolhido): waitpid sem bloquear */
static bool acabou(pid_t p, int *estado) {
	for (int i = 0; i < 40; i++) {
		pid_t r = waitpid(p, estado, WNOHANG);
		if (r == p) {
			return true;
		}
		dorme(0.05);
	}
	return false;
}

int main(void) {
	char erro[200];
	bool root = geteuid() == 0;

	/* 1. ler o /proc: este processo está na lista, com o que se espera */
	struct pr_lista l;
	pr_listar(&l);
	const struct pr_proc *eu = pr_achar(&l, (int)getpid());
	CONFERE(l.n > 3 && eu != NULL, "a lista traz os processos, inclusive este");
	if (eu) {
		CONFERE(eu->memoria_kb > 0 && eu->usuario[0] && eu->nome[0] && eu->linha[0] && eu->threads >= 1 && eu->inicio > 0, "nome, usuário, linha, memória e instante de início");
		CONFERE(eu->nivel == PR_MEDIA, "um processo normal é de prioridade Média");
	}
	const struct pr_proc *init = pr_achar(&l, 1);
	CONFERE(init != NULL && init->ppid == 0, "o PID 1 está na lista");
	pr_liberar(&l);

	/* 2. CPU: um processo em loop passa de 50% de um núcleo entre duas leituras */
	pid_t busy = gerar("while :; do :; done");
	dorme(0.3);
	pr_listar(&l);
	pr_liberar(&l);
	dorme(0.8);
	pr_listar(&l);
	const struct pr_proc *b = pr_achar(&l, (int)busy);
	CONFERE(b != NULL && b->cpu > 50.0, "um processo em loop usa mais de 50% de CPU");
	if (b) {
		CONFERE(b->estado == 'R' || b->estado == 'S', "o estado do processo em loop");
	}
	uint64_t inicio_busy = b ? b->inicio : 0;
	pr_liberar(&l);

	/* 3. prioridade: abaixar vale sempre; subir pede privilégio (como root, passa) */
	enum pr_resultado r = pr_prioridade(busy, inicio_busy, PR_BAIXA, erro, sizeof(erro));
	CONFERE(r == PR_OK, "pôr em prioridade Baixa");
	CONFERE(getpriority(PRIO_PROCESS, (id_t)busy) == 10 && sched_getscheduler(busy) == SCHED_BATCH, "Baixa = nice 10 e política em lote");
	pr_listar(&l);
	b = pr_achar(&l, (int)busy);
	CONFERE(b && b->nivel == PR_BAIXA && b->io_classe == 3, "a lista mostra o nível Baixa e a E/S ociosa");
	pr_liberar(&l);
	r = pr_prioridade(busy, inicio_busy, PR_ALTA, erro, sizeof(erro));
	if (root) {
		CONFERE(r == PR_OK && getpriority(PRIO_PROCESS, (id_t)busy) == -5, "como root, subir para Alta (nice -5) passa");
		r = pr_prioridade(busy, inicio_busy, PR_MAXIMA, erro, sizeof(erro));
		CONFERE(r == PR_OK && getpriority(PRIO_PROCESS, (id_t)busy) == -15, "Máxima = nice -15");
		r = pr_prioridade(busy, inicio_busy, PR_TEMPO_REAL, erro, sizeof(erro));
		if (r == PR_OK) { /* um container pode não ter tempo real: aí devolve PR_PRECISA_PRIV, e isso também vale */
			struct sched_param sp;
			sched_getparam(busy, &sp);
			CONFERE(sched_getscheduler(busy) == SCHED_RR && sp.sched_priority == 20, "Tempo real = SCHED_RR 20");
			pr_listar(&l);
			b = pr_achar(&l, (int)busy);
			CONFERE(b && b->nivel == PR_TEMPO_REAL, "a lista mostra o nível Tempo real");
			pr_liberar(&l);
		} else {
			CONFERE(r == PR_PRECISA_PRIV, "tempo real indisponível aqui devolve 'precisa de privilégio'");
			printf("(tempo real indisponível neste ambiente: %s)\n", erro);
		}
		r = pr_prioridade(busy, inicio_busy, PR_MEDIA, erro, sizeof(erro));
		CONFERE(r == PR_OK && getpriority(PRIO_PROCESS, (id_t)busy) == 0 && sched_getscheduler(busy) == SCHED_OTHER, "voltar para Média tira do tempo real e zera o nice");
	} else {
		CONFERE(r == PR_PRECISA_PRIV, "sem privilégio, subir a prioridade devolve 'precisa de privilégio'");
	}
	r = pr_prioridade(busy, inicio_busy + 1, PR_BAIXA, erro, sizeof(erro));
	CONFERE(r == PR_MUDOU, "um PID com outro instante de início não é o mesmo processo");
	r = pr_prioridade(1, 0, PR_BAIXA, erro, sizeof(erro));
	CONFERE(r == PR_PROTEGIDO, "o PID 1 é protegido");

	/* 4. encerrar o que ignora o pedido: SIGTERM não basta, SIGKILL o tira na hora */
	pid_t teimoso = gerar("trap '' TERM; while :; do sleep 1; done");
	dorme(0.4);
	pr_listar(&l);
	const struct pr_proc *t = pr_achar(&l, (int)teimoso);
	CONFERE(t != NULL, "o processo teimoso está na lista");
	uint64_t inicio_t = t ? t->inicio : 0;
	pr_liberar(&l);
	int quantos = 0;
	r = pr_encerrar(teimoso, inicio_t, false, false, &quantos, erro, sizeof(erro));
	CONFERE(r == PR_OK && quantos == 1, "pedir para encerrar envia o sinal");
	dorme(0.5);
	CONFERE(vivo(teimoso) && waitpid(teimoso, NULL, WNOHANG) == 0, "quem ignora o SIGTERM continua vivo");
	r = pr_encerrar(teimoso, inicio_t + 7, false, true, &quantos, erro, sizeof(erro));
	CONFERE(r == PR_MUDOU && vivo(teimoso), "com o instante de início trocado não mata (o PID podia ser de outro)");
	r = pr_encerrar(teimoso, inicio_t, false, true, &quantos, erro, sizeof(erro));
	int estado = 0;
	CONFERE(r == PR_OK && acabou(teimoso, &estado) && WIFSIGNALED(estado) && WTERMSIG(estado) == SIGKILL, "forçar (SIGKILL) tira o processo na hora");

	/* 5. a árvore: o pai e tudo o que ele abriu */
	pid_t pai = gerar("sleep 100 & sleep 100 & (sleep 100 & sleep 100 & wait) & wait");
	dorme(0.6);
	pr_listar(&l);
	int netos = 0;
	int ids[32], ni = 0;
	for (int i = 0; i < l.n; i++) {
		if (l.v[i].sessao == (int)pai && l.v[i].pid != (int)pai) { /* o gerar() faz setsid: tudo dele está na sessão dele */
			netos++;
			if (ni < 32) {
				ids[ni++] = l.v[i].pid;
			}
		}
	}
	const struct pr_proc *p0 = pr_achar(&l, (int)pai);
	uint64_t inicio_pai = p0 ? p0->inicio : 0;
	pr_liberar(&l);
	CONFERE(netos >= 4, "o pai abriu filhos e netos");
	r = pr_encerrar(pai, inicio_pai, true, true, &quantos, erro, sizeof(erro));
	CONFERE(r == PR_OK && quantos >= 5, "encerrar a árvore sinaliza o pai e todos os descendentes");
	acabou(pai, &estado);
	dorme(0.4);
	int vivos = 0;
	for (int i = 0; i < ni; i++) {
		if (vivo(ids[i]) && waitpid(ids[i], NULL, WNOHANG) == 0) {
			char caminho[64], buf[16] = "";
			snprintf(caminho, sizeof(caminho), "/proc/%d/stat", ids[i]);
			FILE *f = fopen(caminho, "r");
			if (f) {
				char linha[512] = "";
				if (fgets(linha, sizeof(linha), f)) {
					char *fim = strrchr(linha, ')');
					if (fim && fim[2] != 'Z') { /* um zumbi (já morreu, o init ainda não recolheu) não conta */
						vivos++;
					}
				}
				fclose(f);
			}
			(void)buf;
		}
	}
	CONFERE(vivos == 0, "nenhum descendente sobrou depois de encerrar a árvore");

	/* 6. proteções */
	r = pr_encerrar(1, 0, true, true, &quantos, erro, sizeof(erro));
	CONFERE(r == PR_PROTEGIDO && quantos == 0, "o PID 1 não se encerra");
	r = pr_encerrar((int)getpid(), 0, false, true, &quantos, erro, sizeof(erro));
	CONFERE(r == PR_PROTEGIDO, "o próprio gerenciador não se encerra");
	pr_listar(&l);
	for (int i = 0; i < l.n; i++) {
		if (l.v[i].kernel) {
			r = pr_encerrar(l.v[i].pid, l.v[i].inicio, false, true, &quantos, erro, sizeof(erro));
			CONFERE(r == PR_PROTEGIDO, "uma thread do kernel não se encerra");
			break;
		}
	}
	pr_liberar(&l);
	r = pr_encerrar(2147480000, 0, false, true, &quantos, erro, sizeof(erro));
	CONFERE(r == PR_MUDOU, "um PID que não existe responde 'já acabou'");

	/* o resto do que pr_estado_texto e o nível dizem */
	CONFERE(strcmp(pr_nivel_nome(PR_TEMPO_REAL), "Tempo real") == 0 && strstr(pr_estado_texto('D'), "disco") != NULL && strstr(pr_estado_texto('Z'), "Zumbi") != NULL, "os textos de nível e estado");

	/* 7. proteger este processo (o do gerenciador): o que o privilégio deixar */
	char resumo[200];
	pr_proteger_este_processo(resumo, sizeof(resumo));
	CONFERE(resumo[0] != '\0', "proteger este processo devolve o que conseguiu");
	printf("(este processo: %s)\n", resumo);

	kill(busy, SIGKILL);
	waitpid(busy, NULL, 0);
	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("processos (serviço): ok\n");
	return 0;
}
