#define _GNU_SOURCE
#include "armazenamento.h"

#include <errno.h>
#include <ftw.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

/* ---- medir pastas ---- */

static int64_t soma_bytes;
static uid_t soma_dono;
static bool soma_so_do_dono;

static int somar(const char *caminho, const struct stat *st, int tipo, struct FTW *f) {
	(void)caminho;
	(void)f;
	if (tipo == FTW_NS || (soma_so_do_dono && st->st_uid != soma_dono)) {
		return 0;
	}
	soma_bytes += (int64_t)st->st_blocks * 512;
	return 0;
}

static int64_t medir(const char *pasta, bool so_do_dono) {
	soma_bytes = 0;
	soma_dono = getuid();
	soma_so_do_dono = so_do_dono;
	nftw(pasta, somar, 32, FTW_PHYS | FTW_MOUNT);
	return soma_bytes;
}

/* ---- as pastas ---- */

static char *pasta_lixeira(void) {
	const char *dados = g_getenv("XDG_DATA_HOME");
	return dados && *dados ? g_build_filename(dados, "Trash", NULL) : g_build_filename(g_get_home_dir(), ".local/share/Trash", NULL);
}

static char *pasta_cache(void) {
	const char *c = g_getenv("XDG_CACHE_HOME");
	return c && *c ? g_strdup(c) : g_build_filename(g_get_home_dir(), ".cache", NULL);
}

static const char *pasta_tmp(void) {
	const char *t = g_getenv("TMPDIR");
	return t && *t ? t : "/tmp";
}

static const char *pasta_cache_pacman(void) {
	const char *c = g_getenv("LUMNA_PACMAN_CACHE");
	return c && *c ? c : "/var/cache/pacman/pkg";
}

/* ---- ler ---- */

static void campo(const char *linha, const char *chave, char *saida, size_t tam) {
	char *k = g_strdup_printf("%s=\"", chave);
	const char *p = strstr(linha, k);
	saida[0] = '\0';
	/* a chave tem de começar a palavra (NAME não é PKNAME) */
	while (p && p != linha && p[-1] != ' ') {
		p = strstr(p + 1, k);
	}
	if (p) {
		p += strlen(k);
		const char *fim = strchr(p, '"');
		size_t n = fim ? (size_t)(fim - p) : strlen(p);
		if (n >= tam) {
			n = tam - 1;
		}
		memcpy(saida, p, n);
		saida[n] = '\0';
	}
	g_free(k);
}

/* o lsblk escreve \x20 e afins nos textos */
static void destrancar(char *s) {
	char *e = s, *o = s;
	while (*e) {
		if (e[0] == '\\' && e[1] == 'x' && g_ascii_isxdigit(e[2]) && g_ascii_isxdigit(e[3])) {
			*o++ = (char)(g_ascii_xdigit_value(e[2]) * 16 + g_ascii_xdigit_value(e[3]));
			e += 4;
		} else {
			*o++ = *e++;
		}
	}
	*o = '\0';
}

