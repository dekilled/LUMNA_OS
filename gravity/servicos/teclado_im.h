/*
 * A ligação do teclado na tela com o compositor, sem desenho nenhum:
 *   - o método de entrada (zwp_input_method_v2): o Orbit avisa quando um campo de texto ganha o foco (e o que há escrito à volta, se é
 *     senha, número, e-mail...); o que a pessoa toca volta como texto pronto (commit_string) ou como apagar (delete_surrounding_text);
 *   - o teclado virtual (zwp_virtual_keyboard_v1): Enter, Tab, Esc, setas e o apagar de apps que não falam text-input, e as letras
 *     quando não há campo nenhum aberto (um terminal).
 * Tem a própria ligação ao Wayland (um fd para a app vigiar); não depende do Gravity.
 */
#ifndef LUMNA_TECLADO_IM_H
#define LUMNA_TECLADO_IM_H

#include <stdbool.h>
#include <stdint.h>

/* zwp_input_method_context: o propósito do campo (como no protocolo) */
enum ti_proposito {
	TI_NORMAL, TI_ALFA, TI_DIGITOS, TI_NUMERO, TI_TELEFONE, TI_URL, TI_EMAIL, TI_NOME, TI_SENHA, TI_PIN, TI_DATA, TI_HORA, TI_DATAHORA, TI_TERMINAL,
};
/* as dicas (bits) */
enum { TI_DICA_CAPITALIZAR = 0x4, TI_DICA_ESCONDIDO = 0x40, TI_DICA_SENSIVEL = 0x80, TI_DICA_MULTILINHA = 0x200 };

typedef struct ti_campo {
	bool ativo;               /* há um campo de texto com o foco */
	uint32_t proposito, dica;
	char antes[256];          /* o texto antes do cursor (as últimas letras), para decidir a maiúscula */
	int cursor;
} ti_campo;

typedef struct ti_ouvinte {
	void (*campo)(void *dados, const ti_campo *campo); /* o campo mudou (ativou, desativou, mudou o texto à volta) */
	void (*indisponivel)(void *dados);                 /* já há outro método de entrada: este não serve */
} ti_ouvinte;

typedef struct ti_cliente ti_cliente;

/* Liga ao compositor. layout/variante: o mapa do teclado virtual (NULL: us). NULL se não deu (sem Wayland, sem o protocolo). */
ti_cliente *ti_abrir(const ti_ouvinte *ouvinte, void *dados, const char *layout, const char *variante);
void ti_fechar(ti_cliente *c);
int ti_fd(ti_cliente *c);
/* Lê e trata o que chegou. Falso se a ligação caiu. */
bool ti_despachar(ti_cliente *c);
const ti_campo *ti_estado(ti_cliente *c);
bool ti_metodo_disponivel(ti_cliente *c);

/* Escreve o texto no campo (commit_string); sem campo, tenta por teclas (letras e símbolos do mapa). Falso se não deu. */
bool ti_texto(ti_cliente *c, const char *utf8);
/* Apaga um caractere antes do cursor (pelo campo, ou pela tecla Backspace se não há campo). */
void ti_apagar(ti_cliente *c);
/* Teclas (códigos do evdev: linux/input-event-codes.h): apertar e soltar. */
void ti_tecla(ti_cliente *c, uint32_t evdev);
enum { TI_KEY_ESC = 1, TI_KEY_BACKSPACE = 14, TI_KEY_TAB = 15, TI_KEY_ENTER = 28, TI_KEY_UP = 103, TI_KEY_LEFT = 105, TI_KEY_RIGHT = 106, TI_KEY_DOWN = 108 };

#endif
