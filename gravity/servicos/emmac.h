/*
 * O cliente do serviço da Emma (lumna-emma): a topbar, o chat lateral, o painel e o controle do celular
 * falam com ele pelo socket $XDG_RUNTIME_DIR/lumna-emma.sock, uma linha por pedido e um JSON de resposta:
 *
 *   estado                       {"estado": ocioso|ouvindo|transcrevendo|pensando|falando, "online", "conta", "modo_voz"}
 *   assinar                      a conexão fica aberta e recebe um JSON por linha (os eventos, abaixo)
 *   ouvir                        liga o microfone (de novo: termina a fala e transcreve)
 *   ouvir_cancelar | calar       desiste de ouvir | para de falar e de pensar
 *   enviar TEXTO                 uma mensagem (a Emma fala a resposta se vier por voz: enviar_voz TEXTO)
 *   celular TEXTO                a fala que o celular já transcreveu: responde só quando a Emma acaba, com "resposta"
 *   confirmar ID sim|nao         a decisão de uma ação que pediu licença
 *   nova                         recomeça a conversa
 *   historico                    as mensagens da conversa do painel: [{papel, texto, hora, ferramentas}]
 *   contexto [sim|nao]           a janela em foco (o chip "Contexto: ..." do painel) e se a Emma pode usá-la
 *   acoes                        o catálogo de ações e a permissão de cada uma
 *
 * Eventos (assinar): estado, nivel (volume do microfone), ouvido (o que se falou), delta (a resposta chegando),
 * resposta (a final), ferramenta (uma ação rodou, com os argumentos), confirmar (uma ação pede licença), nova, erro.
 */
#ifndef LUMNA_EMMAC_H
#define LUMNA_EMMAC_H

#include <gio/gio.h>
#include <stdbool.h>
#include <stddef.h>

bool emmac_pedido(const char *pedido, char *resposta, size_t tam);
/* o mesmo, sem travar: pronto(resposta ou NULL) volta na linha principal */
void emmac_pedido_async(const char *pedido, void (*pronto)(const char *resposta, void *dados), void *dados);
int emmac_assinar(void);               /* descritor que acorda a cada evento; -1 se o serviço não responde */
/* lê o que chegou: devolve uma linha por vez (g_free) ou NULL se não há mais; *caiu se a conexão fechou */
char *emmac_assinar_ler(int fd, bool *caiu);

char *emmac_socket_caminho(void);

#endif
