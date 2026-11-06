/*
 * A sessão: quais apps estavam abertos, para reabrir no próximo login (Inicialização → Ao entrar). O Orbit diz as
 * janelas (app_id); aqui cada app_id vira o .desktop dele (pelo nome, pelo StartupWMClass ou pelo programa) e a
 * lista fica em ~/.local/state/lumna/sessao.list, um .desktop por linha. As janelas do sistema (a Mídia, o lançador,
 * o seletor de modos) não entram.
 */
#ifndef LUMNA_SESSAO_H
#define LUMNA_SESSAO_H

#include <stdbool.h>
#include <stddef.h>

#define SESSAO_MAX 48

/* de um JSON de "janelas" do Orbit para os .desktop dos apps (sem repetir); devolve quantos */
int sessao_de_json(const char *json, char ids[][160], int max);
/* o mesmo, perguntando ao Orbit agora; -1 se ele não respondeu */
int sessao_agora(char ids[][160], int max);

bool sessao_gravar(const char ids[][160], int n);
int sessao_ler(char ids[][160], int max);
/* abre os apps da lista; devolve quantos */
int sessao_abrir(char ids[][160], int n);
/* o arquivo da lista (para testes e para o Painel) */
char *sessao_arquivo(void);

#endif
