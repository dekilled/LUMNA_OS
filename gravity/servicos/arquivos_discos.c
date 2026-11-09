#include "arquivos_discos.h"

#include <glib.h>
#include <string.h>

int ar_discos_para_montar(struct disco *v, int max) {
	struct armazenamento a;
	armazenamento_ler_discos(&a);
	int n = 0;
	for (int i = 0; i < a.ndiscos && n < max; i++) {
		if (!a.discos[i].montado && !a.discos[i].do_sistema) {
			v[n++] = a.discos[i];
		}
	}
	return n;
}

char *ar_disco_montar(const struct disco *d, char *erro, size_t tam) {
	if (armazenamento_montar(d, erro, tam) != 0) {
		return NULL;
	}
	/* o udisks diz onde montou, mas o lsblk é a fonte que o resto usa: relê */
	struct armazenamento a;
	armazenamento_ler_discos(&a);
	for (int i = 0; i < a.ndiscos; i++) {
		if (!strcmp(a.discos[i].dev, d->dev) && a.discos[i].montado && a.discos[i].ponto[0]) {
			return g_strdup(a.discos[i].ponto);
		}
	}
	g_strlcpy(erro, "Montou, mas não achei onde.", tam);
	return NULL;
}

int ar_disco_do_ponto(const char *ponto, struct disco *saida) {
	struct armazenamento a;
	armazenamento_ler_discos(&a);
	for (int i = 0; i < a.ndiscos; i++) {
		if (a.discos[i].montado && !a.discos[i].do_sistema && !strcmp(a.discos[i].ponto, ponto)) {
			*saida = a.discos[i];
			return 1;
		}
	}
	return 0;
}
