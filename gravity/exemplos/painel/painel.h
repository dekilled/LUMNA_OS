/*
 * O Painel de controle por dentro: as categorias, as seções de cada uma e o que o Painel pede a
 * cada seção. A moldura (abas, grade, barra lateral, busca, Restaurar | Aplicar) fica em
 * exemplos/painel.c; cada categoria num arquivo desta pasta. O plano está em PAINEL.md.
 *
 * Uma seção desenha os ajustes dela em desenhar(). Se tem ajustes por aplicar, dá também
 * pendente/aplicar/descartar/restaurar: o Painel mostra o rodapé e pergunta antes de sair com algo
 * pendente. Uma seção ainda por fazer deixa desenhar a NULL e diz em planos o que vai ter
 * ("um;outro;mais um").
 */
#ifndef LUMNA_PAINEL_H
#define LUMNA_PAINEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gravity.h"

typedef struct pn_secao {
	const char *id, *icone, *nome, *descricao;
	void (*desenhar)(void);
	const char *planos;
	bool (*pendente)(void);  /* há mudanças por aplicar */
	void (*aplicar)(void);   /* grava (e quem vigia o arquivo aplica) */
	void (*descartar)(void); /* volta ao que está gravado */
	void (*restaurar)(void); /* os valores de fábrica, pendentes até ao Aplicar */
	void (*abrir)(void);     /* ao entrar na seção (ler o arquivo) */
} pn_secao;

enum pn_aba { PN_GERAL, PN_INICIALIZACAO, PN_SISTEMA, PN_NABAS };

typedef struct pn_categoria {
	const char *id, *icone, *nome, *subtitulo;
	enum pn_aba aba;
	const pn_secao *secoes;
	int nsecoes;
} pn_categoria;

/* todas as categorias, pela ordem da grade (categorias.c) */
extern const pn_categoria *const pn_categorias[];
extern const int pn_ncategorias;

/* a janela do Painel (para as telas, gv_saidas) */
extern gv_app *pn_app;

/* o modo desenvolvedor → aplicar em tempo real (desenvolvedor.c) */
bool pn_ao_vivo(void);
/* liga o serviço da rede uma vez (rede.c) */
void pn_rede_servico(void);

/* ---- ajudas para as seções (comum.c) ---- */

/* ~/.config/lumna/<nome> (ou em $XDG_CONFIG_HOME) */
void pn_caminho(char *buf, size_t tam, const char *nome);
/* grava inteiro num temporário e troca (quem vigia nunca lê um arquivo a meio) */
bool pn_gravar(const char *nome, const char *conteudo);
/* um título pequeno dentro da seção ("Formato", "Cor de destaque") */
void pn_subtitulo(Clay_ElementId id, const char *texto);
/* a linha de um ajuste: ícone (NULL: sem), nome e descrição (NULL: sem); o controle à direita é
 * de quem chama, entre pn_linha_comecar e pn_linha_acabar */
void pn_linha_comecar(Clay_ElementId id, const char *icone, const char *nome, const char *sub);
void pn_linha_acabar(void);
/* uma linha com uma lista suspensa à direita; devolve verdadeiro quando mudou */
bool pn_ajuste_lista(Clay_ElementId id, const char *icone, const char *nome, const char *sub, const char *const *opcoes,
	int n, int *escolha);
/* uma escolha em segmentos ("Compacta | Confortável"); devolve verdadeiro quando mudou */
bool pn_segmentos(Clay_ElementId id, const char *const *opcoes, int n, int *escolha);
/* uma miniatura de imagem que se toca (com o visto se escolhida e a legenda embaixo, NULL: sem) */
bool pn_miniatura(Clay_ElementId id, const char *caminho, float largura, float altura, bool escolhida, const char *legenda);
/* as imagens da pessoa (Imagens e Downloads): reler a lista, e o popup que as mostra todas (devolve
 * a escolhida no quadro em que se tocou, senão NULL) */
void pn_suas_carregar(void);
const char *pn_suas_popup(Clay_ElementId popup, Clay_ElementId ancora, const char *atual);
/* abrir outro programa sem esperar */
void pn_executar(const char *comando);
/* abre um terminal e roda o comando nele (uma atualização que pede senha, por exemplo); o terminal espera uma tecla ao fim */
void pn_terminal(const char *comando);
/* roda trabalho(dados) numa linha à parte e, quando acaba, pronto(dados) na linha da tela (que redesenha); pronto pode ser NULL */
void pn_tarefa(void (*trabalho)(void *), void (*pronto)(void *), void *dados);
/* um botão que pede confirmação no próprio lugar: o 1º toque troca por "aviso  [Confirmar] [Cancelar]"; devolve verdadeiro no Confirmar */
bool pn_confirmar(Clay_ElementId id, const char *icone, const char *texto, const char *aviso, bool perigo);
/* "1,4 GB", "820 MB": base 1000, como no Android e nos discos */
const char *pn_tamanho(int64_t bytes);
/* "26/10/2026" (0 ou negativo: "—") */
const char *pn_dia(int64_t unix_s);
/* vai a uma seção ("armazenamento/uso"): vale no começo do quadro seguinte */
void pn_ir(const char *id);
/* Aplicativos > Instalados: abre a página deste pacote (o nome no pacman, ou o caminho do AppImage) */
void pn_apps_selecionar(const char *pacote);
struct pacote;
/* os aplicativos lidos (0 e lista NULL enquanto lê) */
int pn_apps_dados(const struct pacote ***lista);

#endif
