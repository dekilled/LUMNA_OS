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

/* uma imagem da mensagem: o id no servidor e/ou o arquivo local (a que a pessoa acabou de mandar já está aqui; as do histórico vêm do servidor) */
struct eu_img {
	char id[40];
	char caminho[512];
};

struct eu_msg {
	bool usuario;
	struct eu_img img[4];
	int nimg;
	char *texto;
	char hora[8];
	char id[40];     /* o id no servidor (vazio: ainda não se sabe; o app descobre ao precisar) */
	bool parcial;
	GArray *ferramentas; /* struct eu_ferramenta */
};

/* O menu da bolha pede ações que mexem na lista de mensagens (apagar, regenerar, guardar na memória); quem tem a lista (o app) as faz
 * depois do desenho. acao: "apagar", "regenerar" ou "memoria". NULL: o menu só tem as de copiar. */
extern void (*eu_acao_mensagem)(int indice, const char *acao);
extern const char *eu_memoria_de; /* o nome da rotina que recebe "Guardar na memória" (NULL: a opção não aparece) */

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
/* as imagens do histórico ficam no servidor: o balão pede a cada uma (baixa para o cache, uma vez) e desenha quando chega */
void eu_imagem_guardar(const char *id, const char *caminho); /* "" = não deu */

/* Markdown (títulos, listas, citações, tabelas, blocos de código com Copiar, **negrito**, *itálico*, `código`, [links]) */
extern float eu_largura_balao;          /* a largura máxima de um balão */
bool eu_tem_markdown(const char *texto);
void eu_markdown(uint32_t k, const char *texto, float tam, Clay_Color cor);

void eu_aviso_erro(const char *erro, float tam);
/* o que mostrar a quem vai aprovar uma ação (o quê, onde, por quê e o código): um bloco de texto fixo, que se pode selecionar e copiar */
void eu_detalhe(const char *detalhe, float tam);
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
