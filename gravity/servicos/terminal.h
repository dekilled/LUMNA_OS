/*
 * terminal: um terminal (a libvterm) ligado a um processo (um shell) por um pseudo-terminal. É só a parte do terminal, sem Gravity: o app
 * (exemplos/pulse.c) lê a grade daqui, desenha, e manda as teclas e o mouse para cá. Dá para testar sem tela (testes/painel/testa_terminal.c).
 *
 * As linhas são contadas assim: 0 .. linhas-1 é a tela de agora; -1, -2... é o histórico que rolou para cima (-1 é a linha logo acima da tela).
 */
#ifndef LUMNA_TERMINAL_H
#define LUMNA_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct terminal terminal;

/* os modificadores (o mesmo valor que o app monta a partir do Gravity) */
enum { TERM_SHIFT = 1, TERM_CTRL = 2, TERM_ALT = 4 };

struct term_celula {
	char texto[32];       /* o que a célula mostra, em UTF-8 ("" numa célula vazia; vazio também na segunda metade de um caractere largo) */
	int largura;          /* 1, ou 2 num caractere largo (CJK, emoji): a célula da direita fica sem texto */
	uint32_t fg, bg;      /* 0xRRGGBB, já resolvidos (o "inverso" já trocou as duas) */
	bool bg_padrao;       /* o fundo é o do terminal (não precisa pintar) */
	bool negrito, italico, sublinhado, riscado;
};

/* Abre um terminal colunas × linhas e roda o comando nele (NULL: o $SHELL, ou /bin/bash), na pasta cwd (NULL: a pessoa em casa).
 * O comando, quando dado, roda por "sh -c". */
terminal *terminal_abrir(int colunas, int linhas, const char *cwd, const char *comando);
void terminal_fechar(terminal *t);

/* o descritor do pty: o app vigia a leitura dele */
int terminal_fd(const terminal *t);
/* Lê o que o processo escreveu e leva à grade. Falso quando o processo acabou (e não há mais o que ler). */
bool terminal_ler(terminal *t);
/* Manda o que ficou na fila de saída (um colar grande); verdadeiro enquanto ainda sobra. */
bool terminal_enviar(terminal *t);
bool terminal_vivo(const terminal *t);
int terminal_status(const terminal *t); /* o código de saída do processo (depois de acabar) */
pid_t terminal_pid(const terminal *t);
/* a pasta onde o shell está agora (/proc/PID/cwd): uma aba nova abre aí. Vale até a próxima chamada; "" se não souber */
const char *terminal_pasta(terminal *t);

void terminal_redimensionar(terminal *t, int colunas, int linhas);
int terminal_colunas(const terminal *t);
int terminal_linhas(const terminal *t);
/* quantas linhas de histórico há (0 na tela alternativa de um editor, de um paginador) */
int terminal_historico(const terminal *t);
/* aumenta a cada mudança na grade: o app só redesenha quando muda */
uint64_t terminal_versao(const terminal *t);

/* As cores: o texto, o fundo e as 16 do ANSI (0xRRGGBB). Chame logo depois de abrir, e de novo se o tema mudar. */
void terminal_cores(terminal *t, uint32_t fg, uint32_t bg, const uint32_t ansi[16]);
bool terminal_celula(terminal *t, int linha, int coluna, struct term_celula *saida);
/* o cursor na tela (linha 0..linhas-1); visivel é falso quando o programa o escondeu */
void terminal_cursor(const terminal *t, int *linha, int *coluna, bool *visivel);
/* o título que o programa pôs (OSD 0/2: o vim, o ssh); "" se não pôs nenhum */
const char *terminal_titulo(const terminal *t);
bool terminal_tela_alternativa(const terminal *t);
/* tocou a campainha desde a última chamada */
bool terminal_campainha(terminal *t);
/* o terminal está com o foco do teclado (alguns programas pedem para ser avisados) */
void terminal_foco(terminal *t, bool com_foco);

/* Uma tecla: o keysym do xkbcommon, o texto que ela escreve (UTF-8, já composto; "" nas que não escrevem) e os modificadores. */
void terminal_tecla(terminal *t, uint32_t keysym, const char *utf8, uint32_t mods);
/* Cola um texto (com "bracketed paste" quando o programa pediu). */
void terminal_colar(terminal *t, const char *texto);

/* O programa pediu o mouse (vim, htop, mc)? Então o app manda os cliques para ele em vez de selecionar. */
bool terminal_mouse_ativo(const terminal *t);
/* botao: 1 esquerdo, 2 do meio, 3 direito, 4 roda para cima, 5 roda para baixo */
void terminal_mouse_botao(terminal *t, int linha, int coluna, int botao, bool apertou, uint32_t mods);
void terminal_mouse_mover(terminal *t, int linha, int coluna, uint32_t mods);

/* O texto entre dois pontos da grade (inclusive), com as linhas separadas por \n e sem os espaços do fim de cada uma. Liberar com free. */
char *terminal_texto(terminal *t, int linha1, int coluna1, int linha2, int coluna2);
/* Os limites da palavra em (linha, coluna): para o clique duplo. */
void terminal_palavra(terminal *t, int linha, int coluna, int *ini, int *fim);

/* ---- a integração com o shell (o Pulse) ----
 * O bash (o integracao.bash que o Pulse põe a rodar no primeiro prompt) manda marcas no fluxo: OSC 133 (A: o prompt começa, B: onde se digita, C: o
 * comando começou a rodar, D;N: acabou com o código N) e o OSC 7770 com o comando e a pasta (em base64). Com elas o terminal sabe, sem adivinhar,
 * quando um comando acaba, com que código, o que ele escreveu e o que está sendo digitado. Sem a integração (outro shell), nada disto aparece. */

enum term_evento_tipo {
	TERM_EV_PROMPT,         /* o prompt apareceu (A) */
	TERM_EV_COMANDO_INICIO, /* a pessoa deu Enter e o comando começou (C) */
	TERM_EV_COMANDO_FIM,    /* o comando acabou (D): comando, pasta, exit_code e saida */
};

struct term_evento {
	enum term_evento_tipo tipo;
	int exit_code;
	char *comando; /* g_free; só no FIM, e NULL quando a linha não entrou no histórico do shell (começou com espaço, repetida, vazia) */
	char *pasta;   /* a pasta onde o shell estava ao acabar (g_free) */
	char *saida;   /* as últimas linhas que o comando escreveu, sem as cores (g_free), no FIM */
};

/* o próximo evento da fila (false se não há); libere com terminal_evento_limpar */
bool terminal_evento(terminal *t, struct term_evento *e);
void terminal_evento_limpar(struct term_evento *e);
/* o shell mandou marcas (a integração está ligada nesta aba) */
bool terminal_integrado(const terminal *t);
/* o que está digitado agora na linha do prompt (do ponto B até o fim do que se escreveu), como o shell o vê; NULL se não há prompt a esperar ou sem integração (g_free) */
char *terminal_linha_digitada(terminal *t);
/* quantas linhas finais da saída de um comando vão no evento (o padrão é 40) */
void terminal_saida_linhas(terminal *t, int linhas);

#endif