static void ler_discos(struct armazenamento *a) {
	char *saida = NULL;
	char *argv[] = { "lsblk", "-P", "-b", "-o", "NAME,PKNAME,LABEL,SIZE,FSTYPE,MOUNTPOINT,RM,HOTPLUG,TYPE,FSUSED", NULL };
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL) || !saida) {
		return;
	}
	char **linhas = g_strsplit(saida, "\n", -1);
	/* 1ª passada: de quem é removível (o disco inteiro e as partições dele) */
	GHashTable *removiveis = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GHashTable *do_sistema = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (int i = 0; linhas[i]; i++) {
		char nome[48], rm[8], hot[8], tipo[24], pk[48], ponto[256], fs[24];
		campo(linhas[i], "NAME", nome, sizeof(nome));
		campo(linhas[i], "PKNAME", pk, sizeof(pk));
		campo(linhas[i], "RM", rm, sizeof(rm));
		campo(linhas[i], "HOTPLUG", hot, sizeof(hot));
		campo(linhas[i], "TYPE", tipo, sizeof(tipo));
		campo(linhas[i], "MOUNTPOINT", ponto, sizeof(ponto));
		campo(linhas[i], "FSTYPE", fs, sizeof(fs));
		if (!nome[0]) {
			continue;
		}
		if (!strcmp(tipo, "disk") && (!strcmp(rm, "1") || !strcmp(hot, "1"))) {
			g_hash_table_add(removiveis, g_strdup(nome));
		}
		if (!strcmp(ponto, "/") || !strcmp(ponto, "/boot") || !strcmp(ponto, "/boot/efi") || !strcmp(ponto, "/home") || !strcmp(fs, "swap")) {
			g_hash_table_add(do_sistema, g_strdup(pk[0] ? pk : nome));
		}
	}
	for (int i = 0; linhas[i] && a->ndiscos < ARMAZ_MAX_DISCOS; i++) {
		char nome[48], pk[48], rotulo[96], tam[32], fs[24], ponto[256], tipo[24], usado[32];
		campo(linhas[i], "NAME", nome, sizeof(nome));
		campo(linhas[i], "TYPE", tipo, sizeof(tipo));
		campo(linhas[i], "PKNAME", pk, sizeof(pk));
		campo(linhas[i], "LABEL", rotulo, sizeof(rotulo));
		campo(linhas[i], "SIZE", tam, sizeof(tam));
		campo(linhas[i], "FSTYPE", fs, sizeof(fs));
		campo(linhas[i], "MOUNTPOINT", ponto, sizeof(ponto));
		campo(linhas[i], "FSUSED", usado, sizeof(usado));
		/* só partições (ou o disco inteiro quando não tem partição) com um sistema de arquivos de verdade */
		if (!nome[0] || !fs[0] || !strcmp(fs, "swap") || !strcmp(fs, "crypto_LUKS") || !strcmp(fs, "LVM2_member") ||
			(strcmp(tipo, "part") && strcmp(tipo, "disk") && strcmp(tipo, "crypt") && strcmp(tipo, "loop"))) {
			continue;
		}
		if (!strcmp(tipo, "loop")) {
			continue;
		}
		const char *pai = pk[0] ? pk : nome;
		struct disco *d = &a->discos[a->ndiscos++];
		memset(d, 0, sizeof(*d));
		destrancar(rotulo);
		destrancar(ponto);
		g_snprintf(d->dev, sizeof(d->dev), "/dev/%s", nome);
		g_snprintf(d->disco, sizeof(d->disco), "/dev/%s", pai);
		g_strlcpy(d->fs, fs, sizeof(d->fs));
		g_strlcpy(d->ponto, ponto, sizeof(d->ponto));
		d->tamanho = g_ascii_strtoll(tam, NULL, 10);
		d->usado = g_ascii_strtoll(usado, NULL, 10);
		d->montado = ponto[0] != '\0';
		d->removivel = g_hash_table_contains(removiveis, pai);
		d->do_sistema = g_hash_table_contains(do_sistema, pai) && !d->removivel;
		if (rotulo[0]) {
			g_strlcpy(d->nome, rotulo, sizeof(d->nome));
		} else {
			g_snprintf(d->nome, sizeof(d->nome), d->removivel ? "Pendrive (%s)" : "Disco (%s)", nome);
		}
		/* montado: o usado vem do statvfs quando o lsblk não disse */
		if (d->montado && d->usado == 0) {
			struct statvfs v;
			if (!statvfs(ponto, &v)) {
				d->usado = (int64_t)(v.f_blocks - v.f_bfree) * (int64_t)v.f_frsize;
			}
		}
	}
	g_hash_table_destroy(removiveis);
	g_hash_table_destroy(do_sistema);
	g_strfreev(linhas);
	g_free(saida);
}

void armazenamento_ler(struct armazenamento *a) {
	memset(a, 0, sizeof(*a));
	const char *raiz = g_getenv("LUMNA_ARMAZ_RAIZ");
	struct statvfs v;
	if (!statvfs(raiz && *raiz ? raiz : "/", &v)) {
		a->total = (int64_t)v.f_blocks * (int64_t)v.f_frsize;
		a->livre = (int64_t)v.f_bavail * (int64_t)v.f_frsize;
		a->usado = a->total - (int64_t)v.f_bfree * (int64_t)v.f_frsize;
	}
	char *l = pasta_lixeira(), *c = pasta_cache();
	a->lixeira = medir(l, false);
	a->temporarios = medir(c, false) + medir(pasta_tmp(), true);
	a->cache_pacman = medir(pasta_cache_pacman(), false);
	g_free(l);
	g_free(c);
	ler_discos(a);
}

