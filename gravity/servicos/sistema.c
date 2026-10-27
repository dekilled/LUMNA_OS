#define _GNU_SOURCE
#include "sistema.h"

#include <ctype.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* um caminho do sistema, ou o mesmo numa árvore falsa de testes */
static char *caminho(const char *absoluto) {
	const char *raiz = g_getenv("LUMNA_RAIZ_SISTEMA");
	return raiz && *raiz ? g_strdup_printf("%s%s", raiz, absoluto) : g_strdup(absoluto);
}

static char *ler(const char *absoluto) {
	char *c = caminho(absoluto), *t = NULL;
	if (!g_file_get_contents(c, &t, NULL, NULL)) {
		t = NULL;
	}
	g_free(c);
	return t;
}

/* o valor de "chave<sep>valor" na primeira linha que começar assim */
static bool valor_de(const char *texto, const char *chave, char sep, char *saida, size_t tam) {
	size_t kl = strlen(chave);
	for (const char *l = texto; l && *l;) {
		const char *fim = strchr(l, '\n');
		size_t len = fim ? (size_t)(fim - l) : strlen(l);
		if (len > kl && !strncmp(l, chave, kl)) {
			const char *p = l + kl;
			while (p < l + len && (*p == ' ' || *p == '\t')) {
				p++;
			}
			if (p < l + len && *p == sep) {
				p++;
				while (p < l + len && (*p == ' ' || *p == '\t' || *p == '"')) {
					p++;
				}
				size_t n = (size_t)(l + len - p);
				while (n && (p[n - 1] == '"' || p[n - 1] == ' ' || p[n - 1] == '\r')) {
					n--;
				}
				g_strlcpy(saida, p, n + 1 < tam ? n + 1 : tam);
				return true;
			}
		}
		l = fim ? fim + 1 : NULL;
	}
	return false;
}

static void tira_ruido(char *s) {
	/* "Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz" → "Intel Core i7-9750H @ 2.60GHz" */
	static const char *const ruido[] = { "(R)", "(TM)", "(tm)", "(r)", " CPU" };
	for (size_t i = 0; i < sizeof(ruido) / sizeof(ruido[0]); i++) {
		size_t len = strlen(ruido[i]);
		char *p;
		while ((p = strstr(s, ruido[i])) != NULL) {
			memmove(p, p + len, strlen(p + len) + 1);
		}
	}
	char *p;
	while ((p = strstr(s, "  ")) != NULL) {
		memmove(p, p + 1, strlen(p));
	}
}

static const char *fabricante_gpu(const char *id) {
	return !strcmp(id, "0x10de") ? "NVIDIA" : !strcmp(id, "0x1002") ? "AMD" : !strcmp(id, "0x8086") ? "Intel" : !strcmp(id, "0x1af4") ? "Virtio" : "Placa de vídeo";
}

