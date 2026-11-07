/*
 * O agente do polkit (lumna-polkit): as partes que não são de tela. Quando algo pede permissão de administrador
 * (o pkexec do Painel, o Flatpak, a Loja...), o polkit chama o agente; ele pergunta a senha numa janela do LUMNA
 * e confere pelo polkit-agent-helper-1 (pelo socket /run/polkit/agent-helper.socket no polkit 126+, ou rodando
 * o helper setuid nas versões antigas), que fala com o PAM.
 *
 * Para os testes: LUMNA_POLKIT_SOCKET e LUMNA_POLKIT_HELPER trocam o socket e o helper.
 */
#ifndef LUMNA_POLKITAG_H
#define LUMNA_POLKITAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A conta cuja senha pedir: a própria pessoa, se o polkit aceita; senão a primeira oferecida (root ou outro admin).
 * Devolve 1 e põe o uid em *uid, ou 0 se não há nenhuma. */
int polkitag_escolher(const uint32_t *uids, int n, uint32_t proprio, uint32_t *uid);

/* Desfaz o escape do g_strescape nas linhas do helper (\n, \t, \\, \"). Devolve texto novo (g_free). */
char *polkitag_desescapar(const char *linha);

/* O nome da conta pelo uid (/etc/passwd, ou $LUMNA_ETC/passwd). g_free; NULL se não existe. */
char *polkitag_nome(uint32_t uid);

enum { POLKITAG_OK, POLKITAG_ERRADA, POLKITAG_FALHA };

/* Confere a senha da conta para o cookie do pedido. POLKITAG_ERRADA: o PAM recusou; POLKITAG_FALHA: o helper nem
 * respondeu (o motivo em erro). A senha é apagada da memória daqui; apague a sua também. */
int polkitag_conferir(const char *usuario, const char *cookie, const char *senha, char *erro, size_t n);

#endif
