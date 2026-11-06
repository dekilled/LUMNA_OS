/*
 * lumna-inicio: o que acontece depois que a sessão sobe (Inicialização no Painel):
 *   - roda os apps de ~/.config/autostart (servicos/autostart.h);
 *   - se "Reabrir as janelas da última sessão" está ligado (~/.config/lumna/inicio.ini, reabrir = sim), reabre os
 *     apps que estavam abertos (servicos/sessao.h);
 *   - e fica vigiando: de 15 em 15 s guarda quais apps estão abertos, para o próximo login. Uma lista vazia só
 *     vale depois de um minuto vazia (ao sair, as janelas fecham juntas e não devem apagar a lista).
 * Sobe com a sessão:
 *   lumna-inicio &
 */
#include <glib.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "autostart.h"
#include "sessao.h"

static bool reabrir;
static char guardada[SESSAO_MAX][160];
static int nguardada = -1;
static guint vazia_desde;
static gint64 vazia_inicio;

static void ler_ajustes(void) {
	const char *config = getenv("XDG_CONFIG_HOME");
	char *c = config && *config ? g_build_filename(config, "lumna", "inicio.ini", NULL)
				    : g_build_filename(g_get_home_dir(), ".config", "lumna", "inicio.ini", NULL);
	char *texto = NULL;
	reabrir = false;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (igual && **l != '#') {
				*igual = '\0';
				if (!strcmp(g_strstrip(*l), "reabrir")) {
					reabrir = !strcmp(g_strstrip(igual + 1), "sim");
				}
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(c);
}

static gboolean guardar(gpointer d) {
	char ids[SESSAO_MAX][160];
	int n = sessao_agora(ids, SESSAO_MAX);
	if (n < 0) {
		return G_SOURCE_CONTINUE; /* o Orbit não respondeu */
	}
	if (n == 0) {
		gint64 agora = g_get_monotonic_time();
		if (!vazia_inicio) {
			vazia_inicio = agora;
		}
		if (agora - vazia_inicio < 60 * G_USEC_PER_SEC) {
			return G_SOURCE_CONTINUE;
		}
	} else {
		vazia_inicio = 0;
	}
	bool igual = n == nguardada;
	for (int i = 0; igual && i < n; i++) {
		igual = !strcmp(ids[i], guardada[i]);
	}
	if (!igual && sessao_gravar((const char(*)[160])ids, n)) {
		memcpy(guardada, ids, sizeof(guardada));
		nguardada = n;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean comecar(gpointer d) {
	ler_ajustes();
	char antes[SESSAO_MAX][160];
	int nantes = reabrir ? sessao_ler(antes, SESSAO_MAX) : 0;
	int a = autostart_rodar();
	int r = nantes > 0 ? sessao_abrir(antes, nantes) : 0;
	printf("lumna-inicio: %d app(s) do autostart, %d da última sessão\n", a, r);
	fflush(stdout);
	/* só começa a guardar depois de reabrir (senão guardaria a sessão vazia por cima da que vai voltar) */
	g_timeout_add_seconds(15, guardar, NULL);
	return G_SOURCE_REMOVE;
}

static gboolean sair(gpointer laco) {
	g_main_loop_quit(laco);
	return G_SOURCE_REMOVE;
}

int main(void) {
	GMainLoop *laco = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGTERM, sair, laco);
	g_unix_signal_add(SIGINT, sair, laco);
	const char *espera = getenv("LUMNA_INICIO_ESPERA"); /* o Orbit e a barra precisam de um instante (testes: 0) */
	g_timeout_add(espera ? (guint)(atof(espera) * 1000) : 2500, comecar, NULL);
	g_main_loop_run(laco);
	(void)vazia_desde;
	return 0;
}
