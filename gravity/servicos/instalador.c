#define _DEFAULT_SOURCE 1
#include "instalador.h"

#include <ctype.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include "json.h"

/* ---- discos ---- */

int instalador_discos_json(const char *texto, struct inst_disco *lista, int max) {
	struct json *raiz = json_ler(texto);
	const struct json *discos = json_chave(raiz, "blockdevices");
	int n = 0;
	for (int i = 0; discos && i < discos->nfilhos && n < max; i++) {
		const struct json *d = json_item(discos, i);
		const char *caminho = json_chave_texto(d, "path");
		if (strcmp(json_chave_texto(d, "type"), "disk") || !*caminho || g_str_has_prefix(caminho, "/dev/zram") || g_str_has_prefix(caminho, "/dev/loop")
			|| g_str_has_prefix(caminho, "/dev/sr")) {
			continue;
		}
		const struct json *filhos = json_chave(d, "children");
		bool do_live = false;
		GString *conteudo = g_string_new(NULL);
		for (int k = -1; k < (filhos ? filhos->nfilhos : 0); k++) {
			const struct json *parte = k < 0 ? d : json_item(filhos, k);
			const struct json *pontos = json_chave(parte, "mountpoints");
			for (int m = 0; pontos && m < pontos->nfilhos; m++) {
				const char *ponto = json_texto(json_item(pontos, m));
				do_live |= ponto && g_str_has_prefix(ponto, "/run/archiso");
			}
			const char *fs = json_chave_texto(parte, "fstype"), *rotulo = json_chave_texto(parte, "label");
			if (*fs) {
				char *item = *rotulo ? g_strdup_printf("%s (%s)", rotulo, fs) : g_strdup(fs);
				g_string_append_printf(conteudo, "%s%s", conteudo->len ? " · " : "", item);
				g_free(item);
			}
		}
		if (!do_live) {
			struct inst_disco *x = &lista[n++];
			memset(x, 0, sizeof(*x));
			g_strlcpy(x->caminho, caminho, sizeof(x->caminho));
			const char *modelo = json_chave_texto(d, "model");
			char *m = g_strstrip(g_strdup(modelo));
			g_strlcpy(x->modelo, *m ? m : "Disco", sizeof(x->modelo));
			g_free(m);
			g_strlcpy(x->transporte, json_chave_texto(d, "tran"), sizeof(x->transporte));
			g_strlcpy(x->conteudo, conteudo->len ? conteudo->str : "Vazio", sizeof(x->conteudo));
			x->tamanho = (int64_t)json_numero(json_chave(d, "size"), 0);
		}
		g_string_free(conteudo, TRUE);
	}
	json_liberar(raiz);
	return n;
}

int instalador_discos(struct inst_disco *lista, int max) {
	char *saida = NULL;
	char *argv[] = { "lsblk", "-J", "-b", "-o", "PATH,MODEL,SIZE,TRAN,TYPE,RM,LABEL,FSTYPE,MOUNTPOINTS", NULL };
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL) || !saida) {
		return 0;
	}
	int n = instalador_discos_json(saida, lista, max);
	g_free(saida);
	return n;
}

/* ---- fusos ---- */

static int comparar(gconstpointer a, gconstpointer b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}

int instalador_fusos(char ***lista) {
	const char *pasta = g_getenv("LUMNA_ZONEINFO");
	char *tab = g_build_filename(pasta && *pasta ? pasta : "/usr/share/zoneinfo", "zone1970.tab", NULL), *texto = NULL;
	GPtrArray *fusos = g_ptr_array_new();
	if (g_file_get_contents(tab, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			if ((*l)[0] == '#') {
				continue;
			}
			char **c = g_strsplit(*l, "\t", -1);
			if (g_strv_length(c) >= 3 && strchr(c[2], '/')) {
				g_ptr_array_add(fusos, g_strdup(c[2]));
			}
			g_strfreev(c);
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(tab);
	if (!fusos->len) {
		g_ptr_array_add(fusos, g_strdup("America/Sao_Paulo"));
	}
	g_ptr_array_sort(fusos, comparar);
	/* sem repetir */
	int n = 0;
	for (guint i = 0; i < fusos->len; i++) {
		if (n && !strcmp(fusos->pdata[n - 1], fusos->pdata[i])) {
			g_free(fusos->pdata[i]);
		} else {
			fusos->pdata[n++] = fusos->pdata[i];
		}
	}
	*lista = g_renew(char *, (char **)g_ptr_array_free(fusos, FALSE), n + 1);
	(*lista)[n] = NULL;
	return n;
}

bool instalador_fuso_existe(char *const *lista, int n, const char *fuso) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(lista[i], fuso)) {
			return true;
		}
	}
	return false;
}