void sistema_ler(struct sistema_info *s) {
	memset(s, 0, sizeof(*s));
	g_strlcpy(s->host, g_get_host_name(), sizeof(s->host));
	g_strlcpy(s->lumna, "1.0.0", sizeof(s->lumna));
	char *t = ler("/etc/lumna-release");
	if (t) {
		valor_de(t, "VERSION", '=', s->lumna, sizeof(s->lumna));
		g_free(t);
	}
	t = ler("/etc/os-release");
	if (t) {
		if (!valor_de(t, "PRETTY_NAME", '=', s->distro, sizeof(s->distro))) {
			valor_de(t, "NAME", '=', s->distro, sizeof(s->distro));
		}
		g_free(t);
	}
	struct utsname u;
	if (uname(&u) == 0) {
		g_strlcpy(s->kernel, u.release, sizeof(s->kernel));
		g_strlcpy(s->arquitetura, u.machine, sizeof(s->arquitetura));
	}
	t = ler("/proc/cpuinfo");
	if (t) {
		if (!valor_de(t, "model name", ':', s->cpu, sizeof(s->cpu)) && !valor_de(t, "Model", ':', s->cpu, sizeof(s->cpu))) {
			valor_de(t, "Hardware", ':', s->cpu, sizeof(s->cpu));
		}
		int n = 0;
		for (const char *p = t; (p = strstr(p, "\nprocessor")) != NULL; p++) {
			n++;
		}
		if (!strncmp(t, "processor", 9)) {
			n++;
		}
		s->nucleos = n;
		g_free(t);
	}
	if (s->nucleos <= 0) {
		s->nucleos = (int)sysconf(_SC_NPROCESSORS_ONLN);
	}
	tira_ruido(s->cpu);
	t = ler("/proc/meminfo");
	if (t) {
		char v[48];
		if (valor_de(t, "MemTotal", ':', v, sizeof(v))) {
			s->memoria_total = (int64_t)atoll(v) * 1024;
		}
		if (valor_de(t, "MemAvailable", ':', v, sizeof(v))) {
			s->memoria_livre = (int64_t)atoll(v) * 1024;
		}
		g_free(t);
	}
	/* o modelo: fabricante e produto do DMI (um notebook, uma placa-mãe, a máquina virtual) */
	char fab[64] = "", prod[64] = "";
	char *f = ler("/sys/class/dmi/id/sys_vendor"), *p = ler("/sys/class/dmi/id/product_name");
	if (f) {
		g_strlcpy(fab, g_strstrip(f), sizeof(fab));
	}
	if (p) {
		g_strlcpy(prod, g_strstrip(p), sizeof(prod));
	}
	g_free(f);
	g_free(p);
	if (fab[0] || prod[0]) {
		snprintf(s->modelo, sizeof(s->modelo), "%s%s%s", fab, fab[0] && prod[0] ? " " : "", prod);
	}
	/* a placa de vídeo: o fabricante e o driver do primeiro card do DRM */
	for (int i = 0; i < 4 && !s->gpu[0]; i++) {
		char rel[96];
		snprintf(rel, sizeof(rel), "/sys/class/drm/card%d/device/vendor", i);
		char *vid = ler(rel);
		if (!vid) {
			continue;
		}
		snprintf(rel, sizeof(rel), "/sys/class/drm/card%d/device/uevent", i);
		char *uev = ler(rel);
		char drv[48] = "";
		if (uev) {
			valor_de(uev, "DRIVER", '=', drv, sizeof(drv));
		}
		snprintf(s->gpu, sizeof(s->gpu), "%s%s%s%s", fabricante_gpu(g_strstrip(vid)), drv[0] ? " (" : "", drv, drv[0] ? ")" : "");
		g_free(uev);
		g_free(vid);
	}
	t = ler("/proc/uptime");
	if (t) {
		s->ligado_ha_s = (int64_t)atof(t);
		g_free(t);
	}
	struct statvfs sv;
	if (statvfs("/", &sv) == 0) {
		s->disco_total = (int64_t)sv.f_blocks * (int64_t)sv.f_frsize;
		s->disco_livre = (int64_t)sv.f_bavail * (int64_t)sv.f_frsize;
	}
}

static void gb(char *buf, size_t tam, int64_t b) {
	snprintf(buf, tam, "%.1f GB", (double)b / 1e9);
}

void sistema_resumo(const struct sistema_info *s, char *buf, size_t tam) {
	char mem[32], disco[32];
	gb(mem, sizeof(mem), s->memoria_total);
	gb(disco, sizeof(disco), s->disco_total);
	snprintf(buf, tam, "LUMNA OS %s\nComputador: %s\nModelo: %s\nProcessador: %s (%d núcleos)\nMemória: %s\nVídeo: %s\nSistema: %s\nKernel: %s (%s)\nDisco do sistema: %s",
		s->lumna, s->host, s->modelo[0] ? s->modelo : "—", s->cpu[0] ? s->cpu : "—", s->nucleos, mem, s->gpu[0] ? s->gpu : "—",
		s->distro, s->kernel, s->arquitetura, disco);
}

