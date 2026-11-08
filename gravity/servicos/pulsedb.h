/*
 * pulsedb: a memória do Pulse Terminal, num SQLite só da pessoa (~/.local/share/lumna/pulse/pulse.db, modo 600): as sessões, os comandos que rodaram
 * (com o código de saída e o fim da saída) e o que a Emma respondeu a cada um. A Emma recebe um resumo das últimas sessões como contexto.
 *
 * Privacidade: senhas, tokens e chaves são trocados por *** ANTES de gravar e antes de irem à Emma (pulse_ocultar_segredos); comando que começa com espaço
 * nem chega aqui (o shell não o põe no histórico). pulsedb_apagar_tudo apaga tudo.
 *
 * O esquema tem versão (PRAGMA user_version) e migrações em ordem: abrir um banco antigo o leva até a versão de agora. Tudo bloqueia um instante (SQLite
 * local): chame da linha principal sem medo.
 */
#ifndef LUMNA_PULSEDB_H
#define LUMNA_PULSEDB_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct pulsedb pulsedb;

/* O banco no lugar de sempre (caminho NULL) ou onde se mandar (os testes). Cria a pasta e o arquivo (700/600) e roda as migrações. NULL e *erro (g_free) se não deu */
pulsedb *pulsedb_abrir(const char *caminho, char **erro);
void pulsedb_fechar(pulsedb *db);
char *pulsedb_caminho_padrao(void); /* g_free */

/* a versão do esquema do banco aberto, e a mais nova que este programa conhece */
int pulsedb_versao(pulsedb *db);
int pulsedb_versao_atual(void);
/* o SQL de todas as migrações, em ordem (para ler, ou rodar à mão com o sqlite3); g_free */
char *pulsedb_esquema_sql(void);

/* as sessões: uma por janela do Pulse */
int64_t pulsedb_sessao_iniciar(pulsedb *db, const char *cwd_inicial);
void pulsedb_sessao_encerrar(pulsedb *db, int64_t sessao);

/* Um comando que acabou. Devolve o id, ou 0 se não vale registrar (vazio). O texto e a saída passam por pulse_ocultar_segredos. */
int64_t pulsedb_comando_gravar(pulsedb *db, int64_t sessao, const char *cwd, const char *comando, int exit_code, const char *saida);
/* uma resposta (ou tentativa) da Emma: tipo "correcao" | "comando" | "explicacao". command_id 0: não ligada a um comando. Devolve o id */
int64_t pulsedb_interacao_gravar(pulsedb *db, int64_t sessao, int64_t command_id, const char *tipo, const char *prompt, const char *resposta);
void pulsedb_interacao_resposta(pulsedb *db, int64_t interacao, const char *resposta);
/* o que a pessoa fez com a sugestão: "aplicada" | "inserida" | "descartada" | "ignorada" */
void pulsedb_interacao_decisao(pulsedb *db, int64_t interacao, const char *decisao);

/* os últimos n comandos da sessão (do mais antigo ao mais novo), uma linha cada: "comando  (código N)" (g_free) */
char *pulsedb_ultimos_comandos(pulsedb *db, int64_t sessao, int n);
/* o contexto que a Emma recebe por padrão: o fim da sessão de agora e as últimas n_sessoes anteriores, com os comandos de cada uma e, nos que falharam,
 * uma linha do fim da saída. No máximo ~6000 caracteres (g_free) */
char *pulsedb_contexto(pulsedb *db, int64_t sessao_atual, int n_sessoes);
/* a busca mais funda, quando a Emma pede (pelo termo, no comando e na saída de todo o histórico): até max linhas "quando · pasta · comando (código)" (g_free) */
char *pulsedb_buscar(pulsedb *db, const char *termo, int max);

/* tira do histórico o que passou de "dias" (e as sessões que ficaram vazias) */
void pulsedb_podar(pulsedb *db, int dias);
void pulsedb_apagar_tudo(pulsedb *db);
/* quantos comandos há, no total (para os testes e o --historico) */
int pulsedb_total_comandos(pulsedb *db);

/* troca por *** o que parece segredo: --password=x, -p x, Authorization: Bearer x, VAR_TOKEN=x, https://usuario:senha@host, chaves conhecidas (sk-..., ghp_...) (g_free) */
char *pulse_ocultar_segredos(const char *texto);

#endif
