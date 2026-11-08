/*
 * emma_terminal: a Emma dentro do Pulse Terminal. Um módulo isolado: monta o contexto (a pasta, os últimos comandos, as últimas sessões do banco),
 * manda a pergunta ao serviço da Emma (o comando "pergunta" do lumna-emma: texto só, sem ferramentas, então ela NUNCA age) e devolve a resposta.
 * Quem usa (o Pulse) decide o que mostrar e só executa algo depois da confirmação da pessoa.
 *
 * Regras que este módulo cumpre:
 *   - nada trava: o pedido é assíncrono; se a Emma não responde a tempo (começar em primeiro_s, acabar em teto_s) ou não está no ar, o aviso volta com
 *     resposta NULL e o Pulse segue em silêncio;
 *   - o que vai à Emma e ao banco passa por pulse_ocultar_segredos;
 *   - a saída dos comandos é DADO: vai entre marcas e a Emma é avisada de que não é instrução;
 *   - a listagem da pasta só vai se a pasta está na home (e sem nomes de segredos): fora dela, só o caminho (o sandbox da Emma);
 *   - a Emma recebe por padrão as últimas N sessões; se ela pedir mais fundo ([[HISTORICO: termo]]), o módulo busca no banco e pergunta uma vez mais.
 *
 * Configuração: ~/.config/lumna/pulse.ini, grupo [pulse] (sessoes_contexto=5, espera_inicio=3, teto=15, emma=sim|nao).
 */
#ifndef LUMNA_EMMA_TERMINAL_H
#define LUMNA_EMMA_TERMINAL_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

#include "pulsedb.h"

struct emt_config {
	bool ligado;          /* a Emma no terminal (emma=sim|nao) */
	int sessoes_contexto; /* quantas sessões anteriores vão por padrão */
	int espera_inicio;    /* segundos para a resposta começar */
	int teto;             /* segundos para a resposta acabar */
};

void emt_config_padrao(struct emt_config *c);
/* lê o ini por cima do padrão (caminho NULL: ~/.config/lumna/pulse.ini); um arquivo que não existe não é erro */
void emt_config_ler(struct emt_config *c, const char *caminho);

typedef struct emt emt;

emt *emt_novo(pulsedb *db, int64_t sessao, const struct emt_config *cfg);
void emt_liberar(emt *e); /* pedidos ainda no ar são largados: o aviso deles não volta */

enum emt_tipo {
	EMT_CORRECAO,  /* um comando falhou: a correção */
	EMT_COMANDO,   /* linguagem natural -> um comando */
	EMT_EXPLICACAO /* o que este comando vai fazer */
};

struct emt_pedido {
	enum emt_tipo tipo;
	const char *cwd;
	const char *comando;  /* CORRECAO e EXPLICACAO: o comando */
	const char *saida;    /* CORRECAO: o fim do que ele escreveu */
	int exit_code;        /* CORRECAO */
	const char *texto;    /* COMANDO: o que a pessoa pediu */
	int64_t comando_id;   /* o comando no banco, se há (0: nenhum) */
};

/* A resposta chegou (resposta != NULL, o texto da Emma) ou não deu (NULL: sem Emma, sem tempo, sem provedor...: o Pulse não mostra nada). interacao: o id no banco. */
typedef void (*emt_pronto)(const char *resposta, int64_t interacao, void *dados);

void emt_pedir(emt *e, const struct emt_pedido *p, emt_pronto pronto, void *dados);
/* Chame de tempos em tempos (a cada quadro): entrega NULL aos pedidos que passaram do prazo, mesmo que o serviço da Emma tenha travado. */
void emt_tick(emt *e);
/* a Emma está ligada no terminal (emma=sim no pulse.ini)? */
bool emt_ligado(const emt *e);
/* quantos pedidos estão no ar (o Pulse acorda o relógio enquanto houver) */
int emt_pendentes(const emt *e);

/* ---- as peças, soltas para testar e para o Pulse usar ---- */

/* o texto da pergunta (a mensagem do usuário) e o do sistema; g_free */
char *emt_montar_usuario(emt *e, const struct emt_pedido *p, const char *historico_extra);
const char *emt_sistema(enum emt_tipo tipo);
/* a Emma pediu mais histórico? devolve o termo (g_free) ou NULL */
char *emt_pediu_historico(const char *resposta);
/* "o comando" e "a explicação" de uma resposta de CORRECAO/COMANDO: a primeira linha (sem crases nem $) e o resto. cmd NULL quando a Emma disse NENHUM. g_free nos dois */
void emt_separar(const char *resposta, char **cmd, char **explicacao);
/* a listagem curta de uma pasta, só dentro da home e sem nomes de segredos; "" fora dela (g_free) */
char *emt_listagem(const char *cwd);
/* Uma falha que vale pedir a correção à Emma? Não valem: a pessoa interrompeu (Ctrl+C, Ctrl+\\, Ctrl+Z, SIGTERM, SIGPIPE) ou o programa usa o código 1 para uma resposta normal
 * (grep sem achar, diff com diferença, test, false...). programas: lista separada por vírgulas desses "código 1 é normal" (NULL: a padrão) */
bool emt_falha_vale_a_pena(const char *comando, int exit_code, const char *programas);
/* o comando dispara a explicação antes de rodar? lista: "rm,dd,mkfs,chmod -R,kill..." (NULL: a padrão) */
bool emt_comando_perigoso(const char *comando, const char *lista);

#endif
