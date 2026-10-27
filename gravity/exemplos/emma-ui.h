/*
 * Peças de interface da Emma que o painel flutuante (emma-chat.c) e o app completo (emma-app.c) dividem:
 * as mensagens, o avatar, os balões, os botões de próximo passo e o cartão que leva a uma seção do Painel.
 */
#ifndef LUMNA_EMMA_UI_H
#define LUMNA_EMMA_UI_H

#include <glib.h>
#include <stdbool.h>

#include "gravity.h"
#include "json.h"

struct eu_ferramenta {
	char nome[64];
	char resumo[160];
	char secao[64];     /* painel.abrir: a seção */
	bool ok;
};

struct eu_msg {
	bool usuario;
	char *texto;
	char hora[8];
	bool parcial;
	GArray *ferramentas; /* struct eu_ferramenta */
};

/* o violeta dos balões e do enviar (um tom mais fundo que o acento dos botões, como no desenho) */
extern const Clay_Color EU_VIOLETA;

struct eu_resposta {
	char *corpo;
	char principal[96];
	char links[2][96];
	int nlinks;
	char sugestoes[2][96];
	int nsug;
};

/* o texto da Emma sem as etiquetas [acao: ...] [link: ...] [sugestao: ...], que viram botões (libere r->corpo) */
void eu_ler_resposta(const char *texto, struct eu_resposta *r);

/* uma imagem da Emma (avatar.jpg, fundo.jpg), do LUMNA_EMMA_ASSETS, de assets/emma ao lado do programa ou de /usr/share/lumna/emma */
char *eu_achar_asset(const char *nome);
void eu_iniciar(void); /* acha o avatar */
extern char *eu_avatar_caminho;

/* a lista de mensagens de uma conversa */
void eu_msg_liberar(gpointer p);
struct eu_msg *eu_msg_nova(GPtrArray *msgs, bool usuario, const char *texto, const char *hora);
struct eu_msg *eu_msg_parcial(GPtrArray *msgs); /* a resposta que ainda está chegando (ou NULL) */
void eu_juntar_ferramenta(struct eu_msg *m, const struct json *t);

void eu_avatar(Clay_ElementId id, float tam);
bool eu_botao_icone(Clay_ElementId id, const char *icone, bool ligado);
bool eu_chip(Clay_ElementId id, const char *texto, bool principal);
void eu_nome_da_secao(const char *secao, char *titulo, size_t tt, char *sub, size_t ts);
void eu_cartao_secao(uint32_t k, const struct eu_ferramenta *f);
/* um balão (e, na resposta da Emma, o que ela fez e os botões); enviar recebe o texto do botão tocado */
void eu_bolha(uint32_t k, const struct eu_msg *m, void (*enviar)(const char *));

/* abre uma seção do Painel / outro programa da LUMNA sem esperar por ele */
void eu_abrir_secao(const char *secao);
void eu_lancar(const char *programa);

#endif
