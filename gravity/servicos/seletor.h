#ifndef SELETOR_H
#define SELETOR_H

#include <stdbool.h>

/*
 * Pede ao sistema para a pessoa escolher arquivos (abrir) ou um nome (salvar): é o portal org.freedesktop.portal.FileChooser, que no LUMNA é o Atlas.
 * Fala D-Bus numa linha à parte e devolve o resultado na linha principal do Gravity; a janela do app segue desenhando enquanto o seletor está aberto.
 * LUMNA_SELETOR=COMANDO troca o portal por um comando que escreve os caminhos escolhidos, um por linha (os testes), e LUMNA_SELETOR_FALHA=1 simula o portal ausente.
 */

/* caminhos: n caminhos (0: a pessoa cancelou) ou erro com o motivo (o portal não respondeu); liberar com g_strfreev. Chamado na linha principal. */
typedef void (*seletor_cb)(char **caminhos, int n, const char *erro, void *dados);

struct seletor_pedido {
	bool salvar;
	const char *titulo;
	const char *nome_sugerido; /* salvar: o nome que já vem no campo */
	const char *pasta;         /* a pasta onde abre (NULL: a do portal) */
	bool multiplo;             /* abrir: vários arquivos */
	const char *filtro_nome;   /* "Texto" */
	const char *const *tipos;  /* os MIME do filtro, terminado em NULL; o "Todos os arquivos" vem sempre junto */
};

void seletor_pedir(const struct seletor_pedido *p, seletor_cb cb, void *dados);

#endif
