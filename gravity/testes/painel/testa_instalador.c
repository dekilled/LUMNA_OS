/* o instalador (servicos/instalador.c): discos, fusos, validações e a execução com o lumna-install de mentira */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "instalador.h"

static int falhas;
#define QUER(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

static const char *LSBLK =
	"{\"blockdevices\":["
	"{\"path\":\"/dev/nvme0n1\",\"model\":\"Samsung SSD 980  \",\"size\":500107862016,\"tran\":\"nvme\",\"type\":\"disk\",\"rm\":false,\"label\":null,\"fstype\":null,\"mountpoints\":[null],"
	"\"children\":[{\"path\":\"/dev/nvme0n1p1\",\"type\":\"part\",\"label\":\"EFI\",\"fstype\":\"vfat\",\"mountpoints\":[\"/boot\"]},"
	"{\"path\":\"/dev/nvme0n1p2\",\"type\":\"part\",\"label\":null,\"fstype\":\"ext4\",\"mountpoints\":[\"/\"]}]},"
	"{\"path\":\"/dev/sda\",\"model\":\"USB DISK 3.0\",\"size\":16000000000,\"tran\":\"usb\",\"type\":\"disk\",\"rm\":true,\"label\":null,\"fstype\":null,\"mountpoints\":[null],"
	"\"children\":[{\"path\":\"/dev/sda1\",\"type\":\"part\",\"label\":\"ARCHISO\",\"fstype\":\"iso9660\",\"mountpoints\":[\"/run/archiso/bootmnt\"]}]},"
	"{\"path\":\"/dev/sdb\",\"model\":null,\"size\":1000204886016,\"tran\":\"sata\",\"type\":\"disk\",\"rm\":false,\"label\":null,\"fstype\":null,\"mountpoints\":[null]},"
	"{\"path\":\"/dev/zram0\",\"model\":null,\"size\":4000000000,\"tran\":null,\"type\":\"disk\",\"mountpoints\":[\"[SWAP]\"]},"
	"{\"path\":\"/dev/loop0\",\"model\":null,\"size\":900000000,\"tran\":null,\"type\":\"loop\",\"mountpoints\":[\"/run/archiso/airootfs\"]},"
	"{\"path\":\"/dev/sr0\",\"model\":\"DVD\",\"size\":1,\"tran\":\"sata\",\"type\":\"rom\",\"mountpoints\":[null]}]}";

static void espera_fim(struct inst_estado *e) {
	for (int i = 0; i < 100; i++) {
		instalador_estado(e);
		if (e->terminou) {
			return;
		}
		g_usleep(100000);
	}
}

static char *le(const char *dir, const char *nome) {
	char *c = g_build_filename(dir, nome, NULL), *t = NULL;
	if (!g_file_get_contents(c, &t, NULL, NULL)) {
		t = g_strdup("");
	}
	g_free(c);
	return g_strstrip(t);
}