int sistema_atualizacoes(struct sistema_atualizacao *v, int max) {
	/* checkupdates (pacman-contrib) sincroniza uma cópia do banco: sem senha. Sem ele, o que o pacman já sabe. */
	const char *comandos[][3] = { { "checkupdates", NULL, NULL }, { "pacman", "-Qu", NULL } };
	for (int c = 0; c < 2; c++) {
		char *argv[] = { "timeout", "90", (char *)comandos[c][0], (char *)comandos[c][1], NULL };
		char *saida = NULL;
		int estado = 0;
		if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, &estado, NULL) || !saida) {
			g_free(saida);
			continue;
		}
		/* checkupdates sai 0 com atualizações, 2 sem nenhuma; 124 é o timeout; 127 é "não existe" */
		int codigo = WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
		if (codigo == 127 || codigo == 124 || codigo < 0) {
			g_free(saida);
			continue;
		}
		int n = 0;
		char **linhas = g_strsplit(saida, "\n", -1);
		for (char **l = linhas; *l && n < max; l++) {
			char nome[64], antiga[48], seta[8], nova[48];
			if (sscanf(*l, "%63s %47s %7s %47s", nome, antiga, seta, nova) == 4 && !strcmp(seta, "->")) {
				g_strlcpy(v[n].nome, nome, sizeof(v[n].nome));
				g_strlcpy(v[n].antiga, antiga, sizeof(v[n].antiga));
				g_strlcpy(v[n].nova, nova, sizeof(v[n].nova));
				n++;
			}
		}
		g_strfreev(linhas);
		g_free(saida);
		return n;
	}
	return -1;
}

static int64_t iso_para_unix(const char *s) {
	/* 2026-10-26T10:11:12-0300 */
	struct tm tm = { 0 };
	int dh = 0, dm = 0;
	char sinal = '+';
	if (sscanf(s, "%d-%d-%dT%d:%d:%d%c%2d%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &sinal, &dh, &dm) < 6) {
		return 0;
	}
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	time_t t = timegm(&tm);
	int off = (dh * 60 + dm) * 60;
	return (int64_t)t - (sinal == '-' ? -off : off);
}

int sistema_historico(struct sistema_evento *v, int max) {
	char *c = caminho("/var/log/pacman.log");
	FILE *f = fopen(c, "r");
	g_free(c);
	if (!f) {
		return 0;
	}
	/* só o fim do arquivo: o log cresce por anos */
	fseek(f, 0, SEEK_END);
	long tam = ftell(f);
	long ini = tam > 600000 ? tam - 600000 : 0;
	fseek(f, ini, SEEK_SET);
	char *texto = g_malloc((size_t)(tam - ini) + 1);
	size_t lidos = fread(texto, 1, (size_t)(tam - ini), f);
	texto[lidos] = '\0';
	fclose(f);
	int n = 0;
	char **linhas = g_strsplit(texto, "\n", -1);
	int total = (int)g_strv_length(linhas);
	for (int i = total - 1; i >= 0 && n < max; i--) {
		char *l = linhas[i];
		char data[40], acao[24], nome[64], a[48] = "", b[48] = "";
		if (sscanf(l, "[%39[^]]] [ALPM] %23s %63s (%47[^ )] -> %47[^)])", data, acao, nome, a, b) >= 3) {
			const char *nome_acao = !strcmp(acao, "installed") ? "instalado" : !strcmp(acao, "upgraded") ? "atualizado" : !strcmp(acao, "removed") ? "removido" :
				!strcmp(acao, "downgraded") ? "rebaixado" : NULL;
			if (!nome_acao) {
				continue;
			}
			struct sistema_evento *e = &v[n++];
			memset(e, 0, sizeof(*e));
			e->quando = iso_para_unix(data);
			g_strlcpy(e->acao, nome_acao, sizeof(e->acao));
			g_strlcpy(e->nome, nome, sizeof(e->nome));
			if (!strcmp(acao, "upgraded") || !strcmp(acao, "downgraded")) {
				g_strlcpy(e->de, a, sizeof(e->de));
				g_strlcpy(e->para, b, sizeof(e->para));
			} else {
				g_strlcpy(e->para, a, sizeof(e->para));
			}
		}
	}
	g_strfreev(linhas);
	g_free(texto);
	return n;
}
