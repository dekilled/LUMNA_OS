#define _GNU_SOURCE
#include "processos.h"

#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_send_signal
#define SYS_pidfd_send_signal 424
#endif
#define IOPRIO_WHO_PROCESS 1
#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_PRIO_VALUE(classe, nivel) (((classe) << IOPRIO_CLASS_SHIFT) | (nivel))
#define PF_KTHREAD 0x00200000

static void erro_texto(char *erro, size_t tam, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void erro_texto(char *erro, size_t tam, const char *fmt, ...) {
	if (!erro || !tam) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(erro, tam, fmt, ap);
	va_end(ap);
}

/* ---- ler o /proc ---- */

struct amostra {
	int pid;
	uint64_t inicio, ticks;
	double em;
};
static struct amostra *antes;
static int n_antes;

static double agora_s(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static bool ler_arquivo(const char *caminho, char *buf, size_t tam, size_t *lido) {
	int fd = open(caminho, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return false;
	}
	ssize_t n = read(fd, buf, tam - 1);
	close(fd);
	if (n < 0) {
		return false;
	}
	buf[n] = '\0';
	if (lido) {
		*lido = (size_t)n;
	}
	return true;
}

/* os campos do /proc/PID/stat (o nome pode ter espaços e parênteses: depois do último ")") */
static bool ler_stat(int pid, struct pr_proc *p, uint64_t *ticks) {
	char caminho[64], buf[2048];
	snprintf(caminho, sizeof(caminho), "/proc/%d/stat", pid);
	if (!ler_arquivo(caminho, buf, sizeof(buf), NULL)) {
		return false;
	}
	char *ini = strchr(buf, '('), *fim = strrchr(buf, ')');
	if (!ini || !fim || fim < ini) {
		return false;
	}
	size_t nn = (size_t)(fim - ini - 1);
	if (nn >= sizeof(p->nome)) {
		nn = sizeof(p->nome) - 1;
	}
	memcpy(p->nome, ini + 1, nn);
	p->nome[nn] = '\0';
	char estado;
	int ppid, pgrp, sessao, tty, tpgid;
	unsigned long flags, minflt, cminflt, majflt, cmajflt, utime, stime;
	long cutime, cstime, prio, nice, threads, itreal;
	unsigned long long inicio;
	unsigned long vsize;
	long rss;
	if (sscanf(fim + 2, "%c %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %llu %lu %ld", &estado, &ppid, &pgrp, &sessao, &tty, &tpgid, &flags, &minflt, &cminflt,
			&majflt, &cmajflt, &utime, &stime, &cutime, &cstime, &prio, &nice, &threads, &itreal, &inicio, &vsize, &rss) < 22) {
		return false;
	}
	p->pid = pid;
	p->estado = estado;
	p->ppid = ppid;
	p->sessao = sessao;
	p->nice = (int)nice;
	p->threads = (int)threads;
	p->inicio = inicio;
	p->kernel = (flags & PF_KTHREAD) != 0;
	p->memoria_kb = (int64_t)rss * (sysconf(_SC_PAGESIZE) / 1024);
	*ticks = (uint64_t)utime + (uint64_t)stime;
	return true;
}

static const char *usuario_de(uid_t uid) {
	static struct { uid_t uid; char nome[32]; } cache[64];
	static int n;
	for (int i = 0; i < n; i++) {
		if (cache[i].uid == uid) {
			return cache[i].nome;
		}
	}
	struct passwd *pw = getpwuid(uid);
	int i = n < 64 ? n++ : 63;
	cache[i].uid = uid;
	snprintf(cache[i].nome, sizeof(cache[i].nome), "%s", pw ? pw->pw_name : "?");
	if (!pw) {
		snprintf(cache[i].nome, sizeof(cache[i].nome), "%u", (unsigned)uid);
	}
	return cache[i].nome;
}

static enum pr_nivel nivel_de(const struct pr_proc *p) {
	if (p->politica == SCHED_FIFO || p->politica == SCHED_RR) {
		return PR_TEMPO_REAL;
	}
	if (p->politica == SCHED_IDLE || p->nice >= 5) {
		return PR_BAIXA;
	}
	if (p->nice <= -10) {
		return PR_MAXIMA;
	}
	if (p->nice <= -3) {
		return PR_ALTA;
	}
	return PR_MEDIA;
}

void pr_listar(struct pr_lista *l) {
	l->v = NULL;
	l->n = 0;
	DIR *d = opendir("/proc");
	if (!d) {
		return;
	}
	int cap = 256;
	l->v = calloc((size_t)cap, sizeof(struct pr_proc));
	struct amostra *novas = calloc((size_t)cap, sizeof(struct amostra));
	int n_novas = 0;
	double agora = agora_s();
	long hz = sysconf(_SC_CLK_TCK);
	struct dirent *e;
	while ((e = readdir(d))) {
		if (!isdigit((unsigned char)e->d_name[0])) {
			continue;
		}
		int pid = atoi(e->d_name);
		struct pr_proc p;
		memset(&p, 0, sizeof(p));
		uint64_t ticks;
		if (!ler_stat(pid, &p, &ticks)) {
			continue;
		}
		char caminho[64];
		snprintf(caminho, sizeof(caminho), "/proc/%d", pid);
		struct stat st;
		if (stat(caminho, &st) == 0) {
			p.uid = st.st_uid;
		}
		snprintf(p.usuario, sizeof(p.usuario), "%s", usuario_de(p.uid));
		/* a linha de comando (NUL entre os argumentos); as threads do kernel não têm */
		char cmd[sizeof(p.linha)];
		size_t lido = 0;
		snprintf(caminho, sizeof(caminho), "/proc/%d/cmdline", pid);
		if (ler_arquivo(caminho, cmd, sizeof(cmd), &lido) && lido > 0) {
			for (size_t i = 0; i + 1 < lido; i++) {
				if (cmd[i] == '\0') {
					cmd[i] = ' ';
				}
			}
			snprintf(p.linha, sizeof(p.linha), "%s", cmd);
			/* o comm corta em 15 letras: o nome do programa da linha de comando é melhor */
			char *prog = cmd, *fimprog = strchr(cmd, ' ');
			if (fimprog) {
				*fimprog = '\0';
			}
			char *b = strrchr(prog, '/');
			b = b ? b + 1 : prog;
			if (b[0] && strlen(p.nome) >= 15 && strncmp(b, p.nome, 15) == 0) {
				snprintf(p.nome, sizeof(p.nome), "%.63s", b);
			}
		} else {
			snprintf(p.linha, sizeof(p.linha), "[%s]", p.nome);
		}
		struct sched_param sp = { 0 };
		p.politica = sched_getscheduler(pid);
		sched_getparam(pid, &sp);
		p.prio_rt = sp.sched_priority;
		long io = syscall(SYS_ioprio_get, IOPRIO_WHO_PROCESS, pid);
		p.io_classe = io >= 0 ? (int)(io >> IOPRIO_CLASS_SHIFT) : 2;
		p.io_nivel = io >= 0 ? (int)(io & 7) : 4;
		p.nivel = nivel_de(&p);
		/* a CPU: a diferença desde a leitura anterior deste mesmo processo (PID + instante) */
		for (int i = 0; i < n_antes; i++) {
			if (antes[i].pid == pid && antes[i].inicio == p.inicio && agora > antes[i].em) {
				double dt = agora - antes[i].em;
				p.cpu = 100.0 * ((double)(ticks - antes[i].ticks) / (double)hz) / dt;
				if (p.cpu < 0) {
					p.cpu = 0;
				}
				break;
			}
		}
		if (l->n == cap) {
			cap *= 2;
			l->v = realloc(l->v, (size_t)cap * sizeof(struct pr_proc));
			novas = realloc(novas, (size_t)cap * sizeof(struct amostra));
		}
		l->v[l->n++] = p;
		novas[n_novas++] = (struct amostra){ pid, p.inicio, ticks, agora };
	}
	closedir(d);
	free(antes);
	antes = novas;
	n_antes = n_novas;
}

void pr_liberar(struct pr_lista *l) {
	free(l->v);
	l->v = NULL;
	l->n = 0;
}

const struct pr_proc *pr_achar(const struct pr_lista *l, int pid) {
	for (int i = 0; i < l->n; i++) {
		if (l->v[i].pid == pid) {
			return &l->v[i];
		}
	}
	return NULL;
}

const char *pr_nivel_nome(enum pr_nivel n) {
	static const char *const nomes[PR_NIVEIS] = { "Baixa", "Média", "Alta", "Máxima", "Tempo real" };
	return n >= 0 && n < PR_NIVEIS ? nomes[n] : "?";
}

const char *pr_estado_texto(char e) {
	switch (e) {
	case 'R': return "Executando";
	case 'S': return "Dormindo";
	case 'D': return "Esperando disco ou driver (D)";
	case 'T': case 't': return "Parado";
	case 'Z': return "Zumbi (já acabou; o pai ainda não o recolheu)";
	case 'I': return "Ocioso";
	case 'X': return "Morto";
	default: return "?";
	}
}

/* ---- encerrar ---- */

/* abre um pidfd depois de conferir que o processo ainda é o mesmo (PID + instante em que começou); devolve -1 com *res dizendo o porquê */
static int pidfd_conferido(int pid, uint64_t inicio, enum pr_resultado *res, char *erro, size_t tam) {
	int fd = (int)syscall(SYS_pidfd_open, pid, 0);
	if (fd < 0) {
		*res = errno == ESRCH ? PR_MUDOU : PR_ERRO;
		erro_texto(erro, tam, errno == ESRCH ? "o processo já acabou" : "não consegui abrir o processo: %s", strerror(errno));
		return -1;
	}
	struct pr_proc p;
	uint64_t t;
	memset(&p, 0, sizeof(p));
	if (!ler_stat(pid, &p, &t)) {
		close(fd);
		*res = PR_MUDOU;
		erro_texto(erro, tam, "o processo já acabou");
		return -1;
	}
	if (inicio && p.inicio != inicio) { /* o PID é de outro processo agora */
		close(fd);
		*res = PR_MUDOU;
		erro_texto(erro, tam, "o PID %d já é de outro processo (o que você via acabou)", pid);
		return -1;
	}
	if (p.kernel) {
		close(fd);
		*res = PR_PROTEGIDO;
		erro_texto(erro, tam, "é uma thread do kernel: não se encerra");
		return -1;
	}
	return fd;
}

static bool protegido(int pid) {
	return pid <= 1 || pid == getpid();
}

static int descendentes(int raiz, int *saida, int max) {
	struct pr_lista l;
	/* uma leitura própria (sem mexer nas amostras de CPU): só PID e PPID */
	l.v = NULL;
	l.n = 0;
	DIR *d = opendir("/proc");
	if (!d) {
		return 0;
	}
	int cap = 256, n = 0;
	int *pid = malloc(sizeof(int) * (size_t)cap), *pai = malloc(sizeof(int) * (size_t)cap);
	struct dirent *e;
	while ((e = readdir(d))) {
		if (!isdigit((unsigned char)e->d_name[0])) {
			continue;
		}
		struct pr_proc p;
		memset(&p, 0, sizeof(p));
		uint64_t t;
		if (!ler_stat(atoi(e->d_name), &p, &t)) {
			continue;
		}
		if (n == cap) {
			cap *= 2;
			pid = realloc(pid, sizeof(int) * (size_t)cap);
			pai = realloc(pai, sizeof(int) * (size_t)cap);
		}
		pid[n] = p.pid;
		pai[n] = p.ppid;
		n++;
	}
	closedir(d);
	/* em largura a partir da raiz; a ordem de volta (os mais fundos primeiro) é a de sinalizar */
	int total = 0;
	saida[total++] = raiz;
	for (int i = 0; i < total && total < max; i++) {
		for (int k = 0; k < n && total < max; k++) {
			if (pai[k] == saida[i] && pid[k] != raiz) {
				bool ja = false;
				for (int j = 0; j < total; j++) {
					ja |= saida[j] == pid[k];
				}
				if (!ja) {
					saida[total++] = pid[k];
				}
			}
		}
	}
	free(pid);
	free(pai);
	(void)l;
	return total;
}

enum pr_resultado pr_encerrar(int pid, uint64_t inicio, bool arvore, bool forcar, int *quantos, char *erro, size_t tam) {
	if (quantos) {
		*quantos = 0;
	}
	if (protegido(pid)) {
		erro_texto(erro, tam, "este processo é protegido (o sistema ou o próprio gerenciador)");
		return PR_PROTEGIDO;
	}
	enum pr_resultado res = PR_OK;
	int raiz_fd = pidfd_conferido(pid, inicio, &res, erro, tam);
	if (raiz_fd < 0) {
		return res;
	}
	int sinal = forcar ? SIGKILL : SIGTERM;
	int alvos[4096];
	int n = 1;
	alvos[0] = pid;
	if (arvore) {
		n = descendentes(pid, alvos, 4096);
	}
	/* os filhos primeiro (do fim da lista para o começo), a raiz por último: ninguém fica órfão adotado pelo init no meio */
	bool eperm = false;
	int feitos = 0;
	for (int i = n - 1; i >= 0; i--) {
		if (protegido(alvos[i])) {
			continue;
		}
		enum pr_resultado r;
		int fd = alvos[i] == pid ? raiz_fd : pidfd_conferido(alvos[i], 0, &r, NULL, 0);
		if (fd < 0) {
			continue; /* acabou no meio do caminho */
		}
		if (syscall(SYS_pidfd_send_signal, fd, sinal, NULL, 0) == 0) {
			feitos++;
		} else if (errno == EPERM) {
			eperm = true;
		}
		if (fd != raiz_fd) {
			close(fd);
		}
	}
	close(raiz_fd);
	if (quantos) {
		*quantos = feitos;
	}
	if (feitos == 0 && eperm) {
		erro_texto(erro, tam, "sem permissão para encerrar este processo");
		return PR_PRECISA_PRIV;
	}
	if (feitos == 0) {
		erro_texto(erro, tam, "não consegui enviar o sinal");
		return PR_ERRO;
	}
	return PR_OK;
}

/* ---- prioridade ---- */

static const struct { int nice; int io_classe, io_nivel; } NIVEIS[PR_NIVEIS] = {
	[PR_BAIXA] = { 10, 3, 0 },
	[PR_MEDIA] = { 0, 2, 4 },
	[PR_ALTA] = { -5, 2, 2 },
	[PR_MAXIMA] = { -15, 2, 0 },
	[PR_TEMPO_REAL] = { -20, 1, 4 },
};
#define PRIORIDADE_RT 20 /* SCHED_RR 20 de 99: acima de tudo o que é normal, abaixo do que o próprio sistema usa (os drivers e o gerenciador) */

enum pr_resultado pr_prioridade(int pid, uint64_t inicio, enum pr_nivel nivel, char *erro, size_t tam) {
	if (nivel < 0 || nivel >= PR_NIVEIS) {
		erro_texto(erro, tam, "nível de prioridade inválido");
		return PR_ERRO;
	}
	if (pid <= 1) {
		erro_texto(erro, tam, "este processo é protegido");
		return PR_PROTEGIDO;
	}
	enum pr_resultado res = PR_OK;
	int fd = pidfd_conferido(pid, inicio, &res, erro, tam);
	if (fd < 0) {
		return res;
	}
	close(fd); /* a conferência acabou; as chamadas de prioridade são por PID (não há versão por pidfd) */
	bool eperm = false;
	/* a política primeiro: tirar do tempo real (para Baixa a Máxima) ou pôr nele */
	struct sched_param sp = { .sched_priority = nivel == PR_TEMPO_REAL ? PRIORIDADE_RT : 0 };
	int politica = nivel == PR_TEMPO_REAL ? SCHED_RR : nivel == PR_BAIXA ? SCHED_BATCH : SCHED_OTHER;
	if (sched_setscheduler(pid, politica, &sp) != 0) {
		if (errno == EPERM) {
			eperm = true;
		} else if (errno == ESRCH) {
			erro_texto(erro, tam, "o processo já acabou");
			return PR_MUDOU;
		} else {
			erro_texto(erro, tam, "política de escalonamento: %s", strerror(errno));
			return PR_ERRO;
		}
	}
	if (nivel != PR_TEMPO_REAL && setpriority(PRIO_PROCESS, (id_t)pid, NIVEIS[nivel].nice) != 0) {
		if (errno == EPERM || errno == EACCES) {
			eperm = true;
		} else if (errno == ESRCH) {
			return PR_MUDOU;
		}
	}
	if (syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, pid, IOPRIO_PRIO_VALUE(NIVEIS[nivel].io_classe, NIVEIS[nivel].io_nivel)) != 0 && errno == EPERM) {
		/* E/S em tempo real ou melhor que a normal também pede privilégio */
		if (NIVEIS[nivel].io_classe == 1 || NIVEIS[nivel].io_nivel < 4) {
			eperm = true;
		}
	}
	if (eperm) {
		erro_texto(erro, tam, "subir a prioridade (ou o tempo real) pede permissão de administrador");
		return PR_PRECISA_PRIV;
	}
	return PR_OK;
}

void pr_proteger_este_processo(char *resumo, size_t tam) {
	char partes[160] = "";
	struct sched_param sp = { .sched_priority = 50 };
	/* o gerenciador acima de qualquer app (do tempo real, de 1 a 99, usa 50: preempta um jogo preso em loop, mas deixa o que é do sistema) */
	if (sched_setscheduler(0, SCHED_FIFO, &sp) == 0) {
		snprintf(partes + strlen(partes), sizeof(partes) - strlen(partes), "tempo real (FIFO 50); ");
	} else if (setpriority(PRIO_PROCESS, 0, -10) == 0) {
		snprintf(partes + strlen(partes), sizeof(partes) - strlen(partes), "prioridade alta (nice -10); ");
	}
	int fd = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
	if (fd >= 0) {
		if (write(fd, "-900", 4) == 4) {
			snprintf(partes + strlen(partes), sizeof(partes) - strlen(partes), "protegido do OOM killer; ");
		}
		close(fd);
	}
	snprintf(resumo, tam, "%s", partes[0] ? partes : "sem privilégio: prioridade normal");
}
