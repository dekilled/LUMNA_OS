#ifndef SGBUFFER_H
#define SGBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * O texto do Signals (o editor de texto do LUMNA): o documento, sem tela nenhuma. Guarda as linhas (UTF-8, sem o \n), desfaz e refaz, busca e substitui,
 * abre e salva com cuidado. Tudo em UTF-8: a posição é (linha, byte dentro da linha) e sempre cai na fronteira de um caractere; quem desenha converte em colunas.
 *
 * O documento é a lista de linhas separadas por \n: um texto que termina em \n tem a última linha vazia, então o que se abre é exatamente o que se salva.
 */

struct sg_pos {
	int linha;
	int col; /* byte dentro da linha */
};

enum sg_fim_linha { SG_LF, SG_CRLF };
enum sg_codificacao { SG_UTF8, SG_LATIN1 }; /* o que o arquivo era (e volta a ser ao salvar) */

typedef struct sg_buffer sg_buffer;

sg_buffer *sg_novo(void);
void sg_liberar(sg_buffer *b);

/* ---- arquivo ---- */
/* 0 se abriu; -1 com o motivo em erro (não achou, sem permissão, binário, grande demais). Substitui o texto e zera o desfazer. */
int sg_abrir(sg_buffer *b, const char *caminho, char *erro, size_t tam);
/* grava de uma vez (num arquivo temporário e rename: um corte no meio não estraga o original); caminho NULL: o de abrir. 0 se gravou; -1 com o motivo. */
int sg_salvar(sg_buffer *b, const char *caminho, char *erro, size_t tam);
const char *sg_caminho(const sg_buffer *b);
/* dá um nome ao documento sem tocar no disco (um arquivo que ainda não existe: sg_salvar o cria) */
void sg_definir_caminho(sg_buffer *b, const char *caminho);
bool sg_modificado(const sg_buffer *b);
/* sobe a cada mudança no texto (editar, desfazer, refazer, abrir): quem guarda um resultado (a contagem da busca) sabe quando refazer */
uint64_t sg_versao(const sg_buffer *b);
/* a pessoa viu que o arquivo mudou no disco e escolheu manter o dela: não avisa de novo por esta mudança */
void sg_ignorar_mudanca_no_disco(sg_buffer *b);
/* quantos caracteres (não bytes) há da linha até a posição: a coluna que se mostra */
int sg_coluna(const sg_buffer *b, struct sg_pos p);
/* o arquivo mudou no disco desde que se abriu ou salvou (outro programa, outra janela) */
bool sg_mudou_no_disco(const sg_buffer *b);
enum sg_fim_linha sg_fim_de_linha(const sg_buffer *b);
void sg_definir_fim_de_linha(sg_buffer *b, enum sg_fim_linha f);
enum sg_codificacao sg_codificacao(const sg_buffer *b);
/* muda como o arquivo vai ser gravado (não mexe no texto); conta como modificação */
void sg_definir_codificacao(sg_buffer *b, enum sg_codificacao c);
bool sg_tem_bom(const sg_buffer *b);
void sg_definir_bom(sg_buffer *b, bool bom);
/* o arquivo tinha os dois tipos de quebra de linha (foi normalizado para o predominante ao abrir) */
bool sg_fim_misto(const sg_buffer *b);

/* ---- leitura ---- */
int sg_linhas(const sg_buffer *b);
const char *sg_linha(const sg_buffer *b, int i, size_t *len); /* vale até a próxima edição */
size_t sg_bytes(const sg_buffer *b);                           /* o tamanho do documento (com os \n) */
char *sg_texto(const sg_buffer *b, struct sg_pos a, struct sg_pos z); /* de a até z (g_free); com a>z troca; NULL se vazio não: devolve "" */
char *sg_texto_todo(const sg_buffer *b);

/* ---- posições ---- */
int sg_pos_cmp(struct sg_pos a, struct sg_pos b);
struct sg_pos sg_pos_valida(const sg_buffer *b, struct sg_pos p);  /* encaixa na linha e na fronteira de caractere */
struct sg_pos sg_pos_fim(const sg_buffer *b);
struct sg_pos sg_pos_proximo(const sg_buffer *b, struct sg_pos p); /* um caractere à frente (passa para a linha seguinte no fim) */
struct sg_pos sg_pos_anterior(const sg_buffer *b, struct sg_pos p);
struct sg_pos sg_pos_palavra_proxima(const sg_buffer *b, struct sg_pos p);   /* Ctrl+Direita */
struct sg_pos sg_pos_palavra_anterior(const sg_buffer *b, struct sg_pos p);  /* Ctrl+Esquerda */
void sg_palavra_em(const sg_buffer *b, struct sg_pos p, struct sg_pos *ini, struct sg_pos *fim); /* a palavra sob a posição (duplo clique) */

/* ---- edição (cada uma entra no desfazer) ---- */
/* Insere texto (UTF-8, com \n e \r\n: o \r é descartado) em p; devolve onde o cursor fica. junta: se vem logo depois de outra digitação (mesmo ponto),
 * desfazer desfaz as duas juntas, como ao digitar uma palavra. */
struct sg_pos sg_inserir(sg_buffer *b, struct sg_pos p, const char *texto, size_t len, bool junta);
/* apaga de a até z (qualquer ordem); devolve o início. junta: como acima (o Backspace repetido) */
struct sg_pos sg_apagar(sg_buffer *b, struct sg_pos a, struct sg_pos z, bool junta);
/* troca o trecho por outro texto, desfeito de uma vez */
struct sg_pos sg_trocar(sg_buffer *b, struct sg_pos a, struct sg_pos z, const char *texto, size_t len);
/* separa o desfazer: a próxima edição não junta com a anterior */
void sg_fechar_grupo(sg_buffer *b);
/* o cursor que a pessoa tinha antes e depois do que foi desfeito/refeito */
bool sg_desfazer(sg_buffer *b, struct sg_pos *cursor);
bool sg_refazer(sg_buffer *b, struct sg_pos *cursor);
bool sg_pode_desfazer(const sg_buffer *b);
bool sg_pode_refazer(const sg_buffer *b);
/* marca o ponto atual como "salvo" (sg_salvar já faz): modificado vira falso até a próxima edição, e volta a ser falso se desfazer até aqui */
void sg_marcar_salvo(sg_buffer *b);

/* ---- busca ---- */
struct sg_busca {
	const char *agulha;
	bool maiusculas;  /* diferencia maiúsculas de minúsculas */
	bool palavra;     /* só palavras inteiras */
};
/* o próximo achado a partir de de (para frente: começa em de; para trás: termina antes de de); volta ao outro lado do documento se não acha (volteou, se dado, diz que passou do fim).
 * A agulha de uma linha só (sem \n). */
bool sg_buscar(const sg_buffer *b, const struct sg_busca *q, struct sg_pos de, bool frente, struct sg_pos *ini, struct sg_pos *fim, bool *voltou);
int sg_contar(const sg_buffer *b, const struct sg_busca *q);
/* os achados de uma linha só (inícios e tamanhos em bytes), até max: para destacar o que se vê sem varrer o documento */
int sg_buscar_linha(const sg_buffer *b, const struct sg_busca *q, int linha, int *inicios, int *tamanhos, int max);
/* troca todas (desfeito de uma vez); devolve quantas. A troca pode ter \n. */
int sg_substituir_tudo(sg_buffer *b, const struct sg_busca *q, const char *por);

#endif
