/*
 * lumna-processos-ajudante: o que o gerenciador de processos faz com os processos que a pessoa não pode mexer sozinha (do sistema, de outro usuário, e
 * subir a prioridade ou pôr em tempo real). Roda como root, chamado por pkexec (política os.lumna.processos: a senha vale por uma sessão).
 *
 *   lumna-processos-ajudante encerrar PID INICIO um|arvore pedir|forcar
 *   lumna-processos-ajudante prioridade PID INICIO 0..4          (0 Baixa, 1 Média, 2 Alta, 3 Máxima, 4 Tempo real)
 *
 * Só números e as palavras acima (nada vai para um shell). O PID 1, este programa e as threads do kernel são protegidos pelo próprio serviço.
 * O código de saída é o enum pr_resultado (0 ok, 2 precisa de privilégio, 3 protegido, 4 o processo mudou); a mensagem vai para a saída de erro.
 */
#include "processos.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	fprintf(stderr, "uso: lumna-processos-ajudante encerrar PID INICIO um|arvore pedir|forcar | prioridade PID INICIO 0..4\n");
	return 1;
}
