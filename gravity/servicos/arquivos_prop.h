/*
 * Propriedades de um arquivo ou pasta (a janela do Atlas): os níveis de acesso que a pessoa escolhe (Sem acesso, Ler e acessar...) e os bits do
 * chmod, os grupos a que ela pertence, aplicar permissões (uma pasta com tudo o que há dentro) e as informações de um atalho .desktop.
 * Sem desenho nenhum, testado em testes/atlas/testa_prop.c.
 */
#ifndef LUMNA_ARQUIVOS_PROP_H
#define LUMNA_ARQUIVOS_PROP_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* ---- níveis de acesso de um trio (dono, grupo ou outros): rwx = 4r 2w 1x ---- */
enum ar_nivel_pasta { AR_NP_NENHUM, AR_NP_ACESSAR, AR_NP_LER_ACESSAR, AR_NP_TUDO, AR_NP_OUTRO /* uma combinação que não tem nome (r--, rw-, -w-...) */ };
enum ar_nivel_arq { AR_NA_NENHUM, AR_NA_LER, AR_NA_LER_GRAVAR, AR_NA_OUTRO };
extern const char *const AR_NIVEIS_PASTA[4]; /* "Sem acesso", "Somente acessar", "Ler e acessar", "Ler, gravar e acessar" */
extern const char *const AR_NIVEIS_ARQ[3];   /* "Sem acesso", "Somente leitura", "Ler e gravar" */
int ar_nivel_pasta_de(int rwx);
int ar_rwx_da_pasta(int nivel);
int ar_nivel_arq_de(int rwx); /* só r e w contam; o x é do "executável" */
int ar_rwx_do_arq(int nivel);

/* ---- grupos ---- */
typedef struct ar_grupo {
	gid_t gid;
	char nome[64];
} ar_grupo;
/* os grupos da pessoa (o principal e os outros) mais o `atual` do arquivo, se não estiver entre eles; devolve quantos */
int ar_grupos_da_pessoa(ar_grupo *g, int max, gid_t atual);

/* ---- aplicar ---- */
typedef struct ar_prop_resultado {
	int64_t alterados;
	int erros;
} ar_prop_resultado;
/* chmod(modo) e, se trocar_grupo, chgrp(gid). recursivo (numa pasta): as pastas de dentro recebem o mesmo modo; os arquivos, só os bits r e w dele
 * (o x de cada um fica como estava). Links simbólicos não são seguidos. */
bool ar_prop_aplicar(const char *caminho, mode_t modo, bool trocar_grupo, gid_t gid, bool recursivo, ar_prop_resultado *r);

/* ---- um atalho de aplicativo (.desktop) ---- */
typedef struct ar_prop_app {
	char nome[128], comentario[256], icone[256], exec[512], versao[64], pacote[128], origem[64];
	char executavel[512]; /* o programa do Exec= achado no PATH ("" se não achou) */
} ar_prop_app;
bool ar_prop_desktop(const char *caminho, ar_prop_app *a);

#endif
