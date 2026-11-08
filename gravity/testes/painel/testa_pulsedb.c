/* A memória do Pulse (servicos/pulsedb.c): o esquema e as migrações, o que se grava e o que se esconde, o contexto da Emma, a busca e o apagar. */
#define _GNU_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pulsedb.h"

static int falhas;
#define CHECA(cond, ...) do { if (!(cond)) { fprintf(stderr, "FALHOU (%s:%d): ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); falhas++; } } while (0)

static void oculta(const char *entrada, const char *esperado) {
	char *r = pulse_ocultar_segredos(entrada);
	CHECA(!strcmp(r, esperado), "ocultar '%s': saiu '%s', esperava '%s'", entrada, r, esperado);
	g_free(r);
}

static void teste_segredos(void) {
	oculta("ls -la --color=auto", "ls -la --color=auto");
	oculta("git status", "git status");
	oculta("curl -H \"Authorization: Bearer abc123xyz\" https://api.exemplo.com/x", "curl -H \"Authorization: Bearer ***\" https://api.exemplo.com/x");
	oculta("mysql -u root -pSuperSecreta banco", "mysql -u root -p*** banco");
	oculta("export API_TOKEN=abc123", "export API_TOKEN=***");
	oculta("DB_PASSWORD='com espaco' ./rodar", "DB_PASSWORD=*** ./rodar");
	oculta("git clone https://maria:pw123@github.com/x/y.git", "git clone https://maria:***@github.com/x/y.git");
	oculta("tool --password=hunter2 --user bob", "tool --password=*** --user bob");
	oculta("tool --password hunter2 --user bob", "tool --password *** --user bob");
	oculta("openssl enc -pass pass:xyz -in a", "openssl enc -pass *** -in a");
	oculta("echo sk-abcdefghijklmnopqr1234", "echo ***");
	oculta("echo ghp_abcdefghijklmnopqrstuvwxyz0123456789", "echo ***");
	oculta("curl -H 'X-Api-Key: segredo' x", "curl -H 'X-Api-Key: ***' x");
	oculta("ssh-keygen -N '' -f ~/.ssh/id", "ssh-keygen -N '' -f ~/.ssh/id");
}

