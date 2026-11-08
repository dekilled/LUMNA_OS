/* A memória do Pulse Terminal (SQLite). Veja pulsedb.h. */
#define _GNU_SOURCE
#include "pulsedb.h"

#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

struct pulsedb {
	sqlite3 *s;
};

/* ---- as migrações: em ordem, nunca editar uma que já saiu (acrescente a próxima) ---- */

static const struct {
	int versao;
	const char *sql;
} migracoes[] = {
	{ 1,
		"CREATE TABLE sessions (\n"
		"  id INTEGER PRIMARY KEY,\n"
		"  started_at INTEGER NOT NULL,\n"
		"  ended_at INTEGER,\n"
		"  cwd_inicial TEXT NOT NULL DEFAULT ''\n"
		");\n"
		"CREATE TABLE commands (\n"
		"  id INTEGER PRIMARY KEY,\n"
		"  session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,\n"
		"  timestamp INTEGER NOT NULL,\n"
		"  cwd TEXT NOT NULL DEFAULT '',\n"
		"  command TEXT NOT NULL,\n"
		"  exit_code INTEGER,\n"
		"  saida_snippet TEXT NOT NULL DEFAULT ''\n"
		");\n"
		"CREATE INDEX commands_session ON commands(session_id, id);\n"
		"CREATE INDEX commands_timestamp ON commands(timestamp);\n"
		"CREATE TABLE emma_interactions (\n"
		"  id INTEGER PRIMARY KEY,\n"
		"  session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,\n"
		"  command_id INTEGER REFERENCES commands(id) ON DELETE SET NULL,\n"
		"  tipo TEXT NOT NULL,\n"
		"  prompt_enviado TEXT NOT NULL,\n"
		"  resposta TEXT NOT NULL DEFAULT '',\n"
		"  timestamp INTEGER NOT NULL\n"
		");\n"
		"CREATE INDEX emma_interactions_session ON emma_interactions(session_id, id);\n" },
};
#define N_MIGRACOES ((int)(sizeof(migracoes) / sizeof(migracoes[0])))

int pulsedb_versao_atual(void) {
	return migracoes[N_MIGRACOES - 1].versao;
}

char *pulsedb_esquema_sql(void) {
	GString *g = g_string_new("-- Pulse Terminal: o esquema do banco (cada bloco é uma migração; PRAGMA user_version guarda a versão)\n");
	for (int i = 0; i < N_MIGRACOES; i++) {
		g_string_append_printf(g, "\n-- migração %d\nBEGIN;\n%sPRAGMA user_version = %d;\nCOMMIT;\n", migracoes[i].versao, migracoes[i].sql, migracoes[i].versao);
	}
	return g_string_free(g, FALSE);
}

/* ---- abrir ---- */

char *pulsedb_caminho_padrao(void) {
	return g_build_filename(g_get_user_data_dir(), "lumna", "pulse", "pulse.db", NULL);
}

static bool sql(sqlite3 *s, const char *comando, char **erro) {
	char *msg = NULL;
	if (sqlite3_exec(s, comando, NULL, NULL, &msg) != SQLITE_OK) {
		if (erro) {
			*erro = g_strdup_printf("%s", msg ? msg : "erro no banco");
		}
		sqlite3_free(msg);
		return false;
	}
	return true;
}

