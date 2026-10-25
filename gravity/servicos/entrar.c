/* Conferir a senha e o PIN (entrar.h). */
#define _DEFAULT_SOURCE
#include "entrar.h"

#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RODADAS 120000

static char *caminho_pin(void) {
	const char *config = getenv("XDG_CONFIG_HOME");
	return config && *config ? g_build_filename(config, "lumna", "mobile-pin", NULL)
				 : g_build_filename(g_get_home_dir(), ".config", "lumna", "mobile-pin", NULL);
}

/* sha256("sal:pin"), depois sha256 do resultado RODADAS vezes (os bytes), em hex */
static void hash_pin(const char *sal, const char *pin, char saida[65]) {
	guint8 h[32];
	gsize n = sizeof(h);
	char *texto = g_strdup_printf("%s:%s", sal, pin);
	GChecksum *c = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(c, (const guchar *)texto, (gssize)strlen(texto));
	g_checksum_get_digest(c, h, &n);
	g_free(texto);
	for (int i = 0; i < RODADAS; i++) {
		g_checksum_reset(c);
		g_checksum_update(c, h, 32);
		n = sizeof(h);
		g_checksum_get_digest(c, h, &n);
	}
	g_checksum_free(c);
	for (int i = 0; i < 32; i++) {
		sprintf(saida + i * 2, "%02x", h[i]);
	}
}

static bool ler_pin(int *tamanho, char sal[64], char hash[80]) {
	char *caminho = caminho_pin();
	char *texto = NULL;
	bool ok = g_file_get_contents(caminho, &texto, NULL, NULL);
	g_free(caminho);
	if (!ok) {
		return false;
	}
	ok = sscanf(texto, "%d:%63[^:]:%79s", tamanho, sal, hash) == 3;
	g_free(texto);
	return ok;
}

int entrar_pin_tamanho(void) {
	int t;
	char sal[64], hash[80];
	return ler_pin(&t, sal, hash) ? t : 0;
}

bool entrar_pin_confere(const char *pin) {
	int t;
	char sal[64], hash[80], conta[65];
	if (!ler_pin(&t, sal, hash) || (int)strlen(pin) != t) {
		return false;
	}
	hash_pin(sal, pin, conta);
	return !strcmp(conta, hash);
}

bool entrar_senha_confere(const char *senha) {
	static const char *const ajudantes[] = { "/usr/bin/unix_chkpwd", "/usr/sbin/unix_chkpwd", "/sbin/unix_chkpwd" };
	const char *ajudante = "unix_chkpwd";
	for (size_t i = 0; i < sizeof(ajudantes) / sizeof(ajudantes[0]); i++) {
		if (access(ajudantes[i], X_OK) == 0) {
			ajudante = ajudantes[i];
			break;
		}
	}
	const char *usuario = g_get_user_name();
	int cano[2];
	if (pipe(cano) < 0) {
		return false;
	}
	pid_t p = fork();
	if (p == 0) {
		dup2(cano[0], 0);
		close(cano[0]);
		close(cano[1]);
		int nulo = open("/dev/null", O_WRONLY);
		if (nulo >= 0) {
			dup2(nulo, 1);
			dup2(nulo, 2);
		}
		execl(ajudante, ajudante, usuario, "nullok", (char *)NULL);
		_exit(127);
	}
	close(cano[0]);
	if (p < 0) {
		close(cano[1]);
		return false;
	}
	/* a senha terminada em zero */
	if (write(cano[1], senha, strlen(senha) + 1) < 0) {
	}
	close(cano[1]);
	int estado;
	waitpid(p, &estado, 0);
	return WIFEXITED(estado) && WEXITSTATUS(estado) == 0;
}

static bool pin_valido(const char *pin) {
	size_t n = strlen(pin);
	if (n < 4 || n > 8) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		if (pin[i] < '0' || pin[i] > '9') {
			return false;
		}
	}
	return true;
}

const char *entrar_pin_definir(const char *senha, const char *pin) {
	if (!pin_valido(pin)) {
		return "O PIN tem de ter de 4 a 8 números";
	}
	if (!entrar_senha_confere(senha)) {
		return "Senha errada";
	}
	guint8 aleatorio[16];
	FILE *f = fopen("/dev/urandom", "r");
	if (!f || fread(aleatorio, 1, sizeof(aleatorio), f) != sizeof(aleatorio)) {
		if (f) {
			fclose(f);
		}
		return "Sem /dev/urandom";
	}
	fclose(f);
	char sal[33], hash[65];
	for (int i = 0; i < 16; i++) {
		sprintf(sal + i * 2, "%02x", aleatorio[i]);
	}
	hash_pin(sal, pin, hash);
	char *caminho = caminho_pin();
	char *pasta = g_path_get_dirname(caminho);
	g_mkdir_with_parents(pasta, 0700);
	g_free(pasta);
	int fd = open(caminho, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	g_free(caminho);
	if (fd < 0) {
		return "Não deu para gravar o PIN";
	}
	char linha[128];
	int n = snprintf(linha, sizeof(linha), "%zu:%s:%s\n", strlen(pin), sal, hash);
	bool ok = write(fd, linha, (size_t)n) == n;
	close(fd);
	return ok ? NULL : "Não deu para gravar o PIN";
}

const char *entrar_pin_tirar(const char *senha) {
	if (!entrar_senha_confere(senha)) {
		return "Senha errada";
	}
	char *caminho = caminho_pin();
	unlink(caminho);
	g_free(caminho);
	return NULL;
}

static int erros;
static time_t espera_ate;

void entrar_errou(void) {
	erros++;
	if (erros % 5 == 0) {
		int rodada = erros / 5 - 1;
		int espera = 30 << (rodada < 5 ? rodada : 5);
		espera_ate = time(NULL) + (espera < 600 ? espera : 600);
	}
}

void entrar_acertou(void) {
	erros = 0;
	espera_ate = 0;
}

int entrar_espera(void) {
	time_t agora = time(NULL);
	return espera_ate > agora ? (int)(espera_ate - agora) : 0;
}
