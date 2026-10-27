/*
 * O backend da Emma (NewHarnessV2, por padrão em 127.0.0.1:8001), para o serviço lumna-emma. Entra com a
 * conta local do pacote lumna-emma (~/.config/lumna/emma/backend.env) ou com o que o app da Emma guardou
 * em ~/.config/lumna/emma.json (o mesmo arquivo do shell antigo: url, email, refresh_token, os ids das
 * conversas e as permissões das ações).
 *
 * Tudo aqui bloqueia: chame de uma linha à parte. Para testar: $LUMNA_EMMA_URL (no lugar do endereço).
 */
#ifndef LUMNA_EMMAB_H
#define LUMNA_EMMAB_H

#include <glib.h>
#include <stdbool.h>

#include "json.h"

/* de onde vem a mensagem: cada canal tem a sua conversa no backend */
enum { EMMAB_APP, EMMAB_ASSIST, EMMAB_NN };

const char *emmab_url(void);
/* a conta de que o serviço entra no backend: o endereço e o token de API (criado no harness; guardados em emma.json, só do usuário).
 * NULL ou vazio: fica como está. O que valia antes (a conta local de backend.env) segue valendo enquanto não houver token */
bool emmab_conta_gravar(const char *url, const char *token, char **erro);
bool emmab_token_ler(char *mascara, size_t tam);  /* "lumna_abcd…wxyz" (nunca o token inteiro); "" se não há */
void emmab_reentrar(void);                        /* esquece o acesso guardado: o próximo pedido pega de novo */
bool emmab_no_ar(void);                       /* o backend respondeu agora */

/* uma chamada com o token (entra de novo se vier 401). Devolve o status HTTP, ou 0 se o backend não respondeu */
int emmab_chamar(const char *metodo, const char *caminho, const char *corpo, char **resposta, char **erro);

/* a conversa do canal (a guardada, ou uma nova) */
bool emmab_sessao(int canal, char *sid, size_t n, char **erro);
void emmab_nova_conversa(int canal);

/* o que vem de uma conversa: cada evento do SSE chega em evento(); tool_client pede uma ação do computador:
 * acao() devolve se deu certo e o texto (resumo para a tela, conteúdo para o modelo) */
struct emmab_acao {
	char *resumo, *conteudo;
	bool ok;
};
struct emmab_ouvinte {
	void (*evento)(const struct json *ev, void *dados);
	void (*acao)(const char *nome, const struct json *args, struct emmab_acao *r, void *dados);
	void *dados;
};
/* manda uma mensagem e cuida da conversa até o fim; resposta é a última fala da Emma */
/* sid: a conversa (NULL: a do canal); extra: pedaço de JSON para o corpo do chat ("skill_id":"..."), ou NULL */
bool emmab_conversar(int canal, const char *sid, const char *texto, bool voz, const char *lumna_tools_json, const char *contexto, const char *extra,
	const struct emmab_ouvinte *o, char **resposta, char **erro);
void emmab_parar(void);                       /* corta a resposta que está chegando */
bool emmab_decidir(const char *sid, const char *call_id, bool permitir);

/* voz: o texto de um áudio WAV, e a fala de um texto (um arquivo de áudio) */
bool emmab_stt(const char *wav, char **texto, char **erro);
bool emmab_tts(const char *texto, const char *saida, char **erro);

/* ~/.config/lumna/emma.json: lê um texto, ou grava uma chave (valor já em JSON; NULL tira) */
char *emmab_config_texto(const char *chave);
bool emmab_config_gravar(const char *chave, const char *valor_json);
/* as permissões das ações ("auto", "ask", "off") que a pessoa mudou */
GHashTable *emmab_permissoes(void);

#endif
