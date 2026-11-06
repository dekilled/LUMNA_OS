#include "luz.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRANSICAO 30 /* minutos de cada ponta, em que a luz muda aos poucos */

static char *caminho_ini(void) {
	const char *cfg = getenv("XDG_CONFIG_HOME");
	return cfg && *cfg ? g_build_filename(cfg, "lumna", "luz.ini", NULL) : g_build_filename(g_get_home_dir(), ".config", "lumna", "luz.ini", NULL);
}

static int minutos_de(const char *hhmm, int padrao) {
	int h, m;
	if (sscanf(hhmm, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
		return h * 60 + m;
	}
	return padrao;
}

void luz_ler(struct luz_cfg *c) {
	*c = (struct luz_cfg){ .modo = LUZ_DESLIGADA, .temperatura = 4000, .inicio = 20 * 60, .fim = 6 * 60 };
	char *caminho = caminho_ini(), *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#') {
				continue;
			}
			*igual = '\0';
			char *k = g_strstrip(*l), *v = g_strstrip(igual + 1);
			if (!strcmp(k, "modo")) {
				c->modo = !strcmp(v, "sempre") ? LUZ_SEMPRE : !strcmp(v, "horario") ? LUZ_HORARIO : LUZ_DESLIGADA;
			} else if (!strcmp(k, "temperatura")) {
				c->temperatura = atoi(v);
			} else if (!strcmp(k, "inicio")) {
				c->inicio = minutos_de(v, c->inicio);
			} else if (!strcmp(k, "fim")) {
				c->fim = minutos_de(v, c->fim);
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(caminho);
	c->temperatura = c->temperatura < 2500 ? 2500 : c->temperatura > 6000 ? 6000 : c->temperatura;
}

bool luz_gravar(const struct luz_cfg *c) {
	char *caminho = caminho_ini(), *dir = g_path_get_dirname(caminho);
	g_mkdir_with_parents(dir, 0755);
	char *texto = g_strdup_printf("# A luz noturna (o Painel de controle grava este arquivo; o lumna-luz aplica)\nmodo = %s\ntemperatura = %d\ninicio = %02d:%02d\nfim = %02d:%02d\n",
		c->modo == LUZ_SEMPRE ? "sempre" : c->modo == LUZ_HORARIO ? "horario" : "desligada", c->temperatura, c->inicio / 60, c->inicio % 60, c->fim / 60, c->fim % 60);
	char *tmp = g_strconcat(caminho, ".novo", NULL);
	bool ok = g_file_set_contents(tmp, texto, -1, NULL) && g_rename(tmp, caminho) == 0;
	g_free(tmp);
	g_free(texto);
	g_free(dir);
	g_free(caminho);
	return ok;
}

/* quanto da luz noturna vale agora (0 a 1) */
static double forca(const struct luz_cfg *c, int minuto) {
	if (c->modo == LUZ_SEMPRE) {
		return 1;
	}
	if (c->modo != LUZ_HORARIO) {
		return 0;
	}
	int dur = (c->fim - c->inicio + 1440) % 1440;
	if (dur == 0) {
		return 0;
	}
	int desde = (minuto - c->inicio + 1440) % 1440; /* minutos desde o início */
	if (desde < TRANSICAO) {
		return (double)desde / TRANSICAO; /* acende aos poucos */
	}
	if (desde < dur) {
		return 1;
	}
	int depois = desde - dur; /* minutos desde o fim */
	if (depois < TRANSICAO) {
		return 1.0 - (double)depois / TRANSICAO; /* apaga aos poucos */
	}
	return 0;
}

int luz_kelvin_agora(const struct luz_cfg *c, int minuto) {
	double f = forca(c, minuto);
	return (int)(LUZ_NORMAL - f * (LUZ_NORMAL - c->temperatura) + 0.5);
}

static double limitar(double v) {
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

void luz_rgb(int kelvin, double *r, double *g, double *b) {
	double t = kelvin / 100.0;
	*r = t <= 66 ? 255 : limitar(329.698727446 * pow(t - 60, -0.1332047592));
	*g = t <= 66 ? limitar(99.4708025861 * log(t) - 161.1195681661) : limitar(288.1221695283 * pow(t - 60, -0.0755148492));
	*b = t >= 66 ? 255 : t <= 19 ? 0 : limitar(138.5177312231 * log(t - 10) - 305.0447927307);
	*r /= 255;
	*g /= 255;
	*b /= 255;
}

void luz_rampa(int kelvin, uint16_t *r, uint16_t *g, uint16_t *b, uint32_t tamanho) {
	double mr, mg, mb;
	luz_rgb(kelvin, &mr, &mg, &mb);
	/* os 6500 K são o branco puro: sem efeito (a Helland dá 255,249,253 ali: normaliza para a rampa ficar reta) */
	double nr, ng, nb;
	luz_rgb(LUZ_NORMAL, &nr, &ng, &nb);
	mr /= nr;
	mg /= ng;
	mb /= nb;
	for (uint32_t i = 0; i < tamanho; i++) {
		double x = tamanho > 1 ? (double)i / (tamanho - 1) : 1;
		r[i] = (uint16_t)(65535.0 * x * (mr > 1 ? 1 : mr) + 0.5);
		g[i] = (uint16_t)(65535.0 * x * (mg > 1 ? 1 : mg) + 0.5);
		b[i] = (uint16_t)(65535.0 * x * (mb > 1 ? 1 : mb) + 0.5);
	}
}
