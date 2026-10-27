#include "hora.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

bool hora_24h(void) {
	static gint64 lida_em;
	static bool vinte_e_quatro = true;
	gint64 agora = g_get_monotonic_time();
	if (lida_em && agora - lida_em < 2 * G_USEC_PER_SEC) {
		return vinte_e_quatro;
	}
	lida_em = agora;
	vinte_e_quatro = true;
	char *caminho = g_build_filename(g_get_user_config_dir(), "lumna", "hora.ini", NULL), *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#') {
				continue;
			}
			*igual = '\0';
			if (!strcmp(g_strstrip(*l), "formato")) {
				vinte_e_quatro = strcmp(g_strstrip(igual + 1), "12") != 0;
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(caminho);
	return vinte_e_quatro;
}

void hora_texto(const struct tm *t, bool segundos, char *buf, size_t tam) {
	if (hora_24h()) {
		if (segundos) {
			snprintf(buf, tam, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
		} else {
			snprintf(buf, tam, "%02d:%02d", t->tm_hour, t->tm_min);
		}
		return;
	}
	int h = t->tm_hour % 12 == 0 ? 12 : t->tm_hour % 12;
	const char *sufixo = t->tm_hour < 12 ? "AM" : "PM";
	if (segundos) {
		snprintf(buf, tam, "%d:%02d:%02d %s", h, t->tm_min, t->tm_sec, sufixo);
	} else {
		snprintf(buf, tam, "%d:%02d %s", h, t->tm_min, sufixo);
	}
}
