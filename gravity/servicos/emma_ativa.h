/*
 * A Emma ligada ou desligada no sistema inteiro (a escolha do pré-setup e do Painel > Emma): ~/.config/lumna/emma-sistema.ini
 *   [emma]
 *   ativa = sim|nao      (padrão: sim)
 * Desligada, o serviço (lumna-emma) não sobe, o chat lateral e o app não abrem e a barra não mostra o ícone dela.
 * Quem liga e desliga de verdade (parar o serviço, reiniciar) é setup_emma_definir (setup.h); aqui só se lê e se grava o ini,
 * para os programas pequenos (barra, chat, app) poderem perguntar sem puxar mais nada.
 */
#ifndef LUMNA_EMMA_ATIVA_H
#define LUMNA_EMMA_ATIVA_H

#include <stdbool.h>

bool emma_ativa(void);
bool emma_ativa_gravar(bool ativa);

#endif
