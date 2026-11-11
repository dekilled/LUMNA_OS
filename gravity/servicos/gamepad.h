/*
 * Os controles (gamepads) do modo Gaming: acha os que o kernel mostra em /dev/input (USB e Bluetooth), lê os botões (evdev) e os
 * traduz para a navegação (A, B, setas, LB, RB, Guia...), guarda as configurações e o apelido de cada um, e faz o controle vibrar. Nada aqui
 * captura o controle (EVIOCGRAB): o jogo continua lendo o dele. Quem usa é o serviço lumna-gamepad (exemplos/gamepad.c).
 *
 * Configurações em ~/.config/lumna/gamepad.ini:
 *   [geral]   zona_morta=15 (% do curso do analógico)  vibracao=sim|nao  trocar_ab=sim|nao (estilo Nintendo)  guia=biblioteca|inicio|emma|nada
 *   [gaming]  energia=performance|balanced|power-saver|nao  fps=sim|nao (MangoHud nos jogos que o Gaming abre)
 *   [apelidos] ID=Nome        [ignorados] ID=sim
 *
 * Para testar sem controle de verdade: $LUMNA_GAMEPAD_FALSOS aponta para uma pasta com "NOME.dev" (ini: nome, uniq, bus=usb|bluetooth,
 * vendor, product, rumble=sim|nao, bateria=N) e "NOME.ev" (um FIFO que o teste enche de `struct input_event`); $LUMNA_POWER_DIR, uma /sys/class/power_supply falsa.
 */
#ifndef LUMNA_GAMEPAD_H
#define LUMNA_GAMEPAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GP_MAX 8

enum gp_botao { GP_A, GP_B, GP_X, GP_Y, GP_LB, GP_RB, GP_LT, GP_RT, GP_SELECT, GP_START, GP_GUIA, GP_CIMA, GP_BAIXO, GP_ESQ, GP_DIR, GP_N };

struct gp_cfg {
	int zona_morta;          /* 0 a 50 (%) */
	bool vibracao;
	bool trocar_ab;
	char guia[16];
	char energia[16];
	bool fps;
};

struct gp_disp {
	char id[64];             /* o uniq (o endereço Bluetooth) ou o caminho físico: o que não muda entre conexões */
	char nome[96];
	char caminho[256];       /* /dev/input/eventN (ou o .ev falso) */
	bool bluetooth;
	int vendor, product;
	bool rumble;
	int bateria;             /* 0 a 100; -1 se não informa */
	char apelido[48];
	bool ignorado;
	int fd;                  /* aberto para leitura; -1 fechado */
	/* o estado da tradução */
	int eixo[2], eixo_min[2], eixo_max[2];
	bool dir[4];             /* cima, baixo, esquerda, direita, pelo analógico ou pelo direcional */
	bool hat[4];
	bool gatilho[2];
	int efeito;              /* o id do efeito de vibração (-1: ainda não enviado) */
};

struct gp_evento {
	int botao;               /* enum gp_botao */
	bool pressionado;
};

void gp_cfg_padrao(struct gp_cfg *c);
void gp_cfg_ler(struct gp_cfg *c);
bool gp_cfg_gravar(const struct gp_cfg *c);
char *gp_cfg_caminho(void);                          /* g_free */
void gp_apelidos_ler(struct gp_disp *lista, int n);  /* preenche apelido e ignorado de cada um */
bool gp_apelido_gravar(const char *id, const char *apelido);
bool gp_ignorar_gravar(const char *id, bool ignorar);

const char *gp_botao_nome(int b);                    /* "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "SELECT", "START", "GUIA", "CIMA"... */

/* procura os controles (fecha os que sumiram, abre os novos); devolve quantos. `lista` guarda o que já se conhecia (mantém os fd abertos). */
int gp_varrer(struct gp_disp *lista, int n, int max);
/* a bateria de um controle (-1 se não informa) */
int gp_bateria(const struct gp_disp *d);
/* lê o que há no fd do controle e chama emite para cada botão que mudou (já com o trocar_ab e a zona morta); devolve false se o controle sumiu */
bool gp_ler(struct gp_disp *d, const struct gp_cfg *c, void (*emite)(const struct gp_disp *, const struct gp_evento *, void *), void *dados);
/* um tranco curto (ms); false se o controle não vibra */
bool gp_vibrar(struct gp_disp *d, int ms);
void gp_fechar(struct gp_disp *d);

#endif
