/*
 * Os avisos do LUMNA: o servidor org.freedesktop.Notifications (o que o notify-send, o
 * navegador e as apps GTK e Qt usam para avisar) numa linha à parte, com o GDBus. O que chega
 * fica numa fila e o descritor de aviso fica legível; a app lê a fila no ciclo dela.
 *
 *   int fd = avisos_iniciar();                 // -1: outro lumna-avisos já está a correr
 *   gv_app_vigiar(app, fd, chegou, NULL);      // em chegou(): avisos_ler(...)
 *   avisos_acao(id, "default");               // a pessoa tocou no aviso
 *   avisos_fechou(id, AVISO_DISPENSADO);
 *
 * O corpo chega sem marcação (as tags saem, &amp; e afins viram texto). Uma imagem em bytes
 * (image-data) é gravada num PNG em $XDG_RUNTIME_DIR/lumna-avisos, e o caminho vem em imagem.
 */
#ifndef LUMNA_AVISOS_H
#define LUMNA_AVISOS_H

#include <stdbool.h>
#include <stdint.h>

#define AVISO_MAX_ACOES 6

enum aviso_urgencia { AVISO_BAIXA, AVISO_NORMAL, AVISO_CRITICA };

struct aviso {
	uint32_t id;
	char app[64];          /* o nome que a app deu ("Firefox") */
	char entrada[128];     /* o .desktop dela (dica desktop-entry), sem o ".desktop" */
	char icone[512];       /* nome no tema de ícones, ou um caminho de arquivo */
	char imagem[512];      /* uma imagem do conteúdo (a capa, a foto de quem escreve), caminho */
	char titulo[256];
	char corpo[1024];
	char acoes[AVISO_MAX_ACOES][2][64]; /* a chave e o rótulo de cada botão */
	int nacoes;
	enum aviso_urgencia urgencia;
	int expira_ms;         /* -1: o tempo do LUMNA; 0: só sai quando a pessoa fechar */
	bool residente;        /* fica depois de uma ação (um player) */
	bool transitorio;      /* não vai para a central */
	int64_t quando;        /* time() da chegada */
};

enum aviso_tipo {
	AVISO_NOVO,   /* um aviso novo, ou um que substitui o do mesmo id */
	AVISO_FECHAR, /* a app pediu para tirar o aviso id (CloseNotification) */
	AVISO_CENTRO, /* alguém (a topbar) pediu para abrir ou fechar a central */
};

struct aviso_evento {
	enum aviso_tipo tipo;
	uint32_t id;
	struct aviso aviso; /* em AVISO_NOVO */
};

/* Os motivos do NotificationClosed (da especificação). */
enum { AVISO_EXPIROU = 1, AVISO_DISPENSADO = 2, AVISO_FECHADO_PELA_APP = 3 };

/* Liga ao barramento da sessão e fica com os nomes. Devolve o descritor de aviso, ou -1 se não
 * há barramento ou se outro lumna-avisos já está a correr. */
int avisos_iniciar(void);
/* Tira até max eventos da fila (e limpa o aviso). Devolve quantos. */
int avisos_ler(struct aviso_evento *eventos, int max);
/* Diz às apps que o aviso saiu (motivo: AVISO_EXPIROU...). */
void avisos_fechou(uint32_t id, uint32_t motivo);
/* Diz à app que a pessoa escolheu a ação (a chave dela; "default" é tocar no aviso). */
void avisos_acao(uint32_t id, const char *chave);
/* Do lado de fora (a topbar): pede ao lumna-avisos que está a correr para alternar a central. */
bool avisos_pedir_centro(void);

#endif
