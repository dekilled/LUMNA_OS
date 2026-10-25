/*
 * O papel de parede do LUMNA: ~/.config/lumna/papel.ini, que o Painel grava e o papel lê (e
 * vigia). Um papel por modo (desktop, gaming, midia, ou "todos") e, com por_tela, um por tela:
 *
 *   [geral]
 *   por_tela = nao
 *   trocar = nao
 *   intervalo = 30          ; minutos
 *   [todos]
 *   imagem = /usr/share/lumna/wallpapers/aurora-azul.jpg
 *   ajuste = preencher      ; preencher, ajustar, esticar
 *   [desktop HDMI-A-1]
 *   imagem = ...
 *
 * Qual vale numa tela, num modo: o da tela nesse modo (com por_tela), o da tela em "todos", o do
 * modo, o de "todos", e por fim o do LUMNA. O antigo ~/.config/lumna/papel (só um caminho)
 * conta como o de "todos" quando o .ini não existe.
 */
#ifndef LUMNA_PAPELINI_H
#define LUMNA_PAPELINI_H

#include <stdbool.h>
#include <stddef.h>

#define PAPEL_MAX 32
#define PAPEL_COLECAO_MAX 64

enum papel_ajuste { PAPEL_PREENCHER, PAPEL_AJUSTAR, PAPEL_ESTICAR };

struct papel_item {
	char modo[16];   /* desktop, gaming, midia ou todos */
	char saida[64];  /* "" : todas as telas */
	char imagem[512];
	int ajuste;
};

struct papel_cfg {
	bool por_tela;
	bool trocar;     /* trocar sozinho entre os da coleção */
	int intervalo;   /* minutos */
	struct papel_item itens[PAPEL_MAX];
	int n;
};

void papel_ler(struct papel_cfg *c);
bool papel_gravar(const struct papel_cfg *c);
/* o texto do arquivo (g_free depois): dois iguais querem dizer nada por aplicar */
char *papel_texto(const struct papel_cfg *c);
/* o que vale nesta tela e neste modo, seguindo a ordem acima (NULL: nenhum, usar o do LUMNA) */
const struct papel_item *papel_para(const struct papel_cfg *c, const char *modo, const char *saida);
/* o item exato (modo e tela), criando-o se não existe */
struct papel_item *papel_editar(struct papel_cfg *c, const char *modo, const char *saida);
/* a imagem de fábrica do LUMNA */
const char *papel_padrao(void);
/* a Coleção LUMNA (as imagens instaladas, ou as do repositório ao lado do programa), ordenada */
int papel_colecao(char caminhos[][512], int max);
/* "aurora-azul.jpg" → "Aurora azul" */
void papel_nome(const char *caminho, char *nome, size_t tam);
/* o modo de agora (~/.config/lumna/modo; desktop se não houver) */
const char *papel_modo_atual(void);
/* o arquivo, para vigiar a pasta */
void papel_caminho_ini(char *buf, size_t tam);

#endif
