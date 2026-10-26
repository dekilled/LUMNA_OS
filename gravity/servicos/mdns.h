/*
 * Anuncia um serviço na rede local (mDNS, RFC 6762) para o celular achar a TV sem digitar IP:
 * `_lumna-remote._tcp` na porta dada, com o nome "LUMNA <máquina>". Um respondedor pequeno, só
 * do que o app precisa: responde às perguntas (PTR, SRV, TXT, A) e se anuncia ao ligar e de tempos
 * em tempos. Roda no laço do GLib.
 */
#ifndef LUMNA_MDNS_H
#define LUMNA_MDNS_H

#include <stdbool.h>

bool mdns_anunciar(const char *tipo, int porta); /* tipo: "_lumna-remote._tcp" */
/* os endereços IPv4 desta máquina na rede local, em texto ("192.168.0.5"); devolve quantos */
int mdns_enderecos(char (*saida)[16], int max);

#endif
