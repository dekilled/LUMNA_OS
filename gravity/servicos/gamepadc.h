/*
 * O cliente do serviço lumna-gamepad: $XDG_RUNTIME_DIR/lumna-gamepad.sock, uma linha por pedido e um JSON de resposta.
 *   estado                      os controles e as configurações
 *   vibrar ID                   um tranco no controle (para testar)
 *   apelido ID NOME             o apelido (vazio: tira)
 *   ignorar ID sim|nao          o controle deixa de mandar na navegação
 *   recarregar                  lê de novo o gamepad.ini (depois de mudar uma configuração)
 *   assinar                     a conexão fica aberta e recebe um evento por linha ({"evento":"botao",...}, {"evento":"controles"})
 */
#ifndef LUMNA_GAMEPADC_H
#define LUMNA_GAMEPADC_H

#include <stdbool.h>
#include <stddef.h>

bool gamepadc_pedido(const char *pedido, char *resposta, size_t tam);
/* abre a conexão de eventos (sem bloquear a leitura); -1 se o serviço não responde */
int gamepadc_assinar(void);
/* lê uma linha do que chegou no fd (NULL se ainda não há uma inteira); *caiu vira true se o serviço fechou. g_free */
char *gamepadc_ler_linha(int fd, bool *caiu);
char *gamepadc_socket_caminho(void);

#endif
