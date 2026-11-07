/*
 * O instalador do LUMNA (lumna-instalador): as partes que não são de tela. Lista os discos (lsblk), os fusos, confere o que a
 * pessoa digitou e roda o /usr/lib/lumna/lumna-install pelo pkexec (a senha chega pela entrada padrão, nunca na linha de
 * comando), lendo o andamento ("@@ PORCENTAGEM texto").
 *
 * Para os testes: LUMNA_INSTALADOR_CMD troca "pkexec /usr/lib/lumna/lumna-install" por outro programa (que recebe os mesmos
 * argumentos), LUMNA_ZONEINFO a pasta dos fusos.
 */
#ifndef LUMNA_INSTALADOR_H
#define LUMNA_INSTALADOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct inst_disco {
	char caminho[64], modelo[96], transporte[16], conteudo[160];
	int64_t tamanho;
};

struct inst_plano {
	char disco[64], usuario[40], nome[128], host[64], fuso[64], teclado[32], idioma[32];
};

/* os discos onde dá para instalar (sem o pendrive do live, zram, loop e CD): lsblk -J do próprio sistema, ou de um JSON dado */
int instalador_discos(struct inst_disco *lista, int max);
int instalador_discos_json(const char *json, struct inst_disco *lista, int max);

/* os fusos ("America/Sao_Paulo"), em ordem, sem repetir; sempre tem ao menos um. Cada um g_free + a lista g_free. */
int instalador_fusos(char ***lista);
bool instalador_fuso_existe(char *const *lista, int n, const char *fuso);

/* "Luan Éric" → "luan": sem acento, minúsculo, a primeira palavra */
void instalador_sugerir_usuario(const char *nome_completo, char *saida, size_t n);

/* o que está errado no que a pessoa digitou ("" se está certo ou ainda vazio) */
const char *instalador_erro_usuario(const char *usuario);
const char *instalador_erro_senha(const char *senha, const char *confirma);
const char *instalador_erro_host(const char *host);

/* a linha "@@ 42 Copiando" → 42 e "Copiando"; false se não é uma linha de andamento */
bool instalador_andamento(const char *linha, int *percentual, const char **texto);

/* a execução, numa linha à parte */
struct inst_estado {
	int percentual;
	char texto[160];
	bool rodando, terminou, ok;
	char erro[400];   /* quando terminou sem ok */
};

/* começa a instalar; devolve false se já há uma rodando. O fd (um eventfd) fica legível a cada mudança: vigie com gv_app_vigiar. */
bool instalador_iniciar(const struct inst_plano *plano, const char *senha);
void instalador_estado(struct inst_estado *e);
int instalador_fd(void);

#endif