/* ---- o que a pessoa digitou ---- */

void instalador_sugerir_usuario(const char *nome, char *saida, size_t n) {
	/* sem acento: o NFD separa a letra do acento e o acento (U+0300–U+036F) é descartado */
	char *d = g_utf8_normalize(nome, -1, G_NORMALIZE_NFD), *p = d;
	size_t k = 0;
	bool primeira_letra = false, na_palavra = false;
	while (*p && k + 1 < n && k < 32) {
		gunichar c = g_utf8_get_char(p);
		p = g_utf8_next_char(p);
		if (c >= 0x300 && c <= 0x36f) {
			continue;
		}
		if (c == ' ' || c == '\t') {
			if (na_palavra) {
				break; /* só a primeira palavra */
			}
			continue;
		}
		na_palavra = true;
		c = g_unichar_tolower(c);
		bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
		if (!ok) {
			continue;
		}
		if (!primeira_letra) { /* tem de começar por letra ou _ */
			if (!((c >= 'a' && c <= 'z') || c == '_')) {
				continue;
			}
			primeira_letra = true;
		}
		saida[k++] = (char)c;
	}
	saida[k] = '\0';
	g_free(d);
}

const char *instalador_erro_usuario(const char *u) {
	if (!*u) {
		return "";
	}
	size_t n = strlen(u);
	bool ok = n <= 32 && (islower((unsigned char)u[0]) || u[0] == '_');
	for (size_t i = 0; ok && i < n; i++) {
		ok = islower((unsigned char)u[i]) || isdigit((unsigned char)u[i]) || u[i] == '-' || u[i] == '_';
	}
	if (!ok) {
		return "Use letras minúsculas, números, - e _ (começando por letra).";
	}
	if (!strcmp(u, "root") || !strcmp(u, "greeter") || !strcmp(u, "lumna-live")) {
		return "Esse nome é reservado pelo sistema.";
	}
	return "";
}

const char *instalador_erro_senha(const char *senha, const char *confirma) {
	if (!*senha) {
		return "";
	}
	if (strlen(senha) < 4) {
		return "Use pelo menos 4 caracteres.";
	}
	if (*confirma && strcmp(senha, confirma)) {
		return "As senhas não são iguais.";
	}
	return "";
}

const char *instalador_erro_host(const char *h) {
	size_t n = strlen(h);
	bool ok = n >= 1 && n <= 63 && isalnum((unsigned char)h[0]);
	for (size_t i = 0; ok && i < n; i++) {
		ok = isalnum((unsigned char)h[i]) || h[i] == '-';
	}
	return ok ? "" : "Só letras, números e -.";
}

const char *instalador_boot(void) {
	const char *forcado = g_getenv("LUMNA_FIRMWARE");
	if (forcado && *forcado) {
		return !g_ascii_strcasecmp(forcado, "bios") ? "BIOS" : "UEFI";
	}
	return g_file_test("/sys/firmware/efi", G_FILE_TEST_IS_DIR) ? "UEFI" : "BIOS";
}

bool instalador_andamento(const char *linha, int *percentual, const char **texto) {
	if (strncmp(linha, "@@ ", 3)) {
		return false;
	}
	char *fim;
	long v = strtol(linha + 3, &fim, 10);
	if (fim == linha + 3 || *fim != ' ') {
		return false;
	}
	*percentual = (int)v;
	*texto = fim + 1;
	return true;
}

/* ---- a execução ---- */

static GMutex trinco;
static struct inst_estado estado;
static int acorda = -1;

static void acordar(void) {
	uint64_t um = 1;
	if (acorda >= 0 && write(acorda, &um, sizeof(um)) < 0) {
	}
}

int instalador_fd(void) {
	if (acorda < 0) {
		acorda = eventfd(0, EFD_CLOEXEC);
	}
	return acorda;
}

void instalador_estado(struct inst_estado *e) {
	g_mutex_lock(&trinco);
	*e = estado;
	g_mutex_unlock(&trinco);
}

struct execucao {
	struct inst_plano plano;
	char *senha;
};

/* o que o instalador escreve de erro, lido à parte: se ninguém lesse, um pipe cheio travaria o instalador */
static gpointer ler_erros(gpointer dados) {
	gpointer *par = dados;
	FILE *f = fdopen(GPOINTER_TO_INT(par[0]), "r");
	GString *erros = par[1];
	char buf[1024];
	while (f && fgets(buf, sizeof(buf), f)) {
		if (erros->len < 64 * 1024) {
			g_string_append(erros, buf);
		} else { /* só o final importa: descarta o começo */
			g_string_erase(erros, 0, 32 * 1024);
			g_string_append(erros, buf);
		}
	}
	if (f) {
		fclose(f);
	}
	return NULL;
}

