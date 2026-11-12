#include "orbit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#if WLR_HAS_XWAYLAND
#include <wlr/xwayland.h>
#endif

/*
 * Um jogo? A janela é de um jogo quando o processo dela (ou um dos pais) tem a marca de quem o abriu:
 *   - a Steam marca todo jogo (o nativo e o do Proton) com SteamGameId / SteamAppId no ambiente;
 *   - o LUMNA Jogos marca os que abre por conta própria (avulsos, GOG, Epic) com LUMNA_JOGO;
 *   - o Lutris e o Heroic abrem o jogo como filho deles (o pai é o lutris/heroic, não a própria janela do lançador).
 * A classe da janela não basta (o CS:GO nativo é csgo_linux64, o CS2 é cs2). Vale para janelas X11 e Wayland: o Wayland dá o PID pelo socket.
 * Só o dono do processo lê o ambiente dele, e o Orbit roda como a pessoa: os jogos são dela.
 */

/* o valor da variável no ambiente do processo (NULL se não tem); o texto vale até a próxima chamada */
static const char *env_de(pid_t pid, const char *nome) {
	static char buf[262144];
	char caminho[64];
	snprintf(caminho, sizeof(caminho), "/proc/%d/environ", (int)pid);
	FILE *f = fopen(caminho, "r");
	if (!f) {
		return NULL;
	}
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	size_t lnome = strlen(nome);
	for (size_t i = 0; i < n;) {
		const char *e = buf + i;
		if (!strncmp(e, nome, lnome) && e[lnome] == '=') {
			return e + lnome + 1;
		}
		i += strlen(e) + 1;
	}
	return NULL;
}

static bool marca_de_jogo(pid_t pid) {
	static const char *const marcas[] = { "SteamGameId", "SteamAppId", "LUMNA_JOGO", NULL };
	for (int i = 0; marcas[i]; i++) {
		const char *v = env_de(pid, marcas[i]);
		if (v && *v && strcmp(v, "0")) { /* a Steam põe 0 em processos que não são de um jogo */
			return true;
		}
	}
	return false;
}

/* o pai e o nome do processo (o comm, entre parênteses no /proc/PID/stat; o nome pode ter espaços e parênteses) */
static bool pai_e_nome(pid_t pid, pid_t *pai, char *nome, size_t tam) {
	char caminho[64], linha[512];
	snprintf(caminho, sizeof(caminho), "/proc/%d/stat", (int)pid);
	FILE *f = fopen(caminho, "r");
	if (!f) {
		return false;
	}
	size_t n = fread(linha, 1, sizeof(linha) - 1, f);
	fclose(f);
	linha[n] = '\0';
	char *abre = strchr(linha, '('), *fecha = strrchr(linha, ')');
	if (!abre || !fecha || fecha < abre) {
		return false;
	}
	size_t ln = (size_t)(fecha - abre - 1);
	if (ln >= tam) {
		ln = tam - 1;
	}
	memcpy(nome, abre + 1, ln);
	nome[ln] = '\0';
	char estado;
	int p = 0;
	if (sscanf(fecha + 1, " %c %d", &estado, &p) != 2) {
		return false;
	}
	*pai = (pid_t)p;
	return true;
}

bool orbit_pid_is_game(pid_t pid) {
	if (pid <= 1) {
		return false;
	}
	if (marca_de_jogo(pid)) {
		return true;
	}
	/* os pais: a marca herdada que o jogo apagou do próprio ambiente, ou o Lutris/Heroic que o abriu */
	pid_t atual = pid;
	for (int nivel = 0; nivel < 8; nivel++) {
		pid_t pai = 0;
		char nome[64];
		if (!pai_e_nome(atual, &pai, nome, sizeof(nome)) || pai <= 1) {
			return false;
		}
		char nome_pai[64];
		pid_t avo = 0;
		if (!pai_e_nome(pai, &avo, nome_pai, sizeof(nome_pai))) {
			return false;
		}
		if (!strcmp(nome_pai, "lutris") || !strncmp(nome_pai, "heroic", 6) || !strcmp(nome_pai, "legendary") || !strcmp(nome_pai, "gogdl")) {
			return true;
		}
		if (!strcmp(nome_pai, "orbit") || !strcmp(nome_pai, "systemd") || !strcmp(nome_pai, "steam")) {
			return false; /* chegou na sessão (ou no cliente da Steam, que não é jogo) sem achar a marca */
		}
		if (marca_de_jogo(pai)) {
			return true;
		}
		atual = pai;
	}
	return false;
}

static pid_t view_pid(const struct orbit_view *view) {
	if (view->toplevel && view->toplevel->resource) {
		pid_t pid = 0;
		uid_t uid;
		gid_t gid;
		wl_client_get_credentials(wl_resource_get_client(view->toplevel->resource), &pid, &uid, &gid);
		return pid;
	}
#if WLR_HAS_XWAYLAND
	if (view->xsurface) {
		return view->xsurface->pid;
	}
#endif
	return 0;
}

bool orbit_view_detect_game(const struct orbit_view *view) {
	const char *app_id = orbit_view_app_id(view);
	/* a biblioteca do Gaming e os lançadores (a Steam, o Lutris, o Heroic) são do modo, mas não são jogos */
	if (orbit_app_is_mode_shell(app_id)) {
		return false;
	}
#if WLR_HAS_XWAYLAND
	if (view->xsurface && view->xsurface->class && !strncmp(view->xsurface->class, "steam_app_", 10)) {
		return true; /* o Proton dá esta classe às janelas do jogo */
	}
#endif
	return orbit_pid_is_game(view_pid(view));
}
