/*
 * Os serviços do usuário que correm por trás (systemd --user): quais há, se ligam ao entrar e se estão rodando.
 * Só os que a pessoa pode ligar e desligar (unit files "enabled" ou "disabled"; os "static", "generated" e
 * "alias" não entram). Para testar, o PATH leva a um systemctl falso.
 */
#ifndef LUMNA_SEGUNDOPLANO_H
#define LUMNA_SEGUNDOPLANO_H

#include <stdbool.h>
#include <stddef.h>

#define SEGUNDOPLANO_MAX 128

struct fundo_servico {
	char unidade[128];    /* "syncthing.service" */
	char descricao[200];  /* o que o systemd diz que ele é (vazio se não está carregado) */
	bool ligado;          /* liga ao entrar (enabled) */
	bool rodando;         /* está ativo agora */
};

int fundo_listar(struct fundo_servico *lista, int max);
/* liga ou desliga ao entrar E agora (enable --now / disable --now); erro recebe o motivo se falhar */
bool fundo_ligar(const char *unidade, bool ligar, char *erro, size_t tam);
/* só para ou inicia agora, sem mexer no que acontece ao entrar */
bool fundo_rodar(const char *unidade, bool rodar, char *erro, size_t tam);

#endif
