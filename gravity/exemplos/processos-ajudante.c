/*
 * lumna-processos-ajudante: o que o gerenciador de processos faz com os processos que a pessoa não pode mexer sozinha (do sistema, de outro usuário, e
 * subir a prioridade ou pôr em tempo real). Roda como root, chamado por pkexec (política os.lumna.processos: a senha vale por uma sessão).
 *
 *   lumna-processos-ajudante encerrar PID INICIO um|arvore pedir|forcar
 *   lumna-processos-ajudante prioridade PID INICIO 0..4          (0 Baixa, 1 Média, 2 Alta, 3 Máxima, 4 Tempo real)
 *   lumna-processos-ajudante servico start|stop|restart|freeze|thaw UNIDADE.service|UNIDADE.timer
 *
 * Só números e as palavras acima (nada vai para um shell). O PID 1, este programa e as threads do kernel são protegidos pelo próprio serviço.
 * O código de saída é o enum pr_resultado (0 ok, 2 precisa de privilégio, 3 protegido, 4 o processo mudou); a mensagem vai para a saída de erro.
 */
#include "processos.h"

#include <stdbool.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* o systemctl: caminho fixo (o ambiente de quem chama não vale: o pkexec o limpa, e aqui nem se olha); os testes compilam com um de mentira */
#ifndef SYSTEMCTL_CAMINHO
#define SYSTEMCTL_CAMINHO "/usr/bin/systemctl"
#endif

static bool numero(const char *s, unsigned long long *v) {
	if (!s || !*s) {
		return false;
	}
	for (const char *p = s; *p; p++) {
		if (*p < '0' || *p > '9') {
			return false;
		}
	}
	errno = 0;
	*v = strtoull(s, NULL, 10);
	return errno == 0;
}

/* o nome de uma unidade: só letras, números e : _ . @ -, de .service ou .timer, sem começar por - ou . (nada que o systemctl leia como opção, e nenhum caminho) */
static bool unidade_ok(const char *u) {
	size_t n = u ? strlen(u) : 0;
	if (n < 9 || n > 100 || u[0] == '-' || u[0] == '.') {
		return false;
	}
	for (const char *p = u; *p; p++) {
		bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == ':' || *p == '_' || *p == '.' || *p == '@' || *p == '-';
		if (!ok) {
			return false;
		}
	}
	const char *fim = strrchr(u, '.');
	return fim && fim > u && (!strcmp(fim, ".service") || !strcmp(fim, ".timer"));
}

/* o que derrubaria a sessão, o barramento ou o login: parar, reiniciar ou congelar não passa (iniciar e retomar sim) */
static bool essencial(const char *u) {
	static const char *const nomes[] = { "dbus.service", "dbus-broker.service", "systemd-journald.service", "systemd-logind.service", "systemd-udevd.service", "polkit.service",
		"greetd.service", "display-manager.service", "gdm.service", "sddm.service", "lightdm.service", "NetworkManager-dispatcher.service", NULL };
	for (int i = 0; nomes[i]; i++) {
		if (!strcmp(u, nomes[i])) {
			return true;
		}
	}
	return !strncmp(u, "user@", 5) || !strncmp(u, "user-runtime-dir@", 17) || !strncmp(u, "getty@", 6) || !strncmp(u, "autovt@", 7);
}

static int servico(const char *acao, const char *unidade) {
	bool derruba = strcmp(acao, "start") && strcmp(acao, "thaw");
	if (derruba && essencial(unidade)) {
		fprintf(stderr, "%s é essencial para o sistema (a sessão, o barramento ou o login): o gerenciador não o para nem o pausa\n", unidade);
		return PR_PROTEGIDO;
	}
	pid_t filho = fork();
	if (filho < 0) {
		fprintf(stderr, "não consegui iniciar o systemctl: %s\n", strerror(errno));
		return PR_ERRO;
	}
	if (filho == 0) {
		char *args[] = { "systemctl", "--no-ask-password", (char *)acao, "--", (char *)unidade, NULL };
		char *amb[] = { "PATH=/usr/bin:/usr/sbin:/bin", "LC_ALL=C.UTF-8", NULL };
		execve(SYSTEMCTL_CAMINHO, args, amb);
		_exit(127);
	}
	int estado = 0;
	if (waitpid(filho, &estado, 0) < 0 || !WIFEXITED(estado)) {
		return PR_ERRO;
	}
	int c = WEXITSTATUS(estado);
	if (c == 127) {
		fprintf(stderr, "o systemctl não está instalado\n");
	}
	return c == 0 ? PR_OK : PR_ERRO; /* o motivo já foi para a saída de erro (a do systemctl) */
}

int main(int argc, char **argv) {
	char erro[240] = "";
	unsigned long long pid, inicio, nivel;
	if (argc == 6 && !strcmp(argv[1], "encerrar") && numero(argv[2], &pid) && numero(argv[3], &inicio) && pid > 0 && pid < 4194305 &&
			(!strcmp(argv[4], "um") || !strcmp(argv[4], "arvore")) && (!strcmp(argv[5], "pedir") || !strcmp(argv[5], "forcar"))) {
		int quantos = 0;
		enum pr_resultado r = pr_encerrar((int)pid, inicio, !strcmp(argv[4], "arvore"), !strcmp(argv[5], "forcar"), &quantos, erro, sizeof(erro));
		if (r != PR_OK) {
			fprintf(stderr, "%s\n", erro);
		} else {
			printf("%d\n", quantos);
		}
		return (int)r;
	}
	if (argc == 5 && !strcmp(argv[1], "prioridade") && numero(argv[2], &pid) && numero(argv[3], &inicio) && numero(argv[4], &nivel) && pid > 0 && pid < 4194305 && nivel < PR_NIVEIS) {
		enum pr_resultado r = pr_prioridade((int)pid, inicio, (enum pr_nivel)nivel, erro, sizeof(erro));
		if (r != PR_OK) {
			fprintf(stderr, "%s\n", erro);
		}
		return (int)r;
	}
	if (argc == 4 && !strcmp(argv[1], "servico") && unidade_ok(argv[3]) &&
			(!strcmp(argv[2], "start") || !strcmp(argv[2], "stop") || !strcmp(argv[2], "restart") || !strcmp(argv[2], "freeze") || !strcmp(argv[2], "thaw"))) {
		return servico(argv[2], argv[3]);
	}
	fprintf(stderr, "uso: lumna-processos-ajudante encerrar PID INICIO um|arvore pedir|forcar | prioridade PID INICIO 0..4 | servico start|stop|restart|freeze|thaw UNIDADE\n");
	return 1;
}
