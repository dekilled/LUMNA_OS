#ifndef SDSERVICOS_H
#define SDSERVICOS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Os serviços do systemd, para o gerenciador de processos: em execução, parados, programados (timers) e em pausa (congelados pelo cgroup freezer, `systemctl freeze`).
 * Só leitura. Fala com o `systemctl` (o do sistema, ou --user para os da sessão), sem senha: ler não pede privilégio.
 */

enum sv_estado {
	SV_EXECUCAO,   /* ativo: rodando, escutando (socket), concluído e mantido (oneshot com RemainAfterExit) */
	SV_PARADO,     /* inativo ou falhou */
	SV_PROGRAMADO, /* um timer ativo: roda a unidade dele quando chegar a hora */
	SV_PAUSA,      /* congelado */
	SV_ESTADOS
};

struct sv_item {
	char unidade[112];    /* "NetworkManager.service" ou "backup.timer" */
	char descricao[128];
	char ativo[16];       /* ActiveState: active, inactive, failed, activating... */
	char sub[24];         /* SubState: running, dead, exited, waiting... */
	enum sv_estado estado;
	bool falhou;
	int pid;              /* MainPID, 0 se não tem */
	int64_t memoria;      /* bytes, -1 se o systemd não mede */
	int64_t proximo;      /* timer: quando roda (segundos Unix), 0 se não se sabe */
	int64_t ultimo;       /* timer: quando rodou a última vez, 0 se nunca */
	char ativa[112];      /* timer: a unidade que ele dispara */
};

struct sv_lista {
	struct sv_item *v;
	int n;
};

/* Lê os serviços (usuario: os da sessão do usuário). 0 se leu; -1 se o systemctl não existe ou o systemd não respondeu (erro, se dado, recebe o motivo). */
int sv_listar(bool usuario, struct sv_lista *l, char *erro, int tam_erro);
void sv_liberar(struct sv_lista *l);
const char *sv_estado_nome(enum sv_estado e);
/* o texto do subestado em português: "rodando", "esperando", "falhou"... */
const char *sv_sub_texto(const struct sv_item *s);

#endif