static int ler_int(sqlite3 *s, const char *consulta) {
	sqlite3_stmt *st = NULL;
	int v = 0;
	if (sqlite3_prepare_v2(s, consulta, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
		v = sqlite3_column_int(st, 0);
	}
	sqlite3_finalize(st);
	return v;
}

pulsedb *pulsedb_abrir(const char *caminho, char **erro) {
	char *padrao = caminho ? NULL : pulsedb_caminho_padrao();
	const char *arq = caminho ? caminho : padrao;
	char *pasta = g_path_get_dirname(arq);
	g_mkdir_with_parents(pasta, 0700);
	g_free(pasta);
	mode_t antes = umask(077); /* o arquivo (e o -wal) nascem só da pessoa */
	sqlite3 *s = NULL;
	int rc = sqlite3_open(arq, &s);
	umask(antes);
	if (rc != SQLITE_OK) {
		if (erro) {
			*erro = g_strdup_printf("não abri o banco do Pulse (%s): %s", arq, s ? sqlite3_errmsg(s) : "sem memória");
		}
		sqlite3_close(s);
		g_free(padrao);
		return NULL;
	}
	g_chmod(arq, 0600);
	g_free(padrao);
	sqlite3_busy_timeout(s, 2000);
	if (!sql(s, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA secure_delete=ON;", erro)) {
		sqlite3_close(s);
		return NULL;
	}
	int v = ler_int(s, "PRAGMA user_version");
	if (v > pulsedb_versao_atual()) {
		if (erro) {
			*erro = g_strdup_printf("o banco do Pulse é de uma versão mais nova (%d) do que este programa conhece (%d)", v, pulsedb_versao_atual());
		}
		sqlite3_close(s);
		return NULL;
	}
	for (int i = 0; i < N_MIGRACOES; i++) {
		if (migracoes[i].versao <= v) {
			continue;
		}
		char *cmd = g_strdup_printf("BEGIN;\n%sPRAGMA user_version = %d;\nCOMMIT;", migracoes[i].sql, migracoes[i].versao);
		bool ok = sql(s, cmd, erro);
		g_free(cmd);
		if (!ok) {
			sql(s, "ROLLBACK;", NULL);
			sqlite3_close(s);
			return NULL;
		}
	}
	pulsedb *db = g_new0(pulsedb, 1);
	db->s = s;
	return db;
}

void pulsedb_fechar(pulsedb *db) {
	if (db) {
		sqlite3_close(db->s);
		g_free(db);
	}
}

int pulsedb_versao(pulsedb *db) {
	return ler_int(db->s, "PRAGMA user_version");
}

/* ---- gravar ---- */

static int64_t agora(void) {
	return (int64_t)time(NULL);
}

int64_t pulsedb_sessao_iniciar(pulsedb *db, const char *cwd_inicial) {
	sqlite3_stmt *st = NULL;
	sqlite3_prepare_v2(db->s, "INSERT INTO sessions(started_at, cwd_inicial) VALUES(?, ?)", -1, &st, NULL);
	sqlite3_bind_int64(st, 1, agora());
	sqlite3_bind_text(st, 2, cwd_inicial ? cwd_inicial : "", -1, SQLITE_TRANSIENT);
	sqlite3_step(st);
	sqlite3_finalize(st);
	return (int64_t)sqlite3_last_insert_rowid(db->s);
}

void pulsedb_sessao_encerrar(pulsedb *db, int64_t sessao) {
	sqlite3_stmt *st = NULL;
	sqlite3_prepare_v2(db->s, "UPDATE sessions SET ended_at = ? WHERE id = ?", -1, &st, NULL);
	sqlite3_bind_int64(st, 1, agora());
	sqlite3_bind_int64(st, 2, sessao);
	sqlite3_step(st);
	sqlite3_finalize(st);
}

int64_t pulsedb_comando_gravar(pulsedb *db, int64_t sessao, const char *cwd, const char *comando, int exit_code, const char *saida) {
	char *c = comando ? g_strstrip(g_strdup(comando)) : g_strdup("");
	if (!*c) {
		g_free(c);
		return 0;
	}
	char *cmd = pulse_ocultar_segredos(c);
	char *out = pulse_ocultar_segredos(saida ? saida : "");
	g_free(c);
	sqlite3_stmt *st = NULL;
	sqlite3_prepare_v2(db->s, "INSERT INTO commands(session_id, timestamp, cwd, command, exit_code, saida_snippet) VALUES(?,?,?,?,?,?)", -1, &st, NULL);
	sqlite3_bind_int64(st, 1, sessao);
	sqlite3_bind_int64(st, 2, agora());
	sqlite3_bind_text(st, 3, cwd ? cwd : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 4, cmd, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(st, 5, exit_code);
	sqlite3_bind_text(st, 6, out, -1, SQLITE_TRANSIENT);
	int64_t id = sqlite3_step(st) == SQLITE_DONE ? (int64_t)sqlite3_last_insert_rowid(db->s) : 0;
	sqlite3_finalize(st);
	g_free(cmd);
	g_free(out);
	return id;
}

int64_t pulsedb_interacao_gravar(pulsedb *db, int64_t sessao, int64_t command_id, const char *tipo, const char *prompt, const char *resposta) {
	char *p = pulse_ocultar_segredos(prompt ? prompt : "");
	char *r = pulse_ocultar_segredos(resposta ? resposta : "");
	sqlite3_stmt *st = NULL;
	sqlite3_prepare_v2(db->s, "INSERT INTO emma_interactions(session_id, command_id, tipo, prompt_enviado, resposta, timestamp) VALUES(?,?,?,?,?,?)", -1, &st, NULL);
	sqlite3_bind_int64(st, 1, sessao);
	if (command_id > 0) {
		sqlite3_bind_int64(st, 2, command_id);
	} else {
		sqlite3_bind_null(st, 2);
	}
	sqlite3_bind_text(st, 3, tipo ? tipo : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 4, p, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 5, r, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(st, 6, agora());
	int64_t id = sqlite3_step(st) == SQLITE_DONE ? (int64_t)sqlite3_last_insert_rowid(db->s) : 0;
	sqlite3_finalize(st);
	g_free(p);
	g_free(r);
	return id;
}

void pulsedb_interacao_resposta(pulsedb *db, int64_t interacao, const char *resposta) {
	char *r = pulse_ocultar_segredos(resposta ? resposta : "");
	sqlite3_stmt *st = NULL;
	sqlite3_prepare_v2(db->s, "UPDATE emma_interactions SET resposta = ? WHERE id = ?", -1, &st, NULL);
	sqlite3_bind_text(st, 1, r, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(st, 2, interacao);
	sqlite3_step(st);
	sqlite3_finalize(st);
	g_free(r);
}

/* ---- ler ---- */

static char *uma_linha(const char *txt, size_t max) {
	if (!txt) {
		return g_strdup("");
	}
	const char *fim = strchr(txt, '\n');
	size_t n = fim ? (size_t)(fim - txt) : strlen(txt);
	if (n > max) {
		n = max;
	}
	return g_strndup(txt, n);
}

char *pulsedb_ultimos_comandos(pulsedb *db, int64_t sessao, int n) {
	sqlite3_stmt *st = NULL;
	sqlite3_prepare_v2(db->s, "SELECT command, exit_code FROM (SELECT id, command, exit_code FROM commands WHERE session_id = ? ORDER BY id DESC LIMIT ?) ORDER BY id", -1, &st, NULL);
	sqlite3_bind_int64(st, 1, sessao);
	sqlite3_bind_int(st, 2, n);
	GString *g = g_string_new(NULL);
	while (sqlite3_step(st) == SQLITE_ROW) {
		char *c = uma_linha((const char *)sqlite3_column_text(st, 0), 300);
		g_string_append_printf(g, "%s  (código %d)\n", c, sqlite3_column_int(st, 1));
		g_free(c);
	}
	sqlite3_finalize(st);
	return g_string_free(g, FALSE);
}

#define CONTEXTO_MAX 6000
#define COMANDOS_POR_SESSAO 15

static void contexto_sessao(pulsedb *db, GString *g, int64_t sid, bool atual) {
	sqlite3_stmt *st = NULL;
	sqlite3_prepare_v2(db->s, "SELECT started_at, cwd_inicial FROM sessions WHERE id = ?", -1, &st, NULL);
	sqlite3_bind_int64(st, 1, sid);
	if (sqlite3_step(st) != SQLITE_ROW) {
		sqlite3_finalize(st);
		return;
	}
	time_t quando = (time_t)sqlite3_column_int64(st, 0);
	char hora[32];
	struct tm tm;
	localtime_r(&quando, &tm);
	strftime(hora, sizeof(hora), "%Y-%m-%d %H:%M", &tm);
	g_string_append_printf(g, "%s (%s, começou em %s):\n", atual ? "Sessão de agora" : "Sessão anterior", hora, (const char *)sqlite3_column_text(st, 1));
	sqlite3_finalize(st);
	sqlite3_prepare_v2(db->s,
		"SELECT command, exit_code, saida_snippet FROM (SELECT id, command, exit_code, saida_snippet FROM commands WHERE session_id = ? ORDER BY id DESC LIMIT ?) ORDER BY id", -1, &st, NULL);
	sqlite3_bind_int64(st, 1, sid);
	sqlite3_bind_int(st, 2, COMANDOS_POR_SESSAO);
	while (sqlite3_step(st) == SQLITE_ROW && g->len < CONTEXTO_MAX) {
		char *c = uma_linha((const char *)sqlite3_column_text(st, 0), 200);
		int ec = sqlite3_column_int(st, 1);
		g_string_append_printf(g, "  $ %s  -> %d\n", c, ec);
		if (ec != 0) {
			const char *saida = (const char *)sqlite3_column_text(st, 2);
			/* a última linha com texto da saída de quem falhou: costuma ser o erro */
			char *copia = g_strdup(saida ? saida : ""), *ult = NULL;
			g_strchomp(copia);
			ult = strrchr(copia, '\n');
			char *erro = uma_linha(ult ? ult + 1 : copia, 160);
			if (*erro) {
				g_string_append_printf(g, "      erro: %s\n", erro);
			}
			g_free(erro);
			g_free(copia);
		}
		g_free(c);
	}
	sqlite3_finalize(st);
}

char *pulsedb_contexto(pulsedb *db, int64_t sessao_atual, int n_sessoes) {
	GString *g = g_string_new(NULL);
	if (sessao_atual > 0) {
		contexto_sessao(db, g, sessao_atual, true);
	}
	sqlite3_stmt *st = NULL;
	sqlite3_prepare_v2(db->s, "SELECT id FROM sessions WHERE id != ? AND EXISTS (SELECT 1 FROM commands WHERE session_id = sessions.id) ORDER BY id DESC LIMIT ?", -1, &st, NULL);
	sqlite3_bind_int64(st, 1, sessao_atual);
	sqlite3_bind_int(st, 2, n_sessoes);
	GArray *ids = g_array_new(FALSE, FALSE, sizeof(int64_t));
	while (sqlite3_step(st) == SQLITE_ROW) {
		int64_t id = sqlite3_column_int64(st, 0);
		g_array_append_val(ids, id);
	}
	sqlite3_finalize(st);
	for (guint i = 0; i < ids->len && g->len < CONTEXTO_MAX; i++) {
		contexto_sessao(db, g, g_array_index(ids, int64_t, i), false);
	}
	g_array_free(ids, TRUE);
	if (g->len > CONTEXTO_MAX) {
		g_string_truncate(g, CONTEXTO_MAX);
	}
	return g_string_free(g, FALSE);
}

char *pulsedb_buscar(pulsedb *db, const char *termo, int max) {
	GString *g = g_string_new(NULL);
	if (!termo || !*termo) {
		return g_string_free(g, FALSE);
	}
	/* o termo vale como texto, não como padrão: % _ e \ ganham a barra */
	GString *esc = g_string_new("%");
	for (const char *p = termo; *p; p++) {
		if (*p == '%' || *p == '_' || *p == '\\') {
			g_string_append_c(esc, '\\');
		}
		g_string_append_c(esc, *p);
	}
	g_string_append_c(esc, '%');
	char *padrao = g_string_free(esc, FALSE);
	sqlite3_stmt *st = NULL;
	sqlite3_prepare_v2(db->s, "SELECT timestamp, cwd, command, exit_code FROM commands WHERE command LIKE ?1 ESCAPE '\\' OR saida_snippet LIKE ?1 ESCAPE '\\' ORDER BY id DESC LIMIT ?2", -1, &st, NULL);
	sqlite3_bind_text(st, 1, padrao, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(st, 2, max);
	while (sqlite3_step(st) == SQLITE_ROW) {
		time_t quando = (time_t)sqlite3_column_int64(st, 0);
		char hora[32];
		struct tm tm;
		localtime_r(&quando, &tm);
		strftime(hora, sizeof(hora), "%Y-%m-%d %H:%M", &tm);
		char *c = uma_linha((const char *)sqlite3_column_text(st, 2), 200);
		g_string_append_printf(g, "%s · %s · %s  (código %d)\n", hora, (const char *)sqlite3_column_text(st, 1), c, sqlite3_column_int(st, 3));
		g_free(c);
	}
	sqlite3_finalize(st);
	g_free(padrao);
	return g_string_free(g, FALSE);
}

/* ---- apagar ---- */

void pulsedb_podar(pulsedb *db, int dias) {
	char *cmd = g_strdup_printf("DELETE FROM commands WHERE timestamp < %lld; DELETE FROM emma_interactions WHERE timestamp < %lld;"
		" DELETE FROM sessions WHERE ended_at IS NOT NULL AND NOT EXISTS (SELECT 1 FROM commands WHERE session_id = sessions.id);",
		(long long)(agora() - (int64_t)dias * 86400), (long long)(agora() - (int64_t)dias * 86400));
	sql(db->s, cmd, NULL);
	g_free(cmd);
}

void pulsedb_apagar_tudo(pulsedb *db) {
	sql(db->s, "DELETE FROM emma_interactions; DELETE FROM commands; DELETE FROM sessions; VACUUM;", NULL);
}

int pulsedb_total_comandos(pulsedb *db) {
	return ler_int(db->s, "SELECT COUNT(*) FROM commands");
}

/* ---- esconder os segredos ---- */

static const struct {
	const char *regex, *troca;
} segredos[] = {
	/* --password=x, --token x, --api-key=x, --secret x... (o valor vai embora, a opção fica) */
	{ "(?i)(--?(?:password|passwd|pass|senha|token|secret|api[-_]?key|apikey|auth|access[-_]?key|client[-_]?secret)[=\\s]+)(?:\"[^\"]*\"|'[^']*'|\\S+)", "\\1***" },
	/* Authorization: Bearer x / Basic x, e o cabeçalho de chave */
	{ "(?i)((?:authorization|x-api-key|api-key)\\s*[:=]\\s*(?:bearer\\s+|basic\\s+|token\\s+)?)[^\\s\"']+", "\\1***" },
	/* VAR_TOKEN=x, SENHA=x, DB_PASSWORD=x (a atribuição de ambiente) */
	{ "\\b([A-Za-z0-9_]*(?i:key|token|secret|passw(?:or)?d|senha|credential)[A-Za-z0-9_]*=)(?:\"[^\"]*\"|'[^']*'|\\S+)", "\\1***" },
	/* https://usuario:senha@host */
	{ "(://[^/\\s:@]+:)[^/\\s@]+@", "\\1***@" },
	/* mysql -pSENHA / -p'senha' (colado, sem espaço) */
	{ "(\\bmysql\\w*\\s[^|;&\\n]*\\s-p)(?!\\s)\\S+", "\\1***" },
	/* chaves com formato conhecido */
	{ "\\b(?:sk-[A-Za-z0-9_-]{16,}|gh[pousr]_[A-Za-z0-9]{20,}|xox[abprs]-[A-Za-z0-9-]{10,}|AKIA[0-9A-Z]{16}|AIza[0-9A-Za-z_-]{30,}|eyJ[A-Za-z0-9_-]{15,}\\.[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{5,})", "***" },
};

char *pulse_ocultar_segredos(const char *texto) {
	char *atual = g_strdup(texto ? texto : "");
	for (size_t i = 0; i < sizeof(segredos) / sizeof(segredos[0]); i++) {
		GError *e = NULL;
		GRegex *re = g_regex_new(segredos[i].regex, G_REGEX_OPTIMIZE, 0, &e);
		if (!re) {
			g_clear_error(&e);
			continue;
		}
		char *novo = g_regex_replace(re, atual, -1, 0, segredos[i].troca, 0, NULL);
		g_regex_unref(re);
		if (novo) {
			g_free(atual);
			atual = novo;
		}
	}
	return atual;
}