/* ---- limpar ---- */

/* apaga o que há dentro (a pasta fica); so_do_dono: só o que é meu (o /tmp é de todo mundo) */
static int limpar_dentro(const char *pasta, bool so_do_dono, char *erro, size_t tam) {
	GDir *d = g_dir_open(pasta, 0, NULL);
	if (!d) {
		return 0; /* nada para limpar */
	}
	int falhas = 0;
	const char *nome;
	while ((nome = g_dir_read_name(d))) {
		char *c = g_build_filename(pasta, nome, NULL);
		struct stat st;
		if (!lstat(c, &st) && (!so_do_dono || st.st_uid == getuid())) {
			/* rm -rf pelo glib: recursivo e sem seguir links */
			char *argv[] = { "rm", "-rf", "--one-file-system", c, NULL };
			int estado = 0;
			if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, NULL,
					&estado, NULL) || estado != 0) {
				falhas++;
			}
		}
		g_free(c);
	}
	g_dir_close(d);
	if (falhas) {
		g_snprintf(erro, tam, "%d itens não puderam ser apagados (em uso?)", falhas);
		return -1;
	}
	return 0;
}

int armazenamento_esvaziar_lixeira(char *erro, size_t tam) {
	erro[0] = '\0';
	char *l = pasta_lixeira(), *f = g_build_filename(l, "files", NULL), *i = g_build_filename(l, "info", NULL);
	int r = limpar_dentro(f, false, erro, tam);
	int r2 = limpar_dentro(i, false, erro, tam);
	g_free(f);
	g_free(i);
	g_free(l);
	return r ? r : r2;
}

int armazenamento_limpar_temporarios(char *erro, size_t tam) {
	erro[0] = '\0';
	char *c = pasta_cache();
	int r = limpar_dentro(c, false, erro, tam);
	int r2 = limpar_dentro(pasta_tmp(), true, erro, tam);
	g_free(c);
	return r ? r : r2;
}

static int rodar(char **argv, char *erro, size_t tam) {
	char *err = NULL;
	int estado = 0;
	GError *e = NULL;
	erro[0] = '\0';
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &err, &estado, &e)) {
		g_strlcpy(erro, e ? e->message : "não consegui rodar", tam);
		g_clear_error(&e);
		g_free(err);
		return -1;
	}
	int codigo = g_spawn_check_wait_status(estado, NULL) ? 0 : (WIFEXITED(estado) ? WEXITSTATUS(estado) : -1);
	if (codigo && err) {
		g_strlcpy(erro, g_strstrip(err), tam);
	}
	g_free(err);
	return codigo;
}

int armazenamento_limpar_cache_pacman(char *erro, size_t tam) {
	/* só o que não está instalado mais (pacman -Sc): as versões em uso ficam, para poder voltar */
	const char *sem = g_getenv("LUMNA_SEM_PKEXEC");
	char *argv_pk[] = { "pkexec", "pacman", "-Sc", "--noconfirm", NULL };
	char *argv_sem[] = { "pacman", "-Sc", "--noconfirm", NULL };
	return rodar(sem && *sem ? argv_sem : argv_pk, erro, tam);
}

/* ---- discos ---- */

int armazenamento_montar(const struct disco *d, char *erro, size_t tam) {
	char *argv[] = { "udisksctl", "mount", "-b", (char *)d->dev, NULL };
	return rodar(argv, erro, tam);
}

int armazenamento_ejetar(const struct disco *d, char *erro, size_t tam) {
	if (d->do_sistema) {
		g_strlcpy(erro, "É o disco do sistema: não dá para ejetar.", tam);
		return -1;
	}
	if (d->montado) {
		char *um[] = { "udisksctl", "unmount", "-b", (char *)d->dev, NULL };
		int r = rodar(um, erro, tam);
		if (r) {
			return r;
		}
	}
	if (d->removivel) {
		char *off[] = { "udisksctl", "power-off", "-b", (char *)d->disco, NULL };
		return rodar(off, erro, tam);
	}
	return 0;
}
