/*
 * Agendamentos do LUMNA Connect: tarefas que o PC faz numa hora marcada, criadas pelo celular (ou
 * pelo Painel). Desligar, reiniciar, suspender, bloquear, um lembrete (aparece no PC e nos celulares)
 * ou um pedido para a Emma.
 *
 * Ficam em $XDG_CONFIG_HOME/lumna/schedule.json (o mesmo formato do LUMNA antigo: lista de
 * {id, at, action, text, daily, by}). Tarefa que venceu com o PC desligado roda assim que ele volta,
 * se não passou de 1 hora; senão só é descartada (ou vai para o dia seguinte, se repete).
 * Para testar, $LUMNA_AGORA (segundos unix) fixa a hora.
 */
#ifndef LUMNA_AGENDA_H
#define LUMNA_AGENDA_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>

struct agenda_tarefa {
	char id[24];
	gint64 em;        /* segundos unix */
	char acao[12];    /* poweroff | reboot | suspend | lock | remind | emma */
	char *texto;      /* o lembrete ou o pedido para a Emma */
	bool diaria;
	char por[64];     /* quem criou */
};

gint64 agenda_agora(void);
/* as tarefas, da mais próxima para a mais distante (cada item se libera com agenda_liberar) */
GPtrArray *agenda_listar(void);
/* valida e guarda; false com o motivo em erro */
bool agenda_adicionar(const char *acao, gint64 em, const char *texto, bool diaria, const char *por, char *erro, size_t tam);
bool agenda_remover(const char *id);
/* tira da lista o que venceu (as diárias vão para o dia seguinte) e devolve o que deve rodar agora */
GPtrArray *agenda_vencidas(void);
/* acrescenta ao texto o JSON {"type":"schedule","tasks":[...]} */
void agenda_json(GString *s);
void agenda_liberar(gpointer tarefa);

#endif
