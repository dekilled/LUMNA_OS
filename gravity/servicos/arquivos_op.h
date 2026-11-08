/*
 * O que o explorador faz com os arquivos (copiar, mover, lixeira com desfazer, apagar, compactar, extrair, renomear, criar), sem desenho nenhum.
 *
 * Copiar, mover e o resto rodam numa thread, com progresso e cancelamento; a tela vai olhando o estado a cada quadro (ar_op_estado) e responde se a thread
 * parou numa pergunta (já existe um arquivo com esse nome). Nada é sobrescrito sem a pessoa dizer. Cada operação que termina deixa o que precisa para
 * desfazer (ar_desfazer): mover volta, lixeira restaura, cópia vai para a lixeira, criar/compactar/extrair também.
 *
 * A lixeira é a do freedesktop (~/.local/share/Trash/files e info): o que apagamos aqui o Thunar e os outros também enxergam, e vice-versa.
 */
#ifndef LUMNA_ARQUIVOS_OP_H
#define LUMNA_ARQUIVOS_OP_H

#include <stdbool.h>
#include <stdint.h>

enum ar_op_tipo { AR_OP_COPIAR, AR_OP_MOVER, AR_OP_LIXEIRA, AR_OP_APAGAR, AR_OP_COMPACTAR, AR_OP_EXTRAIR, AR_OP_RESTAURAR };
enum ar_conflito { AR_C_PERGUNTAR, AR_C_SUBSTITUIR, AR_C_MANTER_AMBOS, AR_C_PULAR };

typedef struct ar_op ar_op;

typedef struct ar_op_estado {
	bool terminou;
	bool cancelado;
	int erros;
	char erro[256];          /* o primeiro erro, se houve */
	int64_t feito, total;    /* bytes */
	int itens_feitos, itens_total;
	char atual[256];         /* o que está sendo feito agora */
	char descricao[96];      /* "Copiando 3 itens" */
	bool pergunta;           /* parou: já existe um item com este nome (ar_op_responder) */
	char pergunta_nome[256];
	bool pergunta_pasta_destino; /* o item que já existe é uma pasta */
	char criado[4096];       /* ao terminar: o primeiro item criado (para selecionar na tela) */
} ar_op_estado;

/*
 * Começa uma operação. origens: caminhos completos; destino: a pasta de destino (copiar, mover, extrair, compactar: onde fica o arquivo novo; ignorada nas outras).
 * acordar(dados) é chamado da thread quando o estado muda (a tela pede um quadro). NULL se não deu.
 */
ar_op *ar_op_iniciar(enum ar_op_tipo tipo, char *const *origens, int n, const char *destino, void (*acordar)(void *), void *dados);
void ar_op_estado_ler(ar_op *op, ar_op_estado *saida);
void ar_op_responder(ar_op *op, enum ar_conflito escolha, bool para_todos);
void ar_op_cancelar(ar_op *op);
void ar_op_liberar(ar_op *op);   /* depois de terminar */

/* ---- coisas pequenas, na hora ---- */
/* Renomeia (só o nome, dentro da mesma pasta). Falso com o motivo em *erro (g_free). O novo caminho em *novo (g_free). */
bool ar_renomear(const char *caminho, const char *novo_nome, char **novo, char **erro);
/* Cria uma pasta ou um arquivo vazio com este nome; se já existe, nome (2). */
bool ar_criar(const char *pasta, const char *nome, bool eh_pasta, char **criado, char **erro);
/* "nome.ext" → "nome (2).ext" até não existir em pasta. g_free. */
char *ar_nome_livre(const char *pasta, const char *nome);
/* O nome é aceitável para um arquivo (não vazio, sem '/', nem "." e ".."). */
bool ar_nome_valido(const char *nome, char **motivo);

/* ---- desfazer ---- */
/* Há o que desfazer? descricao: "Mover para a lixeira (2 itens)". */
bool ar_desfazer_ha(char *descricao, int tam);
/* Desfaz a última operação (falso com o motivo em *erro). */
bool ar_desfazer(char **erro);
void ar_desfazer_limpar(void);

/* ---- lixeira ---- */
char *ar_lixeira_pasta(void);                       /* ~/.local/share/Trash/files (g_free) */
/* O caminho de onde o item da lixeira veio (g_free); NULL se não se sabe. nome é o da pasta Trash/files. */
char *ar_lixeira_origem(const char *nome);
bool ar_lixeira_restaurar(const char *nome, char **destino, char **erro);
bool ar_lixeira_esvaziar(char **erro);

/* ---- a área de transferência de arquivos (copiar/recortar/colar entre janelas) ----
 * Um arquivo em $XDG_RUNTIME_DIR guarda se foi copiar ou recortar e os caminhos; o texto da área de transferência do Wayland leva os mesmos caminhos
 * (para colar num terminal). Só vale se o texto da área ainda for o que gravamos (copiou outra coisa depois: não vale). */
char *ar_area_gravar(bool recortar, char *const *caminhos, int n);   /* devolve o texto para pôr na área de transferência (g_free) */
/* Lê o que há para colar a partir do texto da área de transferência. Devolve os caminhos (g_strfreev) ou NULL; *recortar diz se era recortar. */
char **ar_area_ler(const char *texto_da_area, bool *recortar);

#endif
