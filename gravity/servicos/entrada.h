/*
 * O teclado e o mouse, para o Painel (Teclado e mouse): o que se grava em ~/.config/lumna/entrada.ini, que o
 * Orbit vigia e aplica na hora, e o que o sistema oferece (os layouts do XKB, o que está ligado).
 *
 *   [teclado]  layout=br,us  variant=abnt2,  options=grp:alt_shift_toggle,caps:escape
 *              repeticao_taxa=25  repeticao_atraso=600
 *   [mouse]    botao_primario=esquerdo|direito  velocidade=-1..1  perfil=adaptativo|plano  rolagem_natural=0|1
 *   [touchpad] tocar_para_clicar=0|1  rolagem_natural=0|1  botao_primario=...  velocidade=...  perfil=...  digitar_desativa=0|1
 *
 * Para testar: $XDG_CONFIG_HOME e $LUMNA_XKB_LST (no lugar de /usr/share/X11/xkb/rules/evdev.lst).
 */
#ifndef LUMNA_ENTRADA_H
#define LUMNA_ENTRADA_H

#include <stdbool.h>
#include <stddef.h>

#define ENTRADA_MAX_LAYOUTS 4

struct entrada_cfg {
	/* uma lista por vírgula: "br,us" com "abnt2," (a variante vazia é o layout puro) */
	char layout[128], variante[128], opcoes[256];
	int repeticao_taxa;     /* teclas por segundo */
	int repeticao_atraso;   /* ms até começar a repetir */
	struct {
		bool primario_direito; /* os botões trocados (canhoto) */
		double velocidade;     /* -1 a 1 */
		bool plano;            /* sem aceleração */
		bool natural;
	} mouse;
	struct {
		bool tocar, natural, primario_direito, digitar_desativa;
		double velocidade;
		bool plano;
	} touchpad;
};

void entrada_cfg_padrao(struct entrada_cfg *c);
bool entrada_cfg_ler(struct entrada_cfg *c);   /* false se não há arquivo (c fica com o padrão) */
bool entrada_cfg_gravar(const struct entrada_cfg *c);

/* os layouts e as variantes que o XKB conhece, em ordem de nome */
struct entrada_item {
	char id[40];
	char nome[96];
};
int entrada_layouts(struct entrada_item *v, int max);
int entrada_variantes(const char *layout, struct entrada_item *v, int max);

/* o que está ligado agora, pelo Orbit */
struct entrada_dispositivos {
	bool orbit;
	bool teclado, mouse, touchpad;
	bool ponteiro_ajustavel; /* os ajustes de ponteiro chegam ao aparelho (libinput) */
	char mapa[64];           /* o nome do mapa que o Orbit usa agora */
};
bool entrada_dispositivos(struct entrada_dispositivos *d);

#endif