static void teste_banco(void) {
	char *dir = g_dir_make_tmp("pulsedb-XXXXXX", NULL);
	char *arq = g_build_filename(dir, "sub", "pulse.db", NULL);
	char *erro = NULL;
	pulsedb *db = pulsedb_abrir(arq, &erro);
	CHECA(db != NULL, "abrir: %s", erro ? erro : "(sem erro)");
	if (!db) {
		return;
	}
	CHECA(pulsedb_versao(db) == pulsedb_versao_atual(), "a versão do banco novo: %d", pulsedb_versao(db));
	GStatBuf sb;
	g_stat(arq, &sb);
	CHECA((sb.st_mode & 0777) == 0600, "o arquivo do banco devia ser 600: %o", sb.st_mode & 0777);
	char *pasta = g_path_get_dirname(arq);
	g_stat(pasta, &sb);
	CHECA((sb.st_mode & 0777) == 0700, "a pasta do banco devia ser 700: %o", sb.st_mode & 0777);
	g_free(pasta);

	int64_t s1 = pulsedb_sessao_iniciar(db, "/home/eric");
	CHECA(s1 > 0, "sessão: %lld", (long long)s1);
	int64_t c1 = pulsedb_comando_gravar(db, s1, "/home/eric", "ls -la", 0, "total 0");
	int64_t c2 = pulsedb_comando_gravar(db, s1, "/home/eric/proj", "make", 2, "gcc x.c\nx.c:3: erro: 'y' não declarado\nmake: *** [all] Erro 1");
	int64_t c3 = pulsedb_comando_gravar(db, s1, "/home/eric/proj", "curl -H 'Authorization: Bearer ABC' http://x", 0, "ok");
	CHECA(c1 > 0 && c2 > c1 && c3 > c2, "ids dos comandos: %lld %lld %lld", (long long)c1, (long long)c2, (long long)c3);
	CHECA(pulsedb_comando_gravar(db, s1, "/", "   ", 0, "") == 0, "um comando vazio não se grava");
	CHECA(pulsedb_total_comandos(db) == 3, "total: %d", pulsedb_total_comandos(db));
	int64_t i1 = pulsedb_interacao_gravar(db, s1, c2, "correcao", "make falhou com 2. Saída: ... token=ZZZ", "");
	pulsedb_interacao_resposta(db, i1, "Declare 'y' em x.c");
	CHECA(i1 > 0, "interação: %lld", (long long)i1);

	char *ult = pulsedb_ultimos_comandos(db, s1, 2);
	CHECA(strstr(ult, "make  (código 2)") && !strstr(ult, "ls -la"), "os 2 últimos comandos: '%s'", ult);
	CHECA(strstr(ult, "Bearer ***") && !strstr(ult, "ABC"), "o segredo do curl não pode ficar: '%s'", ult);
	g_free(ult);

	/* uma segunda sessão (a de agora): o contexto traz a anterior com o erro, e a de agora */
	pulsedb_sessao_encerrar(db, s1);
	int64_t s2 = pulsedb_sessao_iniciar(db, "/tmp");
	pulsedb_comando_gravar(db, s2, "/tmp", "pwd", 0, "/tmp");
	char *ctx = pulsedb_contexto(db, s2, 5);
	CHECA(strstr(ctx, "Sessão de agora") && strstr(ctx, "pwd"), "o contexto devia ter a sessão de agora: '%s'", ctx);
	CHECA(strstr(ctx, "Sessão anterior") && strstr(ctx, "make  -> 2"), "o contexto devia ter a sessão anterior: '%s'", ctx);
	CHECA(strstr(ctx, "erro: make: *** [all] Erro 1"), "o comando que falhou leva a linha do erro: '%s'", ctx);
	CHECA(!strstr(ctx, "ABC") && !strstr(ctx, "ZZZ"), "nenhum segredo no contexto: '%s'", ctx);
	g_free(ctx);
	ctx = pulsedb_contexto(db, s2, 0);
	CHECA(!strstr(ctx, "Sessão anterior"), "com 0 sessões não vem a anterior: '%s'", ctx);
	g_free(ctx);

	/* a busca funda, com % e _ como texto */
	char *b = pulsedb_buscar(db, "não declarado", 5);
	CHECA(strstr(b, "make") != NULL, "a busca pela saída: '%s'", b);
	g_free(b);
	b = pulsedb_buscar(db, "%", 5);
	CHECA(b[0] == '\0', "o %% da busca vale como texto, não como curinga: '%s'", b);
	g_free(b);

	/* podar: nada antigo, nada some; com 0 dias a sessão já encerrada sem comandos e os comandos velhos somem */
	pulsedb_podar(db, 365);
	CHECA(pulsedb_total_comandos(db) == 4, "podar 365 dias não pode apagar o de agora: %d", pulsedb_total_comandos(db));
	pulsedb_fechar(db);

	/* reabrir não repete a migração, e mantém os dados */
	db = pulsedb_abrir(arq, &erro);
	CHECA(db && pulsedb_total_comandos(db) == 4, "reabrir mantém os dados");
	pulsedb_apagar_tudo(db);
	CHECA(pulsedb_total_comandos(db) == 0, "apagar tudo: %d", pulsedb_total_comandos(db));
	char *ctx2 = pulsedb_contexto(db, 0, 5);
	CHECA(ctx2[0] == '\0', "sem histórico o contexto é vazio: '%s'", ctx2);
	g_free(ctx2);
	pulsedb_fechar(db);

	/* um banco de uma versão mais nova do que se conhece: recusa, sem estragar */
	sqlite3 *s = NULL;
	sqlite3_open(arq, &s);
	sqlite3_exec(s, "PRAGMA user_version = 99;", NULL, NULL, NULL);
	sqlite3_close(s);
	g_free(erro);
	erro = NULL;
	db = pulsedb_abrir(arq, &erro);
	CHECA(db == NULL && erro && strstr(erro, "mais nova"), "um banco mais novo devia ser recusado: %s", erro ? erro : "(sem erro)");
	pulsedb_fechar(db);
	g_free(erro);
	/* e um banco vazio e sem versão (0) é levado até a de agora */
	g_unlink(arq);
	sqlite3_open(arq, &s);
	sqlite3_exec(s, "CREATE TABLE lixo(x);", NULL, NULL, NULL);
	sqlite3_close(s);
	erro = NULL;
	db = pulsedb_abrir(arq, &erro);
	CHECA(db && pulsedb_versao(db) == pulsedb_versao_atual(), "um banco sem versão é migrado: %s", erro ? erro : "ok");
	pulsedb_fechar(db);
	g_free(erro);

	char *sqltxt = pulsedb_esquema_sql();
	CHECA(strstr(sqltxt, "CREATE TABLE emma_interactions") && strstr(sqltxt, "user_version = 1"), "o esquema em SQL");
	g_free(sqltxt);
	g_free(arq);
	g_free(dir);
}

int main(void) {
	teste_segredos();
	teste_banco();
	if (falhas) {
		fprintf(stderr, "%d falha(s) no pulsedb\n", falhas);
		return 1;
	}
	printf("pulsedb: ok\n");
	return 0;
}
