/*
 * Drivers e kernels da Loja, sem interface: detecta o hardware (placa de vídeo, processador, kernel em uso, gerenciador de boot, espaço em /boot) e diz,
 * para cada driver e kernel, se serve a ESTE computador e por quê. A Loja nunca instala o que a checagem recusa, a não ser que a pessoa ligue o modo
 * avançado. Depois de instalar um kernel, confere o que o pacman deixou em /boot e se o gerenciador de boot o conhece (e AVISA: não mexe no boot).
 *
 * As regras são da LUMNA (tabela de famílias da NVIDIA, par kernel/driver, pacotes que existem nos repositórios) e podem envelhecer: cada resultado traz o
 * motivo, em português, para a pessoa conferir. Em dúvida o resultado é "verifique", nunca "compatível".
 *
 * Para testar: um `lspci` falso no PATH, e $LUMNA_CPUINFO, $LUMNA_UNAME_R, $LUMNA_BOOT (no lugar de /boot), $LUMNA_BOOT_LIVRE (bytes livres em /boot), $LUMNA_EFI (uma pasta que existe ou não, no lugar de /sys/firmware/efi).
 */
#ifndef LUMNA_LOJA_HW_H
#define LUMNA_LOJA_HW_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

#include "loja.h"

typedef struct lh_gpu {
	char vendor[12];        /* "nvidia", "amd", "intel" ou "outro" */
	char nome[128];         /* "GeForce RTX 3060" */
	char pci[12];           /* "10de:2503" */
	int geracao;            /* NVIDIA: 0 não sei, 1 antes da Maxwell, 2 Maxwell/Pascal/Volta, 3 Turing ou mais nova */
} lh_gpu;

typedef struct lh_hw {
	lh_gpu gpus[4];
	int ngpu;
	char cpu[128];
	char cpu_vendor[12];    /* "amd", "intel" ou "outro" */
	char arch[16];          /* x86_64 */
	char kernel_uname[64];  /* 6.10.8-arch1-1 */
	char kernel_pkg[32];    /* linux, linux-lts, linux-zen, linux-hardened; "" se não reconheci */
	char boot_nome[24];     /* "systemd-boot", "grub" ou "" */
	bool uefi;
	int64_t boot_livre;     /* bytes livres em /boot (-1: não sei) */
	int64_t ram;            /* bytes */
} lh_hw;

/* lê o hardware (rápido, mas roda lspci: chame numa thread) */
lh_hw *lh_detectar(void);
void lh_liberar(lh_hw *h);

enum lh_compat { LH_COMPATIVEL, LH_VERIFIQUE, LH_INCOMPATIVEL };
enum lh_grupo { LH_VIDEO, LH_CPU, LH_KERNEL };

typedef struct lh_item {
	char pacote[64];
	char exibicao[64];
	char descricao[200];
	enum lh_grupo grupo;
	enum lh_compat compat;
	char motivo[320];        /* por que é compatível, ou não, ou o que conferir */
	bool instalado, em_uso;  /* em_uso: o kernel em que o sistema está agora */
	bool nao_aplica;         /* não é do hardware daqui (ex.: NVIDIA numa máquina AMD): some com "só compatíveis" */
	char junto[64];          /* instalado na mesma transação (os headers do kernel); "" se não há */
	char repo[16];
} lh_item;

/* os drivers e kernels, com a checagem (até max; devolve quantos) */
int lh_itens(const lh_hw *h, const lj_catalogo *c, lh_item *saida, int max);

/* a pessoa pode instalar? Compatível e verifique sim; incompatível só no modo avançado. Remover o kernel em uso (ou o único) nunca. */
bool lh_pode_instalar(const lh_item *it, bool avancado);
bool lh_pode_remover(const lh_item *it, const lh_hw *h, const lj_catalogo *c, char *motivo, size_t tam);

/* depois de instalar um kernel: o que conferir em /boot e no gerenciador de boot */
typedef struct lh_checagem {
	bool ok;
	char texto[256];
} lh_checagem;
int lh_verificar_kernel(const lh_hw *h, const char *pacote, lh_checagem *saida, int max);

const char *lh_nome_compat(enum lh_compat c);

#endif
