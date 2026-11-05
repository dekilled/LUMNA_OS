/*
 * O PC visto de longe, para o LUMNA Connect: como ele está (a página do aparelho no celular) e as
 * ações de energia (bloquear, suspender, reiniciar, desligar). Para testar, $LUMNA_RAIZ_SISTEMA
 * aponta para uma árvore falsa (proc/, sys/) e o PATH para um systemctl e um lumna-bloqueio falsos.
 */
#ifndef LUMNA_ENERGIA_H
#define LUMNA_ENERGIA_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>

/* acrescenta o JSON {"type":"info","host","user","uptime","load","memory_total_kb","memory_available_kb","battery","charging"} */
void energia_info_json(GString *s);
/* lock | suspend | reboot | poweroff; false com o motivo em erro */
bool energia_acao(const char *acao, char *erro, size_t tam);

#endif
