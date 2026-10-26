/*
 * O cliente do serviço da Mídia (lumna-midia servico): as telas da Mídia (Início, capa, seletor) e o
 * controle do celular falam com ele por este socket, $XDG_RUNTIME_DIR/lumna-midia.sock, uma linha
 * por pedido e um JSON de resposta:
 *
 *   estado                        tudo o que as telas precisam (midiac_estado traduz)
 *   abrir ID [novo]               abre o serviço: se já está vivo, só vem para a frente; senão abre
 *                                 onde parou (o último endereço), ou do começo com "novo"
 *   inicio                        a tela de Início (guarda o serviço que estava na frente)
 *   seletor                       o seletor de serviços sobre o que estiver passando
 *   fechar ID                     fecha o serviço vivo (o endereço fica nos recentes)
 *   servico_adicionar URL NOME... | servico_remover ID | servico_ativar ID sim|nao
 *   assinar                       a conexão fica aberta e recebe {"evento":"estado"} a cada mudança
 */
#ifndef LUMNA_MIDIAC_H
#define LUMNA_MIDIAC_H

#include <stdbool.h>
#include <stddef.h>

#include "midia.h"

struct midia_vivo {
	struct midia_servico s;
	bool aberto;      /* tem aba viva no Firefox */
	bool carregando;
};

struct midia_estado {
	bool servico;           /* o serviço respondeu */
	bool firefox;           /* o Firefox está ligado */
	char ativo[32];         /* o serviço que está na frente ("" se o Início ou nenhum) */
	struct midia_vivo serv[MIDIA_MAX_SERVICOS];
	int n;
	struct midia_recente rec[MIDIA_MAX_RECENTES];
	int nrec;
	char capa[32];          /* o serviço que a capa de loading cobre */
	bool capa_visivel;
};

bool midiac_pedido(const char *pedido, char *resposta, size_t tam);
bool midiac_estado(struct midia_estado *e);
int midiac_assinar(void);            /* descritor que acorda a cada evento; -1 se o serviço não responde */
void midiac_assinar_ler(int fd);     /* limpa; false-ish ao fechar: devolve 0 se a conexão caiu */
bool midiac_assinar_vivo(int fd);
const struct midia_vivo *midiac_achar(const struct midia_estado *e, const char *id);

#endif
