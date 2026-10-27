/*
 * Uma chamada ao servidor da Emma (o backend) pelo serviço lumna-emma: ele cuida do endereço e do login, quem chama
 * (o app, o Painel) só diz o método e o caminho. Bloqueia: numa linha à parte (pn_tarefa) se puder demorar.
 */
#ifndef LUMNA_EMMAR_H
#define LUMNA_EMMAR_H

#include <stdbool.h>
#include <stddef.h>

#include "json.h"

struct emmar {
	struct json *raiz;          /* a resposta inteira do serviço: {ok, status, corpo, erro} */
	const struct json *corpo;   /* o corpo que o servidor devolveu (dentro de raiz) */
	bool ok;                    /* o servidor respondeu 2xx */
	bool servico;               /* o serviço lumna-emma respondeu (senão não está rodando) */
	int status;
	char msg[240];              /* quando !ok: o que deu errado, em português */
};

/* limite: o tamanho máximo da resposta (0: 1 MB) */
bool emmar_chamar(const char *metodo, const char *caminho, const char *corpo, size_t limite, struct emmar *r);
void emmar_liberar(struct emmar *r);

#endif