static gpointer correr(gpointer dados) {
	struct execucao *x = dados;
	const char *cmd = g_getenv("LUMNA_INSTALADOR_CMD");
	GPtrArray *argv = g_ptr_array_new();
	if (cmd && *cmd) {
		g_ptr_array_add(argv, (gpointer)cmd);
	} else {
		g_ptr_array_add(argv, "pkexec");
		g_ptr_array_add(argv, "/usr/lib/lumna/lumna-install");
	}
	const struct inst_plano *p = &x->plano;
	const char *args[] = { "--disk", p->disco, "--user", p->usuario, "--name", p->nome, "--hostname", p->host, "--timezone", p->fuso, "--keyboard", p->teclado,
		"--locale", p->idioma };
	for (size_t i = 0; i < G_N_ELEMENTS(args); i++) {
		g_ptr_array_add(argv, (gpointer)args[i]);
	}
	g_ptr_array_add(argv, NULL);
	GPid pid;
	int entrada, saida, erro_fd;
	GError *e = NULL;
	if (!g_spawn_async_with_pipes(NULL, (char **)argv->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &entrada, &saida, &erro_fd, &e)) {
		g_mutex_lock(&trinco);
		estado.rodando = false;
		estado.terminou = true;
		g_snprintf(estado.erro, sizeof(estado.erro), "Não consegui começar: %s", e->message);
		g_mutex_unlock(&trinco);
		g_error_free(e);
		acordar();
		goto fim;
	}
	char *linha = g_strdup_printf("%s\n", x->senha);
	if (write(entrada, linha, strlen(linha)) < 0) {
	}
	explicit_bzero(linha, strlen(linha));
	g_free(linha);
	close(entrada);
	/* o que o instalador escreve de erro vai para uma linha à parte, para o pipe não encher */
	GString *erros = g_string_new(NULL);
	gpointer par[2] = { GINT_TO_POINTER(erro_fd), erros };
	GThread *leitor = g_thread_new("instalador-erros", ler_erros, par);
	FILE *fs = fdopen(saida, "r");
	char buf[1024];
	while (fs && fgets(buf, sizeof(buf), fs)) {
		buf[strcspn(buf, "\r\n")] = '\0';
		int pct;
		const char *texto;
		if (instalador_andamento(buf, &pct, &texto)) {
			g_mutex_lock(&trinco);
			estado.percentual = pct;
			g_strlcpy(estado.texto, texto, sizeof(estado.texto));
			g_mutex_unlock(&trinco);
			acordar();
		}
	}
	if (fs) {
		fclose(fs);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	g_spawn_close_pid(pid);
	g_thread_join(leitor);
	g_mutex_lock(&trinco);
	estado.rodando = false;
	estado.terminou = true;
	estado.ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
	if (estado.ok) {
		estado.percentual = 100;
		g_strlcpy(estado.texto, "Pronto", sizeof(estado.texto));
	} else {
		g_strstrip(erros->str);
		if (WIFEXITED(st) && (WEXITSTATUS(st) == 126 || WEXITSTATUS(st) == 127) && !erros->str[0]) {
			g_strlcpy(estado.erro, "A senha de administrador não foi aceita (ou a janela foi cancelada).", sizeof(estado.erro));
		} else if (erros->str[0]) {
			/* só o final: as últimas linhas dizem o que deu errado */
			const char *fim_erro = erros->str + (erros->len > 360 ? erros->len - 360 : 0);
			g_strlcpy(estado.erro, fim_erro, sizeof(estado.erro));
		} else {
			g_snprintf(estado.erro, sizeof(estado.erro), "O instalador parou (código %d).", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
		}
	}
	g_mutex_unlock(&trinco);
	g_string_free(erros, TRUE);
	acordar();
fim:
	explicit_bzero(x->senha, strlen(x->senha));
	g_free(x->senha);
	g_free(x);
	g_ptr_array_free(argv, TRUE);
	return NULL;
}

bool instalador_iniciar(const struct inst_plano *plano, const char *senha) {
	g_mutex_lock(&trinco);
	if (estado.rodando) {
		g_mutex_unlock(&trinco);
		return false;
	}
	memset(&estado, 0, sizeof(estado));
	estado.rodando = true;
	g_strlcpy(estado.texto, "Começando…", sizeof(estado.texto));
	g_mutex_unlock(&trinco);
	struct execucao *x = g_new0(struct execucao, 1);
	x->plano = *plano;
	x->senha = g_strdup(senha);
	g_thread_unref(g_thread_new("instalador", correr, x));
	return true;
}