int main(int argc, char **argv) {
	struct inst_disco d[8];
	int n = instalador_discos_json(LSBLK, d, 8);
	QUER(n == 2, "só o nvme e o sdb (o pendrive do live, zram, loop e CD ficam de fora)");
	QUER(!strcmp(d[0].caminho, "/dev/nvme0n1") && !strcmp(d[0].modelo, "Samsung SSD 980") && d[0].tamanho == 500107862016LL, "o primeiro disco");
	QUER(!strcmp(d[0].transporte, "nvme") && !strcmp(d[0].conteudo, "EFI (vfat) · ext4"), "o conteúdo do disco com dados");
	QUER(!strcmp(d[1].caminho, "/dev/sdb") && !strcmp(d[1].modelo, "Disco") && !strcmp(d[1].conteudo, "Vazio"), "disco sem modelo nem partições");
	QUER(instalador_discos_json("lixo", d, 8) == 0, "JSON inválido: nenhum disco");

	char *dir = g_dir_make_tmp("lumna-instalador-XXXXXX", NULL);
	char *tab = g_build_filename(dir, "zone1970.tab", NULL);
	g_file_set_contents(tab, "# comentário\nBR\t-2332-04637\tAmerica/Sao_Paulo\tBrasil\nPT,ES\t+3843-00908\tEurope/Lisbon\nBR\t-0330-04012\tAmerica/Fortaleza\nUS\t+40\tAmerica/Sao_Paulo\nXX\t0\tUTC\n", -1, NULL);
	g_setenv("LUMNA_ZONEINFO", dir, TRUE);
	char **fusos;
	int nf = instalador_fusos(&fusos);
	QUER(nf == 3 && !strcmp(fusos[0], "America/Fortaleza") && !strcmp(fusos[1], "America/Sao_Paulo") && !strcmp(fusos[2], "Europe/Lisbon"), "fusos em ordem, sem repetir e sem os que não têm /");
	QUER(instalador_fuso_existe(fusos, nf, "Europe/Lisbon") && !instalador_fuso_existe(fusos, nf, "Marte/Olimpo"), "fuso existe");
	g_strfreev(fusos);
	g_setenv("LUMNA_ZONEINFO", "/nao/existe", TRUE);
	nf = instalador_fusos(&fusos);
	QUER(nf == 1 && !strcmp(fusos[0], "America/Sao_Paulo"), "sem lista: pelo menos São Paulo");
	g_strfreev(fusos);

	char u[64];
	instalador_sugerir_usuario("Luan Éric", u, sizeof(u));
	QUER(!strcmp(u, "luan"), "Luan Éric → luan");
	instalador_sugerir_usuario("  José da Silva", u, sizeof(u));
	QUER(!strcmp(u, "jose"), "José da Silva → jose");
	instalador_sugerir_usuario("Ça va", u, sizeof(u));
	QUER(!strcmp(u, "ca"), "Ça → ca");
	instalador_sugerir_usuario("123 Maria", u, sizeof(u));
	QUER(!*u, "só números não dá nome (começa por letra)");
	instalador_sugerir_usuario("Éric_Dev-2 x", u, sizeof(u));
	QUER(!strcmp(u, "eric_dev-2"), "_ e - ficam");
	QUER(!*instalador_erro_usuario("") && !*instalador_erro_usuario("maria") && !*instalador_erro_usuario("_a-1"), "nomes bons");
	QUER(*instalador_erro_usuario("Maria") && *instalador_erro_usuario("1abc") && *instalador_erro_usuario("a b") && *instalador_erro_usuario("root")
		&& *instalador_erro_usuario("greeter") && *instalador_erro_usuario("lumna-live"), "nomes ruins e reservados");
	char longo[64];
	memset(longo, 'a', 40);
	longo[40] = '\0';
	QUER(*instalador_erro_usuario(longo), "nome com mais de 32");
	QUER(!*instalador_erro_senha("", "") && *instalador_erro_senha("abc", "") && !*instalador_erro_senha("abcd", "") && *instalador_erro_senha("abcd", "abce")
		&& !*instalador_erro_senha("abcd", "abcd"), "senha");
	QUER(!*instalador_erro_host("lumna") && !*instalador_erro_host("pc-1") && *instalador_erro_host("-pc") && *instalador_erro_host("a b") && *instalador_erro_host(""), "nome do computador");
	int pct;
	const char *txt;
	QUER(instalador_andamento("@@ 42 Copiando o sistema", &pct, &txt) && pct == 42 && !strcmp(txt, "Copiando o sistema"), "andamento");
	QUER(!instalador_andamento("copiando", &pct, &txt) && !instalador_andamento("@@ x y", &pct, &txt), "não é andamento");

	/* a execução, com o lumna-install de mentira (argv[1]) */
	g_setenv("LUMNA_INSTALADOR_CMD", argv[1], TRUE);
	g_setenv("FALSO_DIR", dir, TRUE);
	struct inst_plano p = { .disco = "/dev/sdb", .usuario = "maria", .nome = "Maria da Silva", .host = "lumna", .fuso = "America/Sao_Paulo", .teclado = "br abnt2", .idioma = "pt_BR.UTF-8" };
	g_setenv("FALSO_MODO", "ok", TRUE);
	QUER(instalador_iniciar(&p, "s3nh@ com espaço"), "começou");
	QUER(!instalador_iniciar(&p, "x"), "não começa duas vezes");
	struct inst_estado e;
	espera_fim(&e);
	QUER(e.terminou && e.ok && e.percentual == 100, "terminou bem");
	char *args = le(dir, "args"), *senha = le(dir, "senha");
	QUER(!strcmp(args, "--disk /dev/sdb --user maria --name Maria da Silva --hostname lumna --timezone America/Sao_Paulo --keyboard br abnt2 --locale pt_BR.UTF-8"), "os argumentos");
	QUER(!strcmp(senha, "s3nh@ com espaço"), "a senha pela entrada padrão");
	QUER(!strstr(args, "s3nh"), "a senha nunca vai na linha de comando");
	g_free(args);
	g_free(senha);
	g_setenv("FALSO_MODO", "nega", TRUE);
	QUER(instalador_iniciar(&p, "x"), "recomeça depois de terminar");
	espera_fim(&e);
	QUER(e.terminou && !e.ok && strstr(e.erro, "senha de administrador"), "126 sem erro: autorização negada");
	g_setenv("FALSO_MODO", "erro", TRUE);
	QUER(instalador_iniciar(&p, "x"), "recomeça");
	espera_fim(&e);
	QUER(e.terminou && !e.ok && strstr(e.erro, "as partições novas não apareceram") && e.percentual == 10, "o erro do instalador chega à tela");
	if (!falhas) {
		puts("instalador ok");
	}
	return falhas != 0;
}
