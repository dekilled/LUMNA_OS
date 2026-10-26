/*
 * A rede do LUMNA, pelo NetworkManager (D-Bus do sistema). Uma linha de fundo pergunta de 2 em 2
 * segundos (e logo depois de cada comando) e guarda o estado; quem desenha só copia (rede_estado)
 * e manda comandos, que nunca bloqueiam a interface. O mesmo serviço serve o Painel e a topbar.
 *
 * Para testar sem NetworkManager de verdade: testes/rede-falsa.sh (python-dbusmock).
 * $LUMNA_SYSFS_NET aponta para outra /sys/class/net (os adaptadores de mentira dos testes).
 */
#ifndef LUMNA_REDE_H
#define LUMNA_REDE_H

#include <stdbool.h>
#include <stdint.h>

#define REDE_MAX_DISP 8
#define REDE_MAX_AP 64
#define REDE_MAX_SALVAS 64

enum rede_tipo { REDE_OUTRO, REDE_WIFI, REDE_CABO };
/* o NM_DEVICE_STATE_*: 20 indisponível, 30 desconectado, 40-90 conectando, 100 ligado */
enum rede_estado_disp { REDE_INDISPONIVEL = 20, REDE_DESCONECTADO = 30, REDE_CONECTANDO = 50, REDE_LIGADO = 100 };

struct rede_disp {
	char caminho[96];
	char iface[32];
	enum rede_tipo tipo;
	int estado;
	bool gerenciado;     /* false: o adaptador está desativado para o LUMNA */
	char driver[32];
	char barramento[8];  /* "PCI" (onboard), "USB" (antena/adaptador) ou "" */
	char modelo[112];    /* "Realtek RTL8821CU" */
	char mac[24];
	char ip[48];
	char gateway[48];
	char dns[96];
	char conexao[64];    /* o nome da conexão ligada (ou "") */
	char conexao_uuid[40];
	int metrica;         /* a métrica da rota (menor ganha), 0 se não houver */
	uint32_t velocidade; /* Mb/s do cabo; 0 se não souber */
};

struct rede_ap {
	char caminho[96];
	char disp[96];       /* o adaptador que a vê */
	char ssid[40];
	int sinal;           /* 0 a 100 */
	bool segura;
	bool atual;          /* é a ligada */
	bool salva;          /* já há uma conexão guardada com este nome */
};

struct rede_salva {
	char uuid[40];
	char nome[64];
	enum rede_tipo tipo;
	char ssid[40];
	int64_t ultimo_uso;  /* o instante (s) da última vez que ligou; 0: nunca */
	bool ativa;
	char dns[96];        /* os DNS fixos ("1.1.1.1, 1.0.0.1"); vazio: os da rede */
	int metrica;         /* a métrica da rota IPv4, -1: automática */
	bool automatica;     /* liga sozinha quando está ao alcance */
};

struct rede_estado {
	bool nm;             /* o NetworkManager respondeu */
	bool wifi_ligado;    /* o rádio Wi-Fi geral */
	bool internet;       /* a verificação de conectividade diz que há internet */
	struct rede_disp disp[REDE_MAX_DISP];
	int ndisp;
	struct rede_ap aps[REDE_MAX_AP];
	int naps;
	struct rede_salva salvas[REDE_MAX_SALVAS];
	int nsalvas;
	char erro[160];      /* o último erro de um comando (vazio: nenhum) */
	uint32_t geracao;    /* sobe a cada mudança */
};

/* Liga o serviço; devolve o fd que acorda quando o estado muda (para gv_app_vigiar), -1 se falhar. */
int rede_iniciar(void);
void rede_estado_copiar(struct rede_estado *dest);
void rede_erro_limpar(void);

/* ---- comandos (assíncronos; o resultado vem no estado) ---- */

void rede_varrer(const char *disp);
void rede_wifi_ligar(bool ligar);
/* ligar um adaptador ou desativá-lo (o NM deixa de gerenciar) */
void rede_disp_ativar(const char *disp, bool ativar);
/* conecta a uma rede à vista; senha NULL ou "" para aberta ou já guardada */
void rede_conectar(const char *disp, const char *ap, const char *senha);
void rede_conectar_salva(const char *uuid, const char *disp);
void rede_desconectar(const char *disp);
void rede_esquecer(const char *uuid);
/* seguranca: 0 aberta, 1 WPA/WPA2, 2 WPA3 */
void rede_adicionar(const char *disp, const char *ssid, int seguranca, const char *senha, bool oculta);
/* DNS fixos ("" volta aos da rede) e a métrica (-1 automática) de uma conexão guardada */
void rede_conexao_ajustar(const char *uuid, const char *dns, int metrica, int automatica /* -1 não mexe */);

/* ---- o texto ---- */

/* "Onboard (PCI)" ou "Antena USB" */
const char *rede_barramento_nome(const char *barramento);
const char *rede_estado_nome(int estado);

#endif
