/* arquivos_discos: os discos "para montar", montar (devolve onde ficou) e achar o disco pelo ponto, com lsblk e udisksctl de mentira no PATH. */
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "arquivos_discos.h"

static int falhas;
#define CONFERE(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

int main(void) {
	struct disco v[ARMAZ_MAX_DISCOS];
	int n = ar_discos_para_montar(v, ARMAZ_MAX_DISCOS);
	/* o do sistema (/) e as outras partições do mesmo disco (o Windows de um dual boot) ficam de fora, como no Painel; o pendrive entra */
	CONFERE(n == 1, "só o pendrive para montar");
	int ip = -1;
	for (int i = 0; i < n; i++) {
		if (!strcmp(v[i].dev, "/dev/sdb1")) {
			ip = i;
		}
		CONFERE(strcmp(v[i].dev, "/dev/sda1"), "o disco do sistema não entra");
	}
	CONFERE(ip >= 0, "o pendrive está na lista");
	if (ip >= 0) {
		CONFERE(!strcmp(v[ip].nome, "PENDRIVE") && v[ip].removivel && !v[ip].montado, "nome, removível e não montado");
		char erro[300] = "";
		char *ponto = ar_disco_montar(&v[ip], erro, sizeof(erro));
		CONFERE(ponto != NULL, "montar devolve onde ficou");
		CONFERE(ponto && g_str_has_suffix(ponto, "/PENDRIVE"), "o ponto é o do lsblk");
		struct disco d;
		CONFERE(ponto && ar_disco_do_ponto(ponto, &d) == 1 && !strcmp(d.dev, "/dev/sdb1"), "achar o disco pelo ponto de montagem");
		CONFERE(ar_disco_do_ponto("/", &d) == 0, "o disco do sistema não se ejeta");
		g_free(ponto);
		CONFERE(ar_discos_para_montar(v, ARMAZ_MAX_DISCOS) == 0, "montado, sai da lista dos para montar");
	}
	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("discos do Atlas (montar e ejetar): ok\n");
	return 0;
}
