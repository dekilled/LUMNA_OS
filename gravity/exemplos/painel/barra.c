/* Aparência → Barra superior: o relógio e o que a barra mostra (~/.config/lumna/barra.ini, que a
 * barra vigia e aplica na hora). */
#include "painel.h"

#include <stdio.h>
#include <string.h>

struct barra_ajustes {
	bool segundos, data, areas;
};
static const struct barra_ajustes fabrica = { .areas = true };
static struct barra_ajustes b, gravada;

void pn_barra_abrir(void) {
	gravada = fabrica;
	char caminho[1024];
	pn_caminho(caminho, sizeof(caminho), "barra.ini");
	FILE *f = fopen(caminho, "r");
	if (f) {
		char linha[256];
		while (fgets(linha, sizeof(linha), f)) {
			char chave[64], valor[64];
			if (sscanf(linha, " %63[^= ] = %63s", chave, valor) != 2) {
				continue;
			}
			bool sim = !strcmp(valor, "sim");
			if (!strcmp(chave, "segundos")) {
				gravada.segundos = sim;
			} else if (!strcmp(chave, "data")) {
				gravada.data = sim;
			} else if (!strcmp(chave, "areas")) {
				gravada.areas = sim;
			}
		}
		fclose(f);
	}
	b = gravada;
}

bool pn_barra_pendente(void) {
	return memcmp(&b, &gravada, sizeof(b)) != 0;
}

void pn_barra_aplicar(void) {
	char texto[256];
	snprintf(texto, sizeof(texto), "# A barra superior (o Painel de controle grava este arquivo)\nsegundos = %s\ndata = %s\nareas = %s\n",
		b.segundos ? "sim" : "nao", b.data ? "sim" : "nao", b.areas ? "sim" : "nao");
	if (pn_gravar("barra.ini", texto)) {
		gravada = b;
	}
}

void pn_barra_descartar(void) {
	b = gravada;
}

void pn_barra_restaurar(void) {
	b = fabrica;
}

void pn_barra_desenhar(void) {
	pn_subtitulo(CLAY_ID("barra-s1"), "Relógio");
	pn_linha_comecar(CLAY_ID("barra-seg"), "clock", "Mostrar os segundos", NULL);
	gv_interruptor(CLAY_ID("barra-seg-v"), &b.segundos);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("barra-data"), "clock", "Mostrar a data", "Ao lado da hora (Dom, 25 de out)");
	gv_interruptor(CLAY_ID("barra-data-v"), &b.data);
	pn_linha_acabar();
	pn_subtitulo(CLAY_ID("barra-s2"), "Ao centro");
	pn_linha_comecar(CLAY_ID("barra-areas"), "stack", "Indicador das áreas de trabalho", "Os tracinhos no meio da barra");
	gv_interruptor(CLAY_ID("barra-areas-v"), &b.areas);
	pn_linha_acabar();
}
