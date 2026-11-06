#include "bateria.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *raiz_caminho(const char *rel) {
	const char *r = getenv("LUMNA_RAIZ_SISTEMA");
	return g_build_filename(r && *r ? r : "/", rel, NULL);
}

/* um número de um arquivo (o sysfs dá em micro-unidades); -1 se não existe */
static double numero(const char *pasta, const char *arq) {
	char *c = g_build_filename(pasta, arq, NULL), *t = NULL;
	double v = -1;
	if (g_file_get_contents(c, &t, NULL, NULL)) {
		v = atof(g_strstrip(t));
		g_free(t);
	}
	g_free(c);
	return v;
}

static void texto(const char *pasta, const char *arq, char *saida, size_t tam) {
	char *c = g_build_filename(pasta, arq, NULL), *t = NULL;
	saida[0] = '\0';
	if (g_file_get_contents(c, &t, NULL, NULL)) {
		g_strlcpy(saida, g_strstrip(t), tam);
		g_free(t);
	}
	g_free(c);
}

bool bateria_ler(struct bateria_info *b) {
	memset(b, 0, sizeof(*b));
	b->saude_pct = b->ciclos = b->minutos = -1;
	char *base = raiz_caminho("sys/class/power_supply");
	GDir *dir = g_dir_open(base, 0, NULL);
	const char *nome;
	while (dir && (nome = g_dir_read_name(dir))) {
		char *pasta = g_build_filename(base, nome, NULL), tipo[24];
		texto(pasta, "type", tipo, sizeof(tipo));
		if (strcmp(tipo, "Battery") || g_str_has_prefix(nome, "hid-")) { /* sem as baterias de mouse e fone (hid-...) */
			g_free(pasta);
			continue;
		}
		b->existe = true;
		g_strlcpy(b->nome, nome, sizeof(b->nome));
		double cap = numero(pasta, "capacity");
		b->pct = cap < 0 ? 0 : cap > 100 ? 100 : (int)cap;
		char est[24];
		texto(pasta, "status", est, sizeof(est));
		b->carregando = !strcmp(est, "Charging");
		b->na_tomada = b->carregando || !strcmp(est, "Full") || (!strcmp(est, "Not charging"));
		g_strlcpy(b->estado, b->carregando ? "Carregando" : !strcmp(est, "Discharging") ? "Descarregando" : !strcmp(est, "Full") ? "Cheia" : "Parada", sizeof(b->estado));
		texto(pasta, "technology", b->tecnologia, sizeof(b->tecnologia));
		double ciclos = numero(pasta, "cycle_count");
		b->ciclos = ciclos > 0 ? (int)ciclos : -1;
		/* energia (µWh, com potência em µW) ou carga (µAh, com corrente em µA): as mesmas contas */
		double cheia = numero(pasta, "energy_full"), fabrica = numero(pasta, "energy_full_design"), agora = numero(pasta, "energy_now"),
		       fluxo = numero(pasta, "power_now");
		double volts = numero(pasta, "voltage_now") / 1e6;
		if (cheia <= 0) {
			cheia = numero(pasta, "charge_full");
			fabrica = numero(pasta, "charge_full_design");
			agora = numero(pasta, "charge_now");
			fluxo = numero(pasta, "current_now");
			if (volts > 0 && fluxo > 0) {
				b->watts = fluxo / 1e6 * volts;
			}
		} else if (fluxo > 0) {
			b->watts = fluxo / 1e6;
		}
		if (cheia > 0 && fabrica > 0) {
			b->saude_pct = (int)(cheia * 100.0 / fabrica + 0.5);
			if (b->saude_pct > 100) {
				b->saude_pct = 100;
			}
		}
		if (fluxo > 0 && agora >= 0 && cheia > 0) {
			double horas = b->carregando ? (cheia - agora) / fluxo : !strcmp(est, "Discharging") ? agora / fluxo : -1;
			b->minutos = horas >= 0 ? (int)(horas * 60 + 0.5) : -1;
		}
		g_free(pasta);
		break; /* a primeira bateria do computador */
	}
	if (dir) {
		g_dir_close(dir);
	}
	g_free(base);
	return b->existe;
}
