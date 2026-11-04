/*
 * O cliente do lumna-controle (o serviço do celular): o Painel e a captura falam com ele por $XDG_RUNTIME_DIR/lumna-controle.sock, uma linha por pedido
 * e um JSON de resposta (estado, connect, parear, esquecer, recurso, enviar, copiar_para, connect_ativo, connect_pasta...). Bloqueia até 2 s: numa linha à parte.
 */
#ifndef LUMNA_CONTROLEC_H
#define LUMNA_CONTROLEC_H

#include <stdbool.h>
#include <stddef.h>

bool controlec_pedido(const char *pedido, char *resposta, size_t tam);

#endif
