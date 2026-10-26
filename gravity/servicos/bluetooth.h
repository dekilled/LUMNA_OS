/*
 * O Bluetooth pelo BlueZ (D-Bus do sistema), no mesmo molde do servico da rede (rede.h): uma linha
 * de fundo guarda o estado, quem desenha copia e manda comandos. Regista um agente "sem entrada"
 * (parear sem digitar, o "Just Works"; PIN antigo vira 0000).
 */
#ifndef LUMNA_BLUETOOTH_H
#define LUMNA_BLUETOOTH_H

#include <stdbool.h>

#define BT_MAX_ADAPT 4
#define BT_MAX_DISP 64

struct bt_adapt {
	char caminho[64];
	char nome[64];      /* o nome que os outros veem (Alias) */
	char endereco[24];
	char barramento[8]; /* "PCI", "USB" ou "" */
	bool ligado, procurando, visivel;
};

struct bt_disp {
	char caminho[96];
	char adapt[64];
	char nome[64];
	char endereco[24];
	char icone[32];     /* o tipo, como o BlueZ diz: "audio-headset", "input-keyboard", "phone"... */
	bool pareado, conectado, confiavel;
	int sinal;          /* RSSI em dBm; 0 se não souber */
	int bateria;        /* 0 a 100; -1 se não informa */
};

struct bt_estado {
	bool bluez;         /* o BlueZ respondeu */
	struct bt_adapt adapt[BT_MAX_ADAPT];
	int nadapt;
	struct bt_disp disp[BT_MAX_DISP];
	int ndisp;
	char erro[160];
	unsigned geracao;
};

int bt_iniciar(void);
void bt_estado_copiar(struct bt_estado *dest);
void bt_erro_limpar(void);

void bt_ligar(const char *adapt, bool ligar);
void bt_procurar(const char *adapt, bool procurar);
void bt_visivel(const char *adapt, bool visivel);
void bt_renomear(const char *adapt, const char *nome);
void bt_parear(const char *disp);
void bt_conectar(const char *disp);
void bt_desconectar(const char *disp);
void bt_esquecer(const char *disp);
void bt_confiar(const char *disp, bool confiar);

/* o que o ícone do BlueZ quer dizer, em português */
const char *bt_tipo_nome(const char *icone);

#endif
