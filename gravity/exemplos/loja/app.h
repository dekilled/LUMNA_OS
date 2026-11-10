/*
 * A Loja LUMNA (lumna-loja) em Gravity: o estado compartilhado entre as telas.
 *   principal.c   a janela, o catálogo (threads), a busca, as ações (instalar, remover, atualizar), os ajustes e a depuração
 *   barra.c       a barra lateral, a busca do alto, o painel do andamento e os diálogos
 *   telas.c       Explorar, Pacotes (busca com filtros), Instalados, Atualizações, o detalhe e Configurações
 * Quem sabe do pacman, do AUR e do catálogo é servicos/loja.c; aqui só há interface e threads.
 */
#ifndef LUMNA_LOJA_APP_H
#define LUMNA_LOJA_APP_H

#include <glib.h>

#include "gravity.h"
#include "loja.h"

enum tela { T_EXPLORAR, T_PACOTES, T_INSTALADOS, T_ATUALIZACOES, T_DRIVERS, T_CONFIG };

typedef struct cfg {
	char helper[16];       /* "", "paru" ou "yay": "" é o que estiver instalado */
	bool aur;              /* buscar também no AUR */
	bool confirmar;        /* perguntar antes de instalar ou remover */
} cfg;

typedef struct estado {
	gv_app *app;
	cfg cfg;
	const char *helper;    /* o paru/yay em uso (NULL: nenhum instalado) */
	lj_catalogo *cat;
	bool carregando;
	char erro_carga[256];
	int tela;
	/* a busca e os filtros (Pacotes) */
	char busca[256];
	lj_filtro f;
	int nota_idx;          /* 0 todas, 1 a partir de 4, 2 a partir de 3 */
	int por_idx;           /* 0 nome, 1 descrição, 2 ambos */
	int cat_idx;           /* categoria no dropdown */
	int ordem_idx;         /* 0 relevância, 1 nome, 2 tamanho, 3 nota */
	int *res;              /* os resultados (códigos de lj_buscar) */
	int nres, cap_res, mostrar;
	int natualiz;          /* quantos pacotes têm atualização */
	bool sujo;             /* refazer a lista no próximo quadro */
	/* Explorar */
	int ex_cat, ex_ordem;
	/* Instalados */
	bool inst_todos;       /* também bibliotecas e dependências */
	/* o AUR */
	bool aur_ocupado;
	char aur_termo[256];   /* o último termo consultado */
	double aur_em;         /* quando consultar (debounce); 0: nada pendente */
	char aur_erro[160];
	/* o detalhe */
	char det[96];
	bool det_aberto;
	/* a ação em andamento */
	bool agindo;
	int ag_acao;
	char ag_nome[96], ag_exibicao[64];
	GMutex ag_mutex;
	GPtrArray *ag_linhas;
	bool ag_fim, ag_ok;
	char ag_erro[512];
	bool ag_painel;        /* o painel do andamento está aberto */
	/* a confirmação */
	bool conf_aberta;
	int conf_acao;
	char conf_nome[96];
	/* as atualizações */
	bool atualizando_lista;
} estado;

extern estado L;

/* principal.c */
void marcar_sujo(void);
void pedir_acao(int acao, const char *nome);      /* confirma se precisar e começa */
void iniciar_acao(int acao, const char *nome);
void ir_para(int tela);
void abrir_detalhe(const char *nome);
void buscar_aur_agora(void);
void gravar_cfg(void);
void recarregar_catalogo(void);

/* barra.c */
void barra_lateral(void);
void barra_do_alto(void);
void painel_andamento(void);
void dialogos(void);

/* telas.c */
void tela_explorar(void);
void tela_pacotes(void);
void tela_instalados(void);
void tela_atualizacoes(void);
void tela_drivers(void);
void tela_config(void);
bool botao_acao(Clay_ElementId id, const lj_pacote *p);   /* o Instalar / Instalado / Atualizar de uma linha ou cartão */
void estrelas(const lj_pacote *p);

#endif
