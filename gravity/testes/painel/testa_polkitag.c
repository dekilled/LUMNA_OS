/* as partes puras do agente do polkit (servicos/polkitag.c) */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "polkitag.h"

static int falhas;
#define QUER(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

int main(int argc, char **argv) {
	uint32_t uid = 99;
	uint32_t dois[] = { 0, 1000 };
	QUER(polkitag_escolher(dois, 2, 1000, &uid) && uid == 1000, "a própria pessoa, se o polkit aceita");
	uint32_t so_root[] = { 0 };
	QUER(polkitag_escolher(so_root, 1, 1000, &uid) && uid == 0, "senão, a primeira oferecida");
	QUER(!polkitag_escolher(NULL, 0, 1000, &uid), "sem contas: nenhuma");
	char *a = polkitag_desescapar("Senha:\\t");
	QUER(!strcmp(a, "Senha:\t"), "\\t");
	g_free(a);
	a = polkitag_desescapar("a\\\\b \\\"c\\\" l1\\nl2");
	QUER(!strcmp(a, "a\\b \"c\" l1\nl2"), "\\\\, \\\" e \\n");
	g_free(a);
	char *dir = g_dir_make_tmp("lumna-polkitag-XXXXXX", NULL);
	char *passwd = g_build_filename(dir, "passwd", NULL);
	g_file_set_contents(passwd, "root:x:0:0::/root:/bin/sh\nmaria:x:1000:1000:Maria:/home/maria:/bin/sh\n", -1, NULL);
	g_setenv("LUMNA_ETC", dir, TRUE);
	char *n = polkitag_nome(1000);
	QUER(n && !strcmp(n, "maria"), "o nome pelo uid");
	g_free(n);
	QUER(!polkitag_nome(4242), "uid desconhecido");
	/* um helper que não existe: falha, com o motivo, sem travar */
	g_setenv("LUMNA_POLKIT_HELPER", "/nao/existe/helper", TRUE);
	char erro[200] = "";
	QUER(polkitag_conferir("maria", "c", "x", erro, sizeof(erro)) == POLKITAG_FALHA && erro[0], "helper inexistente é falha com motivo");
	if (!falhas) {
		puts("polkitag ok");
	}
	return falhas != 0;
}
