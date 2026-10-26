/*
 * Os modos do desktop (Desktop, Gaming, Mídia): o estado vem do Orbit (orbit-msg modo) e a
 * configuração fica em ~/.config/lumna/modos.ini, que o Painel grava:
 *
 *   [geral]    layout = tela | estender       um modo por tela, ou todas as telas juntas
 *              ao_ligar = desktop|gaming|midia
 *   [desktop] [gaming] [midia]
 *              esconder_dock = sim|nao        a dock some nesse modo
 *              nao_perturbe = sim|nao         os avisos só deixam passar os urgentes
 *
 * modos_assinar() devolve um descritor que acorda quando o Orbit troca um modo (gv_app_vigiar); em
 * quem acorda, modos_assinar_ler(fd) limpa e modos_ler() traz o estado novo.
 */
#ifndef LUMNA_MODOS_H
#define LUMNA_MODOS_H

#include <stdbool.h>

enum { MODO_DESKTOP, MODO_GAMING, MODO_MIDIA, MODO_N };
#define MODOS_MAX_SAIDAS 8

struct modos_saida {
	char nome[32];
	int modo;
	bool ponteiro; /* o cursor está nesta saída */
};

struct modos_estado {
	bool orbit; /* o Orbit respondeu */
	bool estender;
	struct modos_saida saidas[MODOS_MAX_SAIDAS];
	int n;
};

struct modos_cfg {
	int ao_ligar;
	bool estender;
	struct {
		bool esconder_dock, nao_perturbe;
	} m[MODO_N];
};

const char *modos_nome(int modo);   /* "Desktop", "Gaming", "Mídia" */
const char *modos_id(int modo);     /* "desktop", "gaming", "midia" */
const char *modos_icone(int modo);  /* o ícone do Gravity */
const char *modos_descricao(int modo);
int modos_de_id(const char *id);    /* -1 se não for um modo */

bool modos_ler(struct modos_estado *e);
/* o modo da saída com o cursor (ou da primeira) */
int modos_em_foco(const struct modos_estado *e);
/* o modo de uma saída pelo nome (-1 se não houver) */
int modos_da_saida(const struct modos_estado *e, const char *saida);
/* troca o modo (da saída dada, ou da em foco se NULL; todas, no layout "estender") */
bool modos_definir(int modo, const char *saida);
bool modos_definir_layout(bool estender);

int modos_assinar(void);
void modos_assinar_ler(int fd);

/* o modo desta saída (NULL: a em foco) pede a dock escondida / avisos em silêncio? (lê o Orbit e o modos.ini) */
bool modos_esconde_dock(const char *saida);
bool modos_nao_perturbe(void);

/* modos.ini */
void modos_cfg_ler(struct modos_cfg *c);
bool modos_cfg_gravar(const struct modos_cfg *c);

#endif
