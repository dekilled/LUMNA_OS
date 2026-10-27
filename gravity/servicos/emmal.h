/*
 * A Emma falando direto com o provedor de IA na nuvem (qualquer API compatível com OpenAI: OpenAI, Ollama, Gemini, Claude, um endereço seu).
 * O endereço, a chave e o modelo vêm do Painel > Emma > Provedores (~/.config/lumna/emma/provedores.json, só do usuário); as conversas
 * ficam guardadas em arquivos no PC (emmas.h). Não passa por servidor nenhum nem pede login.
 *
 * Tudo aqui bloqueia: chame de uma linha à parte.
 */
#ifndef LUMNA_EMMAL_H
#define LUMNA_EMMAL_H

#include <glib.h>
#include <stdbool.h>

#include "emmab.h" /* struct emmab_ouvinte: os mesmos avisos de antes (evento, acao) */

struct emmal_prov {
	char nome[96], url[200], chave[200], modelo[96];
};

/* o provedor e o modelo de uma tarefa ("chat", "planner"...), como o Painel > Roteamento deixou; false (e o motivo) se não há nenhum */
bool emmal_provedor(const char *tarefa, struct emmal_prov *p, char **erro);

/* uma conversa: a fala nova entra na sessão sid, a Emma responde (e age com as ferramentas), e tudo fica guardado. resposta: a última fala */
bool emmal_conversar(const char *sid, const char *texto, bool voz, const char *lumna_tools_json, const char *contexto, const struct emmab_ouvinte *o,
	char **resposta, char **erro);
void emmal_parar(void); /* corta a resposta que está chegando */

/* para o Painel: os modelos que o provedor oferece, e o teste de capacidades. Devolvem um JSON (g_free) */
char *emmal_modelos_json(const char *url, const char *chave);
char *emmal_testar_json(const char *url, const char *chave, const char *modelo);

#endif
