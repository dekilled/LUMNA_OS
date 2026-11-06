/* Aparência → Barra superior: o relógio e o que a barra mostra (~/.config/lumna/barra.ini, que a
 * barra vigia e aplica na hora). */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "telas.h"

/* em quais telas: 0 só na principal, 1 em todas, 2 em nenhuma, 3 as que eu escolher (a lista, nomes separados por vírgula) */
struct barra_ajustes {
	bool segundos, data, areas, bateria;
	int telas;
	char lista[256];
};
static struct telas_estado monitores; /* os monitores de agora (lidos ao abrir a página) */
static const struct barra_ajustes fabrica = { .areas = true, .bateria = true };
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
			} else if (!strcmp(chave, "bateria")) {
				gravada.bateria = sim;
			} else if (!strcmp(chave, "telas")) {
				gravada.telas = !strcmp(valor, "principal") ? 0 : !strcmp(valor, "todas") ? 1 : !strcmp(valor, "nenhuma") ? 2 : 3;
				if (gravada.telas == 3) {
					g_strlcpy(gravada.lista, valor, sizeof(gravada.lista));
				}
			}
		}
		fclose(f);
	}
	b = gravada;
	telas_ler(&monitores);
}

bool pn_barra_pendente(void) {
	return memcmp(&b, &gravada, sizeof(b)) != 0;
}

void pn_barra_aplicar(void) {
	char texto[512];
	const char *telas = b.telas == 0 ? "principal" : b.telas == 1 ? "todas" : b.telas == 2 ? "nenhuma" : b.lista[0] ? b.lista : "principal";
	snprintf(texto, sizeof(texto), "# A barra superior (o Painel de controle grava este arquivo)\nsegundos = %s\ndata = %s\nareas = %s\nbateria = %s\ntelas = %s\n",
		b.segundos ? "sim" : "nao", b.data ? "sim" : "nao", b.areas ? "sim" : "nao", b.bateria ? "sim" : "nao", telas);
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

static bool na_lista(const char *nome) {
	char copia[256];
	g_strlcpy(copia, b.lista, sizeof(copia));
	char *resto = NULL;
	for (char *t = strtok_r(copia, ",", &resto); t; t = strtok_r(NULL, ",", &resto)) {
		if (!strcmp(t, nome)) {
			return true;
		}
	}
	return false;
}

static void lista_definir(const char *nome, bool ligar) {
	char nova[256] = "";
	char copia[256];
	g_strlcpy(copia, b.lista, sizeof(copia));
	char *resto = NULL;
	for (char *t = strtok_r(copia, ",", &resto); t; t = strtok_r(NULL, ",", &resto)) {
		if (strcmp(t, nome)) {
			g_strlcat(nova, nova[0] ? "," : "", sizeof(nova));
			g_strlcat(nova, t, sizeof(nova));
		}
	}
	if (ligar) {
		g_strlcat(nova, nova[0] ? "," : "", sizeof(nova));
		g_strlcat(nova, nome, sizeof(nova));
	}
	g_strlcpy(b.lista, nova, sizeof(b.lista));
}

void pn_barra_desenhar(void) {
	static const char *const onde[] = { "Só na tela principal", "Em todas as telas", "Em nenhuma (sem barra)", "Só nas que eu escolher" };
	pn_subtitulo(CLAY_ID("barra-s0"), "Onde aparece");
	if (pn_ajuste_lista(CLAY_ID("barra-onde"), "screens", "Telas com a barra", "A principal é a primeira tela do Orbit", onde, 4, &b.telas) && b.telas == 3 && !b.lista[0]) {
		/* ao escolher "as que eu escolher" a lista começa com a principal */
		if (monitores.n > 0) {
			g_strlcpy(b.lista, monitores.v[0].nome, sizeof(b.lista));
		}
	}
	if (b.telas == 3) {
		for (int i = 0; i < monitores.n; i++) {
			const struct tela *t = &monitores.v[i];
			char nome[160];
			if (t->modelo[0]) {
				g_snprintf(nome, sizeof(nome), "%s%s%s (%s)", t->fabricante, t->fabricante[0] ? " " : "", t->modelo, t->nome);
			} else {
				g_strlcpy(nome, t->nome, sizeof(nome));
			}
			bool tem = na_lista(t->nome);
			pn_linha_comecar(CLAY_IDI("barra-tela", i), "screens", nome, i == 0 ? "Tela principal" : NULL);
			if (gv_interruptor(CLAY_IDI("barra-tela-v", i), &tem)) {
				lista_definir(t->nome, tem);
			}
			pn_linha_acabar();
		}
		if (monitores.n == 0) {
			pn_subtitulo(CLAY_ID("barra-sem"), "O Orbit não respondeu: não dá para listar as telas agora.");
		}
	}
	pn_subtitulo(CLAY_ID("barra-s1"), "Relógio");
	pn_linha_comecar(CLAY_ID("barra-seg"), "clock", "Mostrar os segundos", NULL);
	gv_interruptor(CLAY_ID("barra-seg-v"), &b.segundos);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("barra-data"), "clock", "Mostrar a data", "Ao lado da hora (Dom, 25 de out)");
	gv_interruptor(CLAY_ID("barra-data-v"), &b.data);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("barra-bat"), "battery", "Mostrar a porcentagem da bateria", "Ao lado do ícone da bateria (só em quem tem bateria)");
	gv_interruptor(CLAY_ID("barra-bat-v"), &b.bateria);
	pn_linha_acabar();
	pn_subtitulo(CLAY_ID("barra-s2"), "Ao centro");
	pn_linha_comecar(CLAY_ID("barra-areas"), "stack", "Indicador das áreas de trabalho", "Os tracinhos no meio da barra");
	gv_interruptor(CLAY_ID("barra-areas-v"), &b.areas);
	pn_linha_acabar();
}

/* Energia → Bateria também liga e desliga a porcentagem: lê e grava só esta chave, pelo mesmo arquivo */
bool pn_barra_bateria_pct(void) {
	pn_barra_abrir();
	return gravada.bateria;
}

void pn_barra_bateria_definir(bool ligar) {
	pn_barra_abrir();
	b.bateria = ligar;
	pn_barra_aplicar();
}
