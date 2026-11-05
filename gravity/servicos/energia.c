#include "energia.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

static char *raiz_ler(const char *relativo) {
	const char *raiz = g_getenv("LUMNA_RAIZ_SISTEMA");
	char *c = g_build_filename(raiz && *raiz ? raiz : "/", relativo, NULL), *t = NULL;
	if (!g_file_get_contents(c, &t, NULL, NULL)) {
		t = NULL;
	}
	g_free(c);
	return t;
}

static long long kb_de(const char *meminfo, const char *chave) {
	size_t n = strlen(chave);
	for (const char *p = meminfo; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {
		if (!strncmp(p, chave, n)) {
			return atoll(p + n);
		}
	}
	return 0;
}

/* a primeira bateria: a carga (0 a 100) e se está carregando; false se o PC não tem */
static bool bateria(int *carga, bool *carregando) {
	const char *raiz = g_getenv("LUMNA_RAIZ_SISTEMA");
	char *dir = g_build_filename(raiz && *raiz ? raiz : "/", "sys/class/power_supply", NULL);
	GDir *d = g_dir_open(dir, 0, NULL);
	bool achou = false;
	const char *nome;
	while (d && !achou && (nome = g_dir_read_name(d))) {
		if (strncmp(nome, "BAT", 3)) {
			continue;
		}
		char *rel = g_strdup_printf("sys/class/power_supply/%s/capacity", nome);
		char *cap = raiz_ler(rel);
		g_free(rel);
		if (!cap) {
			continue;
		}
		*carga = atoi(cap);
		g_free(cap);
		rel = g_strdup_printf("sys/class/power_supply/%s/status", nome);
		char *st = raiz_ler(rel);
		g_free(rel);
		*carregando = st && (g_str_has_prefix(st, "Charging") || g_str_has_prefix(st, "Full"));
		g_free(st);
		achou = true;
	}
	if (d) {
		g_dir_close(d);
	}
	g_free(dir);
	return achou;
}

void energia_info_json(GString *s) {
	char *up = raiz_ler("proc/uptime"), *carga = raiz_ler("proc/loadavg"), *mem = raiz_ler("proc/meminfo");
	long long ligado = up ? (long long)g_ascii_strtod(up, NULL) : 0;
	g_string_append(s, "{\"type\":\"info\",\"host\":");
	json_escrever_texto(s, g_get_host_name());
	g_string_append(s, ",\"user\":");
	json_escrever_texto(s, g_get_user_name());
	g_string_append_printf(s, ",\"uptime\":%lld,\"load\":", ligado);
	if (carga) {
		char *fim = carga;
		g_ascii_strtod(carga, &fim);
		char *l = g_strndup(carga, fim - carga);
		json_escrever_texto(s, l);
		g_free(l);
	} else {
		g_string_append(s, "null");
	}
	g_string_append_printf(s, ",\"memory_total_kb\":%lld,\"memory_available_kb\":%lld", mem ? kb_de(mem, "MemTotal:") : 0, mem ? kb_de(mem, "MemAvailable:") : 0);
	int pct = 0;
	bool carregando = false;
	if (bateria(&pct, &carregando)) {
		g_string_append_printf(s, ",\"battery\":%d,\"charging\":%s", pct, carregando ? "true" : "false");
	} else {
		g_string_append(s, ",\"battery\":null,\"charging\":false");
	}
	g_string_append_c(s, '}');
	g_free(up);
	g_free(carga);
	g_free(mem);
}

bool energia_acao(const char *acao, char *erro, size_t tam) {
	char *argv[3] = { NULL, NULL, NULL };
	if (!strcmp(acao, "lock")) {
		argv[0] = "lumna-bloqueio";
	} else if (!strcmp(acao, "suspend") || !strcmp(acao, "reboot") || !strcmp(acao, "poweroff")) {
		argv[0] = "systemctl";
		argv[1] = (char *)acao;
	} else {
		g_strlcpy(erro, "ação de energia desconhecida", tam);
		return false;
	}
	GError *e = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_CHILD_INHERITS_STDIN, NULL, NULL, NULL, &e)) {
		g_snprintf(erro, tam, "não consegui rodar %s: %s", argv[0], e->message);
		g_error_free(e);
		return false;
	}
	return true;
}
